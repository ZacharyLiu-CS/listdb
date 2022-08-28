#pragma once

#include "listdb/common.h"
#include "listdb/index/braided_pmem_skiplist.h"
#include "listdb/lib/murmur3.h"
#include "listdb/lib/sha1.h"

#define DOUBLE_HASHING_T_A 1
#define DOUBLE_HASHING_T_B 2
#ifndef LISTDB_DOUBLE_HASHING
#define LISTDB_DOUBLE_HASHING DOUBLE_HASHING_T_B
#endif

class DoubleHashingCache {
 public:
  using PmemNode = BraidedPmemSkipList::Node;

  struct Bucket {
    std::atomic<PmemNode*> value;

    Bucket() : value(nullptr) { }
  };

  DoubleHashingCache(size_t size, int shard);

  Bucket* at(const int i) { return &(buckets_[i]); }

  void Insert(const Key& key, PmemNode* const p);

  PmemNode* Lookup(const Key& key);

  uint32_t Hash1(const Key& key);

  uint32_t Hash2(const Key& key);

 private:
  const static int probing_distance_ = LISTDB_L0_CACHE_PROBING_DISTANCE;

  const size_t size_;
  const int shard_;
  const uint32_t seed_;
  Bucket* buckets_;
};
