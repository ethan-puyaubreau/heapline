// SPDX-License-Identifier: Apache-2.0

/**
 * @file fork_child.cpp
 * @brief Check that a forked child can start a thread and read the counters
 * while the parent has other threads alive at the time of the fork.
 */

#include "heapline.h"

#include <dlfcn.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace {

using GetStatsFn = void (*)(heapline_stats*);

constexpr int kParentThreads     = 4;
constexpr int kPairs             = 100;
constexpr unsigned kAlarmSeconds = 5;

pthread_barrier_t ready;
pthread_barrier_t release;

void churn(int pairs) {
  for (int i = 0; i < pairs; ++i) std::free(std::malloc(100));
}

/**
 * @brief Allocate a few blocks, then stay alive across the fork.
 */
void* parked(void*) {
  churn(10);
  pthread_barrier_wait(&ready);
  pthread_barrier_wait(&release);
  return nullptr;
}

void* child_worker(void*) {
  churn(kPairs);
  return nullptr;
}

/**
 * @brief Body of the child, where only the forking thread exists. A hang
 * ends with SIGALRM. @p parent holds the counters of the parent just before
 * the fork, which the child inherits.
 */
int run_child(GetStatsFn get_stats, const heapline_stats& parent) {
  alarm(kAlarmSeconds);
  heapline_stats before{};
  heapline_stats after{};
  get_stats(&before);
  if (before.allocs < parent.allocs || before.frees < parent.frees) {
    std::fprintf(stderr, "FAIL: child keeps the counts of its parent\n");
    return 1;
  }
  pthread_t thread;
  if (pthread_create(&thread, nullptr, child_worker, nullptr) != 0) {
    std::fprintf(stderr, "child: pthread_create failed\n");
    return 1;
  }
  pthread_join(thread, nullptr);
  get_stats(&after);
  if (after.allocs - before.allocs < kPairs ||
      after.frees - before.frees < kPairs) {
    std::fprintf(stderr, "FAIL: child counts its thread\n");
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  auto get_stats =
      reinterpret_cast<GetStatsFn>(dlsym(RTLD_DEFAULT, "heapline_get_stats"));
  if (!get_stats) {
    std::fprintf(stderr, "heapline is not preloaded\n");
    return 1;
  }
  pthread_barrier_init(&ready, nullptr, kParentThreads + 1);
  pthread_barrier_init(&release, nullptr, kParentThreads + 1);

  pthread_t threads[kParentThreads];
  for (auto& thread : threads) {
    if (pthread_create(&thread, nullptr, parked, nullptr) != 0) {
      std::fprintf(stderr, "pthread_create failed\n");
      return 1;
    }
  }
  pthread_barrier_wait(&ready);

  heapline_stats at_fork{};
  get_stats(&at_fork);
  const pid_t pid = fork();
  if (pid < 0) {
    std::perror("fork");
    return 1;
  }
  // exit rather than _exit, so that the library destructor runs as well.
  if (pid == 0) std::exit(run_child(get_stats, at_fork));

  int status         = 0;
  const pid_t waited = waitpid(pid, &status, 0);
  pthread_barrier_wait(&release);
  for (auto& thread : threads) pthread_join(thread, nullptr);

  if (waited != pid) {
    std::fprintf(stderr, "FAIL: waitpid\n");
    return 1;
  }
  if (WIFSIGNALED(status)) {
    std::fprintf(stderr, "FAIL: child killed by signal %d%s\n",
                 WTERMSIG(status),
                 WTERMSIG(status) == SIGALRM ? " (hang)" : "");
    return 1;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    std::fprintf(stderr, "FAIL: child exit status\n");
    return 1;
  }
  return 0;
}
