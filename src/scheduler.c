/* scheduler.c - FR1 (job management) and FR2 (CPU scheduling)
 *
 * Policies implemented: FCFS, Round Robin, non-preemptive Priority.
 * Every number printed is computed by the simulation below (nothing hard-coded).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include "common.h"
#include "scheduler.h"

#define MAX_SEGS 2048

/* One block of the Gantt chart. id == 0 means the CPU was idle. */
typedef struct { int id; int start; int end; } Segment;
typedef struct { Segment seg[MAX_SEGS]; int count; } Timeline;

typedef struct {
    double avg_ct, avg_tat, avg_wt;
    int makespan;     /* time at which the last job finished */
    int dispatches;   /* number of times a job was given the CPU */
} Metrics;

typedef enum { ALGO_FCFS, ALGO_RR, ALGO_PRIORITY } Algo;

static const char *STATUS_NAMES[] = { "NEW", "READY", "RUNNING", "DONE" };

/* ------------------------------------------------------------------ */
/* FR1: job loading and validation                                     */
/* ------------------------------------------------------------------ */

int load_jobs(const char *path, Job jobs[], int *n)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    char line[256];
    int lineno = 0;
    *n = 0;

    while (fgets(line, sizeof line, fp)) {
        lineno++;
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;           /* blank line or comment */

        int id, at, bt, pr, pos = 0;
        if (sscanf(p, "%d %d %d %d %n", &id, &at, &bt, &pr, &pos) != 4 ||
            (p[pos] != '\0' && p[pos] != '#')) {
            fprintf(stderr, "Error: %s line %d: malformed job (expected: id arrival burst priority)\n",
                    path, lineno);
            fclose(fp);
            return -1;
        }
        if (id <= 0)  { fprintf(stderr, "Error: %s line %d: job ID must be positive\n", path, lineno); fclose(fp); return -1; }
        if (at < 0)   { fprintf(stderr, "Error: %s line %d: arrival time cannot be negative\n", path, lineno); fclose(fp); return -1; }
        if (bt <= 0)  { fprintf(stderr, "Error: %s line %d: CPU burst must be greater than 0\n", path, lineno); fclose(fp); return -1; }
        if (pr < 0)   { fprintf(stderr, "Error: %s line %d: priority cannot be negative\n", path, lineno); fclose(fp); return -1; }
        for (int i = 0; i < *n; i++) {
            if (jobs[i].id == id) {
                fprintf(stderr, "Error: %s line %d: duplicate job ID %d\n", path, lineno, id);
                fclose(fp);
                return -1;
            }
        }
        if (*n >= MAX_JOBS) {
            fprintf(stderr, "Error: too many jobs (maximum is %d)\n", MAX_JOBS);
            fclose(fp);
            return -1;
        }

        Job *j = &jobs[(*n)++];
        j->id = id; j->arrival = at; j->burst = bt; j->priority = pr;
        j->remaining = bt; j->completion = j->turnaround = j->waiting = 0;
        j->status = JOB_NEW;
    }
    fclose(fp);

    if (*n == 0) {
        fprintf(stderr, "Error: '%s' contains no jobs\n", path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void reset_jobs(Job j[], int n)
{
    for (int i = 0; i < n; i++) {
        j[i].remaining = j[i].burst;
        j[i].completion = j[i].turnaround = j[i].waiting = 0;
        j[i].status = JOB_NEW;
    }
}

static void add_segment(Timeline *t, int id, int start, int end)
{
    if (t->count > 0) {                       /* merge with previous block if contiguous */
        Segment *last = &t->seg[t->count - 1];
        if (last->id == id && last->end == start) { last->end = end; return; }
    }
    if (t->count < MAX_SEGS) {
        t->seg[t->count++] = (Segment){ id, start, end };
    }
}

static void finish_job(Job *job, int time)
{
    job->completion = time;
    job->turnaround = time - job->arrival;
    job->waiting    = job->turnaround - job->burst;
    job->remaining  = 0;
    job->status     = JOB_DONE;
}

/* idx[] = job indices sorted by (arrival, id). Insertion sort: n is tiny. */
static void order_by_arrival(const Job j[], int n, int idx[])
{
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {
        int key = idx[i], k = i - 1;
        while (k >= 0 && (j[idx[k]].arrival > j[key].arrival ||
              (j[idx[k]].arrival == j[key].arrival && j[idx[k]].id > j[key].id))) {
            idx[k + 1] = idx[k];
            k--;
        }
        idx[k + 1] = key;
    }
}

/* Simple circular queue of job indices for Round Robin. */
typedef struct { int a[MAX_JOBS + 1]; int head, size; } Queue;
static void q_push(Queue *q, int v) { q->a[(q->head + q->size) % (MAX_JOBS + 1)] = v; q->size++; }
static int  q_pop(Queue *q)         { int v = q->a[q->head]; q->head = (q->head + 1) % (MAX_JOBS + 1); q->size--; return v; }

/* ------------------------------------------------------------------ */
/* The three scheduling simulations                                    */
/* ------------------------------------------------------------------ */

/* FCFS: run jobs in order of arrival (tie: smaller job ID). */
static void sim_fcfs(Job j[], int n, Timeline *t)
{
    int idx[MAX_JOBS];
    order_by_arrival(j, n, idx);
    int time = 0;
    for (int k = 0; k < n; k++) {
        Job *job = &j[idx[k]];
        if (time < job->arrival) {                    /* CPU idle until the job arrives */
            add_segment(t, 0, time, job->arrival);
            time = job->arrival;
        }
        job->status = JOB_RUNNING;
        add_segment(t, job->id, time, time + job->burst);
        time += job->burst;
        finish_job(job, time);
    }
}

/* Returns 1 if job a should run before job b under the priority policy. */
static int higher_priority(const Job *a, const Job *b)
{
    if (a->priority != b->priority) return a->priority < b->priority;   /* lower number wins */
    if (a->arrival  != b->arrival)  return a->arrival  < b->arrival;    /* tie 1: earlier arrival */
    return a->id < b->id;                                               /* tie 2: smaller ID */
}

/* Non-preemptive Priority: when the CPU is free, pick the best job that has already arrived. */
static void sim_priority(Job j[], int n, Timeline *t)
{
    int time = 0, done = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (j[i].status != JOB_DONE && j[i].arrival <= time) {
                j[i].status = JOB_READY;
                if (best < 0 || higher_priority(&j[i], &j[best])) best = i;
            }
        }
        if (best < 0) {                               /* nobody has arrived: idle until next arrival */
            int next = INT_MAX;
            for (int i = 0; i < n; i++)
                if (j[i].status != JOB_DONE && j[i].arrival < next) next = j[i].arrival;
            add_segment(t, 0, time, next);
            time = next;
            continue;
        }
        j[best].status = JOB_RUNNING;
        add_segment(t, j[best].id, time, time + j[best].burst);
        time += j[best].burst;
        finish_job(&j[best], time);
        done++;
    }
}

static void admit_arrivals(Job j[], const int order[], int n, int *next, int time, Queue *q)
{
    while (*next < n && j[order[*next]].arrival <= time) {
        j[order[*next]].status = JOB_READY;
        q_push(q, order[*next]);
        (*next)++;
    }
}

/* Round Robin. Rule: jobs that arrive during a time slice join the queue BEFORE the
 * job that was just preempted. Equal arrival times are ordered by smaller job ID. */
static void sim_rr(Job j[], int n, int quantum, Timeline *t)
{
    int order[MAX_JOBS];
    order_by_arrival(j, n, order);
    Queue q = { .head = 0, .size = 0 };
    int next = 0, done = 0, time = 0;

    while (done < n) {
        admit_arrivals(j, order, n, &next, time, &q);
        if (q.size == 0) {                            /* ready queue empty: idle until next arrival */
            int na = j[order[next]].arrival;
            add_segment(t, 0, time, na);
            time = na;
            continue;
        }
        int i = q_pop(&q);
        Job *job = &j[i];
        int slice = job->remaining < quantum ? job->remaining : quantum;
        job->status = JOB_RUNNING;
        add_segment(t, job->id, time, time + slice);
        time += slice;
        job->remaining -= slice;
        admit_arrivals(j, order, n, &next, time, &q);
        if (job->remaining > 0) { job->status = JOB_READY; q_push(&q, i); }
        else                    { finish_job(job, time); done++; }
    }
}

static void simulate(Algo a, Job j[], int n, int quantum, Timeline *t)
{
    reset_jobs(j, n);
    t->count = 0;
    switch (a) {
        case ALGO_FCFS:     sim_fcfs(j, n, t);          break;
        case ALGO_RR:       sim_rr(j, n, quantum, t);   break;
        case ALGO_PRIORITY: sim_priority(j, n, t);      break;
    }
}

/* ------------------------------------------------------------------ */
/* Metrics and printing                                                */
/* ------------------------------------------------------------------ */

static Metrics compute_metrics(const Job j[], int n, const Timeline *t)
{
    Metrics m = {0};
    for (int i = 0; i < n; i++) {
        m.avg_ct  += j[i].completion;
        m.avg_tat += j[i].turnaround;
        m.avg_wt  += j[i].waiting;
        if (j[i].completion > m.makespan) m.makespan = j[i].completion;
    }
    m.avg_ct /= n; m.avg_tat /= n; m.avg_wt /= n;
    for (int k = 0; k < t->count; k++) if (t->seg[k].id != 0) m.dispatches++;
    return m;
}

static void print_policy_info(Algo a, int quantum)
{
    switch (a) {
    case ALGO_FCFS:
        printf("Policy : First-Come First-Served (non-preemptive)\n");
        printf("Tie-break: equal arrival time -> smaller job ID first\n");
        break;
    case ALGO_RR:
        printf("Policy : Round Robin (preemptive)   Time quantum = %d\n", quantum);
        printf("Tie-break: jobs arriving during a slice enter the queue before the preempted job; "
               "equal arrival -> smaller job ID\n");
        break;
    case ALGO_PRIORITY:
        printf("Policy : Priority (non-preemptive)  lower number = higher priority\n");
        printf("Tie-break: equal priority -> earlier arrival, then smaller job ID\n");
        break;
    }
}

static void print_timeline(const Timeline *t)
{
    printf("\nExecution order / Gantt timeline:\n  ");
    for (int k = 0; k < t->count; k++) {
        if (t->seg[k].id == 0) printf("| IDLE %d-%d ", t->seg[k].start, t->seg[k].end);
        else                   printf("| J%d %d-%d ", t->seg[k].id, t->seg[k].start, t->seg[k].end);
    }
    printf("|\n");
}

static void print_input_table(const Job j[], int n)
{
    printf("Loaded %d jobs:\n", n);
    printf("  %-4s %-8s %-6s %-9s %-8s\n", "ID", "Arrival", "Burst", "Priority", "Status");
    for (int i = 0; i < n; i++)
        printf("  %-4d %-8d %-6d %-9d %-8s\n", j[i].id, j[i].arrival, j[i].burst,
               j[i].priority, STATUS_NAMES[j[i].status]);
}

static void print_result_table(const Job j[], int n, const Metrics *m)
{
    printf("\n  %-4s %-8s %-6s %-9s %-5s %-5s %-5s %-8s\n",
           "ID", "Arrival", "Burst", "Priority", "CT", "TAT", "WT", "Status");
    for (int i = 0; i < n; i++)
        printf("  %-4d %-8d %-6d %-9d %-5d %-5d %-5d %-8s\n",
               j[i].id, j[i].arrival, j[i].burst, j[i].priority,
               j[i].completion, j[i].turnaround, j[i].waiting, STATUS_NAMES[j[i].status]);
    printf("\n  Average CT = %.2f   Average TAT = %.2f   Average WT = %.2f\n",
           m->avg_ct, m->avg_tat, m->avg_wt);
}

static const char *algo_name(Algo a)
{
    return a == ALGO_FCFS ? "FCFS" : a == ALGO_RR ? "Round Robin" : "Priority";
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */

int run_scheduler(const char *algo, const char *file, int quantum)
{
    Job base[MAX_JOBS];
    int n;
    if (quantum <= 0) {
        fprintf(stderr, "Error: time quantum must be a positive integer\n");
        return -1;
    }
    if (load_jobs(file, base, &n) != 0) return -1;

    Algo which;
    int compare = 0;
    if      (strcmp(algo, "fcfs")     == 0) which = ALGO_FCFS;
    else if (strcmp(algo, "rr")       == 0) which = ALGO_RR;
    else if (strcmp(algo, "priority") == 0) which = ALGO_PRIORITY;
    else if (strcmp(algo, "compare")  == 0) { compare = 1; which = ALGO_FCFS; }
    else {
        fprintf(stderr, "Error: unknown scheduling algorithm '%s' (use fcfs, rr, priority or compare)\n", algo);
        return -1;
    }

    Timeline t;
    Job work[MAX_JOBS];

    if (!compare) {
        memcpy(work, base, sizeof(Job) * n);
        printf("=== CPU Scheduling: %s ===\n", algo_name(which));
        print_input_table(work, n);
        printf("\n");
        print_policy_info(which, quantum);
        simulate(which, work, n, quantum, &t);
        Metrics m = compute_metrics(work, n, &t);
        print_timeline(&t);
        print_result_table(work, n, &m);
        return 0;
    }

    /* compare: same job set under all three policies */
    printf("=== Scheduling Comparison (same job set) ===\n");
    print_input_table(base, n);
    Metrics results[3];
    const Algo all[3] = { ALGO_FCFS, ALGO_RR, ALGO_PRIORITY };
    for (int k = 0; k < 3; k++) {
        memcpy(work, base, sizeof(Job) * n);
        simulate(all[k], work, n, quantum, &t);
        results[k] = compute_metrics(work, n, &t);
        printf("\n--- %s ---\n", algo_name(all[k]));
        print_policy_info(all[k], quantum);
        print_timeline(&t);
    }

    printf("\nComparison summary:\n");
    printf("  %-12s %-8s %-8s %-8s %-10s %-10s\n",
           "Policy", "Avg CT", "Avg TAT", "Avg WT", "Dispatches", "Makespan");
    int best_wt = 0, best_tat = 0;
    for (int k = 0; k < 3; k++) {
        printf("  %-12s %-8.2f %-8.2f %-8.2f %-10d %-10d\n", algo_name(all[k]),
               results[k].avg_ct, results[k].avg_tat, results[k].avg_wt,
               results[k].dispatches, results[k].makespan);
        if (results[k].avg_wt  < results[best_wt].avg_wt)   best_wt  = k;
        if (results[k].avg_tat < results[best_tat].avg_tat) best_tat = k;
    }
    printf("\n  Lowest average waiting time    : %s\n", algo_name(all[best_wt]));
    printf("  Lowest average turnaround time : %s\n", algo_name(all[best_tat]));
    return 0;
}
