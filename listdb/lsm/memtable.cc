#include "memtable.h"


MemTable::MemTable(const size_t table_capacity) : Table(table_capacity, TableType::kMemTable) {
  skiplist_ = new lockfree_skiplist();
}

MemTable::~MemTable() {
  delete skiplist_;
}

void* MemTable::Put(const Key& key, const Value& value) {
  fprintf(stdout, "Not impl!!!! returning NULL\n");
  return nullptr;
}

bool MemTable::Get(const Key& key, void** value_out) {
  fprintf(stdout, "Not impl!!!! DO NOTHING!\n");
  return false;
}