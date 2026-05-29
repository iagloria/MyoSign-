#include "event_bus.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define QUEUE_DEPTH 32

typedef struct node {
    event_t ev;
    struct node *next;
} node_t;

typedef struct {
    node_t *head, *tail;
    int     count;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
} queue_t;

static queue_t g_q[EV_MAX];
static uint32_t g_seq = 0;
static pthread_mutex_t g_seq_mu = PTHREAD_MUTEX_INITIALIZER;

bool event_bus_init(void) {
    for (int i = 0; i < EV_MAX; ++i) {
        g_q[i].head = g_q[i].tail = NULL;
        g_q[i].count = 0;
        pthread_mutex_init(&g_q[i].mu, NULL);
        pthread_cond_init(&g_q[i].cv, NULL);
    }
    return true;
}

void event_bus_close(void) {
    for (int i = 0; i < EV_MAX; ++i) {
        pthread_mutex_lock(&g_q[i].mu);
        node_t *n = g_q[i].head;
        while (n) {
            node_t *nx = n->next;
            free(n->ev.data);
            free(n);
            n = nx;
        }
        g_q[i].head = g_q[i].tail = NULL;
        g_q[i].count = 0;
        pthread_mutex_unlock(&g_q[i].mu);
    }
}

bool event_bus_publish(event_type_t type, const void *data, size_t len) {
    if (type >= EV_MAX) return false;
    queue_t *q = &g_q[type];

    node_t *n = (node_t *)malloc(sizeof(node_t));
    if (!n) return false;
    n->ev.type = type;
    n->ev.len  = len;
    n->ev.data = NULL;
    n->next    = NULL;

    if (data && len > 0) {
        n->ev.data = malloc(len);
        if (!n->ev.data) { free(n); return false; }
        memcpy(n->ev.data, data, len);
    }

    pthread_mutex_lock(&g_seq_mu);
    n->ev.seq = ++g_seq;
    pthread_mutex_unlock(&g_seq_mu);

    pthread_mutex_lock(&q->mu);
    if (q->count >= QUEUE_DEPTH) {
        // 满了 → 丢最旧
        node_t *old = q->head;
        q->head = old->next;
        if (!q->head) q->tail = NULL;
        q->count--;
        free(old->ev.data);
        free(old);
    }
    if (q->tail) { q->tail->next = n; q->tail = n; }
    else         { q->head = q->tail = n; }
    q->count++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
    return true;
}

bool event_bus_pop(event_type_t type, event_t *out, int timeout_ms) {
    if (type >= EV_MAX || !out) return false;
    queue_t *q = &g_q[type];

    pthread_mutex_lock(&q->mu);
    if (q->count == 0) {
        if (timeout_ms < 0) {
            while (q->count == 0) pthread_cond_wait(&q->cv, &q->mu);
        } else {
            struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
            while (q->count == 0) {
                int rc = pthread_cond_timedwait(&q->cv, &q->mu, &ts);
                if (rc == ETIMEDOUT) { pthread_mutex_unlock(&q->mu); return false; }
            }
        }
    }
    node_t *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    pthread_mutex_unlock(&q->mu);

    *out = n->ev;
    free(n);
    return true;
}
