/* modules.h - Person B's entry points. main.c calls these. Each returns 0 on success. */
#ifndef MODULES_H
#define MODULES_H
int run_logger(void);        /* FR3: producer-consumer logger (pthreads)        */
int run_ipc(void);           /* FR4: shared memory + named semaphore            */
int run_worker(int fail);    /* FR6: fork/waitpid; fail=1 -> SIMULATED failure  */
int run_limit(void);         /* FR7: setrlimit demonstration                    */
#endif
