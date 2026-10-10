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
 * Reads OMPI_COMM_WORLD_RANK, PMIX_RANK, PMI_RANK, MV2_COMM_WORLD_RANK and
 * SLURM_PROCID in this order and returns the first value that is a
 * non-negative integer. Empty or invalid values are skipped.
 *
 * SLURM_PROCID comes last because the ranks started by mpirun inside a Slurm
 * allocation inherit the SLURM_PROCID of the process that started them: 0 on
 * the first node, and the index of the node's daemon on the other nodes. It is
 * not their MPI rank.
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

/**
 * @brief Memory figures of the process read from /proc/self/status, in
 * bytes, or -1 when absent.
 */
struct ProcStatus {
  int64_t vmhwm_bytes = -1;  ///< Peak resident set size.
  int64_t vmrss_bytes = -1;  ///< Current resident set size.
};

/**
 * @brief Parse the VmHWM and VmRSS lines of a /proc/self/status text.
 */
ProcStatus parse_status(const char* text);

/**
 * @brief Read /proc/self/status into a stack buffer and parse it.
 *
 * Does not allocate, so it can run from the library destructor.
 */
ProcStatus read_status();

/**
 * @brief Values written to the record of one process.
 */
struct Record {
  long rank                       = -1;  ///< MPI rank, -1 outside MPI.
  const char* host                = "";  ///< Host name.
  long pid                        = 0;   ///< Process id.
  const char* job                 = "";  ///< SLURM_JOB_ID, empty if unset.
  double elapsed_s                = 0;   ///< Time since the library loaded.
  uint64_t allocs                 = 0;   ///< Successful allocations.
  uint64_t frees                  = 0;   ///< Released blocks.
  int64_t peak_heap_bytes         = 0;   ///< Highest live heap.
  double peak_heap_time_s         = 0;   ///< When the peak was reached.
  int64_t live_heap_at_exit_bytes = 0;   ///< Live heap at exit.
  int64_t vmhwm_bytes             = -1;  ///< Peak resident set size.
  int64_t vmrss_at_exit_bytes     = -1;  ///< Resident set size at exit.
};

/**
 * @brief First two lines of a version 1 record: format tag and column names.
 */
inline constexpr char kRecordHeader[] =
    "# heapline 1\n"
    "rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,"
    "peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,"
    "vmrss_at_exit_bytes\n";

/**
 * @brief Write the header and the values of @p record as CSV into @p buf.
 *
 * Commas, quotes and line breaks in the host and job names are replaced with
 * underscores, so that every value stays a single CSV field.
 *
 * @return Number of characters written, or -1 when @p buf is too small.
 */
int format_record(char* buf, size_t len, const Record& record);

}  // namespace heapline

#endif
