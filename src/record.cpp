// SPDX-License-Identifier: Apache-2.0

#include "record.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace heapline {

namespace {

/**
 * @brief Append @p text to @p buf from position @p out, stopping at
 * @p len - 1 characters. Returns the new position.
 */
size_t append(char* buf, size_t len, size_t out, const char* text) {
  while (*text && out + 1 < len) buf[out++] = *text++;
  return out;
}

}  // namespace

long find_rank(EnvLookup lookup) {
  static const char* const names[] = {"SLURM_PROCID", "OMPI_COMM_WORLD_RANK",
                                      "PMI_RANK", "PMIX_RANK",
                                      "MV2_COMM_WORLD_RANK"};
  for (const char* name : names) {
    const char* value = lookup(name);
    if (!value || !*value) continue;
    char* end = nullptr;
    errno     = 0;
    long rank = std::strtol(value, &end, 10);
    if (errno == 0 && *end == '\0' && rank >= 0) return rank;
  }
  return -1;
}

void expand_path(char* buf, size_t len, const char* pattern, long pid,
                 long rank, const char* host) {
  if (len == 0) return;
  size_t out = 0;
  char number[24];
  for (const char* c = pattern; *c && out + 1 < len; ++c) {
    if (c[0] == '%' && (c[1] == 'p' || c[1] == 'r')) {
      std::snprintf(number, sizeof(number), "%ld", c[1] == 'p' ? pid : rank);
      out = append(buf, len, out, number);
      ++c;
    } else if (c[0] == '%' && c[1] == 'h') {
      out = append(buf, len, out, host);
      ++c;
    } else {
      buf[out++] = *c;
    }
  }
  buf[out] = '\0';
}

}  // namespace heapline
