/**
 * sentence_fixer.c — 手语语序整理 STUB
 *
 * 手语原语序通常是 SOV / 话题化语序，例如"我 水 喝"，
 * 翻译为自然汉语后应为"我喝水"。
 *
 * 这里收集若干 EV_GESTURE_TEXT，攒满一句或超时后调用
 * sentence_fix() 调用真正的小型语言模型，发 EV_FIXED_TEXT。
 *
 * 当前为 stub：直接把累积的手势词拼成一行原文上屏。
 */
#include "sentence_fixer.h"
#include "event_bus.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "FIXER";

#define WORDS_MAX 8
#define WORDS_TIMEOUT_MS 2000

static const char *sentence_fix(const char *raw) {
    /* TODO: 真模型上线后，这里输入 raw（如 "我 水 喝"）输出 "我喝水"。
       可选方案：
         - 微型 transformer / GPT-2 子集，量化后跑 NPU
         - 规则法 + n-gram 重排
         - 离线小模型 (Phi-1 / Qwen0.5B 量化)
    */
    return raw;
}

static void emit(char buf[WORDS_MAX][32], int n) {
    if (n == 0) return;
    char raw[WORDS_MAX * 32 + 16] = {0};
    for (int i = 0; i < n; ++i) {
        if (i) strcat(raw, " ");
        strcat(raw, buf[i]);
    }
    const char *fixed = sentence_fix(raw);
    event_bus_publish(EV_FIXED_TEXT, fixed, strlen(fixed) + 1);
    LOGI(TAG, "fix: \"%s\" → \"%s\"", raw, fixed);
}

static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void *fixer_thread(void *arg) {
    fixer_ctx_t *ctx = (fixer_ctx_t *)arg;
    LOGI(TAG, "fixer thread started (stub)");
    char  buf[WORDS_MAX][32];
    int   n = 0;
    long  last_ms = 0;

    while (ctx->running) {
        event_t ev;
        if (event_bus_pop(EV_GESTURE_TEXT, &ev, 500)) {
            if (n < WORDS_MAX) {
                snprintf(buf[n++], 32, "%s", (const char *)ev.data);
                last_ms = now_ms();
            }
            free(ev.data);
            if (n == WORDS_MAX) { emit(buf, n); n = 0; }
        }
        if (n > 0 && now_ms() - last_ms > WORDS_TIMEOUT_MS) {
            emit(buf, n); n = 0;
        }
    }
    LOGI(TAG, "fixer thread exit");
    return NULL;
}

bool fixer_start(fixer_ctx_t *ctx) {
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, fixer_thread, ctx) != 0) { ctx->running = false; return false; }
    return true;
}

void fixer_stop(fixer_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
}
