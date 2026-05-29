#include "power.h"
#include "config.h"
#include "event_bus.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static const char *TAG = "POWER";

static int read_adc_raw(void) {
    FILE *fp = fopen(BAT_ADC_PATH, "r");
    if (!fp) return -1;
    int raw = -1;
    if (fscanf(fp, "%d", &raw) != 1) raw = -1;
    fclose(fp);
    return raw;
}

static int raw_to_mv(int raw) {
    /* RV1106 SARADC: 12 bit, 1.8V 参考 → mV
       实际 BAT 电压 = ADC_mv * 分压比                                 */
    if (raw < 0) return 0;
    float adc_mv = (float)raw * 1800.0f / 4095.0f;
    return (int)(adc_mv * BAT_ADC_DIVIDER);
}

static int mv_to_percent(int mv) {
    if (mv >= BAT_ADC_FULL_MV)  return 100;
    if (mv <= BAT_ADC_EMPTY_MV) return 0;
    return (mv - BAT_ADC_EMPTY_MV) * 100 / (BAT_ADC_FULL_MV - BAT_ADC_EMPTY_MV);
}

static void *power_thread(void *arg) {
    power_ctx_t *ctx = (power_ctx_t *)arg;
    LOGI(TAG, "power thread started");
    while (ctx->running) {
        int raw = read_adc_raw();
        int mv  = raw_to_mv(raw);
        power_state_t st = { .voltage_mv = mv, .percent = mv_to_percent(mv), .charging = false };
        event_bus_publish(EV_POWER_UPDATE, &st, sizeof(st));
        LOGD(TAG, "bat=%dmV %d%%", mv, st.percent);
        for (int i = 0; i < 30 && ctx->running; ++i) sleep(1);
    }
    LOGI(TAG, "power thread exit");
    return NULL;
}

bool power_start(power_ctx_t *ctx) {
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, power_thread, ctx) != 0) { ctx->running = false; return false; }
    return true;
}

void power_stop(power_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
}
