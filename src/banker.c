/* banker.c - FR5: Banker's safety algorithm
 *
 * Two built-in, repeatable test cases share the same Allocation/Maximum matrices;
 * only the Available vector differs, which decides whether the state is safe.
 */
#include <stdio.h>
#include "banker.h"

#define NP 5   /* number of processes */
#define NR 3   /* number of resource types (A, B, C) */

typedef struct {
    const char *title;
    int available[NR];
    int allocation[NP][NR];
    int maximum[NP][NR];
} BankerCase;

static const BankerCase CASES[2] = {
    { "UNSAFE state",
      {0, 1, 1},
      {{0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2}},
      {{7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3}} },
    { "SAFE state",
      {3, 3, 2},
      {{0,1,0}, {2,0,0}, {3,0,2}, {2,1,1}, {0,0,2}},
      {{7,5,3}, {3,2,2}, {9,0,2}, {2,2,2}, {4,3,3}} }
};

static void print_vec(const int v[NR])
{
    printf("[");
    for (int r = 0; r < NR; r++) printf("%s%d", r ? " " : "", v[r]);
    printf("]");
}

static void print_matrix(const char *name, const int m[NP][NR])
{
    printf("%s:\n", name);
    printf("      A  B  C\n");
    for (int p = 0; p < NP; p++) {
        printf("  P%d ", p);
        for (int r = 0; r < NR; r++) printf("%2d ", m[p][r]);
        printf("\n");
    }
}

/* Returns 1 if every process of 'need' can be satisfied in some order; fills seq[]. */
static int is_safe(const int available[NR], const int allocation[NP][NR],
                   const int need[NP][NR], int seq[NP], int *finished_count)
{
    int work[NR], finished[NP] = {0};
    for (int r = 0; r < NR; r++) work[r] = available[r];

    int count = 0, pass = 0, progress = 1;
    while (count < NP && progress) {
        progress = 0;
        pass++;
        for (int p = 0; p < NP; p++) {
            if (finished[p]) continue;
            int ok = 1;
            for (int r = 0; r < NR; r++) if (need[p][r] > work[r]) { ok = 0; break; }
            if (!ok) continue;

            printf("  Pass %d: P%d can finish. Need ", pass, p);
            print_vec(need[p]);
            printf(" <= Work ");
            print_vec(work);
            for (int r = 0; r < NR; r++) work[r] += allocation[p][r];   /* P releases everything */
            printf(" -> Work becomes ");
            print_vec(work);
            printf("\n");

            finished[p] = 1;
            seq[count++] = p;
            progress = 1;
        }
    }
    *finished_count = count;
    return count == NP;
}

int run_banker(int safe_case)
{
    const BankerCase *c = &CASES[safe_case ? 1 : 0];
    int need[NP][NR], seq[NP], finished_count = 0;

    printf("=== Banker's Algorithm: %s ===\n", c->title);
    printf("Available: ");
    print_vec(c->available);
    printf("\n");
    print_matrix("Allocation", c->allocation);
    print_matrix("Maximum", c->maximum);

    for (int p = 0; p < NP; p++)                       /* Need = Maximum - Allocation */
        for (int r = 0; r < NR; r++) need[p][r] = c->maximum[p][r] - c->allocation[p][r];
    print_matrix("Need (= Maximum - Allocation)", need);

    printf("\nSafety check trace:\n");
    int safe = is_safe(c->available, c->allocation, need, seq, &finished_count);

    if (safe) {
        printf("\nRESULT: SAFE. Safe sequence: ");
        for (int i = 0; i < NP; i++) printf("P%d%s", seq[i], i < NP - 1 ? " -> " : "\n");
    } else {
        int done[NP] = {0};
        for (int i = 0; i < finished_count; i++) done[seq[i]] = 1;
        printf("\nRESULT: UNSAFE. No safe sequence exists; deadlock is possible.\n");
        printf("Processes that can never be guaranteed to finish:");
        for (int p = 0; p < NP; p++) if (!done[p]) printf(" P%d", p);
        printf("\n");
    }
    return 0;
}
