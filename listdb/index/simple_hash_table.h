#ifndef LISTDB_INDEX_SIMPLE_HASH_TABLE_H_
#define LISTDB_INDEX_SIMPLE_HASH_TABLE_H_

#include "listdb/common.h"
#include "listdb/lib/hash.h"

class SimpleHashTable {
 public:
  struct Bucket {
    uint64_t version;
    uint64_t key;
    uint64_t value;
  };

  SimpleHashTable(size_t size);

  Bucket* at(const int i) { return &(buckets_[i]); }

  void Add(const Key& key, const Value& value);

  bool Get(const Key& key, Value* value_out);

 private:
  const size_t size_;
  Bucket* buckets_;
};


#endif  // LISTDB_INDEX_SIMPLE_HASH_TABLE_H_
