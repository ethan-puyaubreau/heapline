// SPDX-License-Identifier: Apache-2.0

/**
 * @file mpi_imbalance.cpp
 * @brief MPI program whose ranks use different amounts of heap, to show what
 * heapline-report makes of an imbalanced job.
 *
 * Rank r allocates (r + 1) * 64 MiB, writes every byte so the memory becomes
 * resident, waits for the other ranks and releases it.
 */

#include <mpi.h>

#include <cstddef>
#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
  MPI_Init(&argc, &argv);
  int rank = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  const size_t bytes = static_cast<size_t>(rank + 1) * 64 * 1024 * 1024;
  {
    std::vector<char> block(bytes);  // Zero-filled, so every page is touched.
    MPI_Barrier(MPI_COMM_WORLD);
    std::printf("rank %d holds %zu MiB\n", rank, block.size() >> 20);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
  return 0;
}
