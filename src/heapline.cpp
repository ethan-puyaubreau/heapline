// SPDX-License-Identifier: Apache-2.0

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "heapline.h"
#include "record.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <malloc.h>
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
 * @brief Number of successful allocations.
 */
std::atomic<uint64_t> alloc_count{0};

/**
 * @brief Number of non-null pointers released.
 */
std::atomic<uint64_t> free_count{0};

/**
 * @brief Bytes currently allocated, as reported by malloc_usable_size.
 *
 * Signed so that releasing blocks allocated outside of the tracked entry
 * points shows up as a negative value instead of wrapping around.
 */
std::atomic<int64_t> live_bytes{0};

/**
 * @brief Highest value reached by live_bytes.
 */
std::atomic<int64_t> peak_bytes{0};

/**
 * @brief CLOCK_MONOTONIC_COARSE time, in nanoseconds, at which peak_bytes was
 * last raised. Stored apart from peak_bytes, so the two may briefly disagree.
 */
std::atomic<int64_t> peak_time_ns{0};

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

__attribute__((constructor)) void init() {
  start_ns = now_ns(CLOCK_MONOTONIC);
  resolve();
}

/**
 * @brief Account for a new block.
 *
 * The usable size is read back from the allocator, so no table of live
 * pointers is needed and nothing is allocated on this path.
 */
void record_alloc(void* ptr) {
  if (!ptr) return;
  alloc_count.fetch_add(1, std::memory_order_relaxed);
  auto size    = static_cast<int64_t>(malloc_usable_size(ptr));
  int64_t live = live_bytes.fetch_add(size, std::memory_order_relaxed) + size;
  int64_t peak = peak_bytes.load(std::memory_order_relaxed);
  while (live > peak) {
    if (peak_bytes.compare_exchange_weak(peak, live,
                                         std::memory_order_relaxed)) {
      peak_time_ns.store(now_ns(CLOCK_MONOTONIC_COARSE),
                         std::memory_order_relaxed);
      break;
    }
  }
}

/**
 * @brief Account for a released block of the given usable size.
 */
void record_free(void* ptr, size_t size) {
  if (!ptr) return;
  free_count.fetch_add(1, std::memory_order_relaxed);
  live_bytes.fetch_sub(static_cast<int64_t>(size), std::memory_order_relaxed);
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

  heapline::Record record;
  record.rank = heapline::find_rank(
      +[](const char* name) -> const char* { return getenv(name); });
  record.host            = host;
  record.pid             = getpid();
  record.job             = job ? job : "";
  record.elapsed_s       = (now_ns(CLOCK_MONOTONIC) - start_ns) / 1e9;
  record.allocs          = alloc_count.load();
  record.frees           = free_count.load();
  record.peak_heap_bytes = peak_bytes.load();
  record.peak_heap_time_s =
      std::max<int64_t>(0, peak_time_ns.load() - start_ns) / 1e9;
  record.live_heap_at_exit_bytes = live_bytes.load();
  record.vmhwm_bytes             = status.vmhwm_bytes;
  record.vmrss_at_exit_bytes     = status.vmrss_bytes;
  print_summary(record);
  write_record(record);
}

}  // namespace

extern "C" void heapline_get_stats(heapline_stats* stats) {
  stats->allocs     = alloc_count.load();
  stats->frees      = free_count.load();
  stats->live_bytes = live_bytes.load();
  stats->peak_bytes = peak_bytes.load();
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
