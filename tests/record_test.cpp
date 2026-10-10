// SPDX-License-Identifier: Apache-2.0

#include "record.h"

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
  const char* invalid[]  = {"SLURM_PROCID", "x", "PMI_RANK", "2", nullptr};
  const char* empty[]    = {"SLURM_PROCID", "", nullptr};
  const char* negative[] = {"SLURM_PROCID", "-1", nullptr};
  check(rank_with(none) == -1, "no variable gives -1");
  check(rank_with(slurm) == 3, "SLURM_PROCID");
  check(rank_with(ompi) == 5, "OMPI_COMM_WORLD_RANK");
  check(rank_with(pmi) == 7, "PMI_RANK");
  check(rank_with(pmix) == 9, "PMIX_RANK");
  check(rank_with(mv2) == 11, "MV2_COMM_WORLD_RANK");
  check(rank_with(both) == 3, "SLURM_PROCID wins over OMPI_COMM_WORLD_RANK");
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

}  // namespace

int main() {
  test_find_rank();
  test_expand_path();
  return failures == 0 ? 0 : 1;
}
