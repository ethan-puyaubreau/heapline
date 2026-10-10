// SPDX-License-Identifier: Apache-2.0

/**
 * @file alloc_threads.cpp
 * @brief Check that the counters of several threads add up exactly, while
 * the threads are alive and after they have exited.
 */

#include "heapline.h"

#include <dlfcn.h>
#include <pthread.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

constexpr int kThreads    = 8;
constexpr int kSmall      = 1000;
constexpr int64_t kBig    = int64_t{1} << 20;
constexpr int64_t kAllocs = kThreads * (kSmall + 1);
constexpr int64_t kFrees  = kThreads * kSmall;

/**
 * @brief Allowance for the allocations glibc makes itself when it creates a
 * thread, such as the thread vector of TLS blocks.
 */
constexpr int64_t kSlackCount = 64;
constexpr int64_t kSlackBytes = 64 << 10;

pthread_barrier_t held;
pthread_barrier_t released;
int failures = 0;

void check(bool cond, const char* what) {
  if (cond) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++failures;
}

bool near(int64_t value, int64_t expected, int64_t slack) {
  return value >= expected && value <= expected + slack;
}

/**
 * @brief Allocate and release small blocks, then hold a 1 MiB block until
 * the main thread has read the counters.
 */
void* worker(void*) {
  void* small[kSmall];
  for (auto& block : small) block = std::malloc(100);
  for (auto* block : small) std::free(block);
  void* big = std::malloc(kBig);
  pthread_barrier_wait(&held);
  pthread_barrier_wait(&released);
  std::free(big);
  return big ? nullptr : &failures;
}

}  // namespace

int main() {
  auto get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }
  pthread_barrier_init(&held, nullptr, kThreads + 1);
  pthread_barrier_init(&released, nullptr, kThreads + 1);

  heapline_stats before{};
  heapline_stats during{};
  heapline_stats after{};
  get_stats(&before);

  pthread_t threads[kThreads];
  for (auto& thread : threads) {
    if (pthread_create(&thread, nullptr, worker, nullptr) != 0) {
      std::fprintf(stderr, "pthread_create failed\n");
      return 1;
    }
  }
  pthread_barrier_wait(&held);
  get_stats(&during);
  pthread_barrier_wait(&released);
  for (auto& thread : threads) {
    void* result = nullptr;
    pthread_join(thread, &result);
    check(result == nullptr, "worker allocations succeed");
  }
  get_stats(&after);

  // While the threads are alive, their counts live in their own counters.
  check(near(during.allocs - before.allocs, kAllocs, kSlackCount),
        "allocs of live threads");
  check(near(during.frees - before.frees, kFrees, kSlackCount),
        "frees of live threads");
  check(during.live_bytes - before.live_bytes >= kThreads * kBig,
        "live bytes cover the held blocks");
  check(during.peak_bytes >= before.live_bytes + kThreads * kBig - kSlackBytes,
        "peak covers the held blocks");

  // After the join, exited threads have folded their counts into the totals.
  check(near(after.allocs - before.allocs, kAllocs, kSlackCount),
        "allocs of exited threads");
  check(near(after.frees - before.frees, kAllocs, kSlackCount),
        "frees of exited threads");
  const int64_t drift = after.live_bytes - before.live_bytes;
  check(drift >= -kSlackBytes && drift <= kSlackBytes,
        "live bytes back to start");

  return failures == 0 ? 0 : 1;
}
