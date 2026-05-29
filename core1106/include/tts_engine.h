/** tts_engine.h — 文本转语音（聋人 → 喇叭）STUB */
#ifndef MYOSIGN_TTS_ENGINE_H
#define MYOSIGN_TTS_ENGINE_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} tts_ctx_t;

bool tts_start(tts_ctx_t *ctx);
void tts_stop (tts_ctx_t *ctx);

#endif
