// SPDX-License-Identifier: Apache-2.0

#include "heapline.h"

#include <dlfcn.h>
#include <malloc.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

constexpr int kBlocks       = 100;
constexpr int64_t kSize     = 1000;
constexpr int64_t kAlign    = 4096;
constexpr int64_t kCount    = kBlocks + 7;
constexpr int64_t kRequired = (kBlocks + 4) * kSize + kBlocks * 8 + kAlign;

/**
 * @brief Upper bound on the rounding added by glibc to each usable size.
 */
constexpr int64_t kSlack = 64;

int failures = 0;

void check(bool cond, const char* what) {
  if (cond) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++failures;
}

}  // namespace

/**
 * @brief Run a known allocation pattern under heapline and compare the
 * counter deltas against it.
 *
 * Only deltas are checked, so allocations made by the runtime before main
 * do not matter. Nothing is printed between snapshots.
 */
int main() {
  auto get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }

  heapline_stats before{};
  heapline_stats during{};
  heapline_stats after{};
  get_stats(&before);

  void* blocks[kBlocks];
  for (auto& block : blocks) block = std::malloc(kSize);
  blocks[0]     = std::realloc(blocks[0], 2 * kSize);
  void* zeroed  = std::calloc(kBlocks, 8);
  void* aligned = std::aligned_alloc(kAlign, kAlign);
  void* posix   = nullptr;
  int err       = posix_memalign(&posix, 64, kSize);
  void* legacy  = memalign(64, kSize);
  void* page    = valloc(kSize);
  void* rounded = pvalloc(kSize);

  get_stats(&during);

  for (auto* block : blocks) std::free(block);
  std::free(zeroed);
  std::free(aligned);
  std::free(posix);
  std::free(legacy);
  std::free(page);
  std::free(rounded);

  get_stats(&after);

  check(blocks[0] && zeroed && aligned && err == 0 && legacy && page && rounded,
        "allocations succeed");

  // pvalloc rounds the request up to a whole page.
  const int64_t page_size = sysconf(_SC_PAGESIZE);
  int64_t live            = during.live_bytes - before.live_bytes - page_size;
  check(during.allocs - before.allocs == kCount, "allocs during");
  check(during.frees - before.frees == 1, "frees during");
  check(live >= kRequired, "live bytes cover the requested sizes");
  check(live <= kRequired + kCount * kSlack, "live bytes within rounding");
  check(during.peak_bytes >= during.live_bytes, "peak covers live bytes");

  check(after.allocs - before.allocs == kCount, "allocs after");
  check(after.frees - before.frees == kCount, "frees after");
  check(after.live_bytes == before.live_bytes, "live bytes back to start");

  return failures == 0 ? 0 : 1;
}
