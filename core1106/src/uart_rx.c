#include "uart_rx.h"
#include "protocol.h"
#include "event_bus.h"
#include "log.h"
#include "config.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>

static const char *TAG = "UART_RX";

static int open_serial(const char *dev, int baud) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { LOGE(TAG, "open %s: %s", dev, strerror(errno)); return -1; }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) { close(fd); return -1; }
    cfmakeraw(&tio);

    speed_t s = B115200;
    if (baud == 230400) s = B230400;
    else if (baud == 460800) s = B460800;
    else if (baud == 921600) s = B921600;
    cfsetispeed(&tio, s);
    cfsetospeed(&tio, s);

    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CSIZE;  tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB; tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &tio) != 0) { close(fd); return -1; }
    return fd;
}

static void *uart_rx_thread(void *arg) {
    uart_rx_ctx_t *ctx = (uart_rx_ctx_t *)arg;
    LOGI(TAG, "uart_rx thread on %s @ %d", UART_DEV_BIZ, UART_BAUDRATE);

    proto_parser_t parser;
    proto_parser_init(&parser);

    uint8_t buf[1024];
    proto_msg_t msg;

    while (ctx->running) {
        struct pollfd pfd = { .fd = ctx->fd, .events = POLLIN };
        int r = poll(&pfd, 1, 200);
        if (r < 0) { if (errno == EINTR) continue; LOGE(TAG, "poll: %s", strerror(errno)); break; }
        if (r == 0) continue;

        ssize_t n = read(ctx->fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            LOGE(TAG, "read: %s", strerror(errno)); break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (proto_parser_feed(&parser, buf[i], &msg)) {
                /* 完整帧 → 投递到事件总线 */
                if (msg.cmd == PROTO_CMD_EMG_DATA) {
                    event_bus_publish(EV_EMG_FRAME, msg.data, msg.len);
                    LOGD(TAG, "EMG frame len=%u total=%llu",
                         msg.len, (unsigned long long)parser.total_frames);
                } else {
                    LOGD(TAG, "frame cmd=0x%02X len=%u", msg.cmd, msg.len);
                }
            }
        }
    }
    LOGI(TAG, "uart_rx thread exit");
    return NULL;
}

bool uart_rx_start(uart_rx_ctx_t *ctx) {
    ctx->fd = open_serial(UART_DEV_BIZ, UART_BAUDRATE);
    if (ctx->fd < 0) return false;
    ctx->running = true;
    if (pthread_create(&ctx->thread, NULL, uart_rx_thread, ctx) != 0) {
        close(ctx->fd); ctx->fd = -1; ctx->running = false;
        return false;
    }
    return true;
}

void uart_rx_stop(uart_rx_ctx_t *ctx) {
    ctx->running = false;
    pthread_join(ctx->thread, NULL);
    if (ctx->fd >= 0) close(ctx->fd);
    ctx->fd = -1;
}
