// SPDX-License-Identifier: Apache-2.0

#include "record.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

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

/**
 * @brief Value in bytes of the "<key>: <n> kB" line of @p text, or -1 when
 * the line is absent or malformed.
 */
int64_t field_bytes(const char* text, const char* key) {
  const size_t key_len = std::strlen(key);
  for (const char* line = text; line && *line;) {
    if (std::strncmp(line, key, key_len) == 0 && line[key_len] == ':') {
      const char* start  = line + key_len + 1;
      char* end          = nullptr;
      long long kibibyte = std::strtoll(start, &end, 10);
      return end != start && kibibyte >= 0 ? kibibyte * 1024 : -1;
    }
    line = std::strchr(line, '\n');
    if (line) ++line;
  }
  return -1;
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

ProcStatus parse_status(const char* text) {
  return {field_bytes(text, "VmHWM"), field_bytes(text, "VmRSS")};
}

ProcStatus read_status() {
  char buf[4096];
  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return {};
  size_t used = 0;
  ssize_t n   = 0;
  while (used + 1 < sizeof(buf) &&
         (n = read(fd, buf + used, sizeof(buf) - 1 - used)) > 0)
    used += static_cast<size_t>(n);
  close(fd);
  buf[used] = '\0';
  return parse_status(buf);
}

}  // namespace heapline
