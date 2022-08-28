#ifndef LISTDB_DB_CLIENT_H_
#define LISTDB_DB_CLIENT_H_

#include <algorithm>
#include <vector>

#include "listdb/common.h"
#include "listdb/listdb.h"
#include "listdb/util.h"
#include "listdb/util/random.h"

#define LEVEL_CHECK_PERIOD_FACTOR 1

//#define LOG_NTSTORE
class DBClient {
 public:
  using MemNode = ListDB::MemNode;
  using PmemNode = ListDB::PmemNode;

  DBClient(ListDB* db, int id, int region);

  void SetRegion(int region);

  void Put(const Key& key, const Value& value);

  bool Get(const Key& key, Value* value_out);

#if defined(LISTDB_STRING_KEY) && defined(LISTDB_WISCKEY)
  void PutStringKV(const std::string_view& key_sv, const std::string_view& value);
  bool GetStringKV(const std::string_view& key_sv, Value* value_out);
#endif
  
  //void ReserveLatencyHistory(size_t size);
  
  size_t pmem_get_cnt() { return pmem_get_cnt_; }
  size_t search_visit_cnt() { return search_visit_cnt_; }
  size_t height_visit_cnt(int h) { return height_visit_cnt_[h]; }
  

 private:
  int DramRandomHeight();
  int PmemRandomHeight();

  static int KeyShard(const Key& key);

#ifdef LISTDB_EXPERIMENTAL_SEARCH_LEVEL_CHECK
  PmemPtr LevelLookup(const Key& key, const int pool_id, const int level, BraidedPmemSkipList* skiplist);
#endif
  PmemPtr Lookup(const Key& key, const int pool_id, BraidedPmemSkipList* skiplist);
  PmemPtr LookupL1(const Key& key, const int pool_id, BraidedPmemSkipList* skiplist, const int shard);

  ListDB* db_;
  int id_;
  int region_;
  int l0_pool_id_;
  int l1_pool_id_;
  Random rnd_;
  PmemLog* log_[kNumShards];
#ifdef LISTDB_WISCKEY
  PmemBlob* value_blob_[kNumShards];
#endif
  //BraidedPmemSkipList* bsl_[kNumShards];
  size_t pmem_get_cnt_ = 0;
  size_t search_visit_cnt_ = 0;
  size_t height_visit_cnt_[kMaxHeight] = {};

#ifdef GROUP_LOGGING
  struct LogItem {
    Key key;
    uint64_t tag;
    Value value;
    MemNode* mem_node;
    //uint64_t offset;
  };
  std::vector<LogItem> log_group_[kNumShards];
  size_t log_group_alloc_size_[kNumShards];
#endif

  //std::vector<std::chrono::duration<double>> latencies_;
};

#endif  // LISTDB_DB_CLIENT_H_
