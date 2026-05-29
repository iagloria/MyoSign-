/**
 * ============================================================
 * config.h — Core1106 (RV1106) 应用全局配置
 * ============================================================
 *
 * 所有硬件设备节点、缓冲区大小、采样率统一在这里定义。
 * 调整硬件接线时只改这里。
 */
#ifndef MYOSIGN_CONFIG_H
#define MYOSIGN_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================
 * 1. UART — 与 ESP32-S3 通信
 * ============================================================
 * 原理图网络 "RX/TX"（无数字）对应 Core1106 的业务串口。
 * 默认假设是 /dev/ttyS3（实际节点要按 SDK pinmux 确认；
 * 调试串口 RX3/TX3 是 /dev/ttyFIQ0，跑 console，不要在程序里打开）
 */
#define UART_DEV_BIZ        "/dev/ttyS3"
#define UART_BAUDRATE       115200

/* ============================================================
 * 2. 协议帧
 * ============================================================
 * 必须与 ESP32-S3 端 protocol.h 完全一致
 */
#define PROTO_FRAME_HEAD        0xAA
#define PROTO_FRAME_TAIL        0x55
#define PROTO_CMD_EMG_DATA      0x01
#define PROTO_CMD_CONTROL       0x02
#define PROTO_CMD_STATUS        0x03
#define PROTO_FRAME_OVERHEAD    6
#define PROTO_MAX_PAYLOAD       512
#define PROTO_MAX_FRAME         (PROTO_MAX_PAYLOAD + PROTO_FRAME_OVERHEAD)

/* ============================================================
 * 3. EMG 数据
 * ============================================================
 * 采集端为 2 通道 × 200Hz × int16
 */
#define EMG_CHANNELS            2
#define EMG_SAMPLE_RATE_HZ      200
#define EMG_WINDOW_MS           300     // 滑动窗 300 ms
#define EMG_WINDOW_SAMPLES      ((EMG_SAMPLE_RATE_HZ) * EMG_WINDOW_MS / 1000)
#define EMG_STRIDE_MS           100     // 步长 100 ms
#define EMG_STRIDE_SAMPLES      ((EMG_SAMPLE_RATE_HZ) * EMG_STRIDE_MS / 1000)
#define EMG_NUM_CLASSES         16      // 占位：手势词表大小

/* ============================================================
 * 4. 音频输出 → MAX98357A (I²S0)
 * ============================================================
 */
#define AUDIO_OUT_CARD          "hw:0,0"       // ALSA 卡:子设备
#define AUDIO_OUT_SAMPLE_RATE   16000
#define AUDIO_OUT_CHANNELS      1               // MAX98357A 单声道
#define AUDIO_OUT_PERIOD_MS     20

/* ============================================================
 * 5. 麦克风输入 → I²S1 (H1+H2)
 * ============================================================
 */
#define MIC_IN_CARD             "hw:1,0"       // 另一张 ALSA 卡（看实际 dmesg）
#define MIC_IN_SAMPLE_RATE      16000
#define MIC_IN_CHANNELS         1               // INMP441 单声道
#define MIC_IN_PERIOD_MS        20

/* ============================================================
 * 6. LCD（SPI 屏 via H3）
 * ============================================================
 * 默认 ST7789 240×240，可改 ILI9341 / GC9A01
 */
#define LCD_SPI_DEV             "/dev/spidev0.0"
#define LCD_GPIO_DC             82      // 占位；按 RV1106 pinmux 实际改
#define LCD_GPIO_RES            83
#define LCD_GPIO_BLK            84
#define LCD_WIDTH               240
#define LCD_HEIGHT              240
#define LCD_SPI_SPEED_HZ        40000000

/* ============================================================
 * 7. 电池监测（ADC）
 * ============================================================
 */
#define BAT_ADC_PATH            "/sys/bus/iio/devices/iio:device0/in_voltage0_raw"
#define BAT_ADC_FULL_MV         4200
#define BAT_ADC_EMPTY_MV        3300
#define BAT_ADC_DIVIDER         2.0f    // R26/R27 分压比，根据实际计算

/* ============================================================
 * 8. 文件 / 资源路径
 * ============================================================
 */
#define PATH_PREFIX             "/opt/myosign"
#define PATH_MODELS             PATH_PREFIX "/models"
#define PATH_VOICE              PATH_PREFIX "/voice"
#define PATH_FONT               PATH_PREFIX "/assets/font.ttf"
#define PATH_GESTURE_DICT       PATH_PREFIX "/assets/gesture_dict.json"
#define PATH_LOG_DIR            "/var/log/myosign"

/* ============================================================
 * 9. 线程优先级（Linux SCHED_FIFO，越大越高）
 * ============================================================
 */
#define PRIO_UART_RX            70
#define PRIO_MIC_CAPTURE        65
#define PRIO_AUDIO_OUT          60
#define PRIO_EMG_INFER          50
#define PRIO_ASR_INFER          50
#define PRIO_DISPLAY            20
#define PRIO_POWER              10

/* ============================================================
 * 10. 调试开关
 * ============================================================
 */
#ifndef MYOSIGN_LOG_LEVEL
#define MYOSIGN_LOG_LEVEL       LOG_LEVEL_INFO
#endif

#endif /* MYOSIGN_CONFIG_H */
