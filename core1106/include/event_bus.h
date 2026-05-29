/**
 * event_bus.h — 线程安全消息总线（pub/sub）
 *
 * 各模块之间通过 event_bus_publish 发送事件，
 * 订阅者在自己的线程里调用 event_bus_pop 拿事件。
 * 简单实现：每种事件类型一个队列。
 */
#ifndef MYOSIGN_EVENT_BUS_H
#define MYOSIGN_EVENT_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    EV_EMG_FRAME = 0,      // 协议帧：来自 ESP32 的 EMG 数据
    EV_EMG_GESTURE,        // 推理结果：手势类别索引
    EV_GESTURE_TEXT,       // 手势 → 中文短语
    EV_MIC_PCM,            // 麦克风 PCM 数据片段
    EV_ASR_TEXT,           // ASR 识别结果文字
    EV_FIXED_TEXT,         // 语序整理后的文字
    EV_TTS_REQUEST,        // 请求 TTS 播放某句文字
    EV_DISPLAY_UPDATE,     // 屏幕刷新请求
    EV_POWER_UPDATE,       // 电量更新
    EV_SHUTDOWN,           // 退出
    EV_MAX
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t     seq;
    size_t       len;
    void        *data;     // 调用者拥有，pop 后由消费者 free
} event_t;

bool event_bus_init(void);
void event_bus_close(void);

/** 发布事件（拷贝 data 内存）。返回 false = 队列满。 */
bool event_bus_publish(event_type_t type, const void *data, size_t len);

/** 阻塞拉取某类型事件，超时单位毫秒（<0 表示永久）。需调用方 free(out->data) */
bool event_bus_pop(event_type_t type, event_t *out, int timeout_ms);

#endif
