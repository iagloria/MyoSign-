/**
 * ============================================================
 * main.c — MyoSign Core1106 主程序
 * ============================================================
 *
 * 启动所有线程，等待信号优雅退出。
 *
 * 模块依赖图：
 *
 *   uart_rx ──► EV_EMG_FRAME  ──► emg_infer ──► EV_GESTURE_TEXT ──┐
 *                                                                 ├─► sentence_fixer
 *                                                  EV_TTS_REQUEST │
 *                                                          │      ▼
 *                                                          ▼   EV_FIXED_TEXT
 *                                                       tts ──► audio_out
 *                                                                 │
 *   mic_in  ──► EV_MIC_PCM   ──► asr ──────────► EV_ASR_TEXT ──► ui (LCD)
 *                                                                 ▲
 *   power   ──► EV_POWER_UPDATE ──────────────────────────────────┘
 */
#include "config.h"
#include "log.h"
#include "event_bus.h"
#include "uart_rx.h"
#include "mic_in.h"
#include "audio_out.h"
#include "ui.h"
#include "power.h"
#include "emg_infer.h"
#include "asr_engine.h"
#include "tts_engine.h"
#include "sentence_fixer.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static const char *TAG = "MAIN";

static volatile int g_stop = 0;
static void on_sig(int s) { (void)s; g_stop = 1; }

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    log_init(PATH_LOG_DIR "/myosign.log", LOG_LEVEL_INFO);
    LOGI(TAG, "=== MyoSign Core1106 v1.0 start ===");

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    event_bus_init();

    /* ---- 启动各子系统（顺序：先输出/UI，再生产者）---- */
    if (!audio_out_start())              { LOGE(TAG, "audio_out failed"); return 1; }

    ui_ctx_t      ui      = {0};
    if (!ui_start(&ui))                  { LOGE(TAG, "ui failed"); return 1; }

    tts_ctx_t     tts     = {0}; tts_start(&tts);
    fixer_ctx_t   fixer   = {0}; fixer_start(&fixer);
    asr_ctx_t     asr     = {0}; asr_start(&asr);
    emg_infer_ctx_t emg   = {0}; emg_infer_start(&emg);

    mic_in_ctx_t  mic     = {0}; mic_in_start(&mic);
    uart_rx_ctx_t uart    = {0};
    if (!uart_rx_start(&uart))           { LOGE(TAG, "uart_rx failed"); }

    power_ctx_t   power   = {0}; power_start(&power);

    LOGI(TAG, "all threads up, entering main loop");

    while (!g_stop) sleep(1);

    LOGI(TAG, "shutting down...");
    /* 反序停止 */
    power_stop      (&power);
    uart_rx_stop    (&uart);
    mic_in_stop     (&mic);
    emg_infer_stop  (&emg);
    asr_stop        (&asr);
    fixer_stop      (&fixer);
    tts_stop        (&tts);
    ui_stop         (&ui);
    audio_out_stop  ();
    event_bus_close ();
    log_close();
    return 0;
}
