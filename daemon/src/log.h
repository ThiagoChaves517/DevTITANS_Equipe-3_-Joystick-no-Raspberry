// Log em stderr e, no Android, também no logcat (tag SnesDaemon).
#pragma once
#include <cstdio>
#include <ctime>
#ifdef __ANDROID__
#include <android/log.h>
#define SNES_ALOG(prio, ...) __android_log_print(prio, "SnesDaemon", __VA_ARGS__)
#else
#define SNES_ALOG(prio, ...) ((void)0)
#endif

#define SNES_LOG(level, prio, fmt, ...)                                                     \
    do {                                                                                    \
        timespec ts_;                                                                       \
        clock_gettime(CLOCK_MONOTONIC, &ts_);                                               \
        std::fprintf(stderr, "[%5ld.%03ld] %s " fmt "\n", long(ts_.tv_sec), ts_.tv_nsec / 1000000L, \
                     level, ##__VA_ARGS__);                                                 \
        SNES_ALOG(prio, fmt, ##__VA_ARGS__);                                                \
    } while (0)

#define LOGI(fmt, ...) SNES_LOG("I", 4, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) SNES_LOG("W", 5, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) SNES_LOG("E", 6, fmt, ##__VA_ARGS__)
