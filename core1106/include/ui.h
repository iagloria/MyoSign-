/** ui.h — LCD 上层 UI 调度（上半屏聋人 → 下半屏健听人） */
#ifndef MYOSIGN_UI_H
#define MYOSIGN_UI_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} ui_ctx_t;

bool ui_start(ui_ctx_t *ctx);
void ui_stop (ui_ctx_t *ctx);

#endif
