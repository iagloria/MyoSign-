#include "ring_buf.h"
#include <stdlib.h>
#include <string.h>

bool ring_init(ring_buf_t *r, size_t capacity) {
    r->buf = (uint8_t *)malloc(capacity);
    if (!r->buf) return false;
    r->capacity = capacity;
    r->head = r->tail = 0;
    return true;
}

void ring_free(ring_buf_t *r) {
    free(r->buf); r->buf = NULL; r->capacity = 0;
}

size_t ring_used(const ring_buf_t *r) {
    size_t h = r->head, t = r->tail;
    return (h >= t) ? (h - t) : (r->capacity - t + h);
}

size_t ring_free_space(const ring_buf_t *r) {
    return r->capacity - 1 - ring_used(r);
}

size_t ring_write(ring_buf_t *r, const uint8_t *data, size_t n) {
    size_t space = ring_free_space(r);
    if (n > space) n = space;
    size_t first = r->capacity - r->head;
    if (n <= first) {
        memcpy(r->buf + r->head, data, n);
    } else {
        memcpy(r->buf + r->head, data, first);
        memcpy(r->buf, data + first, n - first);
    }
    r->head = (r->head + n) % r->capacity;
    return n;
}

size_t ring_read(ring_buf_t *r, uint8_t *out, size_t n) {
    size_t used = ring_used(r);
    if (n > used) n = used;
    size_t first = r->capacity - r->tail;
    if (n <= first) {
        memcpy(out, r->buf + r->tail, n);
    } else {
        memcpy(out, r->buf + r->tail, first);
        memcpy(out + first, r->buf, n - first);
    }
    r->tail = (r->tail + n) % r->capacity;
    return n;
}
