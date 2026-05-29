/**
 * lcd.c — ST7789 240×240 SPI 屏驱动（占位/可工作版）
 *
 * 设计要点：
 *   - 通过 spidev 用户态写命令/数据（区别由 DC GPIO 控制）
 *   - DC / RES / BLK 通过 /sys/class/gpio 控制（RV1106 通常如此）
 *   - 文本渲染需要 freetype + 思源黑体子集；若未启用则只画矩形/颜色块
 *
 * 仿真模式（MYOSIGN_SIM）下所有函数为空操作。
 */
#include "lcd.h"
#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef MYOSIGN_SIM
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#endif

static const char *TAG = "LCD";
static int g_spi_fd = -1;

#ifndef MYOSIGN_SIM
static int gpio_write(int pin, int val) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    int r = write(fd, val ? "1" : "0", 1);
    close(fd);
    return r < 0 ? -1 : 0;
}

static int gpio_export_out(int pin) {
    char path[64];
    int fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd >= 0) { dprintf(fd, "%d", pin); close(fd); }
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
    fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    if (write(fd, "out", 3) < 0) { close(fd); return -1; }
    close(fd);
    return 0;
}

static int spi_open(const char *dev, uint32_t speed_hz) {
    int fd = open(dev, O_RDWR);
    if (fd < 0) { LOGE(TAG, "open %s: %s", dev, strerror(errno)); return -1; }
    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    ioctl(fd, SPI_IOC_WR_MODE,          &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed_hz);
    return fd;
}

static void spi_xfer(const uint8_t *tx, size_t n) {
    while (n) {
        size_t chunk = n > 4096 ? 4096 : n;
        write(g_spi_fd, tx, chunk);
        tx += chunk; n -= chunk;
    }
}

static void lcd_cmd(uint8_t c) { gpio_write(LCD_GPIO_DC, 0); spi_xfer(&c, 1); }
static void lcd_dat(const uint8_t *d, size_t n) { gpio_write(LCD_GPIO_DC, 1); spi_xfer(d, n); }
static void lcd_dat1(uint8_t b) { lcd_dat(&b, 1); }
#endif

bool lcd_init(void) {
#ifdef MYOSIGN_SIM
    LOGI(TAG, "lcd_init (sim)");
    return true;
#else
    gpio_export_out(LCD_GPIO_DC);
    gpio_export_out(LCD_GPIO_RES);
    gpio_export_out(LCD_GPIO_BLK);

    g_spi_fd = spi_open(LCD_SPI_DEV, LCD_SPI_SPEED_HZ);
    if (g_spi_fd < 0) return false;

    /* 硬复位 */
    gpio_write(LCD_GPIO_RES, 1); usleep(10000);
    gpio_write(LCD_GPIO_RES, 0); usleep(10000);
    gpio_write(LCD_GPIO_RES, 1); usleep(120000);

    /* ST7789 初始化序列 */
    lcd_cmd(0x01); usleep(150000);          /* SW Reset */
    lcd_cmd(0x11); usleep(120000);          /* Sleep Out */
    lcd_cmd(0x3A); lcd_dat1(0x55);          /* COLMOD: RGB565 */
    lcd_cmd(0x36); lcd_dat1(0x00);          /* MADCTL */
    lcd_cmd(0x21);                          /* INVON */
    lcd_cmd(0x13);                          /* Normal Display */
    lcd_cmd(0x29); usleep(20000);           /* Display On */

    lcd_set_backlight(255);
    lcd_fill(0x0000);
    LOGI(TAG, "ST7789 ready %dx%d", LCD_WIDTH, LCD_HEIGHT);
    return true;
#endif
}

void lcd_close(void) {
#ifndef MYOSIGN_SIM
    if (g_spi_fd >= 0) { close(g_spi_fd); g_spi_fd = -1; }
#endif
}

void lcd_set_backlight(int level) {
#ifndef MYOSIGN_SIM
    /* 简化为开关；正式工程接 PWM 子系统 */
    gpio_write(LCD_GPIO_BLK, level > 0 ? 1 : 0);
#else
    (void)level;
#endif
}

static void set_addr_window(int x, int y, int w, int h) {
#ifndef MYOSIGN_SIM
    uint8_t d[4];
    int x2 = x + w - 1, y2 = y + h - 1;
    lcd_cmd(0x2A); d[0]=x>>8; d[1]=x&0xFF; d[2]=x2>>8; d[3]=x2&0xFF; lcd_dat(d,4);
    lcd_cmd(0x2B); d[0]=y>>8; d[1]=y&0xFF; d[2]=y2>>8; d[3]=y2&0xFF; lcd_dat(d,4);
    lcd_cmd(0x2C);
#else
    (void)x;(void)y;(void)w;(void)h;
#endif
}

void lcd_fill(uint16_t color) {
#ifndef MYOSIGN_SIM
    set_addr_window(0, 0, LCD_WIDTH, LCD_HEIGHT);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    uint8_t row[LCD_WIDTH * 2];
    for (int i = 0; i < LCD_WIDTH; ++i) { row[i*2]=hi; row[i*2+1]=lo; }
    gpio_write(LCD_GPIO_DC, 1);
    for (int y = 0; y < LCD_HEIGHT; ++y) spi_xfer(row, sizeof(row));
#else
    (void)color;
#endif
}

void lcd_blit(int x, int y, int w, int h, const uint16_t *px) {
#ifndef MYOSIGN_SIM
    set_addr_window(x, y, w, h);
    gpio_write(LCD_GPIO_DC, 1);
    /* RGB565 大端发送 */
    size_t n = (size_t)w * h;
    uint8_t row[256];
    size_t i = 0;
    while (i < n) {
        size_t chunk = (n - i > sizeof(row)/2) ? sizeof(row)/2 : (n - i);
        for (size_t k = 0; k < chunk; ++k) {
            row[k*2]   = px[i+k] >> 8;
            row[k*2+1] = px[i+k] & 0xFF;
        }
        spi_xfer(row, chunk * 2);
        i += chunk;
    }
#else
    (void)x;(void)y;(void)w;(void)h;(void)px;
#endif
}

/* 中文渲染：仅在接入 freetype 后填充实现；当前打日志方便调试 */
void lcd_draw_text_utf8(int x, int y, const char *utf8, uint16_t fg, uint16_t bg) {
    (void)x;(void)y;(void)fg;(void)bg;
    LOGI(TAG, "draw_text(\"%s\")", utf8 ? utf8 : "");
}
