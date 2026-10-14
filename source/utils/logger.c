/*
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/logger.h"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stdbool.h>
#include <stdatomic.h>

#define COLOR_RED    "\x1B[38;5;196m"
#define COLOR_PINK   "\x1B[38;5;212m"
#define COLOR_ORANGE "\x1B[38;5;202m"
#define COLOR_BLUE   "\x1B[38;5;32m"
#define COLOR_GREEN  "\x1B[32m"
#define COLOR_CYAN   "\x1B[36m"

#define COLOR_END    "\033[0m"

static SceKernelLwMutexWork _log_mutex;
static atomic_bool _log_mutex_ready = ATOMIC_VAR_INIT(false);

// Buffer A is used to adjust the format string.
static char buffer_a[2048];
// Buffer B is used to compile the final log using the updated format string.
static char buffer_b[2048];

/*
 * File log, per the port's log standard (PORTING_PLAN.md section 6):
 * <DATA_PATH>logs/raymanjunglerun_NNN.log, NNN = 001..999 taken from
 * logs/next.idx. The fd stays open for the whole run; sceIoWrite is
 * unbuffered, so the file is complete up to the last line even after a crash.
 */
#define LOG_SLUG "raymanjunglerun"
#define LOG_INDEX_MIN 1
#define LOG_INDEX_MAX 999
static SceUID _log_fd = -1;
static bool _log_file_tried = false;

static void _log_file_open(void) {
    _log_file_tried = true;
    sceIoMkdir(DATA_PATH "logs", 0777);

    int idx = LOG_INDEX_MIN;
    char buf[16] = {0};
    SceUID f = sceIoOpen(DATA_PATH "logs/next.idx", SCE_O_RDONLY, 0);
    if (f >= 0) {
        int n = sceIoRead(f, buf, sizeof(buf) - 1);
        sceIoClose(f);
        if (n > 0) {
            int v = 0;
            for (int i = 0; i < n && buf[i] >= '0' && buf[i] <= '9'; i++)
                v = v * 10 + (buf[i] - '0');
            idx = v;
        }
    }
    if (idx < LOG_INDEX_MIN || idx > LOG_INDEX_MAX)
        idx = LOG_INDEX_MIN;

    int next = idx >= LOG_INDEX_MAX ? LOG_INDEX_MIN : idx + 1;
    f = sceIoOpen(DATA_PATH "logs/next.idx", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (f >= 0) {
        int n = sceClibSnprintf(buf, sizeof(buf), "%d\n", next);
        sceIoWrite(f, buf, n);
        sceIoClose(f);
    }

    char path[128];
    sceClibSnprintf(path, sizeof(path), DATA_PATH "logs/" LOG_SLUG "_%03d.log", idx);
    _log_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
}

static void _log_file_write(const char *line) {
#ifndef ENABLE_FILE_LOG
    (void)line;
    return;
#endif
    if (!_log_file_tried)
        _log_file_open();
    if (_log_fd < 0)
        return;

    // Strip ANSI color sequences for the file copy.
    static char clean[2048];
    int j = 0;
    for (int i = 0; line[i] && j < (int)sizeof(clean) - 1; i++) {
        if (line[i] == '\x1B') {
            while (line[i] && line[i] != 'm') i++;
            if (!line[i]) break;
            continue;
        }
        clean[j++] = line[i];
    }
    sceIoWrite(_log_fd, clean, j);
}

void _log_print(int t, const char* fmt, ...) {
    if (!atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        int ret = sceKernelCreateLwMutex(&_log_mutex, "log_lock", 0, 0, NULL);
        if (ret < 0) {
            sceClibPrintf("Error: failed to create log mutex: 0x%x\n", ret);
            return;
        }
        atomic_store_explicit(&_log_mutex_ready, true, memory_order_relaxed);
    }
    sceKernelLockLwMutex(&_log_mutex, 1, NULL);

    switch (t) {
        case LT_DEBUG:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s• debug%s    %s\n",
                            COLOR_PINK, COLOR_END, fmt); break;
        case LT_INFO:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %sℹ info%s     %s\n",
                            COLOR_BLUE, COLOR_END, fmt); break;
        case LT_WARN:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⚠ warning%s  %s\n",
                            COLOR_ORANGE, COLOR_END, fmt); break;
        case LT_ERROR:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s⨯ error%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_FATAL:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! fatal%s    %s\n",
                            COLOR_RED, COLOR_END, fmt); break;
        case LT_SUCCESS:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s! success%s  %s\n",
                            COLOR_GREEN, COLOR_END, fmt); break;
        case LT_WAIT:
            sceClibSnprintf(buffer_a, sizeof(buffer_a), " %s… waiting%s  %s\n",
                            COLOR_CYAN, COLOR_END, fmt); break;
        default:
            if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
                sceKernelUnlockLwMutex(&_log_mutex, 1);
            }
            return;
    }

    va_list list;
    va_start(list, fmt);
    sceClibVsnprintf(buffer_b, sizeof(buffer_b), buffer_a, list);
    va_end(list);
    sceClibPrintf(buffer_b);
    if (t != LT_DEBUG) {
        _log_file_write(buffer_b);
    }

    if (atomic_load_explicit(&_log_mutex_ready, memory_order_relaxed)) {
        sceKernelUnlockLwMutex(&_log_mutex, 1);
    }
}
