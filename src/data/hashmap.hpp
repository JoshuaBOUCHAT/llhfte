#pragma once

#include "../utils.hpp"

constexpr u32 HASH_MAP_REAL_MAX_SIZE = 1 << 20;
constexpr u32 HASH_MAP_SIZE = HASH_MAP_REAL_MAX_SIZE << 1; // 50% load factor

constexpr u32 make_idx(u64 hash)
{
  return static_cast<u32>(hash & (HASH_MAP_SIZE - 1));
}

struct HashMapEntry
{
  u64 tx_id = 0;
  u32 inner_tx_idx = U32MAX;
  u32 tsl = 0; // probe distance
};

class HashMap
{
  HashMapEntry *entries;

  HashMapEntry *alloc_map();

public:
  HashMap() : entries(alloc_map()) {}
  ~HashMap();

  u32 remove(u64 tx_id);

  u32 insert(u64 tx_id, u32 inner_tx_idx);
};
