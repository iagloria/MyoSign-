/**
 * protocol.h — Core1106 端协议解帧
 * 必须与 ESP32-S3 端帧结构完全一致：
 *   0xAA | CMD(1B) | LEN(2B 大端) | DATA(N) | CRC8 | 0x55
 */
#ifndef MYOSIGN_PROTOCOL_H
#define MYOSIGN_PROTOCOL_H

#include "config.h"

typedef struct {
    uint8_t  cmd;
    uint16_t len;
    uint8_t  data[PROTO_MAX_PAYLOAD];
} proto_msg_t;

uint8_t proto_crc8(const uint8_t *data, uint16_t len);

/**
 * 增量式解析器。把任意长度的字节流喂进来，解出完整帧时返回 true 并填 out。
 * 内部状态由 proto_parser_t 维护。
 */
typedef enum {
    PS_WAIT_HEAD = 0,
    PS_CMD,
    PS_LEN_HI,
    PS_LEN_LO,
    PS_DATA,
    PS_CRC,
    PS_TAIL,
} proto_state_t;

typedef struct {
    proto_state_t state;
    proto_msg_t   buf;
    uint16_t      data_idx;
    uint64_t      total_frames;
    uint64_t      crc_errors;
    uint64_t      head_lost;
} proto_parser_t;

void proto_parser_init(proto_parser_t *p);

/**
 * 喂一个字节。如果刚好凑成一帧合法数据，把 *out 填为完整帧并返回 true。
 * 失败/未完成时返回 false。
 */
bool proto_parser_feed(proto_parser_t *p, uint8_t byte, proto_msg_t *out);

#endif
