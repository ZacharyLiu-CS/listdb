#ifndef LISTDB_CORE_PMEM_LOG_H_
#define LISTDB_CORE_PMEM_LOG_H_

#include <functional>
#include <mutex>

#include <libpmemobj++/make_persistent_atomic.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>

#include "listdb/common.h"
#include "listdb/pmem/pmem.h"
#include "listdb/pmem/pmem_ptr.h"
#include "listdb/lib/memory.h"

template <typename T>
using Pool = pmem::obj::pool<T>;

struct pmem_log;
struct pmem_log_block;

struct LogRecordId {
  uint32_t block_id;
  uint32_t offset;
};

struct LogRange {
  LogRecordId begin;
  LogRecordId end;
};

struct pmem_log_root {
  pmem::obj::persistent_ptr<pmem_log> shard[kNumShards];
};

struct pmem_log {
  uint32_t block_cnt;
  pmem::obj::persistent_ptr<pmem_log_block> head;
};

struct pmem_log_block {
  uint32_t id;
  size_t p;
  char data[kPmemLogBlockSize];
  pmem::obj::persistent_ptr<pmem_log_block> next;

  pmem_log_block(pmem::obj::persistent_ptr<pmem_log_block> next_ = nullptr) : p(0), data(), next(next_) { }
};

// PmemLog

class PmemLog {
 public:
  struct Block {
    std::atomic<size_t> p;  // current end
    char* data;  // pointer to pmem_log_block::data
    pmem::obj::persistent_ptr<pmem_log_block> p_block;

    explicit Block(pmem::obj::persistent_ptr<pmem_log_block> p_block_);
    void* Allocate(const size_t size);
  };
  
  PmemLog(const int pool_id, const int shard_id);

  ~PmemLog();

  PmemPtr Allocate(const size_t size);

  int pool_id() { return pool_id_; }

  pmem::obj::pool<pmem_log_root> pool() { return pool_; }

 private:
  Block* GetCurrentBlock();

  const int pool_id_;
  pmem::obj::pool<pmem_log_root> pool_;  // for memory allocation
  pmem::obj::persistent_ptr<pmem_log> p_log_;
  std::atomic<Block*> front_;
  std::atomic<size_t> hmm;
  std::mutex block_init_mu_;
};


#endif  // LISTDB_CORE_PMEM_LOG_H_
