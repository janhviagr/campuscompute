#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"

#define QUEUE_CAP     4
#define MSG_LEN       96
#define MAX_PRODUCERS 8
#define MAX_MESSAGES  100

typedef struct {
    char buf[QUEUE_CAP][MSG_LEN];
    int head, tail, count;
    int done;
    pthread_mutex_t lock;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    long produced, consumed;
    long full_waits, empty_waits;
} log_queue_t;

typedef struct {
    int id;
    int nmsgs;
} producer_arg_t;

static log_queue_t q;

static void random_pause(unsigned *seed, int min_us, int spread_us)
{
    usleep((useconds_t)(min_us + (int)(rand_r(seed) % (unsigned)spread_us)));
}

static void *producer(void *arg)
{
    const producer_arg_t *a = arg;
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(a->id * 7919);

    for (int k = 1; k <= a->nmsgs; k++) {
        char msg[MSG_LEN];
        snprintf(msg, sizeof msg, "[producer %d] lab job event %d", a->id, k);

        pthread_mutex_lock(&q.lock);
        while (q.count == QUEUE_CAP) {
            q.full_waits++;
            printf("  P%d: queue FULL (%d/%d) -> waiting\n", a->id, q.count, QUEUE_CAP);
            pthread_cond_wait(&q.not_full, &q.lock);
        }
        memcpy(q.buf[q.tail], msg, MSG_LEN);
        q.tail = (q.tail + 1) % QUEUE_CAP;
        q.count++;
        q.produced++;
        printf("  P%d: put event %d        (queue %d/%d)\n", a->id, k, q.count, QUEUE_CAP);
        pthread_cond_signal(&q.not_empty);
        pthread_mutex_unlock(&q.lock);

        random_pause(&seed, 500, 2500);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    FILE *fp = arg;
    unsigned seed = (unsigned)time(NULL) ^ 0xC0FFEEu;

    for (;;) {
        pthread_mutex_lock(&q.lock);
        while (q.count == 0 && !q.done) {
            q.empty_waits++;
            pthread_cond_wait(&q.not_empty, &q.lock);
        }
        if (q.count == 0 && q.done) {
            pthread_mutex_unlock(&q.lock);
            break;
        }
        char msg[MSG_LEN];
        memcpy(msg, q.buf[q.head], MSG_LEN);
        q.head = (q.head + 1) % QUEUE_CAP;
        q.count--;
        long seq = ++q.consumed;
        int left = q.count;
        pthread_cond_signal(&q.not_full);
        pthread_mutex_unlock(&q.lock);

        fprintf(fp, "%04ld %s\n", seq, msg);
        printf("  C : wrote #%-3ld %s (queue %d/%d)\n", seq, msg, left, QUEUE_CAP);

        random_pause(&seed, 2500, 2000);
    }
    fflush(fp);
    return NULL;
}

static long count_lines(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return -1;
    long lines = 0;
    int ch;
    while ((ch = fgetc(fp)) != EOF)
        if (ch == '\n')
            lines++;
    fclose(fp);
    return lines;
}

int run_logger(int nproducers, int nmsgs)
{
    if (nproducers < 1 || nproducers > MAX_PRODUCERS || nmsgs < 1 || nmsgs > MAX_MESSAGES) {
        fprintf(stderr, "error: producers must be 1-%d and messages 1-%d\n",
                MAX_PRODUCERS, MAX_MESSAGES);
        return 2;
    }

    FILE *fp = fopen(LOG_FILE, "w");
    if (!fp) {
        fprintf(stderr, "error: cannot open %s: %s\n", LOG_FILE, strerror(errno));
        return 1;
    }

    memset(&q, 0, sizeof q);
    int err;
    if ((err = pthread_mutex_init(&q.lock, NULL)) != 0 ||
        (err = pthread_cond_init(&q.not_full, NULL)) != 0 ||
        (err = pthread_cond_init(&q.not_empty, NULL)) != 0) {
        fprintf(stderr, "error: pthread init failed: %s\n", strerror(err));
        fclose(fp);
        return 1;
    }

    printf("=== Concurrent logger: %d producer thread(s) x %d message(s), 1 consumer ===\n",
           nproducers, nmsgs);
    printf("Circular buffer capacity = %d, output file = %s\n", QUEUE_CAP, LOG_FILE);
    printf("Sync: pthread_mutex_t + pthread_cond_t (not_full, not_empty)\n\n");

    pthread_t cons, prod[MAX_PRODUCERS];
    producer_arg_t args[MAX_PRODUCERS];
    int rc = 0;

    if ((err = pthread_create(&cons, NULL, consumer, fp)) != 0) {
        fprintf(stderr, "error: cannot create consumer thread: %s\n", strerror(err));
        fclose(fp);
        return 1;
    }

    int started = 0;
    for (int i = 0; i < nproducers; i++) {
        args[i].id = i + 1;
        args[i].nmsgs = nmsgs;
        if ((err = pthread_create(&prod[i], NULL, producer, &args[i])) != 0) {
            fprintf(stderr, "error: cannot create producer %d: %s\n", i + 1, strerror(err));
            rc = 1;
            break;
        }
        started++;
    }

    for (int i = 0; i < started; i++)
        pthread_join(prod[i], NULL);

    pthread_mutex_lock(&q.lock);
    q.done = 1;
    pthread_cond_broadcast(&q.not_empty);
    pthread_mutex_unlock(&q.lock);
    pthread_join(cons, NULL);

    pthread_cond_destroy(&q.not_full);
    pthread_cond_destroy(&q.not_empty);
    pthread_mutex_destroy(&q.lock);
    if (fclose(fp) != 0) {
        fprintf(stderr, "error: closing %s failed\n", LOG_FILE);
        rc = 1;
    }

    long expected = (long)started * nmsgs;
    long lines = count_lines(LOG_FILE);

    printf("\nSummary:\n");
    printf("  messages produced      : %ld\n", q.produced);
    printf("  messages consumed      : %ld\n", q.consumed);
    printf("  lines in %-14s: %ld\n", LOG_FILE, lines);
    printf("  producer waits (full)  : %ld\n", q.full_waits);
    printf("  consumer waits (empty) : %ld\n", q.empty_waits);
    printf("  threads joined         : %d producer(s) + 1 consumer\n", started);

    if (q.produced == expected && q.consumed == expected && lines == expected) {
        printf("Result: OK - every message was logged exactly once, no message lost.\n");
    } else {
        printf("Result: MISMATCH - expected %ld messages\n", expected);
        rc = 1;
    }
    return rc;
}
