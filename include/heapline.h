// SPDX-License-Identifier: Apache-2.0

#ifndef HEAPLINE_H
#define HEAPLINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Snapshot of the heapline counters.
 */
struct heapline_stats {
  uint64_t allocs;     ///< Successful allocations, reallocations included.
  uint64_t frees;      ///< Released blocks, reallocations included.
  int64_t live_bytes;  ///< Usable bytes currently allocated.
  int64_t peak_bytes;  ///< Highest value reached by live_bytes, see below.
};

/**
 * @brief Copy the current counters into @p stats.
 *
 * allocs, frees and live_bytes count every thread, live or exited, except
 * for operations in progress in other threads. peak_bytes is exact as long
 * as only one thread of the process has allocated. When several threads have,
 * they publish their changes in batches, and peak_bytes may differ from the
 * true peak by up to (ceil(log2 N) + 1)/8192 of it for N threads. Takes a
 * mutex, so it must not be called from a signal handler or a fork handler.
 *
 * The library is usually preloaded rather than linked, so callers look this
 * symbol up with dlsym(RTLD_DEFAULT, "heapline_get_stats").
 */
void heapline_get_stats(struct heapline_stats* stats);

#ifdef __cplusplus
}
#endif

#endif
