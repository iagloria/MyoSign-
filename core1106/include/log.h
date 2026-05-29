/**
 * log.h — 简易日志（线程安全，分级别，写 stderr + 可选文件）
 */
#ifndef MYOSIGN_LOG_H
#define MYOSIGN_LOG_H

#include <stdio.h>

enum { LOG_LEVEL_DEBUG = 0, LOG_LEVEL_INFO, LOG_LEVEL_WARN, LOG_LEVEL_ERROR };

void log_init(const char *file_path, int level);
void log_close(void);
void log_write(int level, const char *tag, const char *fmt, ...);

#define LOGD(tag, ...) log_write(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_write(LOG_LEVEL_INFO,  tag, __VA_ARGS__)
#define LOGW(tag, ...) log_write(LOG_LEVEL_WARN,  tag, __VA_ARGS__)
#define LOGE(tag, ...) log_write(LOG_LEVEL_ERROR, tag, __VA_ARGS__)

#endif
