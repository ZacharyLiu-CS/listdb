#ifndef LISTDB_INDEX_LOCKFREE_SKIPLIST_H_
#define LISTDB_INDEX_LOCKFREE_SKIPLIST_H_

#include <cassert>
#include <cstring>

#include <x86intrin.h>

#include "listdb/common.h"
#include "listdb/lib/memory.h"
#include "listdb/pmem/pmem_ptr.h"

class lockfree_skiplist {
 public:
  struct Node {
    Key key;       // integer value or offset
    uint64_t tag;  // seqorder (56-bit), op (4-bit), height (4-bit)
    uint64_t value;     // integer value or offset, (SHORTCUT: pointer to next memtable node)
    //uint64_t log_paddr;  // log marked offset, (SHORTCUT: moff to upper pmem node)
    std::atomic<Node*> next[1];

#ifndef LISTDB_STRING_KEY
    static Key head_key() { return 0ULL; }
#else
    static Key head_key() { return Key(); }
#endif

    static size_t compute_alloc_size(const Key& key, const int height) {
      return aligned_size(8, sizeof(Node) + (height-1)*8);
    }

    static Node* init_node(char* buf, const Key& key_,
                           const uint64_t seq_order, const ValueType type, const int height,
                           const uint64_t value_,
                           bool init_next_arr = true) {
      Node* node = (Node*) buf;
      node->key = key_;
      node->tag = (seq_order << 8) | (type << 4) | (height & 0xf);
      node->value = value_;
      if (init_next_arr) {
        memset(node->next, 0, height * 8);
      }
      return node;
    }

    int height() const { return tag & 0xf; }
    size_t alloc_size() const { return sizeof(Node) + (height() - 1) * 8; }
    uint8_t type() const { return ValueType((tag & 0xf0) >> 4); }
    char* data() const { return (char*) this; }
  };

  lockfree_skiplist();
  // Returns pred
  Node* Insert(Node* const node, Node* pred = NULL);
  // Returns (node->key == key) ? node : NULL
  Node* find(const Key& key, const Node* pred = NULL);
  Node* Lookup(const Key& key);
  Node* head();

 private:
  void find_position(Node* node, Node* preds[], Node* succs[], Node* pred = NULL, const int min_h = 0);

 public:
  Node* head_;
};
inline void copy_data_256(char* dst, const char* src, size_t size) {
	assert(size % 32 == 0);
	while(size) {
		_mm256_store_si256 ((__m256i*)dst, _mm256_load_si256((__m256i const*)src));
		src += 32;
		dst += 32;
		size -= 32;
	}
}

inline void copy_data_128(char* dst, const char* src, size_t size) {
	assert(size % 16 == 0);
	while(size) {
		_mm_store_si128 ((__m128i*)dst, _mm_load_si128((__m128i const*)src));
		src += 16;
		dst += 16;
		size -= 16;
	}
}

#endif  // LISTDB_INDEX_LOCKFREE_SKIPLIST_H_
