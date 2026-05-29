/**
 * audio_out.c — ALSA 播放（MAX98357A I²S）
 *
 * 设计：始终保持 PCM 设备打开，按需写帧。
 * 若启用 SIM 模式则全部空操作。
 */
#include "audio_out.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef MYOSIGN_SIM
#include <alsa/asoundlib.h>
#endif

static const char *TAG = "AUDIO_OUT";

#ifndef MYOSIGN_SIM
static snd_pcm_t *g_pcm = NULL;
#endif

bool audio_out_start(void) {
#ifdef MYOSIGN_SIM
    LOGI(TAG, "audio_out (sim) ready");
    return true;
#else
    int err = snd_pcm_open(&g_pcm, AUDIO_OUT_CARD, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) { LOGE(TAG, "snd_pcm_open %s: %s", AUDIO_OUT_CARD, snd_strerror(err)); return false; }

    snd_pcm_hw_params_t *hw;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_hw_params_any(g_pcm, hw);
    snd_pcm_hw_params_set_access      (g_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format      (g_pcm, hw, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels    (g_pcm, hw, AUDIO_OUT_CHANNELS);
    unsigned int rate = AUDIO_OUT_SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near   (g_pcm, hw, &rate, 0);
    snd_pcm_uframes_t period = (AUDIO_OUT_SAMPLE_RATE * AUDIO_OUT_PERIOD_MS) / 1000;
    snd_pcm_hw_params_set_period_size_near(g_pcm, hw, &period, 0);
    err = snd_pcm_hw_params(g_pcm, hw);
    if (err < 0) { LOGE(TAG, "hw_params: %s", snd_strerror(err)); return false; }

    snd_pcm_prepare(g_pcm);
    LOGI(TAG, "audio_out ready %s %uHz x%d", AUDIO_OUT_CARD, rate, AUDIO_OUT_CHANNELS);
    return true;
#endif
}

void audio_out_stop(void) {
#ifndef MYOSIGN_SIM
    if (g_pcm) { snd_pcm_drain(g_pcm); snd_pcm_close(g_pcm); g_pcm = NULL; }
#endif
}

bool audio_out_play_pcm(const int16_t *pcm, size_t samples) {
#ifdef MYOSIGN_SIM
    LOGI(TAG, "play_pcm (sim) samples=%zu", samples);
    return true;
#else
    if (!g_pcm) return false;
    size_t off = 0;
    while (off < samples) {
        snd_pcm_sframes_t n = snd_pcm_writei(g_pcm, pcm + off, samples - off);
        if (n == -EPIPE)      { snd_pcm_prepare(g_pcm); continue; }
        if (n == -ESTRPIPE)   { while (snd_pcm_resume(g_pcm) == -EAGAIN) sleep(1); continue; }
        if (n < 0)            { LOGE(TAG, "writei: %s", snd_strerror((int)n)); return false; }
        off += (size_t)n;
    }
    return true;
#endif
}

/* 极简 WAV 读取（仅支持 16-bit PCM，无 LIST chunk） */
bool audio_out_play_wav(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { LOGE(TAG, "open wav %s failed", path); return false; }
    uint8_t hdr[44];
    if (fread(hdr, 1, 44, fp) != 44) { fclose(fp); return false; }
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        LOGE(TAG, "not a WAV: %s", path); fclose(fp); return false;
    }
    /* 直接把后面所有数据当 PCM 播 */
    int16_t buf[4096];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 2, sizeof(buf)/2, fp)) > 0) {
        if (!audio_out_play_pcm(buf, n)) { ok = false; break; }
    }
    fclose(fp);
    return ok;
}
