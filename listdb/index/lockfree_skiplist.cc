#include "lockfree_skiplist.h"


lockfree_skiplist::Node* lockfree_skiplist::Insert(Node* const node, Node* pred)  {
  if (pred == NULL) {
    pred = head_;
  }
  Node* preds[kMaxHeight];
  Node* succs[kMaxHeight];
  //Node* succs[kMaxHeight] __attribute__ ((aligned (32)));
  while (true) {
    find_position(node, preds, succs, pred);
    //const size_t bbb = (8 * node->height) % 16;
    //const size_t ccc = (8 * node->height) - bbb;
    //copy_data_128((char*) &(node->next[0]), (char*) &succs[0], ccc);
    //for (int l = ccc / 8; l < node->height; l++) {
    //  node->next[l].store(succs[l], std::memory_order_relaxed);
    //}
    for (int l = 0; l < node->height(); l++) {
      node->next[l].store(succs[l], std::memory_order_relaxed);
    }
    //if (!preds[0]->next[0].compare_exchange_weak(succs[0], node, std::memory_order_relaxed, std::memory_order_relaxed))
    if (!preds[0]->next[0].compare_exchange_strong(succs[0], node))
    {
      //std::this_thread::sleep_for(std::chrono::microseconds(100));
      pred = preds[kMaxHeight - 1];
      continue;
    }
    for (int l = 1; l < node->height(); l++) {
      while (true) {
        //node->next[l].store(succs[l], std::memory_order_relaxed);
        //if (preds[l]->next[l].compare_exchange_weak(succs[l], node, std::memory_order_relaxed, std::memory_order_relaxed))
        if (preds[l]->next[l].compare_exchange_strong(succs[l], node))
        {
          break;
        }
        //pred = preds[kMaxHeight - 1];
        find_position(node, preds, succs, pred, l);
      }
    }
    break;
  }
  return preds[kMaxHeight - 1];
}

lockfree_skiplist::Node* lockfree_skiplist::Lookup(const Key& key) {
  Node* pred = head_;
  Node* curr = nullptr;
  int h = pred->height();
  for (int l = h - 1; l >= 0; l--) {
    while (true) {
      //curr = *((Node**) &(pred->next[l]));
      curr = pred->next[l].load(std::memory_order_relaxed);
      //curr = pred->next[l].load();
      if (curr && curr->key.Compare(key) < 0) {
        pred = curr;
        continue;
      }
      break;
    }
  }
  return curr;
}

lockfree_skiplist::lockfree_skiplist() {
  auto head_key = Node::head_key();
  const size_t alloc_size = Node::compute_alloc_size(head_key, kMaxHeight);
  void* buf = aligned_alloc(8, alloc_size);
  //void* buf = malloc(alloc_size);
  head_ = Node::init_node((char*) buf, head_key, 0x00ffffffffffffff, ValueType(0xf), kMaxHeight, 0);
  head_->value = PmemPtr(-1, (uint64_t) 0).dump();  // pool_id: -1, offset: 0
  std::atomic_thread_fence(std::memory_order_release);
}

//void lockfree_skiplist::insert(const Key& key, const Value& value, const int height) {
//}



lockfree_skiplist::Node* lockfree_skiplist::find(const Key& key, const Node* pred) {
  if (pred == NULL) {
    pred = head_;
  }
  Node* curr;
  int h = pred->height();
  int cr;
  for (int l = h - 1; l >= 0; l--) {
    while (true) {
      //curr = *((Node**) &(pred->next[l]));
      curr = pred->next[l].load(std::memory_order_relaxed);
      //curr = pred->next[l].load();
      if (curr && (cr = curr->key.Compare(key)) < 0) {
        pred = curr;
        continue;
      }
      break;
    }
  }
  return (cr == 0) ? curr : NULL;
}

lockfree_skiplist::Node* lockfree_skiplist::head() {
  return head_;
}

void lockfree_skiplist::find_position(Node* node, Node* preds[], Node* succs[], Node* pred, const int min_h) {
  if (pred == NULL) {
    pred = head_;
  }
  Node* curr;
  int h = pred->height();
  for (int l = h - 1; l >= min_h; l--) {
    while (true) {
      curr = pred->next[l].load(std::memory_order_relaxed);
      //curr = pred->next[l].load();
      if (curr && curr->key.Compare(node->key) < 0) {
        pred = curr;
        continue;
      }
      break;
    }
    preds[l] = pred;
    succs[l] = curr;
  }
}