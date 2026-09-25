// SPDX-License-Identifier: Apache-2.0

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

using MallocFn = void* (*)(size_t);
using FreeFn   = void (*)(void*);

/**
 * @brief Allocator entry points of the next library in the lookup order.
 */
MallocFn real_malloc = nullptr;
FreeFn real_free     = nullptr;

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
  resolving   = true;
  real_malloc = reinterpret_cast<MallocFn>(dlsym(RTLD_NEXT, "malloc"));
  real_free   = reinterpret_cast<FreeFn>(dlsym(RTLD_NEXT, "free"));
  resolving   = false;
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
