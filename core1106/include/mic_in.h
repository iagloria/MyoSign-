/** mic_in.h — I²S 麦克风采集线程（INMP441 等） */
#ifndef MYOSIGN_MIC_IN_H
#define MYOSIGN_MIC_IN_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} mic_in_ctx_t;

bool mic_in_start(mic_in_ctx_t *ctx);
void mic_in_stop (mic_in_ctx_t *ctx);

#endif
