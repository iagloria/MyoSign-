/** uart_rx.h — 业务串口接收线程 */
#ifndef MYOSIGN_UART_RX_H
#define MYOSIGN_UART_RX_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t thread;
    int       fd;
    volatile bool running;
} uart_rx_ctx_t;

bool uart_rx_start(uart_rx_ctx_t *ctx);
void uart_rx_stop (uart_rx_ctx_t *ctx);

#endif
