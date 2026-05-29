/** lcd.h — SPI LCD 显示驱动（默认 ST7789 240×240） */
#ifndef MYOSIGN_LCD_H
#define MYOSIGN_LCD_H

#include <stdint.h>
#include <stdbool.h>

bool lcd_init(void);
void lcd_close(void);

/** 用纯色清屏 */
void lcd_fill(uint16_t rgb565);

/** 把 RGB565 缓冲推到屏幕某矩形区域 */
void lcd_blit(int x, int y, int w, int h, const uint16_t *pixels);

/** 软件中文渲染（依赖 freetype，未启用时为 stub） */
void lcd_draw_text_utf8(int x, int y, const char *utf8, uint16_t fg, uint16_t bg);

/** 设置背光（0-255） */
void lcd_set_backlight(int level);

#endif
