#ifndef LISTDB_CORE_STATIC_HASHTABLE_CACHE_H_
#define LISTDB_CORE_STATIC_HASHTABLE_CACHE_H_

#include "listdb/common.h"
#include "listdb/index/braided_pmem_skiplist.h"
#include "listdb/lib/murmur3.h"

class StaticHashTableCache {
public:
  using PmemNode = BraidedPmemSkipList::Node;

  struct Bucket {
    std::atomic<PmemNode *> value;

    Bucket() : value(nullptr) {}
  };

  StaticHashTableCache(size_t size, int shard)
      : size_(size), shard_(shard), seed_(std::hash<int>()(shard_)) {
    buckets_ = new Bucket[size];
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }

  Bucket *at(const int i) { return &(buckets_[i]); }

  void Insert(const Key &key, PmemNode *const p);

  PmemNode *Lookup(const Key &key);

  uint32_t Hash(const Key &key);

private:
  const size_t size_;
  const int shard_;
  const uint32_t seed_;
  Bucket *buckets_;
};

#endif // LISTDB_CORE_STATIC_HASHTABLE_CACHE_H_
