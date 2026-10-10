// SPDX-License-Identifier: Apache-2.0

/**
 * @file stale_thread.cpp
 * @brief Check that a thread whose first counted operation falls in the last
 * round of key destructors, and which therefore keeps its counters after its
 * exit, stays counted when glibc gives its stack to the next thread.
 *
 * Prints the least numbers of allocations and releases that the program made,
 * for check_stale_thread.sh to compare with the record written at exit.
 */

#include "heapline.h"

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

/**
 * @brief Number of rounds in which glibc calls the destructors of the keys.
 */
constexpr intptr_t kRounds       = PTHREAD_DESTRUCTOR_ITERATIONS;
constexpr int kMainPairs         = 50;
constexpr int kPairs             = 100;
constexpr int kSkip              = 77;
constexpr unsigned kAlarmSeconds = 5;

/**
 * @brief Allowance for the allocations glibc makes itself when it creates a
 * thread, such as the thread vector of TLS blocks.
 */
constexpr int64_t kSlackCount = 16;
constexpr int64_t kSlackBytes = 64 << 10;

pthread_key_t key;
pthread_t first_self;
pthread_t second_self;
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
 * @brief Ask for another round until the last one, and make the first counted
 * operation of the thread there.
 */
void late_allocation(void* value) {
  const auto round = reinterpret_cast<intptr_t>(value);
  if (round < kRounds) {
    pthread_setspecific(key, reinterpret_cast<void*>(round + 1));
    return;
  }
  std::free(std::malloc(100));
}

void* late_thread(void*) {
  first_self = pthread_self();
  pthread_setspecific(key, reinterpret_cast<void*>(1));
  return nullptr;
}

void* worker(void*) {
  second_self = pthread_self();
  for (int i = 0; i < kPairs; ++i) std::free(std::malloc(100));
  return nullptr;
}

}  // namespace

int main() {
  auto get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }
  alarm(kAlarmSeconds);  // A hang in the registry ends with SIGALRM.
  if (pthread_key_create(&key, late_allocation) != 0) {
    std::fprintf(stderr, "pthread_key_create failed\n");
    return 1;
  }

  heapline_stats before{};
  heapline_stats after{};
  get_stats(&before);
  for (int i = 0; i < kMainPairs; ++i) std::free(std::malloc(100));

  pthread_t thread;
  if (pthread_create(&thread, nullptr, late_thread, nullptr) != 0) {
    std::fprintf(stderr, "pthread_create failed\n");
    return 1;
  }
  pthread_join(thread, nullptr);
  // This thread runs on the stack of the previous one, if glibc reuses it.
  if (pthread_create(&thread, nullptr, worker, nullptr) != 0) {
    std::fprintf(stderr, "pthread_create failed\n");
    return 1;
  }
  pthread_join(thread, nullptr);
  get_stats(&after);

  // The main thread, the thread that made its only operation in the last
  // round, and the thread that took its stack are all counted.
  constexpr int64_t kExpected = kMainPairs + 1 + kPairs;
  check(near(static_cast<int64_t>(after.allocs - before.allocs), kExpected,
             kSlackCount),
        "allocs of all threads");
  check(near(static_cast<int64_t>(after.frees - before.frees), kExpected,
             kSlackCount),
        "frees of all threads");
  const int64_t drift = after.live_bytes - before.live_bytes;
  check(drift >= -kSlackBytes && drift <= kSlackBytes,
        "live bytes back to start");

  char line[64];
  const int len = std::snprintf(line, sizeof(line), "%lld %lld\n",
                                static_cast<long long>(kExpected),
                                static_cast<long long>(kExpected));
  if (len > 0 && write(STDOUT_FILENO, line, static_cast<size_t>(len)) < 0)
    check(false, "write");

  if (failures != 0) return 1;
  if (!pthread_equal(first_self, second_self)) {
    std::fprintf(stderr, "the second thread has a new stack, skipped\n");
    return kSkip;
  }
  return 0;
}
