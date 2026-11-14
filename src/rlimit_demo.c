#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "rlimit_demo.h"

#define EXTRA_FILES 3
#define MEM_LIMIT   (64UL * 1024 * 1024)
#define MEM_CHUNK   (8UL * 1024 * 1024)

static const char *errno_name(int e)
{
    switch (e) {
    case EMFILE: return "EMFILE";
    case ENFILE: return "ENFILE";
    case ENOMEM: return "ENOMEM";
    default:     return "other";
    }
}

static void no_core_files(void)
{
    struct rlimit rl = { 0, 0 };
    setrlimit(RLIMIT_CORE, &rl);
}

static int child_files(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("getrlimit");
        return 1;
    }
    printf("  [child %d] RLIMIT_NOFILE before: soft=%llu hard=%llu\n", getpid(),
           (unsigned long long)rl.rlim_cur, (unsigned long long)rl.rlim_max);

    int in_use = 0;
    for (int fd = 0; fd < 256; fd++)
        if (fcntl(fd, F_GETFD) != -1)
            in_use++;

    rl.rlim_cur = (rlim_t)(in_use + EXTRA_FILES);
    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("setrlimit");
        return 1;
    }
    printf("  [child] setrlimit(RLIMIT_NOFILE) soft=%d  (%d fds already open + %d new files)\n",
           in_use + EXTRA_FILES, in_use, EXTRA_FILES);

    FILE *files[EXTRA_FILES + 2];
    char names[EXTRA_FILES + 2][64];
    int opened = 0, failed_errno = 0;

    for (int i = 1; i <= EXTRA_FILES + 1; i++) {
        snprintf(names[opened], sizeof names[opened], "/tmp/campus_limit_%d_%d.txt", getpid(), i);
        FILE *fp = fopen(names[opened], "w");
        if (!fp) {
            failed_errno = errno;
            printf("  [child] fopen #%d FAILED: errno=%d %s (%s)\n", i, failed_errno,
                   errno_name(failed_errno), strerror(failed_errno));
            break;
        }
        printf("  [child] fopen #%d ok\n", i);
        files[opened++] = fp;
    }
    for (int i = 0; i < opened; i++) {
        fclose(files[i]);
        unlink(names[i]);
    }

    if (opened == EXTRA_FILES && failed_errno == EMFILE) {
        printf("  [child] Result: limit allowed %d files, the %dth fopen failed with EMFILE\n",
               EXTRA_FILES, EXTRA_FILES + 1);
        return 0;
    }
    printf("  [child] Result: unexpected (opened %d)\n", opened);
    return 1;
}

static int child_cpu(void)
{
    no_core_files();
    struct rlimit rl = { 1, 2 };
    if (setrlimit(RLIMIT_CPU, &rl) < 0) {
        perror("setrlimit");
        return 1;
    }
    printf("  [child %d] setrlimit(RLIMIT_CPU) soft=1s hard=2s, now spinning...\n", getpid());
    fflush(stdout);
    volatile unsigned long x = 0;
    for (;;)
        x++;
    return 1;
}

static int child_mem(void)
{
    struct rlimit rl = { MEM_LIMIT, MEM_LIMIT };
    if (setrlimit(RLIMIT_AS, &rl) < 0) {
        perror("setrlimit");
        return 1;
    }
    printf("  [child %d] setrlimit(RLIMIT_AS) = %lu MB, allocating %lu MB chunks\n",
           getpid(), MEM_LIMIT >> 20, MEM_CHUNK >> 20);

    void *chunks[64];
    int n = 0;
    while (n < 64) {
        errno = 0;
        void *p = malloc(MEM_CHUNK);
        if (!p) {
            int e = errno;
            printf("  [child] malloc #%d FAILED: errno=%d %s (%s)\n", n + 1, e, errno_name(e),
                   strerror(e));
            break;
        }
        memset(p, 1, MEM_CHUNK);
        chunks[n++] = p;
        printf("  [child] malloc #%d ok (%lu MB total)\n", n, (n * MEM_CHUNK) >> 20);
    }
    for (int i = 0; i < n; i++)
        free(chunks[i]);

    printf("  [child] Result: allocation stopped at %lu MB because of the %lu MB limit\n",
           (n * MEM_CHUNK) >> 20, MEM_LIMIT >> 20);
    return n < 64 ? 0 : 1;
}

static int run_in_child(const char *title, int (*fn)(void))
{
    printf("\n--- %s ---\n", title);
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: fork: %s\n", strerror(errno));
        return 1;
    }
    if (pid == 0)
        exit(fn());

    int status;
    pid_t r;
    do
        r = waitpid(pid, &status, 0);
    while (r < 0 && errno == EINTR);
    if (r < 0) {
        fprintf(stderr, "error: waitpid: %s\n", strerror(errno));
        return 1;
    }

    if (WIFEXITED(status)) {
        printf("[parent] child %d exited with status %d\n", pid, WEXITSTATUS(status));
        return WEXITSTATUS(status) == 0 ? 0 : 1;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("[parent] child %d was killed by signal %d (%s)%s\n", pid, sig, strsignal(sig),
               sig == SIGXCPU ? " -> CPU limit enforced by the kernel" : "");
        return sig == SIGXCPU ? 0 : 1;
    }
    return 1;
}

int run_limit(const char *which)
{
    int do_files = 0, do_cpu = 0, do_mem = 0;
    if (strcmp(which, "files") == 0)
        do_files = 1;
    else if (strcmp(which, "cpu") == 0)
        do_cpu = 1;
    else if (strcmp(which, "mem") == 0)
        do_mem = 1;
    else if (strcmp(which, "all") == 0)
        do_files = do_cpu = do_mem = 1;
    else {
        fprintf(stderr, "error: limit must be files, cpu, mem or all\n");
        return 2;
    }

    struct rlimit before;
    getrlimit(RLIMIT_NOFILE, &before);

    printf("=== Resource limits with setrlimit() (each test in its own child) ===\n");
    int fails = 0;
    if (do_files)
        fails += run_in_child("RLIMIT_NOFILE: open-file limit", child_files);
    if (do_cpu)
        fails += run_in_child("RLIMIT_CPU: CPU-time limit", child_cpu);
    if (do_mem)
        fails += run_in_child("RLIMIT_AS: address-space (memory) limit", child_mem);

    struct rlimit after;
    getrlimit(RLIMIT_NOFILE, &after);
    printf("\n[parent] own RLIMIT_NOFILE soft limit: %llu before, %llu after "
           "-> parent unaffected\n",
           (unsigned long long)before.rlim_cur, (unsigned long long)after.rlim_cur);
    printf("[parent] all children reaped - no zombies.\n");
    return fails ? 1 : 0;
}
