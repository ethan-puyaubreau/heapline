// SPDX-License-Identifier: Apache-2.0

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

using MallocFn        = void* (*)(size_t);
using CallocFn        = void* (*)(size_t, size_t);
using ReallocFn       = void* (*)(void*, size_t);
using FreeFn          = void (*)(void*);
using PosixMemalignFn = int (*)(void**, size_t, size_t);
using AlignedAllocFn  = void* (*)(size_t, size_t);

/**
 * @brief Allocator entry points of the next library in the lookup order.
 */
MallocFn real_malloc                = nullptr;
CallocFn real_calloc                = nullptr;
ReallocFn real_realloc              = nullptr;
FreeFn real_free                    = nullptr;
PosixMemalignFn real_posix_memalign = nullptr;
AlignedAllocFn real_aligned_alloc   = nullptr;

/**
 * @brief Number of successful allocations.
 */
std::atomic<uint64_t> alloc_count{0};

/**
 * @brief Number of non-null pointers released.
 */
std::atomic<uint64_t> free_count{0};

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
  resolving = false;
}

__attribute__((constructor)) void init() { resolve(); }

void record_alloc(void* ptr) {
  if (ptr) alloc_count.fetch_add(1, std::memory_order_relaxed);
}

void record_free(void* ptr) {
  if (ptr) free_count.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

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
  record_free(ptr);
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
  void* moved = real_realloc(ptr, size);
  if (moved || size == 0) record_free(ptr);
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
