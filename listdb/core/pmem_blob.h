#ifndef LISTDB_CORE_PMEM_BLOB_H_
#define LISTDB_CORE_PMEM_BLOB_H_

#include <mutex>

#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/make_persistent_atomic.hpp>

#include "listdb/common.h"
#include "listdb/pmem/pmem.h"
#include "listdb/pmem/pmem_ptr.h"

struct pmem_blob_block {
  size_t p;
  char data[kPmemBlobBlockSize];
  pmem::obj::persistent_ptr<pmem_blob_block> next;

  pmem_blob_block(pmem::obj::persistent_ptr<pmem_blob_block> next_ = nullptr) : p(0), data(), next(next_) { }
};

struct pmem_blob_root {
  pmem::obj::persistent_ptr<pmem_blob_block> head[kNumShards];
};

class PmemBlob {
 public:
  struct Block {
    std::atomic<size_t> p;
    pmem::obj::persistent_ptr<pmem_blob_block> p_block;

    explicit Block(pmem::obj::persistent_ptr<pmem_blob_block> p_block_, Block* next_);
    void* Allocate(const size_t size);
    char* data() { return p_block->data; }
  };

  /// Constructor
  PmemBlob(const int pool_id, const int shard_id);

  PmemPtr Allocate(const size_t size);

 private:
  Block* GetWritableBlock();

  const int pool_id_;
  const int shard_id_;
  pmem::obj::pool<pmem_blob_root> pool_;
  pmem::obj::persistent_ptr<pmem_blob_root> p_blob_;
  std::atomic<Block*> front_;
  std::mutex block_init_mu_;
};



#endif  // LISTDB_CORE_PMEM_BLOB_H_
