/**
 * ============================================================
 * uart_tx.h — UART 串口发送模块头文件
 * ============================================================
 *
 * 本模块负责:
 *   1. 初始化 UART 外设（波特率、引脚、缓冲区）
 *   2. 从 FreeRTOS 队列 emg_data_q 中取出肌电数据
 *   3. 调用 protocol.c 的函数打包成协议帧
 *   4. 通过 UART 发送给 Core1106
 *
 * 通信方向: ESP32-S3 → Core1106 (单向，只发不收)
 *
 * 为什么用 TX 命名？
 *   虽然 UART 本身是双向的，但本项目只使用 TX（发送）方向
 *   Core1106 的 RX 对应 ESP32 的 TX
 *
 * 波特率选择: 115200 bps
 *   为什么不用更高波特率?
 *     - 100K 采样率 × 2 通道 × 2 字节 = 400KB/s → 3.2Mbps 最低
 *     - 但实际上 BLE 传输才是瓶颈，UART 115200 足够
 *     - 更高波特率对 PCB 走线要求高，容易出误码
 *
 * 发送延迟: 512 字节 / 115200 bps × 10bit/byte ≈ 44ms
 */

#ifndef UART_TX_H
#define UART_TX_H

#include "config.h"

/**
 * @brief  UART 发送任务入口函数
 * @param  arg  任务参数（未使用，传 NULL）
 *
 * 这个任务做以下事情:
 *   1. 初始化 UART 硬件
 *   2. 阻塞等待 emg_data_q 队列中有数据
 *   3. 取出一包数据 → 打包协议帧 → UART 发送
 *   4. 循环执行步骤 2-3
 *
 * 如果队列长期为空，任务会阻塞在 xQueueReceive() 上
 * 不消耗 CPU 时间（FreeRTOS 会把 CPU 让给其他任务）
 */
void uart_tx_task(void *arg);

#endif /* UART_TX_H */
