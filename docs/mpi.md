# Measuring an MPI job

## With srun

Install heapline once, from the build directory of the
[tutorial](tutorial.md):

```
cmake --install build --prefix $HOME/opt/heapline
```

The library goes to `$HOME/opt/heapline/lib`, or `lib64` on some
distributions, such as RHEL, Rocky or SUSE, and `heapline-report` to
`$HOME/opt/heapline/bin`.

Prepare the output directory first: heapline does not create it.

```
#!/bin/bash
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=8

out=$SCRATCH/heapline/$SLURM_JOB_ID
mkdir -p "$out"
# libheapline.so is in lib (or lib64): check with ls $HOME/opt/heapline
srun env LD_PRELOAD=$HOME/opt/heapline/lib/libheapline.so \
         HEAPLINE_OUTPUT=$out/heapline.%h.%r.csv ./app
$HOME/opt/heapline/bin/heapline-report "$out"
```

Pass `LD_PRELOAD` through `env` on the `srun` line rather than exporting it
in the script: exported, it would also preload heapline into `srun` and
every other command of the script.

The rank comes from `SLURM_PROCID`, set by `srun` for each task, unless the MPI
library sets one of its own variables (see the next section); a PMI variable
set by `srun` holds the same value.

## With mpirun

```
mkdir -p records
mpirun -n 16 env LD_PRELOAD=$PWD/build/libheapline.so HEAPLINE_OUTPUT=$PWD/records/heapline.%h.%r.csv ./app
```

With Open MPI on a machine with fewer than 16 cores, add `--oversubscribe`
after `mpirun` (MPICH has no such option). With an installed copy, use the
path of the installed library instead of `$PWD/build/libheapline.so`.

Open MPI sets `OMPI_COMM_WORLD_RANK`, MPICH and Intel MPI `PMI_RANK`,
MVAPICH `MV2_COMM_WORLD_RANK`, and PMIx-based launchers `PMIX_RANK`. heapline
reads `OMPI_COMM_WORLD_RANK`, `PMIX_RANK`, `PMI_RANK`, `MV2_COMM_WORLD_RANK`
and then `SLURM_PROCID`, in this order, and takes the first one that is set.
`SLURM_PROCID` comes last because the ranks started by `mpirun` inside a Slurm
allocation inherit the `SLURM_PROCID` of the batch script, which is 0 for all
of them and is not their rank.

## Sizing --mem

`heapline-report` prints, per node, the sum of `VmHWM` of its ranks. Compare
the largest one with the `--mem` request, which applies per node, and keep a
margin for the page cache and the system. With `--mem-per-cpu`, compare the
largest `VmHWM` of a rank with the request times the CPUs of a task instead.
