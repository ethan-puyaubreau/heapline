// SPDX-License-Identifier: Apache-2.0

/**
 * @file peak_time_threads.cpp
 * @brief Test program whose heap peak, 256 MiB, is reached at once by four
 * threads. Each keeps a 64 MiB block and a few small ones for 1.5 seconds and
 * exits holding them, so that the bytes it held back are folded into the
 * totals long after the peak. The main thread releases the blocks.
 *
 * Once the four threads hold their big block, the main thread allocates 60
 * small blocks, about 6 KiB, which stay below the 8 KiB that it may hold back
 * with five threads. After the join, the limit is 0, and its first release
 * publishes those bytes, held since the peak: that must not move the time of
 * the peak.
 */

#include <pthread.h>
#include <time.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int kThreads      = 4;
constexpr int kSmall        = 8;
constexpr int kMainSmall    = 60;
constexpr std::size_t kBig  = 64u << 20;
constexpr long kSleepMillis = 1500;

/**
 * @brief The blocks of one thread. The small ones stay below the amount that
 * a thread may hold back, so they are not published before the thread exits.
 */
struct Slot {
  void* big;
  void* small[kSmall];
};

Slot slots[kThreads];
pthread_barrier_t holding;

void* worker(void* arg) {
  auto* slot = static_cast<Slot*>(arg);
  slot->big  = std::malloc(kBig);
  for (void*& block : slot->small) block = std::malloc(100);
  pthread_barrier_wait(&holding);
  timespec pause{kSleepMillis / 1000, (kSleepMillis % 1000) * 1000000L};
  nanosleep(&pause, nullptr);
  return slot->big ? nullptr : slots;
}

}  // namespace

int main() {
  pthread_barrier_init(&holding, nullptr, kThreads + 1);
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; ++i) {
    if (pthread_create(&threads[i], nullptr, worker, &slots[i]) != 0) {
      std::fprintf(stderr, "pthread_create failed\n");
      return 1;
    }
  }
  pthread_barrier_wait(&holding);
  void* mine[kMainSmall];
  for (void*& block : mine) block = std::malloc(100);
  bool ok = true;
  for (auto& thread : threads) {
    void* result = nullptr;
    pthread_join(thread, &result);
    ok = ok && result == nullptr;
  }
  // The first release after the join is the first operation with a limit of 0.
  std::free(mine[0]);
  for (int i = 1; i < kMainSmall; ++i) std::free(mine[i]);
  for (Slot& slot : slots) {
    std::free(slot.big);
    for (void* block : slot.small) std::free(block);
  }
  return ok ? 0 : 1;
}
