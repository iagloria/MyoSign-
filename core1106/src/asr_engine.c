/**
 * asr_engine.c — ASR STUB（待集成 sherpa-onnx 或 Vosk）
 *
 * 现在只做：
 *   1. 订阅 EV_MIC_PCM，累积 PCM
 *   2. 调用 asr_decode_chunk() → 现在直接丢弃
 *   3. 模型接入后在 asr_decode_chunk() 内调用真实 ASR，每识别完一句
 *      发 EV_ASR_TEXT。
 *
 * VAD（语音活动检测）也建议在这里实现：
 *   能量门限或 WebRTC VAD / Silero VAD → 切句
 */
#include "asr_engine.h"
#include "event_bus.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "ASR";

static void asr_decode_chunk(const int16_t *pcm, int n_samples) {
    (void)pcm; (void)n_samples;
#ifdef MYOSIGN_ENABLE_ASR
    /* TODO:
       1) sherpa_onnx_offline_recognizer / streaming feeder
       2) 流式喂 PCM
       3) end-of-utterance 时:
          event_bus_publish(EV_ASR_TEXT, text, strlen(text)+1);
    */
#endif
}

static void *asr_thread(void *arg) {
    asr_ctx_t *ctx = (asr_ctx_t *)arg;
    LOGI(TAG, "asr thread started (engine %s)",
#ifdef MYOSIGN_ENABLE_ASR
        "ENABLED"
#else
        "DISABLED (stub)"
#endif
    );

    while (ctx->running) {
        event_t ev;
        if (event_bus_pop(EV_MIC_PCM, &ev, 200)) {
            asr_decode_chunk((const int16_t *)ev.data, (int)(ev.len / 2));
            free(ev.data);
        }
    }
    LOGI(TAG, "asr thread exit");
    return NULL;
}

bool asr_start(asr_ctx_t *ctx) {
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, asr_thread, ctx) != 0) { ctx->running = false; return false; }
    return true;
}

void asr_stop(asr_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
}
