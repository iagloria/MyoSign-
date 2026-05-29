/**
 * tts_engine.c — TTS STUB
 *
 * 当前策略：
 *   收到 EV_TTS_REQUEST → 优先在 /opt/myosign/voice/<text>.wav 找预录文件
 *   找到就调 audio_out_play_wav；找不到打 warning。
 *
 * 模型就绪后在 tts_synth_to_pcm() 内调离线 TTS（如 sherpa-onnx VITS），
 * 直接调用 audio_out_play_pcm() 输出。
 */
#include "tts_engine.h"
#include "audio_out.h"
#include "event_bus.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static const char *TAG = "TTS";

static bool file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

#ifdef MYOSIGN_ENABLE_TTS
static bool tts_synth_to_pcm(const char *text, int16_t **pcm_out, size_t *n_out) {
    (void)text; (void)pcm_out; (void)n_out;
    /* TODO: 调用 sherpa-onnx offline tts；申请 pcm_out 由调用方 free */
    return false;
}
#endif

static void handle_tts(const char *text) {
    char wav_path[256];
    snprintf(wav_path, sizeof(wav_path), "%s/%s.wav", PATH_VOICE, text);
    if (file_exists(wav_path)) {
        LOGI(TAG, "play prerecorded: %s", wav_path);
        audio_out_play_wav(wav_path);
        return;
    }
#ifdef MYOSIGN_ENABLE_TTS
    int16_t *pcm = NULL; size_t n = 0;
    if (tts_synth_to_pcm(text, &pcm, &n)) {
        audio_out_play_pcm(pcm, n);
        free(pcm);
        return;
    }
#endif
    LOGW(TAG, "no TTS available for: %s", text);
}

static void *tts_thread(void *arg) {
    tts_ctx_t *ctx = (tts_ctx_t *)arg;
    LOGI(TAG, "tts thread started (engine %s)",
#ifdef MYOSIGN_ENABLE_TTS
        "ENABLED"
#else
        "DISABLED (only prerecorded WAVs)"
#endif
    );
    while (ctx->running) {
        event_t ev;
        if (event_bus_pop(EV_TTS_REQUEST, &ev, 200)) {
            handle_tts((const char *)ev.data);
            free(ev.data);
        }
    }
    LOGI(TAG, "tts thread exit");
    return NULL;
}

bool tts_start(tts_ctx_t *ctx) {
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, tts_thread, ctx) != 0) { ctx->running = false; return false; }
    return true;
}

void tts_stop(tts_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
}
