// SPDX-License-Identifier: Apache-2.0

/**
 * @file tsan_stress.cpp
 * @brief Stress the counters of heapline from many threads under
 * ThreadSanitizer.
 *
 * The library is built into this program with HEAPLINE_TEST_ENTRY_POINTS,
 * so its entry points are called by name and only the calls made here are
 * counted. Waves of threads allocate, reallocate and release blocks, some of
 * them allocated by other threads, and exit holding blocks, while a reader
 * takes snapshots of the counters and the main thread forks once per wave.
 * TSan checks the
 * registration and exit of threads, the snapshots and the fork handlers; the
 * test checks the invariants of the snapshots and the exact final totals.
 */

#include "heapline.h"

#include <pthread.h>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" {
void* heapline_test_malloc(size_t size);
void heapline_test_free(void* ptr);
void* heapline_test_calloc(size_t count, size_t size);
void* heapline_test_realloc(void* ptr, size_t size);
int heapline_test_posix_memalign(void** ptr, size_t alignment, size_t size);
void* heapline_test_aligned_alloc(size_t alignment, size_t size);
void* heapline_test_memalign(size_t alignment, size_t size);
}

namespace {

/**
 * @brief Waves of workers, started together and joined together. Each wave
 * registers new threads and retires them, so the later waves take their
 * nodes from the free list that the earlier ones filled.
 */
constexpr int kWaves = 10;

/**
 * @brief Workers of a wave. With more than one thread registered, each one
 * holds back part of its bytes instead of publishing them at once, which is
 * the path under test.
 */
constexpr int kWaveThreads = 8;

/**
 * @brief Operations of a worker, enough for each one to go past its limit
 * and publish many times while the others do. The sizes keep a run around a
 * tenth of a second under TSan.
 */
constexpr int kOps = 2000;

/**
 * @brief Blocks a worker holds at once. An operation picks one of these
 * slots at random; with few of them, the slot is often taken, so the
 * operations mix allocations, reallocations and releases.
 */
constexpr int kKept = 32;

/**
 * @brief Room in the exchange: all the blocks of one wave, so that its
 * workers can exit without releasing them. They exit with bytes held back,
 * which retire() folds in, and another thread releases the blocks.
 */
constexpr int kExchange = kWaveThreads * kKept;

/**
 * @brief A hang, for instance between a fork and the registry lock, ends
 * with SIGALRM, well before the timeout of the test.
 */
constexpr unsigned kAlarmSecs = 60;

std::atomic<int> failures{0};

void fail(const char* what) {
  std::fprintf(stderr, "FAIL: %s\n", what);
  failures.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief Allocations and releases made through the entry points, summed over
 * the threads when they are joined.
 */
std::atomic<uint64_t> total_allocs{0};
std::atomic<uint64_t> total_frees{0};

/**
 * @brief Blocks handed from one thread to another, which releases them.
 */
pthread_mutex_t exchange_mutex = PTHREAD_MUTEX_INITIALIZER;
void* exchange[kExchange];
int exchange_used = 0;

/**
 * @brief Put @p ptr in the exchange, or return false when it is full.
 */
bool give(void* ptr) {
  pthread_mutex_lock(&exchange_mutex);
  const bool room = exchange_used < kExchange;
  if (room) exchange[exchange_used++] = ptr;
  pthread_mutex_unlock(&exchange_mutex);
  return room;
}

/**
 * @brief Take a block from the exchange, or null when it is empty.
 */
void* take() {
  pthread_mutex_lock(&exchange_mutex);
  void* ptr = exchange_used > 0 ? exchange[--exchange_used] : nullptr;
  pthread_mutex_unlock(&exchange_mutex);
  return ptr;
}

/**
 * @brief Linear congruential generator modulo 2^64, with the multiplier and
 * increment of Knuth's MMIX, which give the full period.
 *
 * Each thread keeps its own state. rand() would share one behind a lock of
 * glibc, and TSan would take each call for a synchronization between the
 * threads, which could hide a race in heapline. The high bits are returned,
 * since the low bits of such a generator have short periods: bit 0 only
 * alternates.
 */
uint32_t next(uint64_t& state) {
  state = state * 6364136223846793005u + 1442695040888963407u;
  return static_cast<uint32_t>(state >> 33);
}

/**
 * @brief Allocate a block of a random size with a random entry point.
 */
void* allocate(uint64_t& state, uint64_t& allocs) {
  const size_t size = 16 + next(state) % 4096;
  void* ptr         = nullptr;
  switch (next(state) % 5) {
    case 0: ptr = heapline_test_malloc(size); break;
    case 1: ptr = heapline_test_calloc(1, size); break;
    case 2:
      ptr = heapline_test_aligned_alloc(64, (size + 63) & ~size_t{63});
      break;
    case 3:
      if (heapline_test_posix_memalign(&ptr, 128, size) != 0) ptr = nullptr;
      break;
    default: ptr = heapline_test_memalign(256, size); break;
  }
  if (ptr) ++allocs;
  return ptr;
}

/**
 * @brief Set by the main thread once the fork of the current wave is done.
 */
std::atomic<bool> forked{false};

/**
 * @brief Wait for the fork of the current wave, so that no worker of the
 * wave has exited when it happens. The load is relaxed: TSan sees no
 * synchronization between the main thread and the workers here, which could
 * hide a race in heapline.
 */
void wait_for_fork() {
  while (!forked.load(std::memory_order_relaxed)) sched_yield();
}

/**
 * @brief Allocate, reallocate and release blocks, release blocks of other
 * threads and give some of its own away, then exit holding a few, which
 * the main thread releases. Halfway through, wait for the fork of the wave.
 *
 * @p arg is the number of the worker. Multiplied by Knuth's multiplicative
 * hash constant, about 2^32 divided by the golden ratio, it gives seeds far
 * apart, so that neighbouring workers do not follow close sequences.
 */
void* worker(void* arg) {
  uint64_t state  = reinterpret_cast<uintptr_t>(arg) * 2654435761u + 1;
  uint64_t allocs = 0;
  uint64_t frees  = 0;
  void* kept[kKept]{};

  for (int i = 0; i < kOps; ++i) {
    if (i == kOps / 2) wait_for_fork();
    void*& slot = kept[next(state) % kKept];
    switch (next(state) % 4) {
      case 0:
      case 1:
        if (slot) {
          heapline_test_free(slot);
          ++frees;
        }
        slot = allocate(state, allocs);
        break;
      case 2:
        if (slot) {
          void* moved = heapline_test_realloc(slot, 16 + next(state) % 8192);
          if (moved) {
            slot = moved;
            ++allocs;
            ++frees;
          }
        }
        break;
      default:
        if (void* other = take()) {
          heapline_test_free(other);
          ++frees;
        }
        if (slot && give(slot)) slot = nullptr;
        break;
    }
  }
  for (void* ptr : kept) {
    if (ptr && !give(ptr)) {
      heapline_test_free(ptr);
      ++frees;
    }
  }
  total_allocs.fetch_add(allocs, std::memory_order_relaxed);
  total_frees.fetch_add(frees, std::memory_order_relaxed);
  return nullptr;
}

std::atomic<bool> stop{false};

/**
 * @brief Take snapshots while the workers run: the peak never decreases and
 * never falls below the live bytes.
 */
void* reader(void*) {
  int64_t last_peak = 0;
  while (!stop.load(std::memory_order_relaxed)) {
    heapline_stats stats{};
    heapline_get_stats(&stats);
    if (stats.peak_bytes < last_peak) fail("peak never decreases");
    if (stats.peak_bytes < stats.live_bytes) fail("peak covers live bytes");
    last_peak = stats.peak_bytes;
  }
  return nullptr;
}

/**
 * @brief Body of a forked child, where only the forking thread exists. It
 * creates no thread, which TSan does not support after a fork from a
 * process with several threads, and checks the exact deltas of its own
 * operations.
 */
int run_child() {
  alarm(kAlarmSecs);
  heapline_stats before{};
  heapline_stats after{};
  heapline_get_stats(&before);
  void* ptr = heapline_test_malloc(1000);
  ptr       = heapline_test_realloc(ptr, 5000);
  heapline_test_free(ptr);
  heapline_get_stats(&after);
  if (after.allocs - before.allocs != 2 || after.frees - before.frees != 2 ||
      after.live_bytes != before.live_bytes) {
    std::fprintf(stderr, "FAIL: child counts its own operations\n");
    return 1;
  }
  return 0;
}

/**
 * @brief Fork while the workers of a wave run, so that the fork handlers meet
 * threads that register, publish and take snapshots, and check the child.
 */
void fork_and_check() {
  const pid_t pid = fork();
  if (pid < 0) {
    fail("fork");
    return;
  }
  if (pid == 0) _exit(run_child());
  int status = 0;
  if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0)
    fail("child exit status");
}

bool start(pthread_t* thread, void* (*body)(void*), void* arg) {
  if (pthread_create(thread, nullptr, body, arg) == 0) return true;
  fail("pthread_create");
  return false;
}

}  // namespace

int main() {
  alarm(kAlarmSecs);
  heapline_stats before{};
  heapline_get_stats(&before);

  pthread_t reader_thread;
  if (!start(&reader_thread, reader, nullptr)) return 1;

  uintptr_t seed = 0;
  for (int wave = 0; wave < kWaves; ++wave) {
    forked.store(false, std::memory_order_relaxed);
    pthread_t threads[kWaveThreads];
    int started = 0;
    for (auto& thread : threads)
      if (start(&thread, worker, reinterpret_cast<void*>(++seed))) ++started;
    fork_and_check();
    forked.store(true, std::memory_order_relaxed);
    for (int i = 0; i < started; ++i) pthread_join(threads[i], nullptr);
  }

  stop.store(true, std::memory_order_relaxed);
  pthread_join(reader_thread, nullptr);

  uint64_t frees = total_frees.load();
  while (void* ptr = take()) {
    heapline_test_free(ptr);
    ++frees;
  }

  heapline_stats after{};
  heapline_get_stats(&after);
  if (after.allocs - before.allocs != total_allocs.load()) fail("allocs");
  if (after.frees - before.frees != frees) fail("frees");
  if (after.live_bytes != before.live_bytes) fail("live bytes back to start");
  if (after.peak_bytes < after.live_bytes) fail("peak covers live bytes");

  return failures.load() == 0 ? 0 : 1;
}
