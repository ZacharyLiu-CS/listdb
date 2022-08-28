#ifndef LISTDB_LSM_MEMTABLE_LIST_H_
#define LISTDB_LSM_MEMTABLE_LIST_H_

#include <condition_variable>
#include <functional>

#include "listdb/lsm/table_list.h"
#include "listdb/lsm/memtable.h"

class MemTableList : public TableList {
 public:
  MemTableList(const size_t table_capacity, const int shard_id);

  void BindEnqueueFunction(std::function<void(MemTable*)> enqueue_fn);

  void BindArena(int region, PmemLog* arena);

  void CleanUpFlushedImmutables();

  void CreateNewFront();

 protected:
  virtual Table* NewMutable(size_t table_capacity, Table* next_table) override;

  virtual void EnqueueCompaction(Table* table) override;

  const int shard_id_;
  const int max_num_memtables_ = kMaxNumMemTables;
  int num_memtables_ = 0;
  std::function<void(MemTable*)> enqueue_fn_;

  PmemLog* arena_[kNumRegions];

  std::mutex mu_;
  std::condition_variable cv_;
};



#endif  // LISTDB_LSM_MEMTABLE_LIST_H_
