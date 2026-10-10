// SPDX-License-Identifier: Apache-2.0

/**
 * @file peak_threads.cpp
 * @brief Check that the heap peak of several threads stays within the bound
 * of the documentation of the true peak, which is known here.
 *
 * Eight threads each hold a 4 MiB block at the same time, on top of a 1 GiB
 * block held by the main thread, then release them and exit. A library that
 * lets threads hold back too much would miss the 32 MiB of the workers.
 */

#include "heapline.h"

#include <dlfcn.h>
#include <malloc.h>
#include <pthread.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

constexpr int kThreads = 8;
constexpr int kSkip    = 77;

constexpr int64_t kBase  = int64_t{1} << 30;
constexpr int64_t kBlock = int64_t{4} << 20;

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
  void* block = std::malloc(kBlock);
  *size       = block ? static_cast<int64_t>(malloc_usable_size(block)) : 0;
  pthread_barrier_wait(&held);
  pthread_barrier_wait(&released);
  std::free(block);
  return block ? nullptr : size;
}

}  // namespace

int main() {
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
  // Every worker holds its block between the two barriers; nothing is read.
  pthread_barrier_wait(&held);
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
  check(end.peak_bytes >= base_live + sum - bound, "peak covers the workers");
  check(end.peak_bytes <= base_live + sum + bound + kSlackBytes,
        "peak does not exceed the workers");

  std::free(base);
  return failures == 0 ? 0 : 1;
}
