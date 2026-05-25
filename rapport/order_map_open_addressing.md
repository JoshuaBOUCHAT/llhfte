# Order Map — Décisions de design : Open Addressing / Robin Hood

## Contexte

Le moteur consomme un feed NASDAQ ITCH 5.0. Chaque message `Add Order` crée un ordre
identifié par un `order_ref_number` (u64). Les messages suivants (`Execute`, `Cancel`,
`Delete`, `Replace`) référencent uniquement cet identifiant — sans prix, sans côté, sans
quantité. Il faut donc une structure qui permette de retrouver le contexte complet d'un
ordre en O(1) à partir de son `order_ref`.

---

## 1. Order book : deux arrays séparés (bid / ask)

### Décision

Deux ring buffers indépendants : un pour les bids, un pour les asks.

### Pourquoi pas un array partagé

Un array partagé avec bids à gauche et asks à droite fonctionne tant que le marché
reste centré. En cas de mouvement polarisé (rally violent, gap à l'ouverture), un côté
consomme des slots rapidement pendant que l'autre se libère, mais pas au même rythme.
Le buffer partagé peut se retrouver saturé d'un côté avec l'autre vide. Avec deux
buffers séparés, chaque côté absorbe son propre choc indépendamment — aucune
interférence possible.

---

## 2. La flat map : rôle et cycle de vie

### Rôle

```
order_ref (u64) → index dans le pool d'ordres (u32)
```

Quand un message `Execute` arrive avec seulement un `order_ref`, la map fournit le
contexte (prix, quantité, côté, symbole) pour mettre à jour le book correctement.

### Cycle de vie borné

Un ordre quitte la map dès qu'il est exécuté, annulé ou supprimé. La map ne contient
que les ordres **actifs en ce moment**, pas l'historique. En pratique :

- Full feed NASDAQ : 500K–2M ordres actifs simultanément (pic open/close)
- Panier de 100 symboles : 50K–200K ordres actifs max

Le nombre d'entrées est donc **borné et stable** — pas de croissance indéfinie.

### Taille d'un order_ref

Défini par la spec ITCH 5.0 : `u64` — 8 bytes.

---

## 3. Choix de la structure : Robin Hood hashing

### Candidats évalués

**Chaining avec slab overflow**

Bucket array (u32 head index) + slab de nodes `{u64 key, u32 val, u32 next}`.

- Bucket array : 2M × 4B = 8 MB
- Slab inline-first : 1M × 16B = 16 MB  
- Overflow slab : 256K × 16B = 4 MB (voir section 4)
- **Total : 28 MB**

Problème principal : pointer chasing. Chaque maillon de chaîne est un saut mémoire
à adresse arbitraire. Même si la slab est compacte, la **dépendance de données** entre
loads sérialise l'exécution — le CPU ne peut pas pipeliner des loads dont chaque
adresse dépend du résultat du précédent. Une chaîne de 4 noeuds en L3 (~30 cycles
chacun) = 120 cycles bloqués.

**Swiss Table (hashbrown)**

Groupes de 16 slots avec 1 byte de metadata par slot, comparaison SIMD 16-at-a-time.
Load factor ~87%, très bon throughput en lookup.

Problème pour ce use case : les deletes utilisent des **tombstones**. Les ordres sont
constamment annulés/exécutés — les tombstones s'accumulent, le load factor effectif
monte, les lookups se dégradent progressivement. Un rehash périodique serait nécessaire
pour nettoyer, introduisant une latence imprévisible incompatible avec du HFT.

**Robin Hood hashing — retenu**

Open addressing linéaire avec PSL (Probe Sequence Length) : distance entre la position
idéale d'un item et sa position réelle. Règle d'insertion : si l'item en place a un PSL
inférieur au nouveau, on l'évince et on continue l'insertion avec l'item déplacé.

---

## 4. Calcul du overflow slab (chaining, non retenu mais documenté)

Avec un schéma inline-first (premier item stocké directement dans le bucket, overflow
en slab uniquement pour les extras) et α = 0.5 (N = 2M buckets, M = 1M ordres) :

```
E[overflow] = M × (1 - (1/α)(1 - e^{-α}))
            = 1M × (1 - 2 × (1 - e^{-0.5}))
            = 1M × 0.213
            ≈ 213 000 nodes
```

La variance est extrêmement faible car on somme 2M variables de Bernoulli indépendantes :

```
Var[buckets non-vides] = N × p × (1-p) = 2M × 0.3935 × 0.6065 ≈ 477 000
σ ≈ 691
```

220K = E + 10σ. `1 << 18` (256K) slots donne une marge de ~10σ, soit
P(débordement) < 10^-23. En pratique on hardcode 256K et on n'y pense plus.

---

## 5. Mécanique Robin Hood

### Structure d'un slot

```cpp
struct Slot {
    u64 key;    // 8B — order_ref (0 = EMPTY, jamais valide en ITCH)
    u32 val;    // 4B — index dans le pool d'ordres
    u8  psl;    // 1B — probe sequence length
    u8  pad[3]; // 3B — alignement
};              // 16B — 4 slots par cache line (64B)
```

### Insert

```
1. h = hash(order_ref) & MASK
2. Si slot[h] vide → placer, PSL=0
3. Si slot[h] occupé et psl_slot < psl_courant → évincer, continuer avec l'évincé
4. Sinon → incrémenter PSL, avancer, répéter
```

### Lookup

```cpp
u32 lookup(u64 key) {
    u32 pos = hash(key) & MASK;
    u8  psl = 0;
    __builtin_prefetch(&slots[pos],     0, 1);
    __builtin_prefetch(&slots[pos + 4], 0, 1);
    while (true) {
        if (slots[pos].psl < psl)    return NOT_FOUND;  // early exit Robin Hood
        if (slots[pos].key == EMPTY) return NOT_FOUND;
        if (slots[pos].key == key)   return slots[pos].val;
        pos = (pos + 1) & MASK;
        psl++;
        if ((pos & 3) == 0)
            __builtin_prefetch(&slots[(pos + 4) & MASK], 0, 1);
    }
}
```

**Condition d'arrêt** : si `slots[pos].psl < psl_courant`, l'item cherché aurait
évincé cet item à l'insertion (Robin Hood lui aurait volé sa place). Il ne peut donc
pas être plus loin à droite. Arrêt immédiat.

### Delete — backwards shift, zéro tombstone

```cpp
void remove(u64 key) {
    u32 pos = find(key);
    while (true) {
        u32 next = (pos + 1) & MASK;
        if (slots[next].key == EMPTY || slots[next].psl == 0) {
            slots[pos] = EMPTY_SLOT;
            return;
        }
        slots[pos] = slots[next];
        slots[pos].psl--;
        pos = next;
    }
}
```

Pas de tombstone : le trou est rebouché immédiatement en décalant les items suivants
vers leur position idéale. Aucune dégradation dans le temps quelle que soit la
fréquence des deletes.

**Direction de probe** : toujours vers la droite (+1). Un item est toujours déplacé
vers l'avant par rapport à sa position idéale, jamais en arrière. La recherche ne
part jamais à gauche.

---

## 6. Sizing final

```
HT_SIZE      = 1 << 21   // 2M slots  (α = 0.5 pour 1M ordres max)
POOL_SIZE    = 1 << 20   // 1M ordres max live simultanément
```

```
Hash table   : 2M × 16B  = 32 MB
Order pool   : 1M × ~16B = 16 MB  (price u32, qty u32, symbol u32, side u8...)
                           ------
                           ~48 MB fixe au démarrage
```

Comparaison avec chaining retenu :

| Schéma         | Mémoire | Cache behavior        | Delete       |
|----------------|---------|-----------------------|--------------|
| Chaining+slab  | 36 MB   | pointer chasing       | trivial      |
| Robin Hood     | 32 MB   | linéaire, prefetchable| backwards shift |

---

## 7. Cache et prefetch

4 slots de 16B tiennent dans une cache line de 64B. Avec α=0.5 :

```
P(PSL = 0) ≈ 60%   → 1 probe, dans la première cache line
P(PSL ≤ 3) ≈ 99.7% → lookup dans 1 cache line
P(PSL > 4) ≈ 0.003%
P(PSL > 8) ≈ 10^-7
```

Le prefetch double à l'entrée (`pos` et `pos+4`) émet la demande pour la deuxième
cache line pendant le scan de la première. Si PSL > 4, le delta d'attente est minimal
plutôt qu'un cache miss complet (~200 cycles). Si PSL ≤ 4 (quasi toujours), le second
prefetch est simplement ignoré.

`__builtin_prefetch` émet une instruction `PREFETCHT1` non-bloquante : le CPU envoie
la demande au memory subsystem et continue l'exécution immédiatement. Si la cache line
arrive avant qu'on en ait besoin, accès en ~4 cycles. Si elle arrive en retard, on
attend seulement le delta restant, pas le round-trip complet.

---

## 8. Hash function

### Murmur3 finalizer

```cpp
u64 hash_u64(u64 x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
```

~4 cycles, pas de branche, pas d'accès mémoire. Excellent avalanche (chaque bit
d'entrée affecte tous les bits de sortie).

Les `order_ref` ITCH sont assignés séquentiellement par les participants. Sans hash,
un modulo direct provoquerait un clustering massif (tous les ordres d'un même
participant sur les mêmes buckets). Le hash casse cette séquentialité.

### Hash flooding

Murmur3 est déterministe (pas de seed secret) — théoriquement exploitable si un
attaquant contrôle les clés. Dans ce contexte, les `order_ref` viennent du feed
NASDAQ (assignés par la bourse), pas d'une source externe non fiable. La menace
hash flooding ne s'applique pas.

### Modulo vs AND

```cpp
u32 bucket = hash_u64(order_ref) & (HT_SIZE - 1);  // 1 instruction
// vs
u32 bucket = hash_u64(order_ref) % HT_SIZE;         // division = ~20-40 cycles
```

`HT_SIZE` doit être une puissance de 2 pour que le AND soit valide.

---

## 9. Hardware HFT réel

Les top firms (Virtu, Jump, Citadel) utilisent des FPGAs en front-end : le FPGA
parse le protocole ITCH, filtre les symboles cibles, et envoie uniquement les events
pertinents au CPU. La "hash map" peut être implémentée en BRAM avec adressage direct.

Pour les systèmes CPU, les AMD EPYC 3D V-Cache (768 MB–1.1 GB de L3) permettent de
tenir des structures bien plus larges en cache. Intel DDIO fait atterrir les paquets
réseau directement en L3 sans passer par la DRAM.

Pour un engine ciblant un panier de 100 symboles, les 48 MB du design ci-dessus
tiennent confortablement en L3 sur un Xeon Sapphire Rapids (60–120 MB) ou mieux.
