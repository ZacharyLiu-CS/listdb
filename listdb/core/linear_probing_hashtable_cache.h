#pragma once

#include "listdb/common.h"
#include "listdb/index/braided_pmem_skiplist.h"
#include "listdb/lib/murmur3.h"

#define LP_HASH_T_A 1
#ifndef LISTDB_LINEAR_PROBING_HASHTABLE_CACHE
#define LISTDB_LINEAR_PROBING_HASHTABLE_CACHE LP_HASH_T_A
#endif

class LinearProbingHashTableCache {
 public:
  using PmemNode = BraidedPmemSkipList::Node;

  struct Bucket {
    std::atomic<PmemNode*> value;

    Bucket() : value(nullptr) { }
  };

  LinearProbingHashTableCache(size_t size, int shard);

  Bucket* at(const int i) { return &(buckets_[i]); }

  void Insert(const Key& key, PmemNode* const p);

  PmemNode* Lookup(const Key& key);

  uint32_t Hash1(const Key& key);

 private:
  const static int probing_distance_ = LISTDB_L0_CACHE_PROBING_DISTANCE;

  const size_t size_;
  const int shard_;
  const uint32_t seed_;
  Bucket* buckets_;
};

