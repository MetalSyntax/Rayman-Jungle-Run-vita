/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

static int resolve_file_path(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len == 0) return 0;

    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';

    // Normalize duplicate slashes
    char *dup_slash = strstr(out, "//");
    while (dup_slash) {
        memmove(dup_slash, dup_slash + 1, strlen(dup_slash));
        dup_slash = strstr(out, "//");
    }

    struct stat st;
    if (stat(out, &st) == 0) {
        return 1;
    }

    const size_t data_len = strlen(DATA_PATH);
    const char *rel = out;
    if (strncmp(out, DATA_PATH, data_len) == 0) {
        rel = out + data_len;
    } else if (out[0] == '/') {
        return 0;
    }

    // Candidate 1: DATA_PATH + rel
    char cand[PATH_MAX];
    snprintf(cand, sizeof(cand), "%s%s", DATA_PATH, rel);
    if (stat(cand, &st) == 0) {
        strncpy(out, cand, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }

    // Candidate 2: DATA_PATH + files/ + rel
    snprintf(cand, sizeof(cand), "%sfiles/%s", DATA_PATH, rel);
    if (stat(cand, &st) == 0) {
        strncpy(out, cand, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }

    // Candidate 3: DATA_PATH + LocalAssets/ + rel
    snprintf(cand, sizeof(cand), "%sLocalAssets/%s", DATA_PATH, rel);
    if (stat(cand, &st) == 0) {
        strncpy(out, cand, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }

    // Candidate 4: DATA_PATH + assets/ + rel
    snprintf(cand, sizeof(cand), "%sassets/%s", DATA_PATH, rel);
    if (stat(cand, &st) == 0) {
        strncpy(out, cand, out_len - 1);
        out[out_len - 1] = '\0';
        return 1;
    }

    return 0;
}

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

    char resolved[PATH_MAX];
    if (resolve_file_path(filename, resolved, sizeof(resolved))) {
#ifdef USE_SCELIBC_IO
        FILE *ret = sceLibcBridge_fopen(resolved, mode);
#else
        FILE *ret = fopen(resolved, mode);
#endif
        l_debug("fopen(%s -> %s, %s): %p", filename, resolved, mode, ret);
        return ret;
    }

    // Fallback: try raw filename with DATA_PATH prefix if not absolute
    strncpy(resolved, filename, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';
    char *dup_slash = strstr(resolved, "//");
    while (dup_slash) {
        memmove(dup_slash, dup_slash + 1, strlen(dup_slash));
        dup_slash = strstr(resolved, "//");
    }

#ifdef USE_SCELIBC_IO
    FILE *ret = sceLibcBridge_fopen(resolved, mode);
#else
    FILE *ret = fopen(resolved, mode);
#endif

    if (!ret && strncmp(resolved, DATA_PATH, strlen(DATA_PATH)) != 0 && resolved[0] != '/') {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", DATA_PATH, resolved);
#ifdef USE_SCELIBC_IO
        ret = sceLibcBridge_fopen(full, mode);
#else
        ret = fopen(full, mode);
#endif
    }

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);

    char resolved[PATH_MAX];
    if (resolve_file_path(path, resolved, sizeof(resolved))) {
        int ret = open(resolved, oflag, mode);
        l_debug("open(%s -> %s, %x): %i", path, resolved, oflag, ret);
        return ret;
    }

    strncpy(resolved, path, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';
    char *dup_slash = strstr(resolved, "//");
    while (dup_slash) {
        memmove(dup_slash, dup_slash + 1, strlen(dup_slash));
        dup_slash = strstr(resolved, "//");
    }

    int ret = open(resolved, oflag, mode);
    if (ret < 0 && strncmp(resolved, DATA_PATH, strlen(DATA_PATH)) != 0 && resolved[0] != '/') {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", DATA_PATH, resolved);
        ret = open(full, oflag, mode);
    }

    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    char resolved[PATH_MAX];
    struct stat st;
    if (resolve_file_path(path, resolved, sizeof(resolved))) {
        int res = stat(resolved, &st);
        if (res == 0) {
            stat_newlib_to_bionic(&st, buf);
            l_debug("stat(%s -> %s): %i", path, resolved, res);
            return 0;
        }
    }

    strncpy(resolved, path, sizeof(resolved) - 1);
    resolved[sizeof(resolved) - 1] = '\0';
    char *dup_slash = strstr(resolved, "//");
    while (dup_slash) {
        memmove(dup_slash, dup_slash + 1, strlen(dup_slash));
        dup_slash = strstr(resolved, "//");
    }

    int res = stat(resolved, &st);
    if (res != 0 && strncmp(resolved, DATA_PATH, strlen(DATA_PATH)) != 0 && resolved[0] != '/') {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s%s", DATA_PATH, resolved);
        res = stat(full, &st);
    }

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

// AndroidPersistentMgr::readData gates every save load on access(path, F_OK).
// Raw newlib access() skips the "//" normalization fopen_soloader does, so it
// failed and the engine silently reset progress even though slot1.dat existed.
// ponytail: mode is ignored (only F_OK is used; Vita has no per-file perms).
int access_soloader(const char * path, int mode) {
    (void)mode;
    stat64_bionic st;
    int res = stat_soloader(path, &st) == 0 ? 0 : -1;
    if (res != 0)
        l_debug("access(%s): not found", path);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
