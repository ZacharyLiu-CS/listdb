#include "pmem_blob.h"

PmemBlob::Block::Block(pmem::obj::persistent_ptr<pmem_blob_block> p_block_, Block* next_) {
  p.store(p_block_->p);
  p_block = p_block_;
  p_block->next = (next_) ? next_->p_block : nullptr;
}

void* PmemBlob::Block::Allocate(const size_t size) {
  size_t before = p.fetch_add(size, MO_RELAXED);
  if (before + size <= kPmemBlobBlockSize) {
    return (void*) (data() + before);
  } else {
    return nullptr;
  }
}

PmemBlob::PmemBlob(const int pool_id, const int shard_id) : pool_id_(pool_id), shard_id_(shard_id) {
  auto pool = Pmem::pool<pmem_blob_root>(pool_id_);
  pool_ = pool;
  p_blob_ = pool.root();
  front_.store(nullptr);
}

PmemBlob::Block* PmemBlob::GetWritableBlock() {
  Block* ret = front_.load(MO_RELAXED); 
  if (ret == nullptr) {
    std::lock_guard<std::mutex> lk(block_init_mu_);
    ret = front_.load(MO_RELAXED);
    if (ret == nullptr) {
      pmem::obj::persistent_ptr<pmem_blob_block> p_new_block;
      pmem::obj::make_persistent_atomic<pmem_blob_block>(pool_, p_new_block, nullptr);
      p_blob_->head[shard_id_] = p_new_block;
      auto new_block = new Block(p_new_block, nullptr);
      front_.store(new_block, MO_RELAXED);
      ret = new_block;
    }
  }
  return ret;
}

PmemPtr PmemBlob::Allocate(const size_t size) {
  auto block = GetWritableBlock();
  void* buf = nullptr;
  if ((buf = block->Allocate(size)) == nullptr) {
    std::lock_guard<std::mutex> lk(block_init_mu_);
    block = front_.load(MO_RELAXED);
    if ((buf = block->Allocate(size)) == nullptr) {
      pmem::obj::persistent_ptr<pmem_blob_block> p_new_block;
      pmem::obj::make_persistent_atomic<pmem_blob_block>(pool_, p_new_block, block->p_block);
      p_blob_->head[shard_id_] = p_new_block;
      auto new_block = new Block(p_new_block, block);
      front_.store(new_block, MO_RELAXED);
      buf = new_block->Allocate(size);
    }
  }
  PmemPtr ret(pool_id_, (uint64_t) ((uintptr_t) buf - (uintptr_t) pool_.handle()));
  return ret;
}