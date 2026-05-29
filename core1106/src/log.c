#include "log.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static FILE *g_fp = NULL;
static int   g_level = LOG_LEVEL_INFO;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static const char *level_str(int lv) {
    switch (lv) {
        case LOG_LEVEL_DEBUG: return "D";
        case LOG_LEVEL_INFO:  return "I";
        case LOG_LEVEL_WARN:  return "W";
        case LOG_LEVEL_ERROR: return "E";
    }
    return "?";
}

void log_init(const char *file_path, int level) {
    g_level = level;
    if (file_path && *file_path) {
        g_fp = fopen(file_path, "a");
    }
}

void log_close(void) {
    pthread_mutex_lock(&g_mu);
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    pthread_mutex_unlock(&g_mu);
}

void log_write(int level, const char *tag, const char *fmt, ...) {
    if (level < g_level) return;

    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm; localtime_r(&ts.tv_sec, &tm);
    char tbuf[32];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d.%03ld",
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);

    char msg[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_mu);
    fprintf(stderr, "[%s][%s][%s] %s\n", tbuf, level_str(level), tag, msg);
    if (g_fp) {
        fprintf(g_fp, "[%s][%s][%s] %s\n", tbuf, level_str(level), tag, msg);
        fflush(g_fp);
    }
    pthread_mutex_unlock(&g_mu);
}
