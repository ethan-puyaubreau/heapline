// SPDX-License-Identifier: Apache-2.0

#include "record.h"

#include <clocale>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
  if (cond) return;
  std::fprintf(stderr, "FAIL: %s\n", what);
  ++failures;
}

/**
 * @brief Environment seen by fake_lookup: name and value pairs, ended by a
 * null name.
 */
const char* const* fake_env = nullptr;

const char* fake_lookup(const char* name) {
  for (const char* const* e = fake_env; e && *e; e += 2)
    if (std::strcmp(e[0], name) == 0) return e[1];
  return nullptr;
}

long rank_with(const char* const* env) {
  fake_env = env;
  return heapline::find_rank(fake_lookup);
}

void test_find_rank() {
  const char* none[]     = {nullptr};
  const char* slurm[]    = {"SLURM_PROCID", "3", nullptr};
  const char* ompi[]     = {"OMPI_COMM_WORLD_RANK", "5", nullptr};
  const char* pmi[]      = {"PMI_RANK", "7", nullptr};
  const char* pmix[]     = {"PMIX_RANK", "9", nullptr};
  const char* mv2[]      = {"MV2_COMM_WORLD_RANK", "11", nullptr};
  const char* both[]     = {"OMPI_COMM_WORLD_RANK", "5", "SLURM_PROCID", "3",
                            nullptr};
  const char* batch[]    = {"PMI_RANK", "7", "SLURM_PROCID", "0", nullptr};
  const char* invalid[]  = {"OMPI_COMM_WORLD_RANK", "3x", "SLURM_PROCID", "2",
                            nullptr};
  const char* empty[]    = {"SLURM_PROCID", "", nullptr};
  const char* negative[] = {"SLURM_PROCID", "-1", nullptr};
  check(rank_with(none) == -1, "no variable gives -1");
  check(rank_with(slurm) == 3, "SLURM_PROCID");
  check(rank_with(ompi) == 5, "OMPI_COMM_WORLD_RANK");
  check(rank_with(pmi) == 7, "PMI_RANK");
  check(rank_with(pmix) == 9, "PMIX_RANK");
  check(rank_with(mv2) == 11, "MV2_COMM_WORLD_RANK");
  check(rank_with(both) == 5, "the launcher rank wins over SLURM_PROCID");
  check(rank_with(batch) == 7, "PMI_RANK wins over an inherited SLURM_PROCID");
  check(rank_with(invalid) == 2, "an invalid value is skipped");
  check(rank_with(empty) == -1, "an empty value is skipped");
  check(rank_with(negative) == -1, "a negative value is skipped");
}

void test_expand_path() {
  char buf[64];
  heapline::expand_path(buf, sizeof(buf), "heapline.%p.csv", 42, 3, "node");
  check(std::strcmp(buf, "heapline.42.csv") == 0, "%p is the pid");
  heapline::expand_path(buf, sizeof(buf), "%h/%r.csv", 42, 3, "node-a");
  check(std::strcmp(buf, "node-a/3.csv") == 0, "%h and %r");
  heapline::expand_path(buf, sizeof(buf), "r%r", 42, -1, "node");
  check(std::strcmp(buf, "r-1") == 0, "rank -1 outside MPI");
  heapline::expand_path(buf, sizeof(buf), "a%", 42, 3, "node");
  check(std::strcmp(buf, "a%") == 0, "a trailing % stays");
  heapline::expand_path(buf, sizeof(buf), "%x.csv", 42, 3, "node");
  check(std::strcmp(buf, "%x.csv") == 0, "an unknown sequence stays");
  heapline::expand_path(buf, 8, "abcdefghij", 42, 3, "node");
  check(std::strcmp(buf, "abcdefg") == 0, "truncated to len - 1");
}

void test_status() {
  const char* sample =
      "Name:\tapp\n"
      "VmPeak:\t  999999 kB\n"
      "VmHWM:\t    2048 kB\n"
      "VmRSS:\t    1024 kB\n"
      "Threads:\t1\n";
  heapline::ProcStatus parsed = heapline::parse_status(sample);
  check(parsed.vmhwm_bytes == 2048 * 1024, "VmHWM in bytes");
  check(parsed.vmrss_bytes == 1024 * 1024, "VmRSS in bytes");

  heapline::ProcStatus missing = heapline::parse_status("VmHWMX:\t1 kB\n");
  check(missing.vmhwm_bytes == -1, "VmHWMX is not VmHWM");
  check(missing.vmrss_bytes == -1, "missing VmRSS gives -1");

  heapline::ProcStatus truncated = heapline::parse_status("VmHWM:\t    12");
  check(truncated.vmhwm_bytes == -1, "a truncated VmHWM line gives -1");
  heapline::ProcStatus non_numeric =
      heapline::parse_status("VmHWM:\t abc kB\n");
  check(non_numeric.vmhwm_bytes == -1, "a non-numeric VmHWM gives -1");
  heapline::ProcStatus negative = heapline::parse_status("VmHWM:\t -5 kB\n");
  check(negative.vmhwm_bytes == -1, "a negative VmHWM gives -1");
  heapline::ProcStatus next_line = heapline::parse_status("VmHWM:\n123 kB\n");
  check(next_line.vmhwm_bytes == -1, "VmHWM does not take the next line");
  heapline::ProcStatus overflow =
      heapline::parse_status("VmHWM:\t 99999999999999999999 kB\n");
  check(overflow.vmhwm_bytes == -1, "an overflowing VmHWM gives -1");

  heapline::ProcStatus own = heapline::read_status();
  check(own.vmhwm_bytes > 0, "own VmHWM is read");
  check(own.vmrss_bytes > 0, "own VmRSS is read");
  check(own.vmhwm_bytes >= own.vmrss_bytes, "VmHWM covers VmRSS");
}

void test_format_record() {
  heapline::Record record;
  record.rank                    = 3;
  record.host                    = "node,a\"b";
  record.pid                     = 1234;
  record.job                     = "4242";
  record.elapsed_s               = 12.5;
  record.allocs                  = 100;
  record.frees                   = 90;
  record.peak_heap_bytes         = 1048576;
  record.peak_heap_time_s        = 4.25;
  record.live_heap_at_exit_bytes = 512;
  record.vmhwm_bytes             = 2097152;
  record.vmrss_at_exit_bytes     = 1572864;

  char buf[1024];
  int len = heapline::format_record(buf, sizeof(buf), record);
  const char* expected =
      "# heapline 1\n"
      "rank,host,pid,job,elapsed_s,allocs,frees,peak_heap_bytes,"
      "peak_heap_time_s,live_heap_at_exit_bytes,vmhwm_bytes,"
      "vmrss_at_exit_bytes\n"
      "3,node_a_b,1234,4242,12.500,100,90,1048576,4.250,512,2097152,1572864\n";
  check(std::strcmp(buf, expected) == 0, "record text");
  check(len == static_cast<int>(std::strlen(expected)), "record length");

  char small[16];
  check(heapline::format_record(small, sizeof(small), record) == -1,
        "too small a buffer gives -1");
}

void test_format_record_times() {
  heapline::Record record;
  char buf[1024];
  record.elapsed_s = 1.0005;
  heapline::format_record(buf, sizeof(buf), record);
  check(std::strstr(buf, "\n-1,,0,,1.001,0,0,0,0.000,0,-1,-1\n") != nullptr,
        "elapsed_s is rounded half up to milliseconds");

  record.elapsed_s = 0;
  heapline::format_record(buf, sizeof(buf), record);
  check(std::strstr(buf, "\n-1,,0,,0.000,0,0,0,0.000,0,-1,-1\n") != nullptr,
        "zero time prints 0.000");

  record.elapsed_s = -2;
  heapline::format_record(buf, sizeof(buf), record);
  check(std::strstr(buf, "\n-1,,0,,0.000,0,0,0,0.000,0,-1,-1\n") != nullptr,
        "negative time is clamped to 0.000");
}

void test_format_record_locale() {
  if (!std::setlocale(LC_NUMERIC, "fr_FR.UTF-8") &&
      !std::setlocale(LC_NUMERIC, "de_DE.UTF-8")) {
    std::fprintf(stderr,
                 "SKIP: no comma locale installed, locale check not run\n");
    return;
  }
  char probe[16];
  std::snprintf(probe, sizeof(probe), "%.1f", 1.5);
  check(std::strchr(probe, ',') != nullptr, "the comma locale is active");

  heapline::Record record;
  record.elapsed_s        = 12.5;
  record.peak_heap_time_s = 4.25;
  char buf[1024];
  heapline::format_record(buf, sizeof(buf), record);
  check(std::strstr(buf, "\n-1,,0,,12.500,0,0,0,4.250,0,-1,-1\n") != nullptr,
        "times keep the dot under a comma locale");
  std::setlocale(LC_NUMERIC, "C");
}

}  // namespace

int main() {
  test_find_rank();
  test_expand_path();
  test_status();
  test_format_record();
  test_format_record_times();
  test_format_record_locale();
  return failures == 0 ? 0 : 1;
}
