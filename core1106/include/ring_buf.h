/**
 * ring_buf.h — 单生产者单消费者无锁环形缓冲区（字节流）
 * 用于 UART 字节流缓冲、PCM 音频帧缓冲。
 */
#ifndef MYOSIGN_RING_BUF_H
#define MYOSIGN_RING_BUF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buf;
    size_t   capacity;
    volatile size_t head;   // 写指针（生产者）
    volatile size_t tail;   // 读指针（消费者）
} ring_buf_t;

bool   ring_init(ring_buf_t *r, size_t capacity);
void   ring_free(ring_buf_t *r);
size_t ring_write(ring_buf_t *r, const uint8_t *data, size_t n);
size_t ring_read (ring_buf_t *r, uint8_t *out, size_t n);
size_t ring_used (const ring_buf_t *r);
size_t ring_free_space(const ring_buf_t *r);

#endif
