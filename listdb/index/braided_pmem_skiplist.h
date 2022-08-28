#ifndef LISTDB_INDEX_BRAIDED_PMEM_SKIPLIST_H_
#define LISTDB_INDEX_BRAIDED_PMEM_SKIPLIST_H_

#include <x86intrin.h>

#include <map>

#include <libpmemobj++/make_persistent_array_atomic.hpp>

#include "listdb/pmem/pmem.h"
#include "listdb/pmem/pmem_ptr.h"
#include "listdb/core/pmem_log.h"

class BraidedPmemSkipList {
 public:
  struct Node {
    Key key;
    uint64_t tag;  // seqorder (56-bit), op (4-bit), height (4-bit)
    uint64_t value;
    uint64_t next[1];

    int height() const { return tag & 0xf; }

    uint32_t l0_id() const { return (tag >> 32); }
  };

  BraidedPmemSkipList(int primary_region_pool_id);

  void BindArena(int pool_id, PmemLog* arena);

  void BindHead(const int pool_id, void* head_addr);

  void Init();

  void Insert(PmemPtr node_paddr);

  void FindPosition(int pool_id, Node* node, Node* preds[], uint64_t succs[]);

  PmemPtr Lookup(const Key& key, int pool_id);

  void PrintDebugScan();

  Node* head() { return head_[primary_region_pool_id_]; }

  Node* head(int pool_id) { return head_[pool_id]; }

  int primary_pool_id() { return primary_region_pool_id_; }

  PmemPtr head_paddr() { return PmemPtr(primary_region_pool_id_, (char*) head_[primary_region_pool_id_]); }

  pmem::obj::persistent_ptr<char[]> p_head(const int pool_id) { return p_head_[pool_id]; }

 private:
  const int primary_region_pool_id_;
  std::map<int, PmemLog*> arena_;
  std::map<int, Node*> head_;
  std::map<int, pmem::obj::persistent_ptr<char[]>> p_head_;
};


#endif  // LISTDB_INDEX_BRAIDED_PMEM_SKIPLIST_H_
