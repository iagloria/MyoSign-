/** audio_out.h — I²S 输出到 MAX98357A，播 PCM 或 WAV */
#ifndef MYOSIGN_AUDIO_OUT_H
#define MYOSIGN_AUDIO_OUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool audio_out_start(void);
void audio_out_stop (void);

/** 同步阻塞地播 16kHz/单声道/S16LE PCM 数据 */
bool audio_out_play_pcm(const int16_t *pcm, size_t samples);

/** 播一个 WAV 文件（用于预录词条/提示音） */
bool audio_out_play_wav(const char *path);

#endif
