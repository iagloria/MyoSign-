/**
 * ============================================================
 * protocol.c — 通信协议实现
 * ============================================================
 *
 * 本文件实现:
 *   1. CRC8 计算（用于帧校验）
 *   2. 协议帧打包（将原始数据封装为带帧头帧尾的协议帧）
 *   3. 协议帧验证（Core1106 端使用，此处仅作参考）
 *
 * 为什么用 CRC8 而不是简单的校验和？
 *   CRC8 能检测出:
 *     - 所有 1 bit 错误
 *     - 所有奇数个 bit 错误
 *     - 所有长度 ≤ 8 的连续 bit 错误（突发错误）
 *   校验和只能检测出部分错误
 *
 * 计算开销: CRC8 用查表法，非常快，8 字节只需几个 CPU 周期
 */

#include "protocol.h"
#include <string.h>    // memcpy

/* ============================================================
 * CRC8 查找表（预计算，避免每次运行时计算）
 * ============================================================
 *
 * 多项式: 0x07 = x^8 + x^2 + x + 1 (CRC-8-ATM)
 * 初始值: 0x00
 *
 * 使用方法: crc = table[(crc ^ byte) & 0xFF]
 */

static const uint8_t crc8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3,
};

/* ============================================================
 * CRC8 计算（查表法，O(n) 时间）
 * ============================================================ */
uint8_t proto_crc8(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;                     // 初始值 0x00

    while (len--) {
        crc = crc8_table[crc ^ *data++];    // 查表: 当前 CRC XOR 当前字节 → 新 CRC
    }

    return crc;                             // 最终 CRC 值
}

/* ============================================================
 * 打包肌电数据帧
 * ============================================================ */
uint16_t proto_pack_emg(const emg_packet_t *pkt, uint8_t *frame, uint16_t buf_size)
{
    // ---- 1. 检查缓冲区是否足够 ----
    // 帧总长度 = 帧头(1) + CMD(1) + LEN(2) + DATA(pkt->len) + CRC(1) + 帧尾(1)
    uint16_t total_len = PROTO_FRAME_OVERHEAD + pkt->len;

    if (buf_size < total_len) {
        return 0;                           // 缓冲区不够，返回 0
    }

    // ---- 2. 填充帧头 ----
    frame[0] = PROTO_FRAME_HEAD;            // 0xAA

    // ---- 3. 填充命令类型 ----
    frame[1] = PROTO_CMD_EMG_DATA;          // 0x01

    // ---- 4. 填充数据长度（大端序: 高字节在前，低字节在后）----
    // 例: pkt->len = 400 (0x0190)
    //     frame[2] = 0x01  (高字节)
    //     frame[3] = 0x90  (低字节)
    frame[2] = (uint8_t)(pkt->len >> 8);    // 高字节
    frame[3] = (uint8_t)(pkt->len & 0xFF);  // 低字节

    // ---- 5. 拷贝肌电数据到帧中 ----
    // DATA 从 frame[4] 开始
    memcpy(&frame[4], pkt->data, pkt->len);

    // ---- 6. 计算 CRC8 ----
    // 校验范围: 从 CMD(下标1) 到 DATA 末尾(下标 4+pkt->len-1)
    // 即 CMD + LEN + DATA，共 (1 + 2 + pkt->len) = (pkt->len + 3) 字节
    uint8_t crc = proto_crc8(&frame[1], pkt->len + 3);
    frame[4 + pkt->len] = crc;              // CRC 紧跟在 DATA 之后

    // ---- 7. 填充帧尾 ----
    frame[4 + pkt->len + 1] = PROTO_FRAME_TAIL;  // 0x55

    // ---- 8. 返回完整帧长度 ----
    // 让调用者知道 send 多少字节
    return total_len;
}

/* ============================================================
 * 验证帧完整性（Core1106 端使用）
 * ============================================================ */
bool proto_validate(const uint8_t *frame, uint16_t len)
{
    // ---- 1. 最小长度检查 ----
    // 至少要 6 字节: 帧头+CMD+LEN(2)+CRC+帧尾（DATA 可以为空）
    if (len < PROTO_FRAME_OVERHEAD) {
        return false;
    }

    // ---- 2. 帧头检查 ----
    if (frame[0] != PROTO_FRAME_HEAD) {
        return false;
    }

    // ---- 3. 帧尾检查 ----
    if (frame[len - 1] != PROTO_FRAME_TAIL) {
        return false;
    }

    // ---- 4. 长度字段与实际帧长度一致性检查 ----
    uint16_t data_len = (frame[2] << 8) | frame[3];  // 大端序解码
    if (len != PROTO_FRAME_OVERHEAD + data_len) {
        return false;                       // 帧长度不匹配
    }

    // ---- 5. CRC8 校验 ----
    // 校验范围: CMD(1) + LEN(2) + DATA(data_len) 共 data_len + 3 字节
    uint8_t actual_crc = proto_crc8(&frame[1], data_len + 3);
    uint8_t frame_crc  = frame[4 + data_len];

    if (actual_crc != frame_crc) {
        return false;                       // CRC 不匹配，数据可能损坏
    }

    // ---- 全部通过 ----
    return true;
}
