#ifndef BANKER_H
#define BANKER_H
/* FR5: safe_case = 1 -> built-in SAFE state, 0 -> built-in UNSAFE state. Returns 0 on success. */
int run_banker(int safe_case);
#endif
