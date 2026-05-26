#pragma once

#include "../utils.hpp"

struct TransactionNodeData
{
  u64 tx_id;
  u32 qty;
};
struct TransactionNode
{
  TransactionNodeData data;
  u32 prev;
  u32 next;
};
union Slot
{
  TransactionNode tx;
  u32 slot_next; // u32::Max for end sentinel
};

constexpr u32 TX_SLAB_SIZE = 1 << 20;

/// The global allocator for tx nodes it don't verifify the underlying data is
/// the good type
class TxListAllocator
{
  Slot *slab;
  u32 head;
  Slot *alloc_slab();

public:
  TxListAllocator();
  ~TxListAllocator();

  /// return the index where it is allocated
  inline u32 insert(u64 tx_id, u32 qty)
  {
    u32 idx = head;
    head = slab[idx].slot_next;
    slab[idx].tx = TransactionNode{{tx_id, qty}, U32MAX, U32MAX};
    return idx;
  }

  inline TransactionNodeData remove(u32 idx)
  {
    TransactionNodeData data = slab[idx].tx.data;
    slab[idx].slot_next = head;
    head = idx;
    return data;
  }

  inline TransactionNode &operator[](u32 idx) { return slab[idx].tx; }
};