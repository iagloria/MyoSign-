/**
 * emg_infer.c — 手势分类 STUB（模型未就位）
 *
 * 现在只做：
 *   1. 把 EMG 帧字节流拼成滑动窗
 *   2. 投票/平滑（待加）
 *   3. 调用 emg_model_infer() → 现在返回 -1 (未实现)
 *   4. 当返回有效 label 时，查词表 → 发 EV_GESTURE_TEXT + EV_TTS_REQUEST
 *
 * 模型就绪后，把 emg_model_infer() 内部替换成 RKNN 推理即可，
 * 其他模块（UI、TTS）无需改动。
 */
#include "emg_infer.h"
#include "event_bus.h"
#include "config.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "EMG_INFER";

/* ============================================================
 * 占位的标签字典 — 真正部署时从 PATH_GESTURE_DICT 加载
 * ============================================================ */
static const char *g_dict[EMG_NUM_CLASSES] = {
    "你好", "谢谢", "对不起", "请", "再见",
    "我", "你", "吃", "喝水", "厕所",
    "帮助", "医生", "疼", "好", "不",
    "停"
};

const char *emg_label_to_text(int label) {
    if (label < 0 || label >= EMG_NUM_CLASSES) return NULL;
    return g_dict[label];
}

/* ============================================================
 * 模型接口 — 待 RKNN 模型就位后替换
 * 输入: 滑动窗 (samples × channels) int16
 * 输出: label (>=0) + 置信度 [0,1]；返回 -1 表示无可信结果
 * ============================================================ */
static int emg_model_infer(const int16_t *window, int n_samples, float *conf_out) {
    (void)window; (void)n_samples;
    if (conf_out) *conf_out = 0.0f;
#ifdef MYOSIGN_ENABLE_NPU
    /* TODO:
       1) rknn_init(ctx, model_buf, model_len, 0, NULL);
       2) 转 float32, 归一化
       3) rknn_inputs_set + rknn_run + rknn_outputs_get
       4) argmax + softmax 置信度
    */
#endif
    return -1;
}

/* ============================================================
 * EMG 帧 → 滑动窗
 * ============================================================ */
static int16_t g_window[EMG_WINDOW_SAMPLES * EMG_CHANNELS];
static int     g_filled = 0;

static void push_emg_payload(const uint8_t *bytes, int len) {
    /* 字节流是小端 int16 的 ch0,ch1,ch0,ch1...  与采集端约定一致 */
    int n_int16 = len / 2;
    const int16_t *src = (const int16_t *)bytes;
    for (int i = 0; i < n_int16; ++i) {
        if (g_filled < EMG_WINDOW_SAMPLES * EMG_CHANNELS) {
            g_window[g_filled++] = src[i];
        } else {
            memmove(g_window, g_window + EMG_CHANNELS,
                    (EMG_WINDOW_SAMPLES - 1) * EMG_CHANNELS * sizeof(int16_t));
            g_filled -= EMG_CHANNELS;
            g_window[g_filled++] = src[i];
        }
    }
}

static int g_last_label = -1;
static int g_stable_cnt = 0;
#define STABLE_THRESHOLD 3   /* 连续 3 帧相同 label 才认定 */

static void try_infer(void) {
    if (g_filled < EMG_WINDOW_SAMPLES * EMG_CHANNELS) return;
    float conf = 0.0f;
    int label = emg_model_infer(g_window, EMG_WINDOW_SAMPLES, &conf);
    if (label < 0) return;
    if (conf < 0.6f) return;

    if (label == g_last_label) {
        g_stable_cnt++;
    } else {
        g_last_label = label;
        g_stable_cnt = 1;
    }
    if (g_stable_cnt == STABLE_THRESHOLD) {
        const char *txt = emg_label_to_text(label);
        if (txt) {
            event_bus_publish(EV_GESTURE_TEXT, txt, strlen(txt) + 1);
            event_bus_publish(EV_TTS_REQUEST,  txt, strlen(txt) + 1);
            LOGI(TAG, "gesture: %s (conf=%.2f)", txt, conf);
        }
    }
}

static void *emg_thread(void *arg) {
    emg_infer_ctx_t *ctx = (emg_infer_ctx_t *)arg;
    LOGI(TAG, "emg_infer thread started (model %s)",
#ifdef MYOSIGN_ENABLE_NPU
        "ENABLED"
#else
        "DISABLED (stub)"
#endif
    );
    while (ctx->running) {
        event_t ev;
        if (event_bus_pop(EV_EMG_FRAME, &ev, 200)) {
            push_emg_payload((const uint8_t *)ev.data, (int)ev.len);
            free(ev.data);
            try_infer();
        }
    }
    LOGI(TAG, "emg_infer thread exit");
    return NULL;
}

bool emg_infer_start(emg_infer_ctx_t *ctx) {
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, emg_thread, ctx) != 0) { ctx->running = false; return false; }
    return true;
}

void emg_infer_stop(emg_infer_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
}
