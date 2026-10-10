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
 * @brief Value in bytes of the line of @p text that starts with the key
 * followed by a colon, or -1 when there is no such line.
 *
 * The value is a non-negative number followed by " kB" on the same line.
 * Only spaces and tabs are skipped after the colon, never a newline. A
 * truncated line, a non-numeric or negative value, and a value that would
 * overflow int64_t in bytes all give -1.
 */
int64_t field_bytes(const char* text, const char* key) {
  const size_t key_len = std::strlen(key);
  for (const char* line = text; line && *line;) {
    if (std::strncmp(line, key, key_len) == 0 && line[key_len] == ':') {
      const char* start = line + key_len + 1;
      while (*start == ' ' || *start == '\t') ++start;
      if (*start < '0' || *start > '9') return -1;
      char* end          = nullptr;
      long long kibibyte = std::strtoll(start, &end, 10);
      if (std::strncmp(end, " kB", 3) != 0) return -1;
      return kibibyte > INT64_MAX / 1024 ? -1 : kibibyte * 1024;
    }
    line = std::strchr(line, '\n');
    if (line) ++line;
  }
  return -1;
}

/**
 * @brief Copy @p in into @p out, replacing the characters that would break a
 * CSV field with underscores.
 */
void sanitize(char* out, size_t len, const char* in) {
  size_t i = 0;
  for (; in && in[i] && i + 1 < len; ++i) {
    char c = in[i];
    out[i] = (c == ',' || c == '"' || c == '\n' || c == '\r') ? '_' : c;
  }
  out[i] = '\0';
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
  char buf[16384];
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

int format_record(char* buf, size_t len, const Record& record) {
  char host[256];
  char job[64];
  sanitize(host, sizeof(host), record.host);
  sanitize(job, sizeof(job), record.job);
  int n = std::snprintf(
      buf, len, "%s%ld,%s,%ld,%s,%.3f,%llu,%llu,%lld,%.3f,%lld,%lld,%lld\n",
      kRecordHeader, record.rank, host, record.pid, job, record.elapsed_s,
      static_cast<unsigned long long>(record.allocs),
      static_cast<unsigned long long>(record.frees),
      static_cast<long long>(record.peak_heap_bytes), record.peak_heap_time_s,
      static_cast<long long>(record.live_heap_at_exit_bytes),
      static_cast<long long>(record.vmhwm_bytes),
      static_cast<long long>(record.vmrss_at_exit_bytes));
  return n < 0 || static_cast<size_t>(n) >= len ? -1 : n;
}

}  // namespace heapline
