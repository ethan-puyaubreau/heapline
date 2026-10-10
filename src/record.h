// SPDX-License-Identifier: Apache-2.0

#ifndef HEAPLINE_RECORD_H
#define HEAPLINE_RECORD_H

#include <cstddef>
#include <cstdint>

namespace heapline {

/**
 * @brief Function returning the value of an environment variable, or nullptr
 * when it is not set: getenv in the library, a stub in the tests.
 */
using EnvLookup = const char* (*)(const char*);

/**
 * @brief Rank of the process in an MPI job.
 *
 * Reads SLURM_PROCID, OMPI_COMM_WORLD_RANK, PMI_RANK, PMIX_RANK and
 * MV2_COMM_WORLD_RANK in this order and returns the first value that is a
 * non-negative integer. Empty or invalid values are skipped.
 *
 * @return The rank, or -1 when the process is not part of an MPI job.
 */
long find_rank(EnvLookup lookup);

/**
 * @brief Copy @p pattern into @p buf, replacing %p with @p pid, %r with
 * @p rank and %h with @p host.
 *
 * Any other character, including a % that starts no known sequence, is
 * copied as is. The result is null-terminated and cut to @p len - 1
 * characters.
 */
void expand_path(char* buf, size_t len, const char* pattern, long pid,
                 long rank, const char* host);

}  // namespace heapline

#endif
