/** sentence_fixer.h — 语序整理（手势序列 → 自然中文）STUB */
#ifndef MYOSIGN_SENTENCE_FIXER_H
#define MYOSIGN_SENTENCE_FIXER_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} fixer_ctx_t;

bool fixer_start(fixer_ctx_t *ctx);
void fixer_stop (fixer_ctx_t *ctx);

#endif
