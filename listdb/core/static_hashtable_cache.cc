#include "static_hashtable_cache.h"

void StaticHashTableCache::Insert(const Key &key, PmemNode *const p) {
  uint32_t pos = Hash(key);
  buckets_[pos].value.store(p, std::memory_order_seq_cst);
}

StaticHashTableCache::PmemNode *
StaticHashTableCache::Lookup(const Key &key) {
  uint32_t pos = Hash(key);
  PmemNode *value = buckets_[pos].value.load(std::memory_order_seq_cst);
  if (value && value->key.Compare(key) == 0) {
    return value;
  }
  return nullptr;
}

uint32_t StaticHashTableCache::Hash(const Key &key) {
  uint32_t h;
  // static const uint32_t seed = 0xcafeb0ba;
#ifndef LISTDB_STRING_KEY
  MurmurHash3_x86_32(&key, sizeof(uint64_t), seed_, (void *)&h);
#else
  MurmurHash3_x86_32(key.data(), kStringKeyLength, seed_, (void *)&h);
#endif
  return h % size_;
}
