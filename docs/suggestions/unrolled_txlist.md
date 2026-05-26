# Suggestion : Unrolled TxList (node à 4 slots, 1 cache line)

## Layout proposé

```cpp
const int NODE_SLOTS = 4;

struct alignas(64) TxNode {
  u32 next;               // offset  0
  u32 prev;               // offset  4
  u32 qtys[NODE_SLOTS];   // offset  8  (16 bytes)
  u64 tx_ids[NODE_SLOTS]; // offset 24  (32 bytes)
  u32 occupancy;          // offset 56  (voir variante bitmask)
  // padding implicite : 4 bytes → sizeof = 64 ✓
};
```

Chaque node tient exactement **1 cache line (64 bytes)**. Traversal = 1 miss par node au lieu de 1 miss par élément.

---

## Idée 1 — Index public encodé `idx_slab << 2 | slot`

Plutôt qu'un index brut dans le slab, l'index public encode à la fois le node et le slot :

```cpp
// Encoder
u32 public_idx = (slab_idx << 2) | slot_idx;  // slot_idx ∈ [0, 3]

// Décoder
u32 slab_idx = public_idx >> 2;
u32 slot_idx = public_idx & 0x3;
```

### Avantages
- Un seul `u32` pour identifier n'importe quel élément (compatible avec `inner_tx_idx` dans `HashMapEntry`)
- Lookup `O(1)` : 1 shift + 1 mask, pas d'indirection supplémentaire
- Aucun stockage supplémentaire par rapport à aujourd'hui

### Inconvénients
- Limite le slab à `2^30` nodes (≈ 1 milliard) — largement suffisant pour `TX_SLAB_SIZE = 1 << 20`
- Un bug sur l'encodage/décodage est silencieux (pas de type distinct)
- Rend le débogage légèrement moins lisible

---

## Idée 2 — `occupancy` comme bitmask 4 bits (au lieu de `len`)

`len` en `u32` suppose que les slots sont contigus : insertion en queue, suppression par shift.  
Remplacer par un **bitmask** : bit `i` = 1 si le slot `i` est occupé.

```cpp
u32 occupancy;  // bits [0..3] utilisés, [4..31] réservés

// Slot libre le plus bas
u32 free_slot = __builtin_ctz(~occupancy & 0xF);

// Marquer occupé / libre
occupancy |=  (1u << slot);
occupancy &= ~(1u << slot);

// Node plein / vide
bool full  = (occupancy & 0xF) == 0xF;
bool empty = (occupancy & 0xF) == 0;

// Itérer les slots occupés
u32 mask = occupancy & 0xF;
while (mask) {
    u32 slot = __builtin_ctz(mask);
    // traiter slot
    mask &= mask - 1;  // clear lowest bit
}
```

### Avantages
- **Suppression O(1)** : juste `occupancy &= ~(1u << slot)`, pas de shift des éléments
- Pas de fragmentation à gérer : les slots sont indépendants
- `__builtin_ctz` = 1 cycle sur x86 (BSF/TZCNT)
- Combine bien avec l'idée 1 : on peut exposer des public_idx vers n'importe quel slot, y compris des slots non-contigus

### Inconvénients
- Itération légèrement plus complexe (boucle sur bits vs. simple `for i < len`)
- Avec `len`, les données sont denses → meilleure utilisation SIMD sur les qtys/tx_ids
- Un node peut être "fragmenté" (ex. slots 0 et 2 occupés, 1 libre) → la liste est moins compacte
- Pas de notion d'ordre d'insertion sans champ supplémentaire (si l'ordre FIFO est important)

---

## Comparaison résumée

| Critère              | `len` (contigu)       | `occupancy` (bitmask)     |
|----------------------|-----------------------|---------------------------|
| Suppression          | O(4) shift            | O(1) clear bit            |
| Itération            | simple `for`          | boucle sur bits           |
| Densité des données  | toujours dense        | potentiellement fragmenté |
| Ordre FIFO préservé  | oui                   | non (sauf champ extra)    |
| Complexité impl.     | faible                | modérée                   |

## Recommandation

- Si les suppressions sont **rares** (surtout des exécutions totales) → garder `len`, plus simple
- Si les **cancels partiels sont fréquents** (cancel d'un ordre au milieu de la liste) → bitmask, évite les shifts et préserve les public_idx stables après suppression
- Les deux idées (index encodé + bitmask) se combinent naturellement
