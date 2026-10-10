# Measuring an MPI job

## With srun

Prepare the output directory first: heapline does not create it.

```
#!/bin/bash
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=8

out=$SCRATCH/heapline/$SLURM_JOB_ID
mkdir -p "$out"
srun env LD_PRELOAD=$HOME/opt/heapline/lib/libheapline.so \
         HEAPLINE_OUTPUT=$out/heapline.%h.%r.csv ./app
$HOME/opt/heapline/bin/heapline-report "$out"
```

Pass `LD_PRELOAD` through `env` on the `srun` line rather than exporting it
in the script: exported, it would also preload heapline into `srun` and
every other command of the script.

The rank comes from `SLURM_PROCID`, set by `srun` for each task.

## With mpirun

```
mpirun -n 16 env LD_PRELOAD=$PWD/libheapline.so HEAPLINE_OUTPUT=$PWD/records/heapline.%h.%r.csv ./app
```

Open MPI sets `OMPI_COMM_WORLD_RANK`, MPICH and Intel MPI `PMI_RANK`,
MVAPICH `MV2_COMM_WORLD_RANK`; heapline reads whichever is present.

## Sizing --mem

`heapline-report` prints, per node, the sum of `VmHWM` of its ranks. Compare
the largest one with the `--mem` request, which applies per node, and keep a
margin for the page cache and the system. With `--mem-per-cpu`, compare the
largest `VmHWM` of a rank with the request times the CPUs of a task instead.
