#include "txlist.hpp"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>

Slot *TxListAllocator::alloc_slab()
{
  void *ptr = mmap(nullptr, TX_SLAB_SIZE * sizeof(Slot), PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE, -1, 0);
  if (ptr == MAP_FAILED)
  {
    perror("TxListAllocator: mmap failed (check /proc/sys/vm/nr_hugepages)");
    exit(EXIT_FAILURE);
  }
  return static_cast<Slot *>(ptr);
}

TxListAllocator::TxListAllocator() : slab(alloc_slab()), head(0)
{
  for (u32 i = 0; i < TX_SLAB_SIZE - 1; i++)
    slab[i].slot_next = i + 1;
  slab[TX_SLAB_SIZE - 1].slot_next = U32MAX;
}

TxListAllocator::~TxListAllocator()
{
  munmap(slab, TX_SLAB_SIZE * sizeof(Slot));
}
