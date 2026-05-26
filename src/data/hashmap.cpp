#include "hashmap.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <utility>
inline u32 hash_idx(u64 k)
{
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdULL;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53ULL;
  k ^= k >> 33;
  return make_idx(k);
}

u32 HashMap::remove(u64 tx_id)
{
  u32 idx = hash_idx(tx_id);
  u32 pd = 0;

  while (true)
  {
    HashMapEntry &slot = entries[idx];

    if (slot.tx_id == 0 || slot.tsl < pd)
      return U32MAX;

    if (slot.tx_id == tx_id)
      break;

    pd++;
    idx = (idx + 1) & (HASH_MAP_SIZE - 1);
  }

  u32 inner = entries[idx].inner_tx_idx;

  // Backward shift
  while (true)
  {
    u32 next = (idx + 1) & (HASH_MAP_SIZE - 1);
    HashMapEntry &slot_next = entries[next];

    if (slot_next.tx_id == 0 || slot_next.tsl == 0)
    {
      entries[idx] = HashMapEntry{};
      break;
    }

    entries[idx] = slot_next;
    entries[idx].tsl--;
    idx = next;
  }

  return inner;
}
u32 HashMap::insert(u64 tx_id, u32 inner_tx_idx)
{
  HashMapEntry incoming{tx_id, inner_tx_idx, 0};
  u32 idx = hash_idx(tx_id);
  u32 result_idx = HASH_MAP_SIZE; // sentinel "pas encore placé"

  while (true)
  {
    HashMapEntry &slot = entries[idx];

    if (slot.tx_id == 0)
    {
      if (result_idx == HASH_MAP_SIZE)
        result_idx = idx;
      slot = incoming;
      return result_idx;
    }

    // Robin Hood: si l'occupant est plus "riche" (pd plus faible), on lui
    // vole sa place
    if (slot.tsl < incoming.tsl)
    {
      if (result_idx == HASH_MAP_SIZE)
        result_idx = idx;
      std::swap(slot, incoming);
    }

    incoming.tsl++;
    idx = (idx + 1) & (HASH_MAP_SIZE - 1);
  }
}
HashMap::~HashMap()
{
  munmap(entries, HASH_MAP_SIZE * sizeof(HashMapEntry));
}

HashMapEntry *HashMap::alloc_map()
{
  void *ptr = mmap(
      nullptr, HASH_MAP_SIZE * sizeof(HashMapEntry), PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);
  if (ptr == MAP_FAILED)
  {
    perror("HashMap: mmap huge page failed (check /proc/sys/vm/nr_hugepages)");
    exit(EXIT_FAILURE);
  }
  return static_cast<HashMapEntry *>(ptr);
}