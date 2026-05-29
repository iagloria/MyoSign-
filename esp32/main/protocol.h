/**
 * ============================================================
 * protocol.h — ESP32 ↔ Core1106 串口通信协议头文件
 * ============================================================
 *
 * 协议设计原则:
 *   1. 简单可靠 — 帧头帧尾包夹，CRC8 校验
 *   2. 可变长度 — LEN 字段告诉 Core1106 载荷多长
 *   3. 单向通信 — 只有 ESP32 → Core1106 方向
 *
 * 帧结构:
 *   | 0xAA |  CMD  |  LEN  |  DATA...  | CRC8 | 0x55 |
 *     帧头   1字节   2字节   LEN 字节    1字节   帧尾
 *
 * CRC8 计算范围: CMD + LEN + DATA（不含帧头帧尾）
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

/* ============================================================
 * 协议常量
 * ============================================================ */

// ---- 帧定界符 ----
#define PROTO_FRAME_HEAD        0xAA    // 帧头: 一帧开始
#define PROTO_FRAME_TAIL        0x55    // 帧尾: 一帧结束

// ---- 命令类型 ----
#define PROTO_CMD_EMG_DATA      0x01    // 肌电原始数据
#define PROTO_CMD_CONTROL       0x02    // 保留: 控制命令
#define PROTO_CMD_STATUS        0x03    // 保留: 状态查询

// ---- 帧开销（帧头+CMD+LEN+CRC+帧尾 = 6字节）----
#define PROTO_FRAME_OVERHEAD    6

/* ============================================================
 * 帧结构体
 * ============================================================
 *
 * 一帧完整协议的 C 语言表示
 * 注意: data 是柔性数组或外部缓冲区，这里用指针表示
 */

typedef struct {
    uint8_t  head;              // 帧头 0xAA
    uint8_t  cmd;               // 命令类型
    uint16_t len;               // 载荷长度（大端序）
    uint8_t *data;              // 载荷数据指针（不拥有内存）
    uint8_t  crc;               // CRC8 校验值
    uint8_t  tail;              // 帧尾 0x55
} proto_frame_t;

/* ============================================================
 * API 函数声明
 * ============================================================ */

/**
 * @brief  计算 CRC8 校验值
 * @param  data  数据指针
 * @param  len   数据长度（字节）
 * @return CRC8 值（1字节）
 *
 * 算法: CRC-8-ATM (多项式 0x07, 初始值 0x00, 不反转)
 * 多项式: x^8 + x^2 + x + 1
 */
uint8_t proto_crc8(const uint8_t *data, uint16_t len);

/**
 * @brief  将肌电数据包打包成协议帧，写入缓冲区
 * @param  pkt    肌电数据包（来自 BLE 接收的数据）
 * @param  frame  输出缓冲区（存放打包后的完整帧）
 * @param  buf_size 输出缓冲区大小（必须 ≥ pkt->len + 6）
 * @return 打包后的帧总长度（字节），0 表示缓冲区不够
 *
 * 调用示例:
 *   uint8_t frame[EMG_PACKET_MAX_LEN + PROTO_FRAME_OVERHEAD];
 *   uint16_t len = proto_pack_emg(&pkt, frame, sizeof(frame));
 *   uart_write_bytes(UART_PORT, frame, len);
 */
uint16_t proto_pack_emg(const emg_packet_t *pkt, uint8_t *frame, uint16_t buf_size);

/**
 * @brief  验证协议帧的完整性（Core1106 端使用，仅作参考）
 * @param  frame  帧数据缓冲区
 * @param  len    帧总长度
 * @return true=校验通过, false=校验失败
 *
 * 校验内容:
 *   1. 帧头 == 0xAA
 *   2. 帧尾 == 0x55
 *   3. CRC8 匹配
 *   4. 长度字段与实际数据一致
 */
bool proto_validate(const uint8_t *frame, uint16_t len);

#endif /* PROTOCOL_H */
