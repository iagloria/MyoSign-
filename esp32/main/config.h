/**
 * ============================================================
 * config.h — 全局配置文件
 * ============================================================
 *
 * 【重要】这是整个项目最需要你修改的文件！
 * 所有硬件引脚、通信参数、队列大小都在这里定义。
 *
 * 修改流程:
 *   1. 对照你的原理图 (SCH_Schematic1_2026-05-25.pdf)
 *   2. 找到 ESP32-S3 (U27) 的 RX3/TX3 对应哪个 GPIO
 *   3. 修改下面的 UART_1106_TX_PIN / UART_1106_RX_PIN
 *   4. 其他参数一般不需要改
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * 第 1 部分: UART 串口配置（连接 Core1106）
 * ============================================================
 *
 * 原理图上的网络标签:
 *   RX3 — Core1106 的接收脚 — 应连接 ESP32 的 TX
 *   TX3 — Core1106 的发送脚 — 应连接 ESP32 的 RX
 *
 * 交叉连接规则:
 *   ESP32 TX ──→ Core1106 RX  (数据从 ESP32 流出)
 *   ESP32 RX ←── Core1106 TX  (本项目单向通信，可不接)
 */

// ---- 串口号 ----
// ESP32-S3 有 3 个硬件 UART: UART0(日志), UART1, UART2
// UART0 已被系统日志占用，我们用 UART1 连接 Core1106
#define UART_1106_PORT          UART_NUM_1

// ---- 引脚定义（已对照 SCH_Schematic1_2026-05-25.pdf 确认）----
// 原理图全板共享网络名 "RX" / "TX"（注意不是 RX3/TX3，那是 Core1106 调试串口）：
//   网络 "RX"：ESP32 GPIO17 ↔ Core1106 通信 UART 的 RX 脚
//   网络 "TX"：ESP32 GPIO18 ↔ Core1106 通信 UART 的 TX 脚
// 因此 ESP32 这一端必须把 GPIO17 配置为 UART TX、GPIO18 配置为 UART RX。
#define UART_1106_TX_PIN        17      // ESP32 发送 → Core1106 接收
#define UART_1106_RX_PIN        18      // ESP32 接收 ← Core1106 发送（本项目单向，可悬空）

// ---- 通信参数 ----
#define UART_1106_BAUDRATE      115200  // 波特率 (常用: 115200, 256000, 921600)
#define UART_1106_BUF_SIZE      2048    // 发送缓冲区大小（字节）

/* ============================================================
 * 第 2 部分: BLE 蓝牙配置
 * ============================================================
 *
 * ESP32-S3 作为 BLE Server（GATT Server）
 * 采集端 ESP32 作为 BLE Client，连接后向 Characteristic 写入肌电数据
 */

// ---- 设备名称（手机或采集端搜索时看到的名称）----
#define BLE_DEVICE_NAME         "MyoSign_Core"

// ---- BLE UUID ----
// 你可以自定义为任意 128-bit UUID
// 这里使用简化的 16-bit UUID（标准 BLE 格式）
#define BLE_EMG_SERVICE_UUID    0x1800  // 肌电数据 Service
#define BLE_EMG_CHAR_UUID       0x2A00  // 肌电数据 Characteristic

// ---- BLE 连接参数 ----
// 连接间隔越小延迟越低，但功耗越大
// 单位 1.25ms，范围 6~3200
#define BLE_CONN_INTERVAL_MIN   12      // 15ms (12 × 1.25ms)
#define BLE_CONN_INTERVAL_MAX   24      // 30ms (24 × 1.25ms)

/* ============================================================
 * 第 3 部分: FreeRTOS 配置
 * ============================================================
 */

// ---- 任务优先级（数字越大优先级越高，范围 0 ~ configMAX_PRIORITIES-1）----
#define BLE_TASK_PRIORITY       5       // BLE 接收任务（最高优先级，防止丢包）
#define UART_TASK_PRIORITY      4       // UART 发送任务

// ---- 任务栈大小（字节）----
// 如果栈不够用，FreeRTOS 会触发 Stack Overflow 看门狗复位
// BLE 任务需要较大栈（NimBLE 协议栈开销）
// UART 任务栈可以小一些
#define BLE_TASK_STACK_SIZE     4096
#define UART_TASK_STACK_SIZE    4096

/* ============================================================
 * 第 4 部分: 消息队列配置
 * ============================================================
 *
 * 队列是 BLE 任务和 UART 任务之间的唯一通信方式
 * BLE 收到数据 → 放入队列 → UART 取出发送
 * 如果队列满了，BLE 任务会丢弃最旧的数据（防止阻塞）
 */

// ---- 队列名称 ----
#define EMG_DATA_Q_NAME         "emg_data_q"

// ---- 队列深度（能同时缓存多少包数据）----
// 如果 BLE 数据来得比 UART 发送快，队列会堆积
// 8 是合理值：缓冲约 500ms 的数据
#define EMG_DATA_Q_LEN          8

// ---- 单包肌电数据最大字节数 ----
// 决定了队列每个元素的大小
// 2 通道 × 100 采样点 × 2 字节(int16) = 400 字节，取 512 留余量
#define EMG_PACKET_MAX_LEN      512

/* ============================================================
 * 第 5 部分: 数据包结构体
 * ============================================================
 *
 * 这是队列中传递的数据类型
 * 每包数据包含:
 *   data[] — 原始肌电 ADC 采样值（int16 数组，双通道交替）
 *   len   — data 字段的有效字节数
 */

typedef struct {
    uint8_t  data[EMG_PACKET_MAX_LEN];  // 肌电原始数据缓冲区
    uint16_t len;                        // 实际数据长度（字节）
} emg_packet_t;

#endif /* CONFIG_H */
