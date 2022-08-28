#ifndef LISTDB_LSM_PMEMTABLE_H_
#define LISTDB_LSM_PMEMTABLE_H_

#include "listdb/index/braided_pmem_skiplist.h"
#include "listdb/lsm/table.h"

class PmemTable : public Table {
public:
  using Node = BraidedPmemSkipList::Node;

  PmemTable(const size_t table_capacity, BraidedPmemSkipList *skiplist)
      : Table(table_capacity, TableType::kPmemTable), skiplist_(skiplist) {}

  void *Put(const Key &key, const Value &value) override{
    fprintf(stdout, "Not impl!!!! returning NULL\n");
    return nullptr;
  }

  bool Get(const Key &key, void **value_out) override{
    fprintf(stdout, "Not impl!!!! DO NOTHING!\n");
    return false;
  }

  BraidedPmemSkipList *skiplist() { return skiplist_; }

  void SetManifest(pmem::obj::persistent_ptr_base manifest) {
    manifest_ = manifest;
  }

  template <typename T> pmem::obj::persistent_ptr<T> manifest() {
    return manifest_.raw();
  }

private:
  BraidedPmemSkipList *skiplist_;
  pmem::obj::persistent_ptr_base manifest_;
};

#endif // LISTDB_LSM_PMEMTABLE_H_
