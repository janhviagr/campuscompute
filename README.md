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
    ./campuscompute limit [files|cpu|mem|all]
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
- **Logger** (`logger.c`): producer threads put messages into a circular buffer of capacity 4 and one consumer thread writes them to `campus.log`. A mutex protects the queue, and the condition variables `not_full` and `not_empty` coordinate the threads. All threads are joined at the end.
- **IPC** (`ipc.c`): a writer and a reader process exchange a job list through POSIX shared memory (`shm_open`, `mmap`), synchronised with two named semaphores. The object names include the username (for example `/campus_shm_janhvi`, `/campus_ready_janhvi`, `/campus_done_janhvi`) and are released with `munmap`, `shm_unlink`, `sem_close` and `sem_unlink`.
- **Worker** (`worker.c`): the parent creates workers with `fork()` and collects their status with `waitpid()`. `worker fail` runs controlled SIMULATED failures (exit code 3, `abort()`, `SIGSEGV`); they are not real OS failures.
- **Resource limits** (`rlimit_demo.c`): `limit files|cpu|mem|all` applies `setrlimit()` (`RLIMIT_NOFILE`, `RLIMIT_CPU`, `RLIMIT_AS`) inside child processes, so the parent is unaffected.

## Cleanup
- `make clean` removes build output and `campus.log`
- Verify nothing is left behind: `ps aux | grep '[c]ampuscompute'` and `ls /dev/shm | grep campus` should both print nothing
- The program removes only the IPC objects it creates (names starting with `/campus_`)
