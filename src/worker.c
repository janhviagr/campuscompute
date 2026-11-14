#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "worker.h"

#define MAX_WORKERS 4

typedef enum { WK_OK, WK_EXIT3, WK_ABORT, WK_SEGV } worker_kind_t;

static void worker_body(int idx, worker_kind_t kind)
{
    struct rlimit no_core = { 0, 0 };
    setrlimit(RLIMIT_CORE, &no_core);

    volatile unsigned long sum = 0;
    for (unsigned long i = 0; i < 3000000UL; i++)
        sum += i;

    printf("  [worker %d, pid %d] ", idx, getpid());
    switch (kind) {
    case WK_OK:
        printf("finished its job normally -> exit(0)\n");
        fflush(stdout);
        exit(0);
    case WK_EXIT3:
        printf("SIMULATED failure -> exit(3)\n");
        fflush(stdout);
        exit(3);
    case WK_ABORT:
        printf("SIMULATED failure -> abort()\n");
        fflush(stdout);
        abort();
    case WK_SEGV:
        printf("SIMULATED failure -> raise(SIGSEGV)\n");
        fflush(stdout);
        raise(SIGSEGV);
        break;
    }
    _exit(99);
}

int run_worker(const char *mode)
{
    worker_kind_t kinds[MAX_WORKERS];
    int n;

    if (strcmp(mode, "normal") == 0) {
        n = 3;
        for (int i = 0; i < n; i++)
            kinds[i] = WK_OK;
    } else if (strcmp(mode, "fail") == 0) {
        n = 4;
        kinds[0] = WK_OK;
        kinds[1] = WK_EXIT3;
        kinds[2] = WK_ABORT;
        kinds[3] = WK_SEGV;
    } else {
        fprintf(stderr, "error: worker mode must be 'normal' or 'fail'\n");
        return 2;
    }

    printf("=== Worker failure detection (%s mode), parent pid %d ===\n", mode, getpid());
    if (n == 4)
        printf("NOTE: the failures below are deliberately SIMULATED, not real OS failures.\n");

    pid_t pids[MAX_WORKERS];
    int started = 0;
    for (int i = 0; i < n; i++) {
        fflush(stdout);
        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "error: fork: %s\n", strerror(errno));
            break;
        }
        if (pid == 0)
            worker_body(i + 1, kinds[i]);
        pids[i] = pid;
        started++;
        printf("[parent] started worker %d (pid %d)\n", i + 1, pid);
    }

    int ok = 0, failed = 0;
    for (int i = 0; i < started; i++) {
        int status;
        pid_t r;
        do
            r = waitpid(pids[i], &status, 0);
        while (r < 0 && errno == EINTR);
        if (r < 0) {
            fprintf(stderr, "error: waitpid(%d): %s\n", pids[i], strerror(errno));
            continue;
        }

        if (WIFEXITED(status)) {
            int code = WEXITSTATUS(status);
            if (code == 0) {
                printf("[parent] worker %d (pid %d): exited normally, status 0 -> OK\n", i + 1, r);
                ok++;
            } else {
                printf("[parent] worker %d (pid %d): exited with status %d "
                       "-> FAILURE DETECTED (SIMULATED)\n", i + 1, r, code);
                failed++;
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            printf("[parent] worker %d (pid %d): killed by signal %d (%s) "
                   "-> FAILURE DETECTED (SIMULATED)\n", i + 1, r, sig, strsignal(sig));
            failed++;
        }
    }

    printf("\nSummary: %d worker(s) OK, %d failed", ok, failed);
    if (failed)
        printf(" (all failures were SIMULATED on purpose)");
    printf(".\nAll %d children reaped with waitpid() - no zombies left.\n", started);
    return started == n ? 0 : 1;
}
