// SPDX-License-Identifier: Apache-2.0

/**
 * @file malloc_churn.cpp
 * @brief Allocation-heavy benchmark used to measure the overhead of heapline.
 *
 * Each thread keeps a ring of live blocks and replaces one block per
 * iteration with a block of pseudo-random size, so the run mixes
 * allocations, releases and new heap peaks.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

namespace {

constexpr int kSlots = 64;

/**
 * @brief Replace ring slots @p iterations times with blocks of 16 to 4096
 * bytes, sizes drawn from a linear congruential generator seeded by @p seed.
 */
void churn(long iterations, unsigned seed) {
  void* slots[kSlots] = {};
  unsigned state      = seed;
  for (long i = 0; i < iterations; ++i) {
    state    = state * 1664525u + 1013904223u;
    int slot = static_cast<int>(state % kSlots);
    std::free(slots[slot]);
    size_t size = 16 + (state >> 8) % 4081;
    slots[slot] = std::malloc(size);
    if (slots[slot]) static_cast<char*>(slots[slot])[0] = 1;
  }
  for (void* block : slots) std::free(block);
}

}  // namespace

int main(int argc, char** argv) {
  const int threads     = argc > 1 ? std::atoi(argv[1]) : 4;
  const long iterations = argc > 2 ? std::atol(argv[2]) : 10000000;
  if (threads <= 0 || iterations <= 0) {
    std::fprintf(stderr,
                 "usage: malloc_churn [THREADS] [ITERATIONS_PER_THREAD]\n");
    return 1;
  }
  const auto start = std::chrono::steady_clock::now();
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back(churn, iterations, 12345u + static_cast<unsigned>(t));
  for (auto& thread : pool) thread.join();
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  const double allocs = static_cast<double>(threads) * iterations;
  std::printf("malloc_churn: %d threads, %.0f allocs, %.3f s, %.3g allocs/s\n",
              threads, allocs, seconds, allocs / seconds);
  return 0;
}
