#include "table_list.h"

TableList::TableList(const size_t table_capacity)
    : table_capacity_(table_capacity), front_(nullptr) {}

void *TableList::Put(const Key &key, const Value &value) {
  const size_t kv_size = key.size() + 8;
  auto table = GetMutable(kv_size);
  auto ret = table->Put(key, value);
  table->w_UnRef();
  return ret;
}

void TableList::SetFront(Table *table) {
  std::lock_guard<std::mutex> lk(init_mu_);
  front_.store(table);
}

void TableList::PushFront(Table *table) {
  std::lock_guard<std::mutex> lk(init_mu_);
  auto front_old = front_.load();
  table->SetNext(front_old);
  front_.store(table);
}

bool TableList::Get(const Key &key) {
  auto table = GetFront();
  while (table) {
    if (table->Get(key, nullptr)) {
      return true;
    }
    table = table->Next();
  }
  return false;
}

bool TableList::IsEmpty() {
  auto ret = front_.load(MO_RELAXED);
  return ret == nullptr;
}

Table *TableList::GetFront() {
  Table *ret = front_.load(MO_RELAXED);
  if (ret == nullptr) {
    std::lock_guard<std::mutex> lk(init_mu_);
    ret = front_.load(MO_RELAXED);
    if (ret == nullptr) {
      ret = NewMutable(table_capacity_, nullptr);
      front_.store(ret, MO_RELAXED);
    }
  }
  return ret;
}

Table *TableList::GetMutable(const size_t size) {
  auto table = GetFront();
  table->w_Ref(MO_RELAXED);
  if (!table->HasRoom(size)) {
    table->w_UnRef(MO_RELAXED);
    // >>> IMPLICIT MFENCE
    std::unique_lock<std::mutex> lk(init_mu_);
    table = front_.load(MO_RELAXED);
    table->w_Ref(MO_RELAXED);
    if (!table->HasRoom(size)) {
      table->w_UnRef(MO_RELAXED);
      auto new_table = NewMutable(table_capacity_, table);
      new_table->HasRoom(size);
      new_table->w_Ref(MO_RELAXED);
      front_.store(new_table, MO_RELAXED);
      lk.unlock();
      EnqueueCompaction(table);
      table = new_table;
    }
    // <<< MFENCE
  }
  return table;
}