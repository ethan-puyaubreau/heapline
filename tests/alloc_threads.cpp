// SPDX-License-Identifier: Apache-2.0

/**
 * @file alloc_threads.cpp
 * @brief Check that the counters of several threads add up exactly, while
 * the threads are alive and after they have exited, including the bytes that
 * a thread holds back instead of publishing them.
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
constexpr int kSmall   = 1000;
constexpr int kMaxKept = 1024;
constexpr int kSkip    = 77;

constexpr int64_t kBig  = int64_t{1} << 20;
constexpr int64_t kHuge = int64_t{4} << 30;

/**
 * @brief Usable bytes of 100-byte blocks that a worker keeps. With the 4 GiB
 * block alive, the peak is at least 4 GiB, so each of the 9 registered
 * threads may hold back about 64 KiB: these blocks are not published, and
 * only the totals can see them.
 */
constexpr int64_t kKeptBytes = 48 << 10;

/**
 * @brief Allowance for the allocations glibc makes itself when it creates a
 * thread, such as the thread vector of TLS blocks.
 */
constexpr int64_t kSlackCount = 64;
constexpr int64_t kSlackBytes = 64 << 10;

/**
 * @brief What a worker leaves allocated when it exits.
 */
struct Slot {
  void* big;               ///< The 1 MiB block.
  int kept;                ///< Number of kept blocks.
  int64_t usable;          ///< Usable bytes of the big and kept blocks.
  void* blocks[kMaxKept];  ///< The kept blocks.
};

Slot slots[kThreads];
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
 * @brief Allocate and release small blocks, then keep a 1 MiB block and a
 * few small ones, which the thread still holds when it exits. The blocks are
 * released by the main thread.
 */
void* worker(void* arg) {
  auto* slot = static_cast<Slot*>(arg);
  void* small[kSmall];
  for (auto& block : small) block = std::malloc(100);
  for (auto* block : small) std::free(block);

  slot->big    = std::malloc(kBig);
  slot->usable = static_cast<int64_t>(malloc_usable_size(slot->big));
  int64_t kept = 0;
  while (kept < kKeptBytes && slot->kept < kMaxKept) {
    void* block = std::malloc(100);
    const auto usable =
        static_cast<int64_t>(block ? malloc_usable_size(block) : 0);
    slot->blocks[slot->kept++] = block;
    slot->usable += usable;
    kept += usable;
  }
  pthread_barrier_wait(&held);
  pthread_barrier_wait(&released);
  return slot->big ? nullptr : &failures;
}

}  // namespace

int main() {
  auto get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }
  void* huge = std::malloc(kHuge);
  if (!huge) {
    std::fprintf(stderr, "cannot allocate 4 GiB, skipped\n");
    return kSkip;
  }
  const auto huge_usable = static_cast<int64_t>(malloc_usable_size(huge));
  pthread_barrier_init(&held, nullptr, kThreads + 1);
  pthread_barrier_init(&released, nullptr, kThreads + 1);

  heapline_stats before{};
  heapline_stats during{};
  heapline_stats after{};
  heapline_stats last{};
  get_stats(&before);

  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; ++i) {
    if (pthread_create(&threads[i], nullptr, worker, &slots[i]) != 0) {
      std::fprintf(stderr, "pthread_create failed\n");
      return 1;
    }
  }
  pthread_barrier_wait(&held);
  get_stats(&during);
  int64_t held_bytes = 0;
  int64_t allocs     = 0;
  for (const Slot& slot : slots) {
    held_bytes += slot.usable;
    allocs += kSmall + 1 + slot.kept;
  }
  pthread_barrier_wait(&released);
  for (auto& thread : threads) {
    void* result = nullptr;
    pthread_join(thread, &result);
    check(result == nullptr, "worker allocations succeed");
  }
  get_stats(&after);

  // While the threads are alive, their counts and held bytes live in their
  // own counters.
  check(near(during.allocs - before.allocs, allocs, kSlackCount),
        "allocs of live threads");
  check(near(during.frees - before.frees, kThreads * kSmall, kSlackCount),
        "frees of live threads");
  check(near(during.live_bytes - before.live_bytes, held_bytes, kSlackBytes),
        "bytes of live threads");
  check(during.peak_bytes >= during.live_bytes, "peak covers the live bytes");

  // After the join, exited threads have folded everything into the totals.
  // Nothing has been released yet.
  check(near(after.allocs - before.allocs, allocs, kSlackCount),
        "allocs of exited threads");
  check(near(after.frees - before.frees, kThreads * kSmall, kSlackCount),
        "frees of exited threads");
  check(near(after.live_bytes - before.live_bytes, held_bytes, kSlackBytes),
        "bytes of exited threads");

  // Release the blocks of the workers and the large one.
  for (const Slot& slot : slots) {
    std::free(slot.big);
    for (int i = 0; i < slot.kept; ++i) std::free(slot.blocks[i]);
  }
  std::free(huge);
  get_stats(&last);

  check(near(last.allocs - before.allocs, allocs, kSlackCount),
        "allocs at the end");
  check(near(last.frees - before.frees, allocs + 1, kSlackCount),
        "frees at the end");
  const int64_t drift = last.live_bytes - (before.live_bytes - huge_usable);
  check(drift >= -kSlackBytes && drift <= kSlackBytes,
        "live bytes back to start");

  return failures == 0 ? 0 : 1;
}
