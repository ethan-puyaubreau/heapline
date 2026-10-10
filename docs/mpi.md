# Measuring an MPI job

## With srun

Install heapline once, from the source tree where you built it in the
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

`./app` must be the program itself. If it is a script that starts the real
program, read [Wrapper processes](#wrapper-processes) first: the script is
preloaded too, and its record can replace the program's.

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

Keep `env` directly in front of the program. A script or `/usr/bin/time`
between them would be preloaded too, and its record can replace the program's:
see [Wrapper processes](#wrapper-processes).

Open MPI sets `OMPI_COMM_WORLD_RANK`, MPICH and Intel MPI `PMI_RANK`,
MVAPICH `MV2_COMM_WORLD_RANK`, and PMIx-based launchers `PMIX_RANK`. heapline
reads `OMPI_COMM_WORLD_RANK`, `PMIX_RANK`, `PMI_RANK`, `MV2_COMM_WORLD_RANK`
and then `SLURM_PROCID`, in this order, and takes the first one that is set.
`SLURM_PROCID` comes last because the ranks started by `mpirun` inside a Slurm
allocation inherit the one of the batch step: 0 on the first node, and the
index of the node's daemon on the other nodes. Either way it is not their
rank.

## Wrapper processes

`LD_PRELOAD` reaches every process started by the preloaded command. A shell
script run without `exec`, `/usr/bin/time`, or any other parent of the program
is preloaded too. It has the same rank and host as the program and usually
exits last, so with `HEAPLINE_OUTPUT=heapline.%h.%r.csv` its record overwrites
the program's, and nothing warns you.

There are two ways to avoid it:

- Start the program with `exec` in wrapper scripts, and put `LD_PRELOAD` on the
  program itself rather than on the wrapper. The last line of the script
  becomes `exec env LD_PRELOAD=... HEAPLINE_OUTPUT=... ./app`.
- Or add `%p` to the file name, as in `heapline.%h.%r.%p.csv`. Every process
  then keeps its own record. `heapline-report` shows both, and its warning
  about duplicate ranks names the two files, the wrapper's among them.

## Sizing --mem

`heapline-report` prints, per node, the sum of `VmHWM` of its ranks. Compare
the largest one with the `--mem` request, which applies per node, and keep a
margin for the page cache and the system.

`--mem-per-cpu` is also enforced per node: the limit is the request times the
CPUs allocated on the node. Comparing it with the same per-node sum is
therefore the exact check. Comparing the largest `VmHWM` of a rank with the
request times the CPUs of a task is a stricter one.
