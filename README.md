# CampusCompute

Command-line prototype demonstrating Operating Systems concepts: CPU scheduling,
a threaded producer-consumer logger, POSIX shared-memory IPC, the Banker's safety
algorithm, worker-failure detection and resource limits.
Course: Operating Systems (UE25CS242A), Level 3 Mini-Project.

## Prerequisites
- Ubuntu or WSL Ubuntu
- `gcc` and `make`:  `sudo apt update && sudo apt install build-essential`

## Build
    make            # builds ./campuscompute
    make clean      # removes build/, the executable and campus.log

## Run
    ./campuscompute help
    ./campuscompute sched fcfs|rr|priority|compare [jobs.txt] [quantum]
    ./campuscompute logger [producers] [messages]
    ./campuscompute ipc
    ./campuscompute banker safe|unsafe
    ./campuscompute worker normal|fail
    ./campuscompute limit [which]
    ./campuscompute demo [jobs.txt]      # runs every feature and prints a summary

Each feature runs independently. Invalid commands or input print an error and the usage text.

## Job file format
One job per line: `id arrival burst priority`. Lines starting with `#` are comments.
Validation: positive unique ID, arrival >= 0, burst > 0, priority >= 0, at most 50 jobs.

## Scheduling rules
- Priority: **lower number = higher priority**
- FCFS tie-break: equal arrival -> smaller job ID
- Priority tie-break (non-preemptive): equal priority -> earlier arrival -> smaller job ID
- Round Robin: default time quantum **4** (override: `sched rr jobs.txt 3`);
  jobs arriving during a slice join the queue before the preempted job
- Metrics: CT = completion time, TAT = CT - arrival, WT = TAT - burst

## Banker's algorithm
Two built-in repeatable cases (`banker safe`, `banker unsafe`), 5 processes and 3 resource types.
Need = Maximum - Allocation; the safety check prints each step and the safe sequence (or the
processes that cannot finish).

## Testing invalid input
    ./campuscompute nonsense
    ./campuscompute sched fcfs tests/bad_negative_burst.txt
    ./campuscompute sched fcfs tests/bad_duplicate_id.txt
    ./campuscompute sched fcfs tests/bad_malformed.txt
    ./campuscompute sched fcfs tests/bad_empty.txt
    ./campuscompute sched rr jobs.txt 0

## Project layout
    Makefile  README.md  jobs.txt
    include/  common.h scheduler.h banker.h logger.h ipc.h worker.h rlimit_demo.h
    src/      main.c scheduler.c banker.c logger.c ipc.c worker.c rlimit_demo.c
    tests/    bad-input job files

## Logger, IPC, worker and resource-limit modules
<!-- Person B: describe design, IPC object names, accepted `limit` values -->

## Cleanup
- `make clean` removes build output and `campus.log`
- <!-- Person B: how to verify no leftover /dev/shm objects, semaphores or processes -->
