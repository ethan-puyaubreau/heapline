// SPDX-License-Identifier: Apache-2.0

/**
 * @file killed.cpp
 * @brief Test program that holds 64 MiB of heap and kills itself with
 * SIGKILL, as the OOM killer would, so that the library destructor never runs.
 */

#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>

int main() {
  constexpr std::size_t kSize = 64u << 20;
  void* ptr                   = std::malloc(kSize);
  if (!ptr) return 1;
  std::memset(ptr, 1, kSize);
  std::raise(SIGKILL);
  return 1;
}
