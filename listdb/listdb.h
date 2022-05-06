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

#include <experimental/filesystem>

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
//#include "listdb/core/pmem_db.h"
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


namespace fs = std::experimental::filesystem::v1;

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

  ~ListDB();

  void Init();

  //void Put(const Key& key, const Value& value);

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

  PmemLog* log(int region, int shard) { return log_[region][shard]; }

#ifdef LISTDB_WISCKEY
  PmemBlob* value_blob(const int region, const int shard) { return value_blob_[region][shard]; }
#endif

  int pool_id_to_region(const int pool_id) { return pool_id_to_region_[pool_id]; }

  int l0_pool_id(const int region) { return l0_pool_id_[region]; }

  int l1_pool_id(const int region) { return l1_pool_id_[region]; }

  Reporter* GetOrCreateReporter(const std::string& fname);

  // Background Works
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

  std::queue<MemTableFlushTask*> memtable_flush_queue_;
  std::mutex bg_mu_;
  std::condition_variable bg_cv_;

  std::deque<Task*> work_request_queue_;
  std::deque<Task*> work_completion_queue_;
  std::mutex wq_mu_;
  std::condition_variable wq_cv_;

  std::thread bg_thread_;
  bool stop_ = false;

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


ListDB::~ListDB() {
  fprintf(stdout, "D \n");
  stop_ = true;
  bg_thread_.join();

  for (int i = 0; i < kNumWorkers; i++) {
    worker_data_[i].stop = true;
    worker_data_[i].cv.notify_one();
    worker_threads_[i].join();
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    auto reporter = reporter_.load();
    if (reporter != nullptr) {
      delete reporter_;
    }
  }
}

void ListDB::Init() {
  // Log Pmem Pool
  for (int i = 0; i < kNumRegions; i++) {
    std::stringstream pss;
    pss << "/pmem" << i << "/wkim/listdb_log";
    std::string path = pss.str();
    fs::remove_all(path);
    fs::create_directories(path);

    std::string poolset = path + ".set";
    std::fstream strm(poolset, strm.out);
    strm << "PMEMPOOLSET" << std::endl;
    strm << "OPTION SINGLEHDR" << std::endl;
    strm << "400G " << path << "/" << std::endl;
    strm.close();

    int pool_id = Pmem::BindPoolSet<pmem_log_root>(poolset, "");
    pool_id_to_region_[pool_id] = i;
    log_pool_id_[i] = pool_id;
    auto pool = Pmem::pool<pmem_log_root>(pool_id);

    for (int j = 0; j < kNumShards; j++) {
      log_[i][j] = new PmemLog(pool_id, j);
    }
  }
#ifndef LISTDB_WAL
  // IUL
  for (int i = 0; i < kNumRegions; i++) {
    l0_pool_id_[i] = log_pool_id_[i];
    for (int j = 0; j < kNumShards; j++) {
      l0_arena_[i][j] = log_[i][j];
    }
  }
#else
  #if LISTDB_FLUSH_MEMTABLE_TO_L1 == 1
  for (int i = 0; i < kNumRegions; i++) {
    l0_pool_id_[i] = log_pool_id_[i];
    for (int j = 0; j < kNumShards; j++) {
      l0_arena_[i][j] = log_[i][j];
    }
  }
  #else
  // WAL
  for (int i = 0; i < kNumRegions; i++) {
    std::stringstream pss;
    pss << "/pmem" << i << "/wkim/listdb_nonunified_l0";
    std::string path = pss.str();
    fs::remove_all(path);
    fs::create_directories(path);

    std::string poolset = path + ".set";
    std::fstream strm(poolset, strm.out);
    strm << "PMEMPOOLSET" << std::endl;
    strm << "OPTION SINGLEHDR" << std::endl;
    strm << "400G " << path << "/" << std::endl;
    strm.close();

    int pool_id = Pmem::BindPoolSet<pmem_blob_root>(poolset, "");
    pool_id_to_region_[pool_id] = i;
    l0_pool_id_[i] = pool_id;
    auto pool = Pmem::pool<pmem_blob_root>(pool_id);

    for (int j = 0; j < kNumShards; j++) {
      l0_arena_[i][j] = new PmemLog(pool_id, j);
    }
  }
  #endif
#endif


#ifdef LISTDB_WISCKEY
  for (int i = 0; i < kNumRegions; i++) {
    std::stringstream pss;
    pss << "/pmem" << i << "/wkim/listdb_value";
    std::string path = pss.str();
    fs::remove_all(path);
    fs::create_directories(path);

    std::string poolset = path + ".set";
    std::fstream strm(poolset, strm.out);
    strm << "PMEMPOOLSET" << std::endl;
    strm << "OPTION SINGLEHDR" << std::endl;
    strm << "400G " << path << "/" << std::endl;
    strm.close();

    int pool_id = Pmem::BindPoolSet<pmem_blob_root>(poolset, "");
    pool_id_to_region_[pool_id] = i;
    auto pool = Pmem::pool<pmem_blob_root>(pool_id);

    for (int j = 0; j < kNumShards; j++) {
      value_blob_[i][j] = new PmemBlob(pool_id, j);
    }
  }
#endif

#ifdef L1_COW
  // Pmem Pool for L1
  for (int i = 0; i < kNumRegions; i++) {
    std::stringstream pss;
    pss << "/pmem" << i << "/wkim/listdb_l1";
    std::string path = pss.str();
    fs::remove_all(path);
    fs::create_directories(path);

    std::string poolset = path + ".set";
    std::fstream strm(poolset, strm.out);
    strm << "PMEMPOOLSET" << std::endl;
    strm << "OPTION SINGLEHDR" << std::endl;
    strm << "400G " << path << "/" << std::endl;
    strm.close();

    int pool_id = Pmem::BindPoolSet<pmem_log_root>(poolset, "");
    pool_id_to_region_[pool_id] = i;
    auto pool = Pmem::pool<pmem_log_root>(pool_id);

    for (int j = 0; j < kNumShards; j++) {
      l1_arena_[i][j] = new PmemLog(pool_id, j);
    }
  }
#else
  for (int i = 0; i < kNumRegions; i++) {
    for (int j = 0; j < kNumShards; j++) {
      l1_arena_[i][j] = l0_arena_[i][j];
    }
  }
#endif
  for (int i = 0; i < kNumRegions; i++) {
    l1_pool_id_[i] = l1_arena_[i][0]->pool_id();
  }

  for (int i = 0; i < kNumShards; i++) {
    ll_[i] = new LevelList();
    // MemTableList
    {
      auto tl = new MemTableList(kMemTableCapacity / kNumShards);
      tl->BindEnqueueFunction([&, tl, i](MemTable* mem) {
        //fprintf(stdout, "binded enq fn, mem = %p\n", mem);
        auto task = new MemTableFlushTask();
        task->type = TaskType::kMemTableFlush;
        task->shard = i;
        task->imm = mem;
        task->memtable_list = tl;
        std::unique_lock<std::mutex> lk(wq_mu_);
        work_request_queue_.push_back(task);
        lk.unlock();
        wq_cv_.notify_one();
      });
      for (int j = 0; j < kNumRegions; j++) {
        tl->BindArena(j, l0_arena_[j][i]);
      }
      ll_[i]->SetTableList(0, tl);
    }

    // L1 List
    {
      auto tl = new PmemTableList(std::numeric_limits<size_t>::max(), l1_arena_[0][0]->pool_id());
      for (int j = 0; j < kNumRegions; j++) {
        tl->BindArena(l1_arena_[j][i]->pool_id(), l1_arena_[j][i]);
      }
      ll_[i]->SetTableList(1, tl);
    }
  }

#if 1
  for (int i = 0; i < kNumShards; i++) {
    auto l1_tl = ll_[i]->GetTableList(1);
    auto l1_table = (PmemTable*) l1_tl->GetFront();
  }
#endif

#ifdef LISTDB_L1_LRU
  for (int i = 0; i < kNumShards; i++) {
    for (int j = 0; j < kNumRegions; j++) {
      cache_[i][j] = new LruSkipList(100000000);
    }
  }
#endif
#ifdef LISTDB_SKIPLIST_CACHE
  for (int i = 0; i < kNumShards; i++) {
    for (int j = 0; j < kNumRegions; j++) {
      cache_[i][j] = new SkipListCacheRep(l1_arena_[j][i]->pool_id(), kSkipListCacheCapacity / kNumShards / kNumRegions);
    }
  }
#endif

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
  for (int i = 0; i < 1; i++) {
    hash_table_[i] = new SimpleHashTable(kHTSize);
    for (size_t j = 0; j < kHTSize; j++) {
      hash_table_[i]->at(j)->version = 1UL;
    }
  }
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
  for (int i = 0; i < kNumShards; i++) {
    hash_table_[i] = new StaticHashTableCache(kHTSize / kNumShards, i);
  }
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
  for (int i = 0; i < kNumShards; i++) {
    hash_table_[i] = new DoubleHashingCache(kHTSize / kNumShards, i);
  }
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
  for (int i = 0; i < kNumShards; i++) {
    hash_table_[i] = new LinearProbingHashTableCache(kHTSize / kNumShards, i);
  }
#endif

  bg_thread_ = std::thread(std::bind(&ListDB::BackgroundThreadLoop, this));

  for (int i = 0; i < kNumWorkers; i++) {
    worker_data_[i].id = i;
    worker_data_[i].stop = false;
    worker_threads_[i] = std::thread(std::bind(&ListDB::CompactionWorkerThreadLoop, this, &worker_data_[i]));

    //sched_param sch;
    //int policy; 
    //pthread_getschedparam(worker_threads_[i].native_handle(), &policy, &sch);
    //sch.sched_priority = 20;
    //pthread_setschedparam(worker_threads_[i].native_handle(), SCHED_FIFO, &sch);
  }
}

Reporter* ListDB::GetOrCreateReporter(const std::string& fname) {
  auto rv = reporter_.load();
  if (rv == nullptr) {
    std::unique_lock<std::mutex> lk(mu_);
    rv = reporter_.load();
    if (rv == nullptr) {
      rv = new Reporter(fname);
      reporter_.store(rv);
    }
  }
  return rv;
}

#if 0
void ListDB::Recover() {
  // Log Pmem Pool
  for (int i = 0; i < kNumRegions; i++) {
    std::stringstream pss;
    pss << "/pmem" << i << "/wkim/listdb_log";
    std::string path = pss.str();

    int pool_id = Pmem::BindPoolSet<pmem_log_root>(poolset, "");
    pool_id_to_region_[pool_id] = i;
    //auto pool = Pmem::pool<pmem_log_root>(pool_id);

    for (int j = 0; j < kNumShards; j++) {
      log_[i][j] = new PmemLog(pool_id, j);
    }
  }

  std::string db_path = "/pmem/wkim/listdb";
  pmem::obj::pool<pmem_db> db_pop;
  try {
    db_pop = pmem::obj::pool<pmem_db>::open(db_path, "");
  } catch (pmem::pool_error& e) {
    std::cerr << e.what() << std::endl;
    exit(1);
  }
  auto db_root = db_pop->root();

  // read l0 info
  // check the status
  // classify the L0 tables according to the status
  //  - recover as memtable
  //  - recover as L0
  //  - recover as L1
}
#endif

void ListDB::BackgroundThreadLoop() {
#if 0
  numa_run_on_node(0);
#endif
  static const int kWorkerQueueDepth = 2;
  std::vector<int> num_assigned_tasks(kNumWorkers);
  std::unordered_map<Task*, int> task_to_worker;
  std::deque<Task*> memtable_flush_requests;
  std::deque<Task*> l0_compaction_requests;
  std::vector<int> l0_compaction_state(kNumShards);
  struct ReqCompCounter {
    size_t req_cnt = 0;
    size_t comp_cnt = 0;
  };
  std::map<TaskType, ReqCompCounter> req_comp_cnt;
  req_comp_cnt.emplace(TaskType::kMemTableFlush, ReqCompCounter());

  while (true) {
    std::deque<Task*> new_work_requests;
    std::deque<Task*> work_completions;
    std::unique_lock<std::mutex> lk(wq_mu_);
    wq_cv_.wait_for(lk, std::chrono::seconds(1), [&]{
      //return !work_request_queue_.empty() || !work_completion_queue_.empty();
      return !work_request_queue_.empty() || !memtable_flush_requests.empty();
    });
    new_work_requests.swap(work_request_queue_);
    work_completions.swap(work_completion_queue_);
    lk.unlock();

    for (auto& task : work_completions) {
      auto it = task_to_worker.find(task);
      int worker_id = it->second;
      task_to_worker.erase(it);
      if (task->type == TaskType::kL0Compaction) {
        l0_compaction_state[task->shard] = 0;
      }
      num_assigned_tasks[worker_id]--;
      req_comp_cnt[task->type].comp_cnt++;
      delete task;
    }

    for (auto& task : new_work_requests) {
      if (task->type == TaskType::kMemTableFlush) {
        memtable_flush_requests.push_back(task);
      } else {
        fprintf(stdout, "unknown task request: %d\n", (int) task->type);
        exit(1);
      }
      req_comp_cnt[task->type].req_cnt++;
    }
#if 1
    for (int i = 0; i < kNumShards; i++) {
      if (l0_compaction_state[i] == 0) {
        auto tl = ll_[i]->GetTableList(0);
        auto table = tl->GetFront();
        while (true) {
          auto next_table = table->Next();
          if (next_table) {
            table = next_table;
          } else {
            break;
          }
        }
        if (table->type() == TableType::kPmemTable) {
          auto task = new L0CompactionTask();
          task->type = TaskType::kL0Compaction;
          task->shard = i;
          task->l0 = (PmemTable*) table;
          task->memtable_list = (MemTableList*) tl;
          l0_compaction_state[i] = 1;  // configured
          l0_compaction_requests.push_back(task);
          req_comp_cnt[task->type].req_cnt++;
        }
      }
    }
#endif

    std::vector<CompactionWorkerData*> available_workers;
    for (int i = 0; i < kNumWorkers; i++) {
      if (num_assigned_tasks[i] < kWorkerQueueDepth) {
        available_workers.push_back(&worker_data_[i]);
      }
    }

    if (!memtable_flush_requests.empty()) {
      while (!available_workers.empty() && !memtable_flush_requests.empty()) {
        std::sort(available_workers.begin(), available_workers.end(), [&](auto& a, auto& b) {
          return num_assigned_tasks[a->id] > num_assigned_tasks[b->id];
        });
        auto& worker = available_workers.back();
        auto& task = memtable_flush_requests.front();
        memtable_flush_requests.pop_front();
        std::unique_lock<std::mutex> wlk(worker->mu);
        worker->q.push(task);
        wlk.unlock();
        worker->cv.notify_one();
        task_to_worker[task] = worker->id;
        num_assigned_tasks[worker->id]++;
        if (num_assigned_tasks[worker->id] >= kWorkerQueueDepth) {
          available_workers.pop_back();
        }
      }
#ifdef L0_COMPACTION_ON_IDLE
      continue;
#endif
    }

    //available_workers.clear();
    //for (int i = 0; i < kNumWorkers; i++) {
    //  if (num_assigned_tasks[i] == 0) {
    //    available_workers.push_back(&worker_data_[i]);
    //  }
    //}
#ifndef LISTDB_NO_L0_COMPACTION
    while (!available_workers.empty() && !l0_compaction_requests.empty()) {
      std::sort(available_workers.begin(), available_workers.end(), [&](auto& a, auto& b) {
        return num_assigned_tasks[a->id] > num_assigned_tasks[b->id];
      });
      auto& worker = available_workers.back();
      auto& task = l0_compaction_requests.front();
      l0_compaction_requests.pop_front();

      std::unique_lock<std::mutex> wlk(worker->mu);
      worker->q.push(task);
      wlk.unlock();
      worker->cv.notify_one();

      task_to_worker[task] = worker->id;
      num_assigned_tasks[worker->id]++;
      l0_compaction_state[task->shard] = 2;  // assigned
      if (num_assigned_tasks[worker->id] >= kWorkerQueueDepth) {
        available_workers.pop_back();
      }
    }
#endif

    if (stop_) {
      fprintf(stdout, "bg thread terminating\n");
      break;
    }
  }
}

void ListDB::CompactionWorkerThreadLoop(CompactionWorkerData* td) {
  td->rnd.Reset((td->id + 1) * (td->id + 1));
  while (true) {
    std::unique_lock<std::mutex> lk(td->mu);
    td->cv.wait(lk, [&]{ return td->stop || !td->q.empty(); });
    if (td->stop) {
      break;
    }
    auto task = td->q.front();
    td->q.pop();
    td->current_task = task;
    lk.unlock();

    if (task->type == TaskType::kMemTableFlush) {
#ifndef LISTDB_WAL
      FlushMemTable((MemTableFlushTask*) task, td);
#else
      FlushMemTableWAL((MemTableFlushTask*) task, td);
#endif
      td->current_task = nullptr;
    } else if (task->type == TaskType::kL0Compaction) {
      ZipperCompactionL0(td, (L0CompactionTask*) task);
      //L0CompactionCopyOnWrite((L0CompactionTask*) task);
      td->current_task = nullptr;
    }
    std::unique_lock<std::mutex> bg_lk(wq_mu_);
    work_completion_queue_.push_back(task);
    bg_lk.unlock();
    wq_cv_.notify_one();
  }
}

//void ListDB::Put(const Key& key, const Value& value) {
//}

// Any table this function returns must be unreferenced manually
// TODO(wkim): Make this function to return a wrapper of table
//   table is unreferenced on the destruction of its wrapper
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

void ListDB::FlushMemTable(MemTableFlushTask* task, CompactionWorkerData* td) {
#if LISTDB_FLUSH_MEMTABLE_TO_L1 == 1
  fprintf(stdout, "FlushMemTableToL1 is not implemented for IUL.\n");
  abort();
#endif
  if (task->shard == 0) fprintf(stdout, "FlushMemTable: %p\n", task->imm);
  // Check Reference Counter
  while (task->imm->w_RefCount() > 0) {
    continue;
  }

  // Flush (IUL)
#if 0
  BraidedPmemSkipList* l0_skiplist = new BraidedPmemSkipList();
  for (int i = 0; i < kNumRegions; i++) {
    auto& arena = log_[i][task->shard];
    l0_skiplist->BindArena(arena->pool_id(), arena);
  }
  l0_skiplist->Init();
#else
  auto l0_skiplist = task->imm->l0_skiplist();
#endif

  auto skiplist = task->imm->skiplist();
  auto mem_node = skiplist->head();
  mem_node = mem_node->next[0].load(MO_RELAXED);

  using Node = PmemNode;
  Node* preds[kNumRegions][kMaxHeight];
  Node* pred;
  pred = l0_skiplist->head(l0_arena_[0][0]->pool_id());
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l0_arena_[i][0]->pool_id();
    for (int j = 1; j < kMaxHeight; j++) {
      preds[i][j] = l0_skiplist->head(pool_id);
    }
  }

#ifdef LISTDB_L0_CACHE
  auto hash_table = GetHashTable(task->shard);
#endif

  uint64_t flush_cnt = 0;
  uint64_t begin_micros = Clock::NowMicros();
  INIT_REPORTER_CLIENT;
  while (mem_node) {
    //std::this_thread::yield();
#ifdef GROUP_LOGGING
    while (((std::atomic<uint64_t>*) &mem_node->value)->load(std::memory_order_relaxed) == 0) continue;
#endif
    int pool_id = ((PmemPtr*) &mem_node->value)->pool_id();
    int region = pool_id_to_region_[pool_id];
    Node* node = ((PmemPtr*) &mem_node->value)->get<Node>();
    int height = node->height();
    for (int i = 1; i < height; i++) {
      //std::this_thread::yield();
      preds[region][i]->next[i] = mem_node->value;
      preds[region][i] = ((PmemPtr*) &(preds[region][i]->next[i]))->get<Node>();
    }
    pred->next[0] = mem_node->value;
    pred = ((PmemPtr*) &(pred->next[0]))->get<Node>();

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
    hash_table->Add(mem_node->key, mem_node->value);
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
    hash_table->Insert(mem_node->key, node);
#endif

    REPORT_FLUSH_OPS(1);
    flush_cnt++;

    //std::this_thread::yield();
    mem_node = mem_node->next[0].load(MO_RELAXED);
  }
  REPORT_DONE;  // Up report all remainings
  uint64_t end_micros = Clock::NowMicros();
  td->flush_cnt += flush_cnt;
  td->flush_time_usec += (end_micros - begin_micros);

  PmemTable* l0_table = new PmemTable(kMemTableCapacity, l0_skiplist);
  task->imm->SetPersistentTable((Table*) l0_table);
  // TODO(wkim): Log this L0 table for recovery
  //task->imm->FinalizeFlush();
  task->memtable_list->CleanUpFlushedImmutables();
}

void ListDB::FlushMemTableToL1WAL(MemTableFlushTask* task, CompactionWorkerData* td) {
  if (task->shard == 0) fprintf(stdout, "MemTable -> L1\n");
  // Check Reference Counter
  while (task->imm->w_RefCount() > 0) {
    continue;
  }

  auto l1_tl = ll_[task->shard]->GetTableList(1);
  auto l1_table = (PmemTable*) l1_tl->GetFront();
  auto l1_skiplist = l1_table->skiplist();

  auto skiplist = task->imm->skiplist();
  auto mem_node = skiplist->head();
  mem_node = mem_node->next[0].load(MO_RELAXED);

  using Node = PmemNode;
  Node* preds[kNumRegions][kMaxHeight];
  uint64_t succs[kNumRegions][kMaxHeight];
  //Node* pred;
  //pred = l1_skiplist->head(l1_arena_[0][0]->pool_id());
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l1_arena_[i][0]->pool_id();
    for (int j = 0; j < kMaxHeight; j++) {
      preds[i][j] = l1_skiplist->head(pool_id);
      succs[i][j] = 0;
    }
  }

  uint64_t flush_cnt = 0;
  uint64_t begin_micros = Clock::NowMicros();
  INIT_REPORTER_CLIENT;
  while (mem_node) {
    int mem_value_pool_id = ((PmemPtr*) &mem_node->value)->pool_id();
    int region = pool_id_to_region_[mem_value_pool_id];

#if 1
    // Determine height
    auto rnd = Random::GetTLSInstance();
    static const unsigned int kBranching = 4;
    int height = 1;
    while (height < kMaxHeight && ((rnd->Next() % kBranching) == 0)) {
      height++;
    }

    // FindPosition
    PmemPtr curr_paddr = preds[region][height - 1]->next[height - 1];
    for (int i = height - 1; i > 0; i--) {
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(mem_node->key) < 0) {
          preds[region][i] = curr;
          curr_paddr = curr->next[i];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      succs[region][i] = curr_paddr.dump();
    }
    {
      PmemPtr curr_paddr = preds[0][0]->next[0];
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(mem_node->key) < 0) {
          preds[0][0] = curr;
          curr_paddr = curr->next[0];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      succs[0][0] = curr_paddr.dump();
    }

    // Init new node
    size_t node_size = sizeof(PmemNode) + (height - 1) * sizeof(uint64_t);
    auto pool = Pmem::pool<pmem_log_root>(mem_value_pool_id);
    pmem::obj::persistent_ptr<char[]> p_buf;
    pmem::obj::make_persistent_atomic<char[]>(pool, p_buf, node_size);
    Node* node = (PmemNode*) p_buf.get();
    PmemPtr node_paddr = PmemPtr(mem_value_pool_id, ((uintptr_t) node - (uintptr_t) pool.handle()));
    node->tag = height;
    node->value = mem_node->value;
    node->next[0] = succs[0][0];
    for (int i = 1; i < height; i++) {
      node->next[i] = succs[region][i];
    }
    node->key = mem_node->key;
    clwb(node, node_size);
    _mm_sfence();
    preds[0][0]->next[0] = node_paddr.dump();
    clwb(&(preds[0][0]->next[0]), 8);
    _mm_sfence();
    for (int i = 1 ;i < height; i++) {
      preds[region][i]->next[i] = node_paddr.dump();
    }
#else
    auto rnd = Random::GetTLSInstance();
    static const unsigned int kBranching = 4;
    int height = 1;
    while (height < kMaxHeight && ((rnd->Next() % kBranching) == 0)) {
      height++;
    }
    size_t node_size = sizeof(PmemNode) + (height - 1) * sizeof(uint64_t);
    auto pool = Pmem::pool<pmem_log_root>(mem_value_pool_id);
    pmem::obj::persistent_ptr<char[]> p_buf;
    pmem::obj::make_persistent_atomic<char[]>(pool, p_buf, node_size);
    Node* node = (PmemNode*) p_buf.get();
    PmemPtr node_paddr = PmemPtr(mem_value_pool_id, ((uintptr_t) node - (uintptr_t) pool.handle()));
    node->tag = height;
    node->value = mem_node->value;
    _mm_sfence();
    node->key = mem_node->key;
    clwb(node, sizeof(PmemNode) - sizeof(uint64_t));
    l1_skiplist->Insert(node_paddr);
#endif
    REPORT_FLUSH_OPS(1);
    flush_cnt++;

    //std::this_thread::yield();
    mem_node = mem_node->next[0].load(MO_RELAXED);
  }
  REPORT_DONE;  // Up report all remainings
  uint64_t end_micros = Clock::NowMicros();
  td->flush_cnt += flush_cnt;
  td->flush_time_usec += (end_micros - begin_micros);

  task->imm->SetPersistentTable((Table*) l1_table);
  // TODO(wkim): Log this L0 table for recovery
  //task->imm->FinalizeFlush();
  task->memtable_list->CleanUpFlushedImmutables();
}

void ListDB::FlushMemTableWAL(MemTableFlushTask* task, CompactionWorkerData* td) {
#if LISTDB_FLUSH_MEMTABLE_TO_L1 == 1
  return FlushMemTableToL1WAL(task, td);
#endif
  if (task->shard == 0) fprintf(stdout, "FlushMemTable: %p\n", task->imm);
  // Check Reference Counter
  while (task->imm->w_RefCount() > 0) {
    continue;
  }

  // Flush (WAL)
  auto l0_skiplist = task->imm->l0_skiplist();

  auto skiplist = task->imm->skiplist();
  auto mem_node = skiplist->head();
  mem_node = mem_node->next[0].load(MO_RELAXED);

  using Node = PmemNode;
  Node* preds[kNumRegions][kMaxHeight];
  Node* pred;
  pred = l0_skiplist->head(l0_arena_[0][0]->pool_id());
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l0_arena_[i][0]->pool_id();
    for (int j = 1; j < kMaxHeight; j++) {
      preds[i][j] = l0_skiplist->head(pool_id);
    }
  }

#ifdef LISTDB_L0_CACHE
  auto hash_table = GetHashTable(task->shard);
#endif

  uint64_t flush_cnt = 0;
  uint64_t begin_micros = Clock::NowMicros();
  INIT_REPORTER_CLIENT;
  while (mem_node) {
    //std::this_thread::yield();
#ifdef GROUP_LOGGING
    while (((std::atomic<uint64_t>*) &mem_node->value)->load(std::memory_order_relaxed) == 0) continue;
#endif
    int mem_value_pool_id = ((PmemPtr*) &mem_node->value)->pool_id();
    int region = pool_id_to_region_[mem_value_pool_id];

    auto rnd = Random::GetTLSInstance();

#if defined(LISTDB_L1_LRU) || defined(LISTDB_SKIPLIST_CACHE)
    static const unsigned int kBranching = 2;
#else
    static const unsigned int kBranching = 4;
#endif
    int height = 1;
    if (rnd->Next() % std::max<int>(1, (kBranching / kNumRegions)) == 0) {
      height++;
      while (height < kMaxHeight && ((rnd->Next() % kBranching) == 0)) {
        height++;
      }
    }

    size_t node_size = sizeof(PmemNode) + (height - 1) * sizeof(uint64_t);
    //auto node_paddr = l0_arena_[region][task->shard]->Allocate(node_size);
    //Node* node = node_paddr.get<PmemNode>();
    auto pool = Pmem::pool<pmem_log_root>(mem_value_pool_id);
    pmem::obj::persistent_ptr<char[]> p_buf;
    pmem::obj::make_persistent_atomic<char[]>(pool, p_buf, node_size);
    Node* node = (PmemNode*) p_buf.get();
    PmemPtr node_paddr = PmemPtr(mem_value_pool_id, ((uintptr_t) node - (uintptr_t) pool.handle()));

    node->tag = height;
    node->value = mem_node->value;
    _mm_sfence();
    node->key = mem_node->key;
    clwb(node, sizeof(PmemNode) - sizeof(uint64_t));

    pred->next[0] = node_paddr.dump();
    _mm_sfence();
    clwb(&pred->next[0], 8);
    for (int i = 1; i < height; i++) {
      //std::this_thread::yield();
      preds[region][i]->next[i] = node_paddr.dump();
      preds[region][i] = ((PmemPtr*) &(preds[region][i]->next[i]))->get<Node>();
    }
    pred = ((PmemPtr*) &(pred->next[0]))->get<Node>();

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
    hash_table->Add(mem_node->key, mem_node->value);
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
    hash_table->Insert(mem_node->key, node);
#endif

    REPORT_FLUSH_OPS(1);
    flush_cnt++;

    //std::this_thread::yield();
    mem_node = mem_node->next[0].load(MO_RELAXED);
  }
  REPORT_DONE;  // Up report all remainings
  uint64_t end_micros = Clock::NowMicros();
  td->flush_cnt += flush_cnt;
  td->flush_time_usec += (end_micros - begin_micros);

  PmemTable* l0_table = new PmemTable(kMemTableCapacity, l0_skiplist);
  task->imm->SetPersistentTable((Table*) l0_table);
  // TODO(wkim): Log this L0 table for recovery
  //task->imm->FinalizeFlush();
  task->memtable_list->CleanUpFlushedImmutables();
}

void ListDB::ManualFlushMemTable(int shard) {
#ifndef LISTDB_WAL
  auto tl = reinterpret_cast<MemTableList*>(ll_[shard]->GetTableList(0));

  auto table = tl->GetFront();
  auto table2 = table->Next();

  if (table->type() != TableType::kMemTable || (table2 != nullptr && table2->type() != TableType::kPmemTable)) {
    fprintf(stdout, "ManualFlushMemTable failed\n");
    return;
  }

  tl->NewFront();
  
  // Flush (IUL)
  auto l0_skiplist = reinterpret_cast<MemTable*>(table)->l0_skiplist();

  auto skiplist = reinterpret_cast<MemTable*>(table)->skiplist();
  auto mem_node = skiplist->head();
  mem_node = mem_node->next[0].load(MO_RELAXED);

  using Node = PmemNode;
  Node* preds[kNumRegions][kMaxHeight];
  Node* pred;
  pred = l0_skiplist->head(l0_arena_[0][0]->pool_id());
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l0_arena_[i][0]->pool_id();
    for (int j = 1; j < kMaxHeight; j++) {
      preds[i][j] = l0_skiplist->head(pool_id);
    }
  }

#ifdef LISTDB_L0_CACHE
  auto hash_table = GetHashTable(shard);
#endif

  INIT_REPORTER_CLIENT;
  while (mem_node) {
#ifdef GROUP_LOGGING
    while (((std::atomic<uint64_t>*) &mem_node->value)->load(std::memory_order_relaxed) == 0) continue;
#endif
    int pool_id = ((PmemPtr*) &mem_node->value)->pool_id();
    int region = pool_id_to_region_[pool_id];
    Node* node = ((PmemPtr*) &mem_node->value)->get<Node>();
    int height = node->height();
    for (int i = 1; i < height; i++) {
      preds[region][i]->next[i] = mem_node->value;
      preds[region][i] = ((PmemPtr*) &(preds[region][i]->next[i]))->get<Node>();
    }
    pred->next[0] = mem_node->value;
    pred = ((PmemPtr*) &(pred->next[0]))->get<Node>();

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
    hash_table->Add(mem_node->key, mem_node->value);
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
    hash_table->Insert(mem_node->key, node);
#endif

    REPORT_FLUSH_OPS(1);

    //std::this_thread::yield();
    mem_node = mem_node->next[0].load(MO_RELAXED);
  }
  REPORT_DONE;  // Up report all remainings

  PmemTable* l0_table = new PmemTable(kMemTableCapacity, l0_skiplist);
  reinterpret_cast<MemTable*>(table)->SetPersistentTable((Table*) l0_table);
  // TODO(wkim): Log this L0 table for recovery
  tl->CleanUpFlushedImmutables();
#else

  auto tl = reinterpret_cast<MemTableList*>(ll_[shard]->GetTableList(0));

  auto table = tl->GetFront();
  auto table2 = table->Next();

  if (table->type() != TableType::kMemTable || (table2 != nullptr && table2->type() != TableType::kPmemTable)) {
    fprintf(stdout, "ManualFlushMemTable failed\n");
    return;
  }

  tl->NewFront();
  
  // Flush (WAL)
  auto l0_skiplist = reinterpret_cast<MemTable*>(table)->l0_skiplist();

  auto skiplist = reinterpret_cast<MemTable*>(table)->skiplist();
  auto mem_node = skiplist->head();
  mem_node = mem_node->next[0].load(MO_RELAXED);

  using Node = PmemNode;
  Node* preds[kNumRegions][kMaxHeight];
  Node* pred;
  pred = l0_skiplist->head(l0_arena_[0][0]->pool_id());
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l0_arena_[i][0]->pool_id();
    for (int j = 1; j < kMaxHeight; j++) {
      preds[i][j] = l0_skiplist->head(pool_id);
    }
  }

#ifdef LISTDB_L0_CACHE
  auto hash_table = GetHashTable(shard);
#endif

  INIT_REPORTER_CLIENT;
  while (mem_node) {
#ifdef GROUP_LOGGING
    while (((std::atomic<uint64_t>*) &mem_node->value)->load(std::memory_order_relaxed) == 0) continue;
#endif
    int mem_value_pool_id = ((PmemPtr*) &mem_node->value)->pool_id();
    int region = pool_id_to_region_[mem_value_pool_id];

    auto rnd = Random::GetTLSInstance();

#if defined(LISTDB_L1_LRU) || defined(LISTDB_SKIPLIST_CACHE)
    static const unsigned int kBranching = 2;
#else
    static const unsigned int kBranching = 4;
#endif
    int height = 1;
    if (rnd->Next() % std::max<int>(1, (kBranching / kNumRegions)) == 0) {
      height++;
      while (height < kMaxHeight && ((rnd->Next() % kBranching) == 0)) {
        height++;
      }
    }

    size_t node_size = sizeof(PmemNode) + (height - 1) * sizeof(uint64_t);
    //auto node_paddr = l0_arena_[region][shard]->Allocate(node_size);
    //Node* node = node_paddr.get<PmemNode>();
    auto pool = Pmem::pool<pmem_log_root>(mem_value_pool_id);
    pmem::obj::persistent_ptr<char[]> p_buf;
    pmem::obj::make_persistent_atomic<char[]>(pool, p_buf, node_size);
    Node* node = (PmemNode*) p_buf.get();
    PmemPtr node_paddr = PmemPtr(mem_value_pool_id, ((uintptr_t) node - (uintptr_t) pool.handle()));

    node->tag = height;
    node->value = mem_node->value;
    _mm_sfence();
    node->key = mem_node->key;
    clwb(node, sizeof(PmemNode) - sizeof(uint64_t));

    pred->next[0] = node_paddr.dump();
    _mm_sfence();
    clwb(&pred->next[0], 8);
    for (int i = 1; i < height; i++) {
      preds[region][i]->next[i] = node_paddr.dump();
      preds[region][i] = ((PmemPtr*) &(preds[region][i]->next[i]))->get<Node>();
    }
    pred = ((PmemPtr*) &(pred->next[0]))->get<Node>();

#if LISTDB_L0_CACHE == L0_CACHE_T_SIMPLE
    hash_table->Add(mem_node->key, mem_node->value);
#elif LISTDB_L0_CACHE == L0_CACHE_T_STATIC
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_DOUBLE_HASHING
    hash_table->Insert(mem_node->key, node);
#elif LISTDB_L0_CACHE == L0_CACHE_T_LINEAR_PROBING
    hash_table->Insert(mem_node->key, node);
#endif

    REPORT_FLUSH_OPS(1);

    //std::this_thread::yield();
    mem_node = mem_node->next[0].load(MO_RELAXED);
  }
  REPORT_DONE;  // Up report all remainings

  PmemTable* l0_table = new PmemTable(kMemTableCapacity, l0_skiplist);
  reinterpret_cast<MemTable*>(table)->SetPersistentTable((Table*) l0_table);
  // TODO(wkim): Log this L0 table for recovery
  tl->CleanUpFlushedImmutables();
#endif
}

void ListDB::ZipperCompactionL0(CompactionWorkerData* td, L0CompactionTask* task) {
#if 0
  if (task->shard == 0) fprintf(stdout, "L0 compaction\n");
  using Node = PmemNode;
  auto l0_skiplist = task->l0->skiplist();

  auto l1_tl = ll_[task->shard]->GetTableList(1);
  if (l1_tl->IsEmpty()) {
    auto l1_table = new PmemTable(std::numeric_limits<size_t>::max(), l0_skiplist);
    l1_tl->SetFront(l1_table);
    auto table = task->memtable_list->GetFront();
    while (true) {
      auto next_table = table->Next();
      if (next_table) {
        if (next_table == (Table*) task->l0) {
          table->SetNext(nullptr);
          break;
        }
        table = next_table;
      } else {
        break;
      }
    }
    return;
  }
  auto l1_skiplist = ((PmemTable*) l1_tl->GetFront())->skiplist();

  struct ZipperItem {
    PmemPtr node_paddr;
    Node* preds[kMaxHeight];
    uint64_t succs[kMaxHeight];
  };

  std::stack<ZipperItem*> zstack;
  Node* preds[kNumRegions][kMaxHeight];
  uint64_t succs[kNumRegions][kMaxHeight];
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l1_arena_[i][0]->pool_id();
    for (int j = 0; j < kMaxHeight; j++) {
      preds[i][j] = l1_skiplist->head(pool_id);
      succs[i][j] = 0;
    }
  }

  PmemPtr node_paddr = l0_skiplist->head_paddr();

  // 1. Scan
  while (true) {
    std::this_thread::yield();
    auto l0_node = node_paddr.get<Node>();
    if (l0_node == nullptr) {
      break;
    }
    int pool_id = node_paddr.pool_id();
    int region = pool_id_to_region_[pool_id];
    int height = l0_node->height();
    PmemPtr curr_paddr = preds[region][height - 1]->next[height - 1];
    for (int i = height - 1; i > 0; i--) {
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(l0_node->key) < 0) {
          preds[region][i] = curr;
          curr_paddr = curr->next[i];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      succs[region][i] = curr_paddr.dump();
    }
    {
      PmemPtr curr_paddr = preds[0][0]->next[0];
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(l0_node->key) < 0) {
          preds[0][0] = curr;
          curr_paddr = curr->next[0];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      succs[0][0] = curr_paddr.dump();
    }
    auto z = new ZipperItem();
    z->node_paddr = node_paddr;
    z->preds[0] = preds[0][0];
    for (int i = 1; i < kMaxHeight; i++) {
      z->preds[i] = preds[region][i];
    }
    z->succs[0] = succs[0][0];
    for (int i = 1; i < kMaxHeight; i++) {
      z->succs[i] = succs[region][i];
    }
    zstack.push(z);
    node_paddr = l0_node->next[0];
  }

  // 2. Merge
  //bool print_debug = false;
  //if (preds[0][0]->next[0] != 0) {
  //  print_debug = true;
  //}
  while (!zstack.empty()) {
    std::this_thread::yield();
    auto& z = zstack.top();
    auto l0_node = z->node_paddr.get<Node>();
    //if (print_debug) {
    //  std::stringstream debug_ss;
    //  debug_ss << "L0 key: " << l0_node->key << std::endl;
    //  for (int i = kMaxHeight - 1; i >= 0; i--) {
    //    Node* succ = ((PmemPtr*) &(z->succs[i]))->get<Node>();
    //    debug_ss << "level: " << std::setw(2) << i << "\t" << z->preds[i]->key << "\t";
    //    if (succ) {
    //      debug_ss << succ->key << std::endl;
    //    } else {
    //      debug_ss << "NULL" << std::endl;
    //    }
    //  }
    //  fprintf(stdout, "%s\n", debug_ss.str().c_str());
    //}
    {
      auto l0_succ = ((PmemPtr*) &(l0_node->next[0]))->get<Node>();
      auto l1_succ = ((PmemPtr*) &(z->succs[0]))->get<Node>();
      if (l0_succ == nullptr || (l1_succ && l1_succ->key.Compare(l0_succ->key) < 0)) {
        l0_node->next[0] = z->succs[0];
      }
      z->preds[0]->next[0] = z->node_paddr.dump();
    }
    for (int i = 1; i < l0_node->height(); i++) {
      auto l0_succ = ((PmemPtr*) &(l0_node->next[i]))->get<Node>();
      auto l1_succ = ((PmemPtr*) &(z->succs[i]))->get<Node>();
      if (l0_succ == nullptr || (l1_succ && l1_succ->key.Compare(l0_succ->key) < 0)) {
        l0_node->next[i] = z->succs[i];
      }
      z->preds[i]->next[i] = z->node_paddr.dump();
    }
    zstack.pop();
    delete z;
  }
  // Remove empty L0 from MemTableList
  //auto tl = ll_[task->shard]->GetTableList(0);
  auto table = task->memtable_list->GetFront();
  while (true) {
    auto next_table = table->Next();
    if (next_table) {
      if (next_table == (Table*) task->l0) {
        table->SetNext(nullptr);
        break;
      }
      table = next_table;
    } else {
      break;
    }
  }
#else
#if 1
  if (task->shard == 0) fprintf(stdout, "L0 compaction\n");
  using Node = PmemNode;
  auto l0_skiplist = task->l0->skiplist();

  auto l1_tl = ll_[task->shard]->GetTableList(1);
  if (l1_tl->IsEmpty()) {
    auto l1_table = new PmemTable(std::numeric_limits<size_t>::max(), l0_skiplist);
    l1_tl->SetFront(l1_table);
    auto table = task->memtable_list->GetFront();
    while (true) {
      auto next_table = table->Next();
      if (next_table) {
        if (next_table == (Table*) task->l0) {
          table->SetNext(nullptr);
          break;
        }
        table = next_table;
      } else {
        break;
      }
    }
    return;
  }
  auto l1_skiplist = ((PmemTable*) l1_tl->GetFront())->skiplist();

  struct ZipperItem {
    PmemPtr node_paddr;
    Node* preds[kMaxHeight];
  };

  std::stack<ZipperItem*> zstack;
  Node* preds[kNumRegions][kMaxHeight];
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l1_arena_[i][0]->pool_id();
    for (int j = 0; j < kMaxHeight; j++) {
      preds[i][j] = l1_skiplist->head(pool_id);
    }
  }

  PmemPtr node_paddr = l0_skiplist->head_paddr();

  // 1. Scan
  while (true) {
#ifdef L0_COMPACTION_YIELD
    std::this_thread::yield();
#endif
    auto l0_node = node_paddr.get<Node>();
    if (l0_node == nullptr) {
      break;
    }
    int pool_id = node_paddr.pool_id();
    int region = pool_id_to_region_[pool_id];
    int height = l0_node->height();
    Node* pred = preds[region][height - 1];
    for (int i = height - 1; i > 0; i--) {
      PmemPtr curr_paddr = pred->next[i];
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(l0_node->key) < 0) {
          pred = curr;
          curr_paddr = pred->next[i];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      preds[region][i] = pred;
    }
    {
      PmemPtr curr_paddr = preds[0][0]->next[0];
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(l0_node->key) < 0) {
          preds[0][0] = curr;
          curr_paddr = curr->next[0];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
    }
    auto z = new ZipperItem();
    z->node_paddr = node_paddr;
    z->preds[0] = preds[0][0];
    for (int i = 1; i < kMaxHeight; i++) {
      z->preds[i] = preds[region][i];
    }
    zstack.push(z);
    node_paddr = l0_node->next[0];
  }

  // 2. Merge
  //bool print_debug = false;
  //if (preds[0][0]->next[0] != 0) {
  //  print_debug = true;
  //}
  INIT_REPORTER_CLIENT;
  while (!zstack.empty()) {
#ifdef L0_COMPACTION_YIELD
    std::this_thread::yield();
#endif
    auto& z = zstack.top();
    auto l0_node = z->node_paddr.get<Node>();
    {
      l0_node->next[0] = z->preds[0]->next[0];
      z->preds[0]->next[0] = z->node_paddr.dump();
      //uint64_t tag = l0_node->tag;
      //tag |= 0x100;
      //l0_node->tag = tag;
    }
    for (int i = 1; i < l0_node->height(); i++) {
      l0_node->next[i] = z->preds[i]->next[i];
      z->preds[i]->next[i] = z->node_paddr.dump();
    }
#ifdef LISTDB_L1_LRU
    if (l0_node->height() >= kMaxHeight - (kNumCachedLevels - 1)) {
      int region = z->node_paddr.pool_id();
      //sorted_arr_[region][task->shard].emplace_back(l0_node->key, z->node_paddr.dump());
      //int lru_height = l0_node->height() - (kMaxHeight - kLruMaxHeight);
      //lru_height = (lru_height + 1) / 2;
      int lru_height = 1;
      while (lru_height < kLruMaxHeight && td->rnd.Next() % 2== 0) {
        lru_height++;
      }
      cache_[task->shard][region]->Insert(l0_node->key, z->node_paddr.dump(), lru_height);
    }
#endif

#ifdef LISTDB_SKIPLIST_CACHE
    if (l0_node->height() >= kSkipListCacheMinPmemHeight) {
      int region = pool_id_to_region_[z->node_paddr.pool_id()];
      cache_[task->shard][region]->Insert(l0_node);
    }
#endif
    REPORT_COMPACTION_OPS(1);
    zstack.pop();
    delete z;
  }
  REPORT_DONE;  // Up report all remainings

#ifdef LISTDB_L1_LRU
  using MyType1 = std::pair<uint64_t, uint64_t>;
  for (int i = 0; i < kNumRegions; i++) {
    std::sort(sorted_arr_[i][task->shard].begin(), sorted_arr_[i][task->shard].end(),
        [&](const MyType1 &a, const MyType1 &b) { return a.first > b.first; });
  }
#endif

  // Remove empty L0 from MemTableList
  //auto tl = ll_[task->shard]->GetTableList(0);
  auto table = task->memtable_list->GetFront();
  while (true) {
    auto next_table = table->Next();
    if (next_table) {
      if (next_table == (Table*) task->l0) {
        table->SetNext(nullptr);
        break;
      }
      table = next_table;
    } else {
      break;
    }
  }
#else
  // Insert N times
  // For Test
  if (task->shard == 0) fprintf(stdout, "L0 compaction\n");

  using Node = PmemNode;
  auto l0_skiplist = task->l0->skiplist();

  auto l1_tl = ll_[task->shard]->GetTableList(1);
  if (l1_tl->IsEmpty()) {
    auto l1_table = new PmemTable(std::numeric_limits<size_t>::max(), l0_skiplist);
    l1_tl->SetFront(l1_table);
    auto table = task->memtable_list->GetFront();
    while (true) {
      auto next_table = table->Next();
      if (next_table) {
        if (next_table == (Table*) task->l0) {
          table->SetNext(nullptr);
          break;
        }
        table = next_table;
      } else {
        break;
      }
    }
    return;
  }
  auto l1_skiplist = ((PmemTable*) l1_tl->GetFront())->skiplist();

  PmemPtr node_paddr = l0_skiplist->head_paddr();

  std::stack<PmemPtr> nstack;

  // 1. Scan
  while (true) {
    auto l0_node = node_paddr.get<Node>();
    if (l0_node == nullptr) {
      break;
    }

    nstack.push(node_paddr);
    node_paddr = l0_node->next[0];
  }

  // 2. Merge
  while (!nstack.empty()) {
    auto& l0_paddr = nstack.top();
    //auto l0_node = l0_paddr.get<Node>();

    l1_skiplist->Insert(l0_paddr);

    nstack.pop();
  }
  // Remove empty L0 from MemTableList
  //auto tl = ll_[task->shard]->GetTableList(0);
  auto table = task->memtable_list->GetFront();
  while (true) {
    auto next_table = table->Next();
    if (next_table) {
      if (next_table == (Table*) task->l0) {
        table->SetNext(nullptr);
        break;
      }
      table = next_table;
    } else {
      break;
    }
  }
#endif
#endif
}

void ListDB::L0CompactionCopyOnWrite(L0CompactionTask* task) {
  if (task->shard == 0) fprintf(stdout, "L0 compaction\n");

  using Node = PmemNode;
  auto l0_skiplist = task->l0->skiplist();

  auto l1_tl = ll_[task->shard]->GetTableList(1);
  auto l1_skiplist = ((PmemTable*) l1_tl->GetFront())->skiplist();

  Node* preds[kNumRegions][kMaxHeight];
  uint64_t succs[kNumRegions][kMaxHeight];
  for (int i = 0; i < kNumRegions; i++) {
    int pool_id = l1_arena_[i][0]->pool_id();
    for (int j = 0; j < kMaxHeight; j++) {
      preds[i][j] = l1_skiplist->head(pool_id);
      succs[i][j] = 0;
    }
  }

  PmemPtr node_paddr = l0_skiplist->head_paddr();

  INIT_REPORTER_CLIENT;
  while (true) {
#ifdef L0_COMPACTION_YIELD
    std::this_thread::yield();
#endif
    auto l0_node = node_paddr.get<Node>();
    if (l0_node == nullptr) {
      break;
    }

    int pool_id = node_paddr.pool_id();
    int region = pool_id_to_region_[pool_id];
    int height = l0_node->height();
    PmemPtr curr_paddr = preds[region][height - 1]->next[height - 1];
    for (int i = height - 1; i > 0; i--) {
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(l0_node->key) < 0) {
          preds[region][i] = curr;
          curr_paddr = curr->next[i];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      succs[region][i] = curr_paddr.dump();
    }
    {
      PmemPtr curr_paddr = preds[0][0]->next[0];
      auto curr = curr_paddr.get<Node>();
      while (curr) {
        if (curr->key.Compare(l0_node->key) < 0) {
          preds[0][0] = curr;
          curr_paddr = curr->next[0];
          curr = curr_paddr.get<Node>();
          continue;
        }
        break;
      }
      succs[0][0] = curr_paddr.dump();
    }

    size_t node_size = sizeof(PmemNode) + (height - 1) * 8;
    auto l1_node_paddr = l1_arena_[region][task->shard]->Allocate(node_size);
    auto l1_node = l1_node_paddr.get<Node>();
    l1_node->key = l0_node->key;
    l1_node->tag = l0_node->tag;
    l1_node->value = l0_node->value;
    l1_node->next[0] = succs[0][0];
    for (int i = 1; i < height; i++) {
      l1_node->next[i] = succs[region][i];
    }
    clwb(l1_node, node_size);
    _mm_sfence();
    preds[0][0]->next[0] = l1_node_paddr.dump();
    for (int i = 1 ;i < height; i++) {
      preds[region][i]->next[i] = l1_node_paddr.dump();
    }

    REPORT_COMPACTION_OPS(1);
    
    node_paddr = l0_node->next[0];
  }
  REPORT_DONE;  // Up report all remainings

  // Remove empty L0 from MemTableList
  //auto tl = ll_[task->shard]->GetTableList(0);
  auto table = task->memtable_list->GetFront();
  while (true) {
    auto next_table = table->Next();
    if (next_table) {
      if (next_table == (Table*) task->l0) {
        table->SetNext(nullptr);
        break;
      }
      table = next_table;
    } else {
      break;
    }
  }
}

void ListDB::PrintDebugLsmState(int shard) {
  auto tl = GetTableList(0, shard);
  auto table = tl->GetFront();
  int mem_cnt = 0;
  int l0_cnt = 0;
  while (table) {
    if (table->type() == TableType::kMemTable) {
      mem_cnt++;
    } else if (table->type() == TableType::kPmemTable) {
      l0_cnt++;
    }
    table = table->Next();
  }

  fprintf(stdout, "mem: %d, L0: %d\n", mem_cnt, l0_cnt);
}

int ListDB::GetStatString(const std::string& name, std::string* buf) {
  int rv = 0;
  std::stringstream ss;
  if (name == "l1_cache_size") {
  #ifdef LISTDB_SKIPLIST_CACHE
    size_t sum = 0;
    size_t max = 0;
    for (int i = 0; i < kNumShards; i++) {
      size_t shard_size = 0;
      for (int j = 0; j < kNumRegions; j++) {
        shard_size += cache_[i][j]->AcquireLoadSize();
      }
      max = std::max<size_t>(max, shard_size);
      sum += shard_size;
    }
    ss << name << ": " << sum << " (per shard max: " << max << ")";
  #else
    rv = 1;
  #endif
  } else if (name == "flush_stats") {
    for (int i = 0; i < kNumWorkers; i++) {
      ss << "worker " << i << ": flush_cnt = " << worker_data_[i].flush_cnt << " flush_time_usec = " << worker_data_[i].flush_time_usec << std::endl;
    }
  } else {
    ss << "Unknown name: " << name;
    rv = 1;
  }
  buf->assign(std::move(ss.str()));
  return rv;
}

#endif  // LISTDB_LISTDB_H_
