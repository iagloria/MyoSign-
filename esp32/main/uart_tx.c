/**
 * ============================================================
 * uart_tx.c — UART 串口发送模块实现
 * ============================================================
 *
 * 本模块是整个数据链路的最后一环:
 *   BLE 接收 → 队列缓冲 → 协议打包 → UART 发送 → Core1106
 *
 * 为什么用 ESP-IDF 的 UART 驱动而不是自己写？
 *   - ESP-IDF 的 UART 驱动是硬件中断驱动的，不阻塞 CPU
 *   - 有发送 FIFO (128 字节硬件缓冲区)
 *   - 支持 DMA（可选，数据量大时可以开启）
 *   - 自动处理波特率、停止位、校验位
 *
 * 发送流程:
 *   1. uart_write_bytes() 把数据放入硬件 TX FIFO
 *   2. 硬件自动按波特率逐位发出
 *   3. 函数立即返回（非阻塞），不等待发送完成
 *   4. 如果 FIFO 满了，函数会短暂阻塞直到有空间
 *
 * 为什么不需要流控 (Flow Control)?
 *   - 单向通信，ESP32 只管发，Core1106 只管收
 *   - Core1106 的 UART RX 有足够的硬件 FIFO
 *   - 115200 bps 对 RV1106 来说太慢了，不会丢数据
 */

#include "uart_tx.h"
#include "protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "driver/uart.h"

/* ============================================================
 * 静态标签
 * ============================================================ */
static const char *TAG = "UART_TX";

/* ============================================================
 * 外部变量引用
 * ============================================================ */
extern QueueHandle_t emg_data_q;

/* ============================================================
 * 发送缓冲区（静态分配，避免反复 malloc）
 * ============================================================
 *
 * 为什么用静态缓冲区而不是动态分配?
 *   1. 栈上分配更快（无 heap 开销）
 *   2. 大小固定，不会出现内存碎片
 *   3. 编译期就知道够不够用
 *
 * 大小计算:
 *   最大帧长度 = 帧开销(6) + 最大载荷(512) = 518 字节
 */
#define TX_BUF_SIZE  (EMG_PACKET_MAX_LEN + PROTO_FRAME_OVERHEAD)

/* ============================================================
 * UART 硬件初始化
 * ============================================================
 *
 * 调用时机: uart_tx_task 启动时调用一次
 *
 * 配置项:
 *   - 波特率:  115200
 *   - 数据位:  8 bits
 *   - 校验位:  无 (通信距离短，抗干扰需求低)
 *   - 停止位:  1 bit
 *   - 流控:    无 (单向发送)
 *   - TX 引脚: UART_1106_TX_PIN (在 config.h 中定义)
 *   - RX 引脚: 不配置（单向通信）
 */
static void uart_init(void)
{
    // ---- 1. UART 参数配置 ----
    uart_config_t uart_config = {
        .baud_rate  = UART_1106_BAUDRATE,        // 波特率
        .data_bits  = UART_DATA_8_BITS,           // 8 位数据
        .parity     = UART_PARITY_DISABLE,        // 无校验
        .stop_bits  = UART_STOP_BITS_1,           // 1 位停止
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,   // 无硬件流控
        .source_clk = UART_SCLK_DEFAULT,          // 使用默认时钟源
    };

    ESP_ERROR_CHECK(uart_param_config(UART_1106_PORT, &uart_config));

    // ---- 2. 配置 TX 引脚 ----
    // 参数: 串口号, TX引脚, RX引脚, RTS引脚, CTS引脚
    // UART_PIN_NO_CHANGE 表示不修改该引脚配置
    ESP_ERROR_CHECK(uart_set_pin(
        UART_1106_PORT,
        UART_1106_TX_PIN,           // TX 引脚
        UART_PIN_NO_CHANGE,         // RX 引脚（本项目不需要）
        UART_PIN_NO_CHANGE,         // RTS 引脚（不需要流控）
        UART_PIN_NO_CHANGE          // CTS 引脚（不需要流控）
    ));

    // ---- 3. 安装 UART 驱动 ----
    // 参数: 串口号, TX缓冲区, RX缓冲区(0=不需要), 队列大小, 队列句柄, 中断标志
    // RX 缓冲区设为 0 因为我们不需要接收
    ESP_ERROR_CHECK(uart_driver_install(
        UART_1106_PORT,
        UART_1106_BUF_SIZE,         // TX 缓冲区大小
        0,                          // RX 缓冲区: 0（不接收）
        0,                          // 事件队列: 0
        NULL,                       // 事件队列句柄: NULL
        0                           // 中断分配标志: 0 (默认)
    ));

    ESP_LOGI(TAG, "UART initialized: port=%d, TX=GPIO%d, baud=%d",
             UART_1106_PORT, UART_1106_TX_PIN, UART_1106_BAUDRATE);
}

/* ============================================================
 * 发送一包肌电数据
 * ============================================================
 *
 * 流程:
 *   1. 调用 proto_pack_emg() 将原始数据打包成协议帧
 *   2. 调用 uart_write_bytes() 通过 UART 发送
 *
 * 参数:
 *   pkt — 从队列中取出的肌电数据包
 *
 * 返回值:
 *   true  = 发送成功
 *   false = 打包或发送失败
 */
static bool send_emg_packet(const emg_packet_t *pkt)
{
    // ---- 1. 准备发送缓冲区 ----
    // 用 static 关键字让缓冲区在整个程序生命周期中存在
    // 避免每次函数调用都在栈上分配 500+ 字节
    static uint8_t tx_buf[TX_BUF_SIZE];

    // ---- 2. 打包成协议帧 ----
    uint16_t frame_len = proto_pack_emg(pkt, tx_buf, sizeof(tx_buf));
    if (frame_len == 0) {
        ESP_LOGE(TAG, "Protocol pack failed! buf_size=%d, data_len=%d",
                 sizeof(tx_buf), pkt->len);
        return false;
    }

    // ---- 3. 通过 UART 发送 ----
    // uart_write_bytes 是非阻塞的，数据放入 TX FIFO 后立即返回
    // 如果 FIFO 满了会短暂阻塞等待（通常不会发生）
    int sent = uart_write_bytes(UART_1106_PORT, (const char *)tx_buf, frame_len);
    if (sent != frame_len) {
        ESP_LOGE(TAG, "UART send incomplete: %d/%d bytes", sent, frame_len);
        return false;
    }

    // ---- 4. 日志 ----
    ESP_LOGD(TAG, "Sent EMG frame: %d bytes payload → %d bytes on wire",
             pkt->len, frame_len);

    return true;
}

/* ============================================================
 * UART 发送任务 — FreeRTOS 任务入口
 * ============================================================
 *
 * 任务生命周期:
 *   - 启动后一直运行，永不退出
 *   - 没有数据时阻塞在 xQueueReceive()，不消耗 CPU
 *   - 有数据时取出 → 打包 → 发送 → 继续等待
 *
 * 为什么不用 for(;;) { ... } 而用 while(1) { ... } ?
 *   语义相同，但 while(1) 更直观地表达 "无限循环"
 */
void uart_tx_task(void *arg)
{
    ESP_LOGI(TAG, "UART TX task started (priority=%d)", UART_TASK_PRIORITY);

    // ---- 1. 初始化 UART 硬件 ----
    uart_init();

    // ---- 2. 分配数据包接收变量 ----
    emg_packet_t pkt;

    // ---- 3. 主循环 ----
    ESP_LOGI(TAG, "Waiting for EMG data from BLE task...");

    while (1) {
        // ---------------------------------------------------------
        // 阻塞等待: 从队列取出数据
        //
        // portMAX_DELAY 表示无限等待，直到有数据为止
        // 在等待期间，FreeRTOS 会把 CPU 让给其他任务
        // 这个任务不消耗任何 CPU 时间
        //
        // 如果采集端停止发送数据，这个任务就会一直等在这里
        // ---------------------------------------------------------
        if (xQueueReceive(emg_data_q, &pkt, portMAX_DELAY) == pdTRUE) {

            // ---- 收到数据，打包并发送 ----
            // 如果发送成功，打印简洁的日志
            if (send_emg_packet(&pkt)) {
                // 数据发送成功（INFO 级别，生产环境可改为 DEBUG）
            }

            // ---- 检查队列是否堆积 ----
            // 如果队列中还有超过 2 包数据在等待
            // 说明 UART 发送速度可能跟不上 BLE 接收速度
            // 需要关注，但不做额外处理（队列机制会自动丢弃最旧的）
            UBaseType_t waiting = uxQueueMessagesWaiting(emg_data_q);
            if (waiting > 2) {
                ESP_LOGW(TAG, "Queue backlog: %d packets waiting", waiting);
            }
        }
    }
}
