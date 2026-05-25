# Rapports de recherche — llhfte

Décisions d'architecture et justifications pour le moteur NASDAQ ITCH 5.0.

## Documents

### [Order Map — Open Addressing / Robin Hood](order_map_open_addressing.md)
Design de la hash map `order_ref → node` :
- Pourquoi deux arrays séparés bid/ask (coups de marché polarisés)
- Rôle et cycle de vie de la flat map
- Évaluation chaining vs Swiss Table vs Robin Hood
- Calcul probabiliste du overflow slab (σ ≈ 700 sur 213K expected)
- Mécanique complète Robin Hood : insert, lookup (early exit PSL), delete (backwards shift)
- Sizing final, cache/prefetch, hash function (Murmur3 finalizer)

### [Order Book — Intrusive List avec Sentinel Encoding](intrusive_list_sentinel.md)
Design de la liste chainée par niveau de prix :
- Sentinel intrinsèque : `head.prev` encode (symbol, side, slot) sur 32 bits
- Noeud lean 16B : qty + prev + next uniquement, zéro redondance
- Les 4 cas de déliage O(1) avec transfert de sentinel
- Gestion du Order Replace (migration entre niveaux de prix)
- Bilan mémoire : 52 MB fixe vs 64 MB pour le design naïf
