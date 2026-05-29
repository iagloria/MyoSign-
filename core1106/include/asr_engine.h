/** asr_engine.h — 语音识别（健听人 → 文字）STUB */
#ifndef MYOSIGN_ASR_ENGINE_H
#define MYOSIGN_ASR_ENGINE_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} asr_ctx_t;

bool asr_start(asr_ctx_t *ctx);
void asr_stop (asr_ctx_t *ctx);

#endif
