# Order Book — Linked List Intrusive avec Sentinel Encoding

## Contexte

Chaque niveau de prix dans le book contient une liste d'ordres (FIFO, price-time priority).
Quand un `Cancel` ou `Execute` arrive avec uniquement un `order_ref`, il faut pouvoir :
1. Retrouver l'ordre en O(1)
2. Le délier de sa liste en O(1)
3. Mettre à jour le `total_qty` du niveau de prix
4. Tout ça sans stocker symbol/price/side dans chaque noeud

## Problème du NULL sentinel classique

Approche naïve : `head.prev = NULL_IDX` (U32_MAX).

Problème : l'information "ce noeud est le head du niveau de prix X" est perdue.
Pour mettre à jour `level.total_qty` lors d'une suppression, il faut connaître le niveau —
ce qui force soit à stocker price+side+symbol dans chaque noeud (redondant et coûteux),
soit à avoir une structure externe qui fait ce mapping.

## Solution : sentinel intrinsèque

Le `prev` du head n'est jamais NULL — il encode directement l'identité du niveau de prix.
Bit 31 = 1 sert de flag (les indices valides sont < 2M donc bit 31 = 0 toujours).

```
bit 31      = 1         (flag sentinel, distingue d'un index valide)
bits 30:18  = symbol_id (13 bits → jusqu'à 8192 symboles)
bits 17:7   = slot      (11 bits → jusqu'à 2048 niveaux de prix par symbole)
bit 6       = side      (0 = bid, 1 = ask)
bits 5:0    = réservé
```

```cpp
static constexpr u32 SENTINEL_FLAG = 1u << 31;

u32 make_sentinel(u16 symbol_id, u16 slot, u8 side) {
    return SENTINEL_FLAG | (u32(symbol_id) << 18) | (u32(slot) << 7) | (u32(side) << 6);
}

bool is_sentinel(u32 val)      { return val & SENTINEL_FLAG; }
u16  sentinel_symbol(u32 s)    { return (s >> 18) & 0x1FFF; }
u16  sentinel_slot(u32 s)      { return (s >> 7)  & 0x7FF;  }
u8   sentinel_side(u32 s)      { return (s >> 6)  & 0x1;    }
```

## Structure de la liste

```
[sentinel=encode(AAPL,bid,slot_42)] ← prev du head
        ↓
    [ordre A] ←→ [ordre B] ←→ [ordre C]
                               next=NULL_IDX (tail)
```

Noeud ultra-lean — 12B paddé 16B :

```cpp
struct Node {
    u32 qty;   // 4B
    u32 prev;  // 4B — sentinel si head, index prédécesseur sinon
    u32 next;  // 4B — NULL_IDX si tail, index successeur sinon
};             // 12B → paddé 16B pour alignement cache
```

Pas de price, side, symbol, order_ref dans le noeud. Le noeud est aveugle à son identité —
il connaît seulement sa quantité et ses voisins.

## Les 4 cas de déliage

### Head (prev = sentinel)

```cpp
u32 sentinel = nodes[idx].prev;   // récupérer avant de supprimer
u32 new_head  = nodes[idx].next;

if (new_head != NULL_IDX) {
    nodes[new_head].prev = sentinel;  // transfert du sentinel au nouveau head
} else {
    // liste vide — marquer le niveau comme vide dans le book
    auto [sym, slot, side] = decode(sentinel);
    book[sym][side][slot].head = NULL_IDX;
}
// décoder pour mise à jour total_qty
auto [sym, slot, side] = decode(sentinel);
book[sym][side][slot].total_qty -= nodes[idx].qty;
book[sym][side][slot].head       = new_head;
```

### Tail (next = NULL_IDX)

```cpp
u32 new_tail = nodes[idx].prev;   // prev est soit sentinel soit un index

if (is_sentinel(new_tail)) {
    // seul élément — liste devient vide
    book[sym][side][slot].head = NULL_IDX;
    book[sym][side][slot].tail = NULL_IDX;
} else {
    nodes[new_tail].next = NULL_IDX;
    book[sym][side][slot].tail = new_tail;
}
book[sym][side][slot].total_qty -= nodes[idx].qty;
```

### Milieu

```cpp
// prev et next sont tous les deux des indices valides (pas de sentinel, pas de NULL)
nodes[nodes[idx].prev].next = nodes[idx].next;
nodes[nodes[idx].next].prev = nodes[idx].prev;
book[sym][side][slot].total_qty -= nodes[idx].qty;
// head et tail inchangés
```

### Seul élément (prev = sentinel, next = NULL_IDX)

Couvert par le cas Head avec `new_head == NULL_IDX`.

## Robin Hood map

La map stocke : `order_ref → node_idx` uniquement.

```cpp
struct Slot {
    u64 key;    // 8B — order_ref (0 = vide)
    u32 val;    // 4B — node_idx dans le pool
    u8  psl;    // 1B
    u8  pad[3]; // 3B
};              // 16B — 4 slots par cache line
```

Le `level_id` / symbol / side ne sont PAS dans la RH map — ils sont dans le sentinel
du noeud lui-même. La RH map fait une seule chose : `order_ref → node_idx`.

## Cas Order Replace (`U`)

Le Replace est le seul message qui change le prix — il faut migrer l'ordre vers un
nouveau niveau de prix, potentiellement dans un autre window slot.

```
Replace(old_ref, new_ref, new_shares, new_price)

1. RH lookup(old_ref) → old_node_idx
2. old_sentinel = nodes[old_node_idx].prev  ← si head, sinon remonter jusqu'au head
3. decode(old_sentinel) → symbol, side, old_slot
4. unlink(old_node_idx)
5. new_slot = price_to_slot(new_price)
6. new_sentinel = make_sentinel(symbol, side, new_slot)
7. insérer new_ref à new_slot avec new_sentinel
8. RH remove(old_ref), RH insert(new_ref, new_node_idx)
```

Note : si l'ordre à remplacer n'est PAS le head, il faut soit stocker un `head_idx`
dans chaque noeud (1 u32 de plus), soit remonter la chaîne prev jusqu'au sentinel.
Remonter est O(profondeur) — acceptable si les files sont courtes (FIFO HFT : rarement
> 10 ordres au même prix sur un symbole ciblé).

## Bilan mémoire

```
Robin Hood map  : 2M × 16B  = 32 MB   (α = 0.5, 1M ordres max)
Node pool       : 1M × 16B  = 16 MB
Free list       : 1M ×  4B  =  4 MB
                             ------
                             52 MB fixe au démarrage
```

Comparaison avec le design "pool qui stocke tout" :

| Design                  | Mémoire |
|-------------------------|---------|
| Pool complet (32B/ordre)| 64 MB   |
| Sentinel intrinsèque    | 52 MB   |

## Invariants à maintenir

1. `head.prev` est toujours un sentinel, jamais un index valide
2. `tail.next` est toujours `NULL_IDX`
3. Tout noeud milieu a `prev` et `next` qui sont des indices valides (bit 31 = 0)
4. Le sentinel encode exactement le même (symbol, side, slot) que le niveau de prix
   auquel appartient la liste — toute corruption ici = book corrompu silencieusement
5. À l'insertion d'un premier ordre sur un niveau vide : construire le sentinel et
   l'assigner comme `prev` du noeud inséré
