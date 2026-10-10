// SPDX-License-Identifier: Apache-2.0

/**
 * @file peak_threads.cpp
 * @brief Check the heap peak of several threads, in two modes. Usage:
 * peak_threads bound|monotonic.
 *
 * Eight threads each hold a block at the same time, on top of a 1 GiB block
 * held by the main thread, then release them and exit.
 *
 * bound: the blocks are 120 KiB, above the 16 KiB that a thread may hold back
 * with nine threads, so they are published. Nothing is read while they are
 * held, and the peak read at the end must stay within the documented bound of
 * the true peak, which is known here. A library that lets each thread hold
 * back 82 KiB or more would miss more than that bound.
 *
 * monotonic: the blocks are 8 KiB, so the threads hold them back. The main
 * thread reads the counters while they are held, and again after the threads
 * are gone: the second peak must not be lower than the first, and the first
 * must show the held bytes.
 */

#include "heapline.h"

#include <dlfcn.h>
#include <malloc.h>
#include <pthread.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

constexpr int kThreads = 8;
constexpr int kSkip    = 77;

constexpr int64_t kBase           = int64_t{1} << 30;
constexpr int64_t kBoundBlock     = int64_t{120} << 10;
constexpr int64_t kMonotonicBlock = int64_t{8} << 10;

/**
 * @brief Size of the block of each worker, set before the workers start.
 */
int64_t block_size = kBoundBlock;

/**
 * @brief Allowance for the allocations glibc makes itself when it creates a
 * thread, such as the thread vector of TLS blocks.
 */
constexpr int64_t kSlackBytes = 64 << 10;

/**
 * @brief Usable size of the block of each worker.
 */
int64_t usable[kThreads];
pthread_barrier_t held;
pthread_barrier_t released;
int failures = 0;

void check(bool cond, const char* what) {
  if (cond) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++failures;
}

/**
 * @brief Hold a block until every worker does, then release it and exit.
 */
void* worker(void* arg) {
  auto* size  = static_cast<int64_t*>(arg);
  void* block = std::malloc(static_cast<size_t>(block_size));
  *size       = block ? static_cast<int64_t>(malloc_usable_size(block)) : 0;
  pthread_barrier_wait(&held);
  pthread_barrier_wait(&released);
  std::free(block);
  return block ? nullptr : size;
}

}  // namespace

int main(int argc, char** argv) {
  const bool monotonic = argc == 2 && std::strcmp(argv[1], "monotonic") == 0;
  if (!monotonic && !(argc == 2 && std::strcmp(argv[1], "bound") == 0)) {
    std::fprintf(stderr, "usage: peak_threads bound|monotonic\n");
    return 2;
  }
  block_size = monotonic ? kMonotonicBlock : kBoundBlock;
  auto get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }
  // The base raises the peak, and with it the amount that a thread may hold
  // back, to about 1 GiB / 65536 = 16 KiB.
  void* base = std::malloc(kBase);
  if (!base) {
    std::fprintf(stderr, "cannot allocate 1 GiB, skipped\n");
    return kSkip;
  }
  heapline_stats start{};
  get_stats(&start);
  const int64_t base_live = start.live_bytes;

  pthread_barrier_init(&held, nullptr, kThreads + 1);
  pthread_barrier_init(&released, nullptr, kThreads + 1);
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; ++i) {
    if (pthread_create(&threads[i], nullptr, worker, &usable[i]) != 0) {
      std::fprintf(stderr, "pthread_create failed\n");
      return 1;
    }
  }
  // Every worker holds its block between the two barriers. Only the monotonic
  // mode reads the counters there.
  heapline_stats during{};
  pthread_barrier_wait(&held);
  if (monotonic) get_stats(&during);
  pthread_barrier_wait(&released);
  for (auto& thread : threads) {
    void* result = nullptr;
    pthread_join(thread, &result);
    check(result == nullptr, "worker allocations succeed");
  }

  heapline_stats end{};
  get_stats(&end);
  int64_t sum = 0;
  for (int64_t size : usable) sum += size;

  // Nine threads allocate: (ceil(log2 9) + 1) / 8192 = 5 / 8192 of the peak.
  const int64_t bound = (5 * end.peak_bytes) / 8192;
  if (monotonic) {
    check(during.live_bytes >= base_live + sum, "live shows the held bytes");
    check(end.peak_bytes >= during.peak_bytes, "peak never decreases");
  } else {
    check(end.peak_bytes >= base_live + sum - bound, "peak covers the workers");
  }
  check(end.peak_bytes <= base_live + sum + bound + kSlackBytes,
        "peak does not exceed the workers");

  std::free(base);
  return failures == 0 ? 0 : 1;
}
