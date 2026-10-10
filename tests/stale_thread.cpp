// SPDX-License-Identifier: Apache-2.0

/**
 * @file stale_thread.cpp
 * @brief Check that a thread whose first counted operation falls in the last
 * round of key destructors, and which therefore stays in the registry after
 * its exit, does not break the registry when glibc gives its stack to the
 * next thread. The totals lose some counts in that case, so the test only
 * checks that they can still be read.
 */

#include "heapline.h"

#include <dlfcn.h>
#include <pthread.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

/**
 * @brief Number of rounds in which glibc calls the destructors of the keys.
 */
constexpr intptr_t kRounds       = 4;
constexpr int kPairs             = 100;
constexpr unsigned kAlarmSeconds = 5;

pthread_key_t key;

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
  pthread_setspecific(key, reinterpret_cast<void*>(1));
  return nullptr;
}

void* worker(void*) {
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

  pthread_t thread;
  if (pthread_create(&thread, nullptr, late_thread, nullptr) != 0) {
    std::fprintf(stderr, "pthread_create failed\n");
    return 1;
  }
  pthread_join(thread, nullptr);

  // This thread runs on the stack of the previous one, which holds the
  // counters that are still in the registry.
  if (pthread_create(&thread, nullptr, worker, nullptr) != 0) {
    std::fprintf(stderr, "pthread_create failed\n");
    return 1;
  }
  pthread_join(thread, nullptr);
  heapline_stats stats{};
  get_stats(&stats);
  return 0;
}
