#ifndef LISTDB_LISTDB_H_
#define LISTDB_LISTDB_H_

#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <stack>
#include <thread>
#include <unordered_map>

#include <filesystem>

#include <libpmemobj++/pexceptions.hpp>
#include <numa.h>

#include "listdb/common.h"
#ifdef LISTDB_L1_LRU
#include "listdb/core/lru_skiplist.h"
#endif
#ifdef LISTDB_SKIPLIST_CACHE
#include "listdb/core/skiplist_cache.h"
#endif
#include "listdb/core/pmem_blob.h"
#include "listdb/core/pmem_log.h"
#include "listdb/core/static_hashtable_cache.h"
#include "listdb/core/double_hashing_cache.h"
#include "listdb/core/linear_probing_hashtable_cache.h"
#include "listdb/core/pmem_db.h"
#include "listdb/index/braided_pmem_skiplist.h"
#include "listdb/index/lockfree_skiplist.h"
#include "listdb/index/simple_hash_table.h"
#include "listdb/lsm/level_list.h"
#include "listdb/lsm/memtable_list.h"
#include "listdb/lsm/pmemtable.h"
#include "listdb/lsm/pmemtable_list.h"
#include "listdb/util/clock.h"
#include "listdb/util/random.h"
#include "listdb/util/reporter.h"
#include "listdb/util/reporter_client.h"

#define L0_COMPACTION_ON_IDLE
#define L0_COMPACTION_YIELD

//#define REPORT_BACKGROUND_WORKS
#ifdef REPORT_BACKGROUND_WORKS
#define INIT_REPORTER_CLIENT auto reporter_client = new ReporterClient(reporter_)
#define REPORT_FLUSH_OPS(x) reporter_client->ReportFinishedOps(Reporter::OpType::kFlush, x)
#define REPORT_COMPACTION_OPS(x) reporter_client->ReportFinishedOps(Reporter::OpType::kCompaction, x)
#define REPORT_DONE delete reporter_client
#else
#define INIT_REPORTER_CLIENT
#define REPORT_FLUSH_OPS(x)
#define REPORT_COMPACTION_OPS(x)
#define REPORT_DONE
#endif


//#define L0_COMPACTION_ON_IDLE
//#define L0_COMPACTION_YIELD


// // namespace fs = std::experimental::filesystem::v1;
namespace fs = std::filesystem;
class ListDB {
 public:
  using MemNode = lockfree_skiplist::Node;
  using PmemNode = BraidedPmemSkipList::Node;

  struct Task {
    TaskType type;
    int shard;
  };

  struct MemTableFlushTask : Task {
    MemTable* imm;
    MemTableList* memtable_list;
  };

  struct L0CompactionTask : Task {
    PmemTable* l0;
    MemTableList* memtable_list;
  };

  struct alignas(64) CompactionWorkerData {
    int id;
    bool stop;
    Random rnd = Random(0);
    std::queue<Task*> q;
    std::mutex mu;
    std::condition_variable cv;
    Task* current_task;
    uint64_t flush_cnt = 0;
    uint64_t flush_time_usec = 0;
    //uint64_t compaction_cnt = 0;
    //uint64_t compaction_time_usec = 0;
  };

  enum class ServiceStatus {
    kActive,
    kStop,
  };

  ~ListDB();

  void Init(std::string db_path="/mnt/pmem0/listdb", uint64_t pool_size = 64 * 1024 * 1024);

  void Open(std::string db_path="/mnt/pmem0/listdb", uint64_t pool_size = 64 * 1024 * 1024);

  void Close();

  //void Put(const Key& key, const Value& value);

  void WaitForStableState();

  Reporter* GetOrCreateReporter(const std::string& fname);

  // private:
  MemTable* GetWritableMemTable(size_t kv_size, int shard);

  MemTable* GetMemTable(int shard);

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
  SimpleHashTable* GetHashTable(int shard);
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
  StaticHashTableCache* GetHashTable(int shard);
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
  DoubleHashingCache* GetHashTable(int shard);
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
  LinearProbingHashTableCache* GetHashTable(int shard);
#endif

  TableList* GetTableList(int level, int shard);

  template <typename T>
  T* GetTableList(int level, int shard);

  PmemLog* log(int region, int shard) { return log_[region][shard]; }

#ifdef LISTDB_WISCKEY
  PmemBlob* value_blob(const int region, const int shard) { return value_blob_[region][shard]; }
#endif

  int pool_id_to_region(const int pool_id) { return pool_id_to_region_[pool_id]; }

  int l0_pool_id(const int region) { return l0_pool_id_[region]; }

  int l1_pool_id(const int region) { return l1_pool_id_[region]; }

  // Background Works
  void SetL0CompactionSchedulerStatus(const ServiceStatus& status);

  void BackgroundThreadLoop();

  void CompactionWorkerThreadLoop(CompactionWorkerData* td);

  void FlushMemTable(MemTableFlushTask* task, CompactionWorkerData* td);

  void FlushMemTableWAL(MemTableFlushTask* task, CompactionWorkerData* td);

  void FlushMemTableToL1WAL(MemTableFlushTask* task, CompactionWorkerData* td);

  void ManualFlushMemTable(int shard);

  void ZipperCompactionL0(CompactionWorkerData* td, L0CompactionTask* task);

  void L0CompactionCopyOnWrite(L0CompactionTask* task);

  // Utility Functions
  void PrintDebugLsmState(int shard);

  int GetStatString(const std::string& name, std::string* buf);

#ifdef LISTDB_L1_LRU
  std::vector<std::pair<uint64_t, uint64_t>>& sorted_arr(int r, int s) { return sorted_arr_[r][s]; }
  LruSkipList* lru_cache(int s, int r) { return cache_[s][r]; }

  size_t total_sorted_arr_size() {
    size_t total_size = 0;
    for (auto& rs : sorted_arr_) {
      for (auto& ss : rs) {
        total_size += ss.size() * 16;
      }
    }
    return total_size;
  }
#endif
#ifdef LISTDB_SKIPLIST_CACHE
  SkipListCacheRep* skiplist_cache(int s, int r) { return cache_[s][r]; }
#endif

 private:
#ifdef LISTDB_WISCKEY
  PmemBlob* value_blob_[kNumRegions][kNumShards];
#endif
  PmemLog* log_[kNumRegions][kNumShards];
  PmemLog* l0_arena_[kNumRegions][kNumShards];
  PmemLog* l1_arena_[kNumRegions][kNumShards];
  LevelList* ll_[kNumShards];

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
  SimpleHashTable* hash_table_[kNumShards];
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
  StaticHashTableCache* hash_table_[kNumShards];
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
  DoubleHashingCache* hash_table_[kNumShards];
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
  LinearProbingHashTableCache* hash_table_[kNumShards];
#endif

  std::unordered_map<int, int> pool_id_to_region_;
  std::unordered_map<int, int> log_pool_id_;
  std::unordered_map<int, int> l0_pool_id_;
  std::unordered_map<int, int> l1_pool_id_;

  //std::queue<MemTableFlushTask*> memtable_flush_queue_;
  //std::mutex bg_mu_;
  //std::condition_variable bg_cv_;

  std::deque<Task*> work_request_queue_;
  std::deque<Task*> work_completion_queue_;
  std::mutex wq_mu_;
  std::condition_variable wq_cv_;

  std::thread bg_thread_;
  bool stop_ = false;
  ServiceStatus l0_compaction_scheduler_status_ = ServiceStatus::kActive;

  CompactionWorkerData worker_data_[kNumWorkers];
  std::thread worker_threads_[kNumWorkers];

#ifdef LISTDB_L1_LRU
  std::vector<std::pair<uint64_t, uint64_t>> sorted_arr_[kNumRegions][kNumShards];
  LruSkipList* cache_[kNumShards][kNumRegions];
#endif

#ifdef LISTDB_SKIPLIST_CACHE
  SkipListCacheRep* cache_[kNumShards][kNumRegions];
#endif

  std::atomic<Reporter*> reporter_;
  std::mutex mu_;
};
inline MemTable* ListDB::GetWritableMemTable(size_t kv_size, int shard) {
  auto tl = ll_[shard]->GetTableList(0);
  auto mem = tl->GetMutable(kv_size);
  return (MemTable*) mem;
}

inline MemTable* ListDB::GetMemTable(int shard) {
  auto tl = ll_[shard]->GetTableList(0);
  auto mem = tl->GetFront();
  return (MemTable*) mem;
}

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
  inline SimpleHashTable* ListDB::GetHashTable(int shard) {
    //return hash_table_[shard];
    return hash_table_[0];
  }
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
  inline StaticHashTableCache* ListDB::GetHashTable(int shard) {
    return hash_table_[shard];
  }
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
  inline DoubleHashingCache* ListDB::GetHashTable(int shard) {
    return hash_table_[shard];
  }
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
  inline LinearProbingHashTableCache* ListDB::GetHashTable(int shard) {
    return hash_table_[shard];
  }
#endif

inline TableList* ListDB::GetTableList(int level, int shard) {
  auto tl = ll_[shard]->GetTableList(level);
  return tl;
}

template <typename T>
inline T* ListDB::GetTableList(int level, int shard) {
  auto tl = ll_[shard]->GetTableList(level);
  return (T*) tl;
}


#endif  // LISTDB_LISTDB_H_
