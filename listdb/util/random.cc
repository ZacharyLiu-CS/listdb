#include "random.h"
#include <algorithm>

Random *Random::GetTLSInstance() {
  thread_local Random *tls_instance;
  thread_local std::aligned_storage<sizeof(Random)>::type tls_instance_bytes;

  auto rv = tls_instance;
  if (UNLIKELY(rv == nullptr)) {
    size_t seed = std::hash<std::thread::id>()(std::this_thread::get_id());
    rv = new (&tls_instance_bytes) Random((uint32_t)seed);
    tls_instance = rv;
  }
  return rv;
}

std::string Random::HumanReadableString(int len) {
  std::string ret;
  ret.resize(len);
  for (int i = 0; i < len; ++i) {
    ret[i] = static_cast<char>('a' + Uniform(26));
  }
  return ret;
}

std::string Random::RandomString(int len) {
  std::string ret;
  ret.resize(len);
  for (int i = 0; i < len; i++) {
    ret[i] = static_cast<char>(' ' + Uniform(95)); // ' ' .. '~'
  }
  return ret;
}

std::string Random::RandomBinaryString(int len) {
  std::string ret;
  ret.resize(len);
  for (int i = 0; i < len; i++) {
    ret[i] = static_cast<char>(Uniform(CHAR_MAX));
  }
  return ret;
}

// A seeded replacement for removed std::random_shuffle
template <class RandomIt>
void RandomShuffle(RandomIt first, RandomIt last, uint32_t seed) {
  std::mt19937 rng(seed);
  std::shuffle(first, last, rng);
}

// A replacement for removed std::random_shuffle
template <class RandomIt> void RandomShuffle(RandomIt first, RandomIt last) {
  RandomShuffle(first, last, std::random_device{}());
}