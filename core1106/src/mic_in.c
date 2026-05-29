/**
 * mic_in.c — I²S 麦克风采集
 *
 * 读 ALSA capture，按 20 ms 周期投递 PCM 数据到事件总线，
 * 后续 VAD / ASR 模块订阅 EV_MIC_PCM。
 */
#include "mic_in.h"
#include "config.h"
#include "event_bus.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef MYOSIGN_SIM
#include <alsa/asoundlib.h>
#endif

static const char *TAG = "MIC_IN";

#ifndef MYOSIGN_SIM
static snd_pcm_t *open_capture(void) {
    snd_pcm_t *pcm;
    int err = snd_pcm_open(&pcm, MIC_IN_CARD, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) { LOGE(TAG, "open %s: %s", MIC_IN_CARD, snd_strerror(err)); return NULL; }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access  (pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format  (pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, hw, MIC_IN_CHANNELS);
    unsigned int rate = MIC_IN_SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0);
    snd_pcm_uframes_t period = (MIC_IN_SAMPLE_RATE * MIC_IN_PERIOD_MS) / 1000;
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, 0);
    err = snd_pcm_hw_params(pcm, hw);
    if (err < 0) { snd_pcm_close(pcm); return NULL; }
    snd_pcm_prepare(pcm);
    return pcm;
}
#endif

static void *mic_in_thread(void *arg) {
    mic_in_ctx_t *ctx = (mic_in_ctx_t *)arg;
    LOGI(TAG, "mic_in thread on %s @ %dHz", MIC_IN_CARD, MIC_IN_SAMPLE_RATE);

    const size_t frames = (MIC_IN_SAMPLE_RATE * MIC_IN_PERIOD_MS) / 1000;
    int16_t *buf = (int16_t *)malloc(frames * MIC_IN_CHANNELS * sizeof(int16_t));
    if (!buf) { LOGE(TAG, "oom"); return NULL; }

#ifndef MYOSIGN_SIM
    snd_pcm_t *pcm = open_capture();
    if (!pcm) { free(buf); return NULL; }
#endif

    while (ctx->running) {
#ifdef MYOSIGN_SIM
        memset(buf, 0, frames * MIC_IN_CHANNELS * sizeof(int16_t));
        usleep(MIC_IN_PERIOD_MS * 1000);
#else
        snd_pcm_sframes_t n = snd_pcm_readi(pcm, buf, frames);
        if (n == -EPIPE)    { snd_pcm_prepare(pcm); continue; }
        if (n == -ESTRPIPE) { while (snd_pcm_resume(pcm) == -EAGAIN) sleep(1); continue; }
        if (n < 0)          { LOGE(TAG, "readi: %s", snd_strerror((int)n)); continue; }
#endif
        event_bus_publish(EV_MIC_PCM, buf,
                          frames * MIC_IN_CHANNELS * sizeof(int16_t));
    }

#ifndef MYOSIGN_SIM
    snd_pcm_close(pcm);
#endif
    free(buf);
    LOGI(TAG, "mic_in thread exit");
    return NULL;
}

bool mic_in_start(mic_in_ctx_t *ctx) {
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, mic_in_thread, ctx) != 0) {
        ctx->running = false; return false;
    }
    return true;
}

void mic_in_stop(mic_in_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
}
