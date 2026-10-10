// SPDX-License-Identifier: Apache-2.0

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "heapline.h"
#include "record.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

namespace {

using MallocFn        = void* (*)(size_t);
using CallocFn        = void* (*)(size_t, size_t);
using ReallocFn       = void* (*)(void*, size_t);
using FreeFn          = void (*)(void*);
using PosixMemalignFn = int (*)(void**, size_t, size_t);
using AlignedAllocFn  = void* (*)(size_t, size_t);
using MemalignFn      = void* (*)(size_t, size_t);
using VallocFn        = void* (*)(size_t);

/**
 * @brief Allocator entry points of the next library in the lookup order.
 */
MallocFn real_malloc                = nullptr;
CallocFn real_calloc                = nullptr;
ReallocFn real_realloc              = nullptr;
FreeFn real_free                    = nullptr;
PosixMemalignFn real_posix_memalign = nullptr;
AlignedAllocFn real_aligned_alloc   = nullptr;
MemalignFn real_memalign            = nullptr;
VallocFn real_valloc                = nullptr;
VallocFn real_pvalloc               = nullptr;

/**
 * @brief Allocations and releases counted outside of the per-thread
 * counters: those of exited threads, and those made by a thread that has no
 * counters, before the library constructor or after its exit handler.
 */
std::atomic<uint64_t> alloc_count{0};
std::atomic<uint64_t> free_count{0};

/**
 * @brief The globals that threads read or write on the allocation path. Each
 * group sits on cache lines of its own, and the structure is padded to whole
 * lines, so that no other variable shares a line with them and publishing does
 * not slow down the reads of the others.
 *
 * live_bytes is the published bytes currently allocated, as reported by
 * malloc_usable_size. Registered threads hold back a bounded part of their
 * own changes, see ThreadCounters. It is signed so that releasing blocks
 * allocated outside of the tracked entry points shows up as a negative value
 * instead of wrapping around.
 *
 * peak_bytes is the highest value reached by live_bytes, and peak_time_ns the
 * CLOCK_MONOTONIC_COARSE time, in nanoseconds, at which it was last raised
 * with a time stamp. They are stored apart, so the two may briefly disagree.
 *
 * A registered thread holds back at most peak_bytes >> limit_shift bytes, for
 * the limit_shift it last read. See shift_for() for the total.
 */
struct alignas(64) Shared {
  alignas(64) std::atomic<int64_t> live_bytes{0};
  alignas(64) std::atomic<int64_t> peak_bytes{0};
  std::atomic<int64_t> peak_time_ns{0};
  alignas(64) std::atomic<int> limit_shift{63};
};

static_assert(sizeof(Shared) == 192, "three cache lines, none shared");

Shared shared;

/**
 * @brief Counters of one thread, kept in a node of a pool outside of the
 * thread-local storage, so that they outlive the stack of the thread.
 *
 * Only the owning thread writes them, with relaxed loads and stores rather
 * than read-modify-write operations, so the allocation path touches no
 * shared cache line. pending is published to live_bytes once its magnitude
 * exceeds peak_bytes >> limit_shift. totals() reads the counters of other
 * threads under registry_mutex, which also guards next. Each node has a
 * cache line of its own, so that the nodes of two threads never share one.
 */
struct alignas(64) ThreadCounters {
  std::atomic<int64_t> pending;  ///< Bytes not yet added to live_bytes.
  std::atomic<uint64_t> allocs;  ///< Allocations made by this thread.
  std::atomic<uint64_t> frees;   ///< Releases made by this thread.
  ThreadCounters* next;          ///< Next node of the registry or free list.
};

static_assert(sizeof(ThreadCounters) == 64, "one node per cache line");

/**
 * @brief State of the calling thread. Trivial and zero-initialized, so that
 * the thread needs no TLS constructor or destructor, which could allocate. The
 * initial-exec model avoids a call to __tls_get_addr on every access; it
 * requires the library to be loaded at startup, as LD_PRELOAD does.
 *
 * local is the node of the thread, or null when the thread counts in the
 * globals. registered tells that the thread has got a node, or has failed to
 * get one for good.
 */
__attribute__((tls_model("initial-exec"))) thread_local ThreadCounters* local;
__attribute__((tls_model("initial-exec"))) thread_local bool registered;

/**
 * @brief Nodes of the threads that have counters, and their number.
 * registry_mutex also serializes the exit of a thread with the computation of
 * totals, and guards the node pool below.
 */
pthread_mutex_t registry_mutex = PTHREAD_MUTEX_INITIALIZER;
ThreadCounters* registry       = nullptr;
int64_t registered_threads     = 0;

/**
 * @brief Node pool: nodes released by exited threads, and the rest of the
 * current chunk. Chunks come from mmap, not from malloc, and are never
 * unmapped.
 */
constexpr size_t kChunkBytes = 64 << 10;
ThreadCounters* free_nodes   = nullptr;
char* chunk_next             = nullptr;
char* chunk_end              = nullptr;

/**
 * @brief Key whose destructor folds the counters of an exiting thread into
 * the globals. Threads register only once the constructor has created it.
 */
pthread_key_t exit_key;
std::atomic<bool> registry_ready{false};

/**
 * @brief CLOCK_MONOTONIC time, in nanoseconds, at which the library was
 * loaded.
 */
int64_t start_ns = 0;

/**
 * @brief Current time of @p clock in nanoseconds.
 */
int64_t now_ns(clockid_t clock) {
  timespec ts{};
  clock_gettime(clock, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

/**
 * @brief Arena serving allocations made while the real allocator is being
 * resolved, since dlsym itself may allocate.
 */
alignas(max_align_t) char bootstrap_buf[4096];
size_t bootstrap_used = 0;

void* bootstrap_alloc(size_t size) {
  const size_t align = alignof(max_align_t);
  size               = (size + align - 1) & ~(align - 1);
  if (size > sizeof(bootstrap_buf) - bootstrap_used) return nullptr;
  void* ptr = bootstrap_buf + bootstrap_used;
  bootstrap_used += size;
  return ptr;
}

bool from_bootstrap(const void* ptr) {
  auto* p = static_cast<const char*>(ptr);
  return p >= bootstrap_buf && p < bootstrap_buf + sizeof(bootstrap_buf);
}

/**
 * @brief Resolve the real allocator functions.
 *
 * Called from the library constructor, and lazily by the first allocation
 * when another constructor allocates before ours runs. Re-entrant calls
 * issued by dlsym return early and fall back to the bootstrap arena.
 *
 * @note Not synchronized. Allocations are expected to stay single-threaded
 * until the library constructor has run.
 */
void resolve() {
  static bool resolving = false;
  if (resolving) return;
  resolving    = true;
  real_malloc  = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
  real_calloc  = reinterpret_cast<CallocFn>(dlsym(RTLD_NEXT, "calloc"));
  real_realloc = reinterpret_cast<ReallocFn>(dlsym(RTLD_NEXT, "realloc"));
  real_free    = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
  real_posix_memalign =
      reinterpret_cast<PosixMemalignFn>(dlsym(RTLD_NEXT, "posix_memalign"));
  real_aligned_alloc =
      reinterpret_cast<AlignedAllocFn>(dlsym(RTLD_NEXT, "aligned_alloc"));
  real_memalign = reinterpret_cast<MemalignFn>(dlsym(RTLD_NEXT, "memalign"));
  real_valloc   = reinterpret_cast<VallocFn>(dlsym(RTLD_NEXT, "valloc"));
  real_pvalloc  = reinterpret_cast<VallocFn>(dlsym(RTLD_NEXT, "pvalloc"));
  resolving     = false;
}

/**
 * @brief limit_shift for @p threads registered threads.
 *
 * A single thread holds nothing back, so its peak is exact. Each thread holds
 * back at most 1/(4096 * 2^ceil(log2 n)) of the peak, for the n it last saw.
 * Once every thread has operated since the last registration, the total is at
 * most 1/4096. In the worst case it is (ceil(log2 N) + 1)/8192, because the
 * j-th live thread in registration order saw at least j threads.
 */
int shift_for(int64_t threads) {
  if (threads <= 1) return 63;
  int shift = 12;
  while ((int64_t{1} << (shift - 12)) < threads) ++shift;
  return shift;
}

/**
 * @brief Add @p delta to live_bytes and raise the peak if needed.
 *
 * The time of the peak is stored only when @p stamp is set. Bytes folded in
 * from the counters of a thread were held since an earlier moment that is not
 * known, so they raise the peak without moving its time.
 */
void publish(int64_t delta, bool stamp) {
  int64_t live =
      shared.live_bytes.fetch_add(delta, std::memory_order_relaxed) + delta;
  int64_t peak = shared.peak_bytes.load(std::memory_order_relaxed);
  while (live > peak) {
    if (shared.peak_bytes.compare_exchange_weak(peak, live,
                                                std::memory_order_relaxed)) {
      if (stamp)
        shared.peak_time_ns.store(now_ns(CLOCK_MONOTONIC_COARSE),
                                  std::memory_order_relaxed);
      break;
    }
  }
}

/**
 * @brief Publish the bytes held back by the calling thread.
 */
void publish_own() {
  ThreadCounters* counters = local;
  if (!counters) return;
  int64_t pending = counters->pending.load(std::memory_order_relaxed);
  if (pending == 0) return;
  counters->pending.store(0, std::memory_order_relaxed);
  publish(pending, false);
}

/**
 * @brief Take a zeroed node from the pool, or null when no memory is left for
 * a new chunk. Called with registry_mutex held.
 */
ThreadCounters* take_node() {
  ThreadCounters* node = free_nodes;
  if (node) {
    free_nodes = node->next;
  } else {
    if (chunk_next == chunk_end) {
      void* chunk = mmap(nullptr, kChunkBytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (chunk == MAP_FAILED) return nullptr;
      chunk_next = static_cast<char*>(chunk);
      chunk_end  = chunk_next + kChunkBytes;
    }
    node = new (chunk_next) ThreadCounters;
    chunk_next += sizeof(ThreadCounters);
  }
  node->pending.store(0, std::memory_order_relaxed);
  node->allocs.store(0, std::memory_order_relaxed);
  node->frees.store(0, std::memory_order_relaxed);
  node->next = nullptr;
  return node;
}

void retire(void* arg);

/**
 * @brief Give the calling thread a node on its first counted operation.
 *
 * registry_mutex is taken with a trylock, and a thread that finds it busy
 * does not register: the operation counts in the globals and the next one
 * tries again. Waiting could deadlock with a fork. The prepare handler of
 * heapline holds the mutex before glibc takes its own locks (for the name
 * services, the list of streams, malloc), so a thread whose first counted
 * operation is made under one of them would wait for the mutex while the
 * forking thread waits for that lock. An allocation made by a signal handler
 * while its thread holds the mutex finds it busy instead of deadlocking; a
 * fork from such a handler still deadlocks, as it would in retire() or
 * totals(). The forking thread, and the child before it resets the registry,
 * find the mutex busy too.
 *
 * registered is set once the mutex is held, so that an allocation made while
 * registering, by pthread_setspecific for instance, does not register again.
 * Nothing under the mutex calls malloc: take_node uses mmap. If no node can be
 * had, the thread keeps counting in the globals.
 *
 * A thread that registers after its last round of key destructors sets
 * exit_key too late for retire() to run: it keeps its node after its exit.
 * Destructors of C++ thread_local objects run before the keys, so only another
 * key can cause this, in a thread that made no counted operation earlier. The
 * counts of that thread stay exact, at the cost of one node. A new thread that
 * gets the same stack starts with zeroed thread-local storage and takes a
 * node of its own.
 */
void register_thread() {
  if (!registry_ready.load(std::memory_order_acquire)) return;
  if (pthread_mutex_trylock(&registry_mutex) != 0) return;
  registered = true;

  ThreadCounters* node = take_node();
  if (node) {
    node->next = registry;
    registry   = node;
    shared.limit_shift.store(shift_for(++registered_threads),
                             std::memory_order_relaxed);
    local = node;
  }
  pthread_mutex_unlock(&registry_mutex);
  // Without the key, the node would stay in the registry after the exit.
  if (node && pthread_setspecific(exit_key, node) != 0) retire(node);
}

/**
 * @brief Destructor of exit_key: fold the counters of the exiting thread into
 * the globals and give its node back to the pool. Later operations of the
 * thread, made by other TLS destructors, are counted in the globals directly.
 */
void retire(void* arg) {
  auto* counters = static_cast<ThreadCounters*>(arg);
  local          = nullptr;
  pthread_mutex_lock(&registry_mutex);
  publish(counters->pending.load(std::memory_order_relaxed), false);
  alloc_count.fetch_add(counters->allocs.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
  free_count.fetch_add(counters->frees.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
  for (ThreadCounters** link = &registry; *link; link = &(*link)->next) {
    if (*link == counters) {
      *link = counters->next;
      break;
    }
  }
  counters->next = free_nodes;
  free_nodes     = counters;
  shared.limit_shift.store(shift_for(--registered_threads),
                           std::memory_order_relaxed);
  pthread_mutex_unlock(&registry_mutex);
}

/**
 * @brief Count one operation of @p delta bytes: the usable size for an
 * allocation, minus the usable size for a release.
 *
 * The usable size is read back from the allocator, so no table of live
 * pointers is needed and nothing is allocated on this path.
 */
void account(int64_t delta, bool is_alloc) {
  ThreadCounters* counters = local;
  if (__builtin_expect(!counters, 0)) {
    if (!registered) register_thread();
    counters = local;
    if (!counters) {
      (is_alloc ? alloc_count : free_count)
          .fetch_add(1, std::memory_order_relaxed);
      publish(delta, true);
      return;
    }
  }
  std::atomic<uint64_t>& count = is_alloc ? counters->allocs : counters->frees;
  count.store(count.load(std::memory_order_relaxed) + 1,
              std::memory_order_relaxed);
  int64_t pending = counters->pending.load(std::memory_order_relaxed) + delta;
  int64_t limit   = shared.peak_bytes.load(std::memory_order_relaxed) >>
                  shared.limit_shift.load(std::memory_order_relaxed);
  if (pending > limit || pending < -limit) {
    counters->pending.store(0, std::memory_order_relaxed);
    publish(pending, true);
  } else {
    counters->pending.store(pending, std::memory_order_relaxed);
  }
}

/**
 * @brief Account for a new block.
 */
void record_alloc(void* ptr) {
  if (!ptr) return;
  account(static_cast<int64_t>(malloc_usable_size(ptr)), true);
}

/**
 * @brief Account for a released block of the given usable size.
 */
void record_free(void* ptr, size_t size) {
  if (!ptr) return;
  account(-static_cast<int64_t>(size), false);
}

/**
 * @brief Exact totals: the globals plus the counters of the threads still
 * registered. An operation in progress in another thread may be missing. The
 * peak is raised to the live bytes, which include the bytes held back by the
 * threads: the true peak is not below the true live bytes.
 */
heapline_stats totals() {
  heapline_stats stats{};
  pthread_mutex_lock(&registry_mutex);
  stats.allocs     = alloc_count.load(std::memory_order_relaxed);
  stats.frees      = free_count.load(std::memory_order_relaxed);
  stats.live_bytes = shared.live_bytes.load(std::memory_order_relaxed);
  for (ThreadCounters* counters = registry; counters;
       counters                 = counters->next) {
    stats.allocs += counters->allocs.load(std::memory_order_relaxed);
    stats.frees += counters->frees.load(std::memory_order_relaxed);
    stats.live_bytes += counters->pending.load(std::memory_order_relaxed);
  }
  pthread_mutex_unlock(&registry_mutex);
  stats.peak_bytes = std::max(shared.peak_bytes.load(std::memory_order_relaxed),
                              stats.live_bytes);
  return stats;
}

/**
 * @brief Fork handlers. lock_registry runs before the fork, so that no other
 * thread holds registry_mutex while the process forks; the parent unlocks it
 * again, and the child resets the registry, see reset_registry_in_child().
 * Until then no thread of the process registers, see register_thread().
 */
void lock_registry() { pthread_mutex_lock(&registry_mutex); }
void unlock_registry() { pthread_mutex_unlock(&registry_mutex); }

/**
 * @brief Fork handler of the child, where only the forking thread exists.
 * Fold the counters of the parent's other threads into the globals and give
 * their nodes back to the pool; only the node of the forking thread, if it
 * has one, stays in the registry.
 */
void reset_registry_in_child() {
  ThreadCounters* mine = local;
  ThreadCounters* node = registry;
  while (node) {
    ThreadCounters* next = node->next;
    if (node != mine) {
      publish(node->pending.load(std::memory_order_relaxed), false);
      alloc_count.fetch_add(node->allocs.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
      free_count.fetch_add(node->frees.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
      node->next = free_nodes;
      free_nodes = node;
    }
    node = next;
  }
  registry = mine;
  if (mine) mine->next = nullptr;
  registered_threads = mine ? 1 : 0;
  shared.limit_shift.store(shift_for(registered_threads),
                           std::memory_order_relaxed);
  pthread_mutex_unlock(&registry_mutex);
}

__attribute__((constructor)) void init() {
  start_ns = now_ns(CLOCK_MONOTONIC);
  resolve();
  // Without the key or the fork handlers, every thread keeps counting in
  // the globals, as before.
  if (pthread_key_create(&exit_key, retire) == 0 &&
      pthread_atfork(lock_registry, unlock_registry, reset_registry_in_child) ==
          0)
    registry_ready.store(true, std::memory_order_release);
}

/**
 * @brief Format a byte count with a binary unit suffix.
 */
void format_bytes(char* buf, size_t len, int64_t bytes) {
  static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  auto value                       = static_cast<double>(bytes);
  size_t unit                      = 0;
  while ((value >= 1024.0 || value <= -1024.0) && unit + 1 < std::size(units)) {
    value /= 1024.0;
    ++unit;
  }
  if (unit == 0)
    std::snprintf(buf, len, "%lld B", static_cast<long long>(bytes));
  else
    std::snprintf(buf, len, "%.1f %s", value, units[unit]);
}

/**
 * @brief Print the one-line summary of @p record to stderr.
 */
void print_summary(const heapline::Record& record) {
  char peak[32];
  char live[32];
  char line[160];
  format_bytes(peak, sizeof(peak), record.peak_heap_bytes);
  format_bytes(live, sizeof(live), record.live_heap_at_exit_bytes);
  int len = std::snprintf(
      line, sizeof(line),
      "heapline: %llu allocs, %llu frees, peak %s, live at exit %s\n",
      static_cast<unsigned long long>(record.allocs),
      static_cast<unsigned long long>(record.frees), peak, live);
  if (len <= 0) return;
  [[maybe_unused]] ssize_t written =
      write(STDERR_FILENO, line, std::min<size_t>(len, sizeof(line) - 1));
}

/**
 * @brief Write @p record as CSV to the file named by HEAPLINE_OUTPUT, or to
 * stderr when that file cannot be opened. Does nothing when HEAPLINE_OUTPUT
 * is not set.
 */
void write_record(const heapline::Record& record) {
  const char* pattern = getenv("HEAPLINE_OUTPUT");
  if (!pattern || !*pattern) return;
  char text[1024];
  int len = heapline::format_record(text, sizeof(text), record);
  if (len < 0) return;
  char path[PATH_MAX];
  heapline::expand_path(path, sizeof(path), pattern, record.pid, record.rank,
                        record.host);
  int fd  = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  int out = fd < 0 ? STDERR_FILENO : fd;
  [[maybe_unused]] ssize_t written = write(out, text, len);
  if (fd >= 0) close(fd);
}

/**
 * @brief Gather the final counters when the library is unloaded, print the
 * summary and write the record.
 *
 * Uses stack buffers, snprintf and write rather than stdio streams, which may
 * allocate or already be closed at this point.
 */
__attribute__((destructor)) void finish() {
  char host[256] = "";
  gethostname(host, sizeof(host) - 1);
  const char* job             = getenv("SLURM_JOB_ID");
  heapline::ProcStatus status = heapline::read_status();

  publish_own();
  const heapline_stats stats = totals();

  heapline::Record record;
  record.rank = heapline::find_rank(
      +[](const char* name) -> const char* { return getenv(name); });
  record.host            = host;
  record.pid             = getpid();
  record.job             = job ? job : "";
  record.elapsed_s       = (now_ns(CLOCK_MONOTONIC) - start_ns) / 1e9;
  record.allocs          = stats.allocs;
  record.frees           = stats.frees;
  record.peak_heap_bytes = stats.peak_bytes;
  record.peak_heap_time_s =
      std::max<int64_t>(0, shared.peak_time_ns.load() - start_ns) / 1e9;
  record.live_heap_at_exit_bytes = stats.live_bytes;
  record.vmhwm_bytes             = status.vmhwm_bytes;
  record.vmrss_at_exit_bytes     = status.vmrss_bytes;
  print_summary(record);
  write_record(record);
}

}  // namespace

extern "C" void heapline_get_stats(heapline_stats* stats) {
  publish_own();
  *stats = totals();
}

extern "C" void* malloc(size_t size) noexcept {
  if (!real_malloc) resolve();
  if (!real_malloc) return bootstrap_alloc(size);
  void* ptr = real_malloc(size);
  record_alloc(ptr);
  return ptr;
}

extern "C" void free(void* ptr) noexcept {
  if (from_bootstrap(ptr)) return;
  if (!real_free) resolve();
  record_free(ptr, malloc_usable_size(ptr));
  real_free(ptr);
}

extern "C" void* calloc(size_t count, size_t size) noexcept {
  if (!real_calloc) resolve();
  if (!real_calloc) {
    if (size != 0 && count > SIZE_MAX / size) return nullptr;
    return bootstrap_alloc(count * size);
  }
  void* ptr = real_calloc(count, size);
  record_alloc(ptr);
  return ptr;
}

/**
 * @brief Counted as a release of the old block followed by an allocation of
 * the new one, so that allocations and releases stay balanced.
 */
extern "C" void* realloc(void* ptr, size_t size) noexcept {
  if (from_bootstrap(ptr)) {
    void* moved = malloc(size);
    if (moved) {
      auto* end = bootstrap_buf + sizeof(bootstrap_buf);
      std::memcpy(moved, ptr,
                  std::min<size_t>(size, end - static_cast<char*>(ptr)));
    }
    return moved;
  }
  if (!real_realloc) resolve();
  size_t old_size = malloc_usable_size(ptr);
  void* moved     = real_realloc(ptr, size);
  if (moved || size == 0) record_free(ptr, old_size);
  record_alloc(moved);
  return moved;
}

extern "C" int posix_memalign(void** ptr, size_t alignment,
                              size_t size) noexcept {
  if (!real_posix_memalign) resolve();
  int err = real_posix_memalign(ptr, alignment, size);
  if (err == 0) record_alloc(*ptr);
  return err;
}

extern "C" void* aligned_alloc(size_t alignment, size_t size) noexcept {
  if (!real_aligned_alloc) resolve();
  void* ptr = real_aligned_alloc(alignment, size);
  record_alloc(ptr);
  return ptr;
}

/**
 * @brief Legacy page and alignment allocators, still found in older codes.
 * Tracked so that releasing their blocks keeps the live byte count right.
 */
extern "C" void* memalign(size_t alignment, size_t size) noexcept {
  if (!real_memalign) resolve();
  void* ptr = real_memalign(alignment, size);
  record_alloc(ptr);
  return ptr;
}

extern "C" void* valloc(size_t size) noexcept {
  if (!real_valloc) resolve();
  void* ptr = real_valloc(size);
  record_alloc(ptr);
  return ptr;
}

extern "C" void* pvalloc(size_t size) noexcept {
  if (!real_pvalloc) resolve();
  void* ptr = real_pvalloc(size);
  record_alloc(ptr);
  return ptr;
}
