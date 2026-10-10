// SPDX-License-Identifier: Apache-2.0

/**
 * @file malloc_churn.cpp
 * @brief Allocation-heavy benchmark used to measure the overhead of heapline.
 *
 * Each thread keeps a ring of live blocks and replaces one block per
 * iteration with a block of pseudo-random size, so the run mixes
 * allocations, releases and new heap peaks.
 *
 * Usage: malloc_churn [THREADS [ITERATIONS [MIN_SIZE MAX_SIZE [RESIDENT_MIB]]]]
 * The sizes are in bytes. RESIDENT_MIB is a block held for the whole run, to
 * mimic a process that already holds a large heap.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

constexpr int kSlots = 64;

/**
 * @brief Replace ring slots @p iterations times with blocks of @p min_size
 * to @p max_size bytes, sizes drawn from a linear congruential generator
 * seeded by @p seed.
 */
void churn(long iterations, unsigned seed, size_t min_size, size_t max_size) {
  void* slots[kSlots] = {};
  unsigned state      = seed;
  const size_t span   = max_size - min_size + 1;
  for (long i = 0; i < iterations; ++i) {
    state    = state * 1664525u + 1013904223u;
    int slot = static_cast<int>(state % kSlots);
    std::free(slots[slot]);
    size_t size = min_size + (state >> 8) % span;
    slots[slot] = std::malloc(size);
    if (slots[slot]) static_cast<char*>(slots[slot])[0] = 1;
  }
  for (void* block : slots) std::free(block);
}

}  // namespace

int main(int argc, char** argv) {
  const int threads       = argc > 1 ? std::atoi(argv[1]) : 4;
  const long iterations   = argc > 2 ? std::atol(argv[2]) : 10000000;
  const long min_size     = argc > 3 ? std::atol(argv[3]) : 16;
  const long max_size     = argc > 4 ? std::atol(argv[4]) : 4096;
  const long resident_mib = argc > 5 ? std::atol(argv[5]) : 0;
  // (state >> 8) has 24 bits, so larger ranges would not be covered.
  if (threads <= 0 || iterations <= 0 || min_size <= 0 || max_size < min_size ||
      max_size - min_size >= (1L << 24) || resident_mib < 0) {
    std::fprintf(stderr,
                 "usage: malloc_churn [THREADS [ITERATIONS [MIN_SIZE "
                 "MAX_SIZE [RESIDENT_MIB]]]]\n");
    return 1;
  }
  // A block held for the whole run, like the arrays of a simulation. It is
  // not touched: heapline counts heap bytes, not resident pages.
  void* resident = nullptr;
  if (resident_mib > 0) {
    resident = std::malloc(static_cast<size_t>(resident_mib) << 20);
    if (!resident) {
      std::fprintf(stderr, "malloc_churn: cannot allocate %ld MiB\n",
                   resident_mib);
      return 1;
    }
  }
  const auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back(churn, iterations, 12345u + static_cast<unsigned>(t),
                      static_cast<size_t>(min_size),
                      static_cast<size_t>(max_size));
  for (auto& thread : pool) thread.join();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  std::free(resident);
  const double allocs = static_cast<double>(threads) * iterations;
  std::printf("malloc_churn: %d threads, %.0f allocs, %.3f s, %.3g allocs/s\n",
              threads, allocs, seconds, allocs / seconds);
  return 0;
}
