#ifndef SCHEDULER_H
#define SCHEDULER_H
#include "common.h"

/* FR1: read + validate jobs. Returns 0 on success, -1 on error (message printed). */
int load_jobs(const char *path, Job jobs[], int *n);

/* FR2: algo = "fcfs" | "rr" | "priority" | "compare". Returns 0 on success, -1 on error. */
int run_scheduler(const char *algo, const char *file, int quantum);

#endif
