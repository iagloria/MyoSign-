/** emg_infer.h — EMG 手势分类推理（接 RKNN/NPU） */
#ifndef MYOSIGN_EMG_INFER_H
#define MYOSIGN_EMG_INFER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} emg_infer_ctx_t;

bool emg_infer_start(emg_infer_ctx_t *ctx);
void emg_infer_stop (emg_infer_ctx_t *ctx);

/** 类别索引 → 中文短语映射（外部可改） */
const char *emg_label_to_text(int label);

#endif
