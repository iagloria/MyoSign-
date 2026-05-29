/** power.h — 电池电压采样 + 充电状态 */
#ifndef MYOSIGN_POWER_H
#define MYOSIGN_POWER_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    volatile bool running;
} power_ctx_t;

typedef struct {
    int  voltage_mv;
    int  percent;          // 0..100
    bool charging;
} power_state_t;

bool power_start(power_ctx_t *ctx);
void power_stop (power_ctx_t *ctx);

#endif
