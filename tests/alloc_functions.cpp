// SPDX-License-Identifier: Apache-2.0

#include "heapline.h"

#include <dlfcn.h>
#include <malloc.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {

using GetStatsFn         = void (*)(heapline_stats*);
using FreeSizedFn        = void (*)(void*, size_t);
using FreeAlignedSizedFn = void (*)(void*, size_t, size_t);

constexpr size_t kSize  = 1000;
constexpr size_t kAlign = 256;

GetStatsFn get_stats = nullptr;
int failures         = 0;

void check(bool cond, const char* what, const char* step) {
  if (cond) return;
  std::fprintf(stderr, "FAIL: %s: %s\n", step, what);
  ++failures;
}

/**
 * @brief Counters before a step, compared with those after it.
 */
struct Step {
  const char* name;
  heapline_stats before{};

  explicit Step(const char* step_name) : name(step_name) { get_stats(&before); }

  /**
   * @brief Check that the step made @p allocs allocations and @p frees
   * releases, and changed the live bytes by exactly @p bytes.
   */
  void expect(uint64_t allocs, uint64_t frees, int64_t bytes) const {
    heapline_stats after{};
    get_stats(&after);
    check(after.allocs - before.allocs == allocs, "allocs", name);
    check(after.frees - before.frees == frees, "frees", name);
    check(after.live_bytes - before.live_bytes == bytes, "live bytes", name);
  }
};

int64_t usable(void* ptr) {
  return static_cast<int64_t>(malloc_usable_size(ptr));
}

/**
 * @brief Allocate a block with @p alloc, check that it counts one allocation
 * of its usable size, release it with free and check that it counts one
 * release that gives the bytes back.
 */
template <typename Alloc>
void alloc_then_free(const char* name, Alloc alloc) {
  Step step(name);
  void* ptr = alloc();
  check(ptr != nullptr, "allocation succeeds", name);
  if (!ptr) return;
  const int64_t bytes = usable(ptr);
  step.expect(1, 0, bytes);

  Step release(name);
  std::free(ptr);
  release.expect(0, 1, -bytes);
}

void test_allocators() {
  alloc_then_free("malloc", [] { return std::malloc(kSize); });
  alloc_then_free("calloc", [] { return std::calloc(kSize, 8); });
  alloc_then_free("aligned_alloc",
                  [] { return std::aligned_alloc(kAlign, 4 * kAlign); });
  alloc_then_free("posix_memalign", [] {
    void* ptr = nullptr;
    return posix_memalign(&ptr, kAlign, kSize) == 0 ? ptr : nullptr;
  });
  alloc_then_free("memalign", [] { return memalign(kAlign, kSize); });
  alloc_then_free("valloc", [] { return valloc(kSize); });
  alloc_then_free("pvalloc", [] { return pvalloc(kSize); });
  alloc_then_free("realloc of null",
                  [] { return std::realloc(nullptr, kSize); });
  alloc_then_free("reallocarray of null",
                  [] { return reallocarray(nullptr, kSize, 8); });
}

/**
 * @brief A reallocation counts as a release of the old block and an
 * allocation of the new one, whether it grows, shrinks or moves the block.
 */
void test_realloc() {
  void* ptr = std::malloc(kSize);
  for (size_t size : {16 * kSize, kSize / 4, 64 * kSize}) {
    const int64_t old_bytes = usable(ptr);
    Step step("realloc");
    void* moved = std::realloc(ptr, size);
    check(moved != nullptr, "reallocation succeeds", step.name);
    if (!moved) break;
    step.expect(1, 1, usable(moved) - old_bytes);
    ptr = moved;
  }

  const int64_t old_bytes = usable(ptr);
  Step array("reallocarray");
  void* moved = reallocarray(ptr, 4, 8 * kSize);
  check(moved != nullptr, "reallocation succeeds", array.name);
  if (moved) {
    array.expect(1, 1, usable(moved) - old_bytes);
    ptr = moved;
  }

  // glibc releases the block and returns null.
  const int64_t last_bytes = usable(ptr);
  Step zero("realloc to 0");
  void* none = std::realloc(ptr, 0);
  if (none) {
    zero.expect(1, 1, usable(none) - last_bytes);
    std::free(none);
  } else {
    zero.expect(0, 1, -last_bytes);
  }
}

/**
 * @brief Failed allocations count nothing, and neither does releasing null.
 * The sizes are volatile so that the compiler does not warn about them.
 */
void test_failures() {
  volatile size_t huge = SIZE_MAX;
  Step step("failures");
  void* ptr = nullptr;
  check(std::malloc(huge) == nullptr, "malloc fails", step.name);
  check(std::calloc(huge, huge) == nullptr, "calloc fails", step.name);
  check(posix_memalign(&ptr, 3, kSize) == EINVAL, "posix_memalign fails",
        step.name);
  check(reallocarray(nullptr, huge, huge) == nullptr, "reallocarray fails",
        step.name);
  std::free(nullptr);
  step.expect(0, 0, 0);

  // A failed reallocation keeps the old block, which stays counted.
  void* block = std::malloc(kSize);
  Step keep("failed realloc");
  check(std::realloc(block, huge) == nullptr, "realloc fails", keep.name);
  keep.expect(0, 0, 0);
  std::free(block);
}

/**
 * @brief The C23 sized releases, in glibc 2.43 and later. Looked up at run
 * time so that the test builds and runs on older systems.
 */
void test_sized_free() {
  auto free_sized =
      reinterpret_cast<FreeSizedFn>(dlsym(RTLD_DEFAULT, "free_sized"));
  auto free_aligned_sized = reinterpret_cast<FreeAlignedSizedFn>(
      dlsym(RTLD_DEFAULT, "free_aligned_sized"));
  if (!free_sized || !free_aligned_sized) {
    std::fprintf(stderr, "SKIP: no free_sized, sized releases not checked\n");
    return;
  }

  void* ptr           = std::malloc(kSize);
  const int64_t bytes = usable(ptr);
  Step sized("free_sized");
  free_sized(ptr, kSize);
  sized.expect(0, 1, -bytes);

  void* aligned               = std::aligned_alloc(kAlign, 4 * kAlign);
  const int64_t aligned_bytes = usable(aligned);
  Step aligned_sized("free_aligned_sized");
  free_aligned_sized(aligned, kAlign, 4 * kAlign);
  aligned_sized.expect(0, 1, -aligned_bytes);
}

}  // namespace

/**
 * @brief Check each allocation entry point on its own: the exact number of
 * allocations and releases it counts, and the exact change of the live bytes,
 * which is the usable size of the blocks.
 *
 * Nothing is printed between two snapshots of a step, so that stdio does not
 * allocate its buffer in the middle of one.
 */
int main() {
  get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }

  test_allocators();
  test_realloc();
  test_failures();
  test_sized_free();

  return failures == 0 ? 0 : 1;
}
