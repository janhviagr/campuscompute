/* common.h - shared types and constants for CampusCompute */
#ifndef COMMON_H
#define COMMON_H

#define MAX_JOBS    50   /* maximum jobs accepted from a job file   */
#define RR_QUANTUM   4   /* default Round Robin time quantum        */

typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_DONE } JobStatus;

typedef struct {
    int id;          /* unique positive job ID                      */
    int arrival;     /* arrival time (>= 0)                         */
    int burst;       /* CPU burst time (> 0)                        */
    int priority;    /* LOWER number = HIGHER priority (>= 0)       */
    int remaining;   /* burst left (used by Round Robin)            */
    int completion;  /* CT  */
    int turnaround;  /* TAT = CT - arrival */
    int waiting;     /* WT  = TAT - burst  */
    JobStatus status;
} Job;

#endif
