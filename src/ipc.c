#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ipc.h"

#define SHM_MAX_JOBS 16

typedef struct {
    int njobs;
    int ids[SHM_MAX_JOBS];
    int bursts[SHM_MAX_JOBS];
    char note[96];
    int total_burst;
    int longest_id;
    char reply[96];
} shm_block_t;

static void make_name(char *out, size_t len, const char *prefix)
{
    const char *user = getenv("USER");
    if (!user || !*user)
        user = "user";
    snprintf(out, len, "/%s_%s", prefix, user);
}

static int sem_wait_retry(sem_t *s)
{
    int r;
    do
        r = sem_wait(s);
    while (r < 0 && errno == EINTR);
    return r;
}

static sem_t *open_new_sem(const char *name, int *created)
{
    sem_t *s = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
    if (s == SEM_FAILED && errno == EEXIST) {
        printf("note: stale semaphore %s left by an earlier run of this program - removing it\n", name);
        sem_unlink(name);
        s = sem_open(name, O_CREAT | O_EXCL, 0600, 0);
    }
    if (s == SEM_FAILED)
        fprintf(stderr, "error: sem_open(%s): %s\n", name, strerror(errno));
    else
        *created = 1;
    return s;
}

static void reader_process(shm_block_t *blk, sem_t *ready, sem_t *done)
{
    printf("[reader pid %d] waiting on 'ready' semaphore...\n", getpid());
    if (sem_wait_retry(ready) < 0) {
        fprintf(stderr, "[reader] sem_wait failed: %s\n", strerror(errno));
        exit(1);
    }

    printf("[reader pid %d] semaphore posted, reading shared memory:\n", getpid());
    printf("[reader] note from writer: \"%s\"\n", blk->note);
    int total = 0, longest = -1, longest_burst = -1;
    for (int i = 0; i < blk->njobs && i < SHM_MAX_JOBS; i++) {
        printf("[reader]   job J%d burst %d\n", blk->ids[i], blk->bursts[i]);
        total += blk->bursts[i];
        if (blk->bursts[i] > longest_burst) {
            longest_burst = blk->bursts[i];
            longest = blk->ids[i];
        }
    }
    blk->total_burst = total;
    blk->longest_id = longest;
    snprintf(blk->reply, sizeof blk->reply, "reader pid %d processed %d jobs", getpid(), blk->njobs);

    if (sem_post(done) < 0) {
        fprintf(stderr, "[reader] sem_post failed: %s\n", strerror(errno));
        exit(1);
    }
    printf("[reader] reply written, posted 'done' semaphore\n");
    exit(0);
}

int run_ipc(void)
{
    char shm_name[64], ready_name[64], done_name[64];
    make_name(shm_name, sizeof shm_name, "campus_shm");
    make_name(ready_name, sizeof ready_name, "campus_ready");
    make_name(done_name, sizeof done_name, "campus_done");

    int rc = 1, fd = -1;
    int shm_created = 0, ready_created = 0, done_created = 0;
    shm_block_t *blk = MAP_FAILED;
    sem_t *ready = SEM_FAILED, *done = SEM_FAILED;
    pid_t child = -1;

    printf("=== Shared-memory IPC (shm_open + mmap + named semaphores) ===\n");

    fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno == EEXIST) {
        printf("note: stale %s left by an earlier run of this program - removing it\n", shm_name);
        shm_unlink(shm_name);
        fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    }
    if (fd < 0) {
        fprintf(stderr, "error: shm_open(%s): %s\n", shm_name, strerror(errno));
        goto cleanup;
    }
    shm_created = 1;

    if (ftruncate(fd, sizeof(shm_block_t)) < 0) {
        fprintf(stderr, "error: ftruncate: %s\n", strerror(errno));
        goto cleanup;
    }
    blk = mmap(NULL, sizeof(shm_block_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (blk == MAP_FAILED) {
        fprintf(stderr, "error: mmap: %s\n", strerror(errno));
        goto cleanup;
    }
    memset(blk, 0, sizeof *blk);

    ready = open_new_sem(ready_name, &ready_created);
    if (ready == SEM_FAILED)
        goto cleanup;
    done = open_new_sem(done_name, &done_created);
    if (done == SEM_FAILED)
        goto cleanup;

    printf("created shared memory %s (%zu bytes) and semaphores %s, %s\n",
           shm_name, sizeof(shm_block_t), ready_name, done_name);

    fflush(stdout);
    child = fork();
    if (child < 0) {
        fprintf(stderr, "error: fork: %s\n", strerror(errno));
        goto cleanup;
    }
    if (child == 0)
        reader_process(blk, ready, done);

    usleep(300000);
    printf("[writer pid %d] writing job list into shared memory\n", getpid());
    const int ids[] = { 1, 2, 3, 4, 5 };
    const int bursts[] = { 5, 3, 8, 6, 2 };
    blk->njobs = 5;
    for (int i = 0; i < blk->njobs; i++) {
        blk->ids[i] = ids[i];
        blk->bursts[i] = bursts[i];
    }
    snprintf(blk->note, sizeof blk->note, "job list from writer pid %d", getpid());

    if (sem_post(ready) < 0) {
        fprintf(stderr, "error: sem_post: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("[writer] finished writing, posted 'ready' semaphore\n");

    if (sem_wait_retry(done) < 0) {
        fprintf(stderr, "error: sem_wait: %s\n", strerror(errno));
        goto cleanup;
    }
    printf("[writer] reply from reader: \"%s\", total burst = %d, longest job = J%d\n",
           blk->reply, blk->total_burst, blk->longest_id);

    int status;
    if (waitpid(child, &status, 0) < 0) {
        fprintf(stderr, "error: waitpid: %s\n", strerror(errno));
        goto cleanup;
    }
    child = -1;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("[writer] reader exited with status 0 (reaped, no zombie)\n");
        rc = 0;
    } else {
        printf("[writer] reader ended abnormally\n");
    }

cleanup:
    if (child > 0) {
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
    }
    if (blk != MAP_FAILED && munmap(blk, sizeof(shm_block_t)) < 0)
        fprintf(stderr, "warning: munmap: %s\n", strerror(errno));
    if (fd >= 0)
        close(fd);
    if (shm_created && shm_unlink(shm_name) < 0)
        fprintf(stderr, "warning: shm_unlink: %s\n", strerror(errno));
    if (ready != SEM_FAILED)
        sem_close(ready);
    if (done != SEM_FAILED)
        sem_close(done);
    if (ready_created && sem_unlink(ready_name) < 0)
        fprintf(stderr, "warning: sem_unlink(%s): %s\n", ready_name, strerror(errno));
    if (done_created && sem_unlink(done_name) < 0)
        fprintf(stderr, "warning: sem_unlink(%s): %s\n", done_name, strerror(errno));

    printf("cleanup: munmap, shm_unlink(%s), sem_close + sem_unlink(%s, %s) done\n",
           shm_name, ready_name, done_name);
    printf("check with: ls /dev/shm | grep campus   (should print nothing)\n");
    return rc;
}
