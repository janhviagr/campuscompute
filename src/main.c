/* main.c - FR8: command-line interface for CampusCompute */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common.h"
#include "scheduler.h"
#include "banker.h"
#include "modules.h"

#define DEFAULT_JOBS_FILE "jobs.txt"

static void print_usage(FILE *out)
{
    fprintf(out,
        "CampusCompute - intelligent job scheduling and resource management\n\n"
        "Usage: ./campuscompute <command> [arguments]\n\n"
        "Commands:\n"
        "  help                              show this message\n"
        "  sched fcfs     [jobs.txt]         FCFS scheduling\n"
        "  sched rr       [jobs.txt] [q]     Round Robin (quantum q, default %d)\n"
        "  sched priority [jobs.txt]         non-preemptive Priority scheduling\n"
        "  sched compare  [jobs.txt] [q]     all three policies on the same jobs\n"
        "  logger                            producer-consumer logger (threads)\n"
        "  ipc                               shared memory + semaphore demo\n"
        "  banker safe|unsafe                Banker's algorithm test cases\n"
        "  worker normal|fail                fork/waitpid (fail = SIMULATED failure)\n"
        "  limit                             setrlimit demonstration\n"
        "  demo [jobs.txt]                   run every feature and print a summary\n\n"
        "Job file format: one job per line: id arrival burst priority\n"
        "(lines starting with # are comments; lower priority number = higher priority)\n",
        RR_QUANTUM);
}

static int bad_usage(const char *msg)
{
    fprintf(stderr, "Error: %s\n\n", msg);
    print_usage(stderr);
    return 1;
}

/* Parse a positive integer; returns -1 if the text is not one. */
static int parse_positive(const char *s)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > 1000000) return -1;
    return (int)v;
}

static int run_demo(const char *jobs_file)
{
    struct { const char *name; int rc; } steps[8];
    int k = 0;

    printf("################ CampusCompute integrated demo ################\n\n");

    steps[k].name = "CPU scheduling comparison";
    steps[k++].rc = run_scheduler("compare", jobs_file, RR_QUANTUM);
    printf("\n");
    steps[k].name = "Producer-consumer logger";
    steps[k++].rc = run_logger();
    printf("\n");
    steps[k].name = "Shared-memory IPC";
    steps[k++].rc = run_ipc();
    printf("\n");
    steps[k].name = "Banker's algorithm (safe)";
    steps[k++].rc = run_banker(1);
    printf("\n");
    steps[k].name = "Banker's algorithm (unsafe)";
    steps[k++].rc = run_banker(0);
    printf("\n");
    steps[k].name = "Worker: normal completion";
    steps[k++].rc = run_worker(0);
    printf("\n");
    steps[k].name = "Worker: simulated failure";
    steps[k++].rc = run_worker(1);
    printf("\n");
    steps[k].name = "Resource limit";
    steps[k++].rc = run_limit();

    printf("\n================ DEMO SUMMARY ================\n");
    int failures = 0;
    for (int i = 0; i < k; i++) {
        printf("  [%s] %s\n", steps[i].rc == 0 ? " OK " : "FAIL", steps[i].name);
        if (steps[i].rc != 0) failures++;
    }
    printf("  %d of %d steps completed successfully\n", k - failures, k);
    return failures ? 1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) return bad_usage("no command given");

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(stdout);
        return 0;
    }

    if (strcmp(cmd, "sched") == 0) {
        if (argc < 3 || argc > 5) return bad_usage("sched needs: fcfs|rr|priority|compare [jobs.txt] [quantum]");
        const char *algo = argv[2];
        const char *file = argc >= 4 ? argv[3] : DEFAULT_JOBS_FILE;
        int quantum = RR_QUANTUM;
        if (argc == 5) {
            if (strcmp(algo, "rr") != 0 && strcmp(algo, "compare") != 0)
                return bad_usage("a quantum is only valid with 'sched rr' or 'sched compare'");
            quantum = parse_positive(argv[4]);
            if (quantum < 0) return bad_usage("quantum must be a positive integer");
        }
        return run_scheduler(algo, file, quantum) == 0 ? 0 : 1;
    }

    if (strcmp(cmd, "banker") == 0) {
        if (argc != 3) return bad_usage("banker needs: safe|unsafe");
        if (strcmp(argv[2], "safe") == 0)   return run_banker(1);
        if (strcmp(argv[2], "unsafe") == 0) return run_banker(0);
        return bad_usage("banker case must be 'safe' or 'unsafe'");
    }

    if (strcmp(cmd, "worker") == 0) {
        if (argc != 3) return bad_usage("worker needs: normal|fail");
        if (strcmp(argv[2], "normal") == 0) return run_worker(0) == 0 ? 0 : 1;
        if (strcmp(argv[2], "fail") == 0)   return run_worker(1) == 0 ? 0 : 1;
        return bad_usage("worker mode must be 'normal' or 'fail'");
    }

    if (strcmp(cmd, "logger") == 0) { if (argc != 2) return bad_usage("logger takes no arguments"); return run_logger() == 0 ? 0 : 1; }
    if (strcmp(cmd, "ipc") == 0)    { if (argc != 2) return bad_usage("ipc takes no arguments");    return run_ipc() == 0 ? 0 : 1; }
    if (strcmp(cmd, "limit") == 0)  { if (argc != 2) return bad_usage("limit takes no arguments");  return run_limit() == 0 ? 0 : 1; }

    if (strcmp(cmd, "demo") == 0) {
        if (argc > 3) return bad_usage("demo takes at most one argument (job file)");
        return run_demo(argc == 3 ? argv[2] : DEFAULT_JOBS_FILE);
    }

    char msg[128];
    snprintf(msg, sizeof msg, "unknown command '%.60s'", cmd);
    return bad_usage(msg);
}
