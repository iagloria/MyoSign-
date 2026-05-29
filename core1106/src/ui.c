/**
 * ui.c — 屏幕 UI 渲染线程
 *
 * 订阅 EV_GESTURE_TEXT / EV_FIXED_TEXT / EV_POWER_UPDATE，
 * 用简单的双区滚动文本绘制到 LCD。
 *
 * 当前未集成 freetype，调用 lcd_draw_text_utf8 暂时只打日志。
 * 接入 freetype 后无需改动本文件，只补 lcd.c 中的实现即可。
 */
#include "ui.h"
#include "lcd.h"
#include "event_bus.h"
#include "power.h"
#include "config.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "UI";

#define LINE_MAX  6     /* 上下各保留 6 行历史 */
#define TEXT_MAX  64

typedef struct {
    char lines[LINE_MAX][TEXT_MAX];
    int  count;
} text_region_t;

static text_region_t g_top;
static text_region_t g_bot;
static power_state_t g_pwr;

static void region_push(text_region_t *r, const char *s) {
    if (r->count == LINE_MAX) {
        memmove(r->lines[0], r->lines[1], (LINE_MAX - 1) * TEXT_MAX);
        r->count--;
    }
    snprintf(r->lines[r->count++], TEXT_MAX, "%s", s);
}

static void render(void) {
    lcd_fill(0x0000);
    /* 顶部状态栏 */
    char buf[64];
    snprintf(buf, sizeof(buf), "BAT %d%%  BLE OK", g_pwr.percent);
    lcd_draw_text_utf8(2, 2, buf, 0xFFFF, 0x0000);

    /* 上半屏 */
    for (int i = 0; i < g_top.count; ++i)
        lcd_draw_text_utf8(4, 24 + i * 18, g_top.lines[i], 0x07E0, 0x0000);

    /* 中线 */
    /* 下半屏 */
    for (int i = 0; i < g_bot.count; ++i)
        lcd_draw_text_utf8(4, 140 + i * 18, g_bot.lines[i], 0xFFE0, 0x0000);
}

static void *ui_thread(void *arg) {
    ui_ctx_t *ctx = (ui_ctx_t *)arg;
    LOGI(TAG, "ui thread started");

    while (ctx->running) {
        event_t ev;
        bool dirty = false;

        if (event_bus_pop(EV_GESTURE_TEXT, &ev, 50)) {
            region_push(&g_top, (const char *)ev.data);
            free(ev.data);
            dirty = true;
        }
        if (event_bus_pop(EV_FIXED_TEXT, &ev, 0)) {
            region_push(&g_bot, (const char *)ev.data);
            free(ev.data);
            dirty = true;
        }
        if (event_bus_pop(EV_ASR_TEXT, &ev, 0)) {
            /* 若没有语序整理模型，直接把 ASR 原文上屏 */
            region_push(&g_bot, (const char *)ev.data);
            free(ev.data);
            dirty = true;
        }
        if (event_bus_pop(EV_POWER_UPDATE, &ev, 0)) {
            memcpy(&g_pwr, ev.data, sizeof(g_pwr));
            free(ev.data);
            dirty = true;
        }

        if (dirty) render();
    }
    LOGI(TAG, "ui thread exit");
    return NULL;
}

bool ui_start(ui_ctx_t *ctx) {
    if (!lcd_init()) return false;
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, ui_thread, ctx) != 0) { ctx->running = false; return false; }
    return true;
}

void ui_stop(ui_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
    lcd_close();
}
