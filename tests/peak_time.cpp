// SPDX-License-Identifier: Apache-2.0

/**
 * @file peak_time.cpp
 * @brief Test program whose heap peak, 16 MiB, is reached at a known time: at
 * once with "early", after one second with "late". A 1 MiB block is allocated
 * and released on the other side of a one second sleep.
 */

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

constexpr std::size_t kBig   = 16u << 20;
constexpr std::size_t kSmall = 1u << 20;

/**
 * @brief Allocate @p size bytes, touch them and release them.
 */
bool use(std::size_t size) {
  void* ptr = std::malloc(size);
  if (!ptr) return false;
  std::memset(ptr, 1, size);
  std::free(ptr);
  return true;
}

void wait_one_second() { std::this_thread::sleep_for(std::chrono::seconds(1)); }

}  // namespace

int main(int argc, char** argv) {
  bool early = argc == 2 && std::strcmp(argv[1], "early") == 0;
  bool late  = argc == 2 && std::strcmp(argv[1], "late") == 0;
  if (!early && !late) {
    std::fprintf(stderr, "usage: %s early|late\n", argv[0]);
    return 2;
  }
  bool ok = use(early ? kBig : kSmall);
  wait_one_second();
  ok = use(early ? kSmall : kBig) && ok;
  return ok ? 0 : 1;
}
