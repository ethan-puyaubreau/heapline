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
  int64_t peak_bytes;  ///< Highest value reached by live_bytes.
};

/**
 * @brief Copy the current counters into @p stats.
 *
 * The library is usually preloaded rather than linked, so callers look this
 * symbol up with dlsym(RTLD_DEFAULT, "heapline_get_stats").
 */
void heapline_get_stats(struct heapline_stats* stats);

#ifdef __cplusplus
}
#endif

#endif
