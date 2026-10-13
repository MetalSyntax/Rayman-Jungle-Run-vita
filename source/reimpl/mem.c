/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/mem.h"
#include "utils/logger.h"

#include <string.h>
#include <malloc.h>
#include <psp2/kernel/clib.h>

void *sceClibMemclr(void *dst, size_t len) {
    return sceClibMemset(dst, 0, len);
}

// Zero-filled malloc for the .so modules. Android usually hands back zeroed
// memory for fresh allocations and this engine depends on it: e.g.
// ArcadeMainMenu's ctor leaves +0x64 unset when one store/login flag is on and
// the other off, and its dtor then deletes the garbage (run 007, PC
// ArcadeMainMenu::~ArcadeMainMenu+0x74 with R0=0x61002f00). Every engine
// `operator new` goes Pasta::MemoryMgr::allocate -> malloc import.
void *malloc_soloader(size_t size) {
    return calloc(1, size);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offs) {
    l_warn("mmap(%p, %i, %i, %i, %i, %li)", addr, length, prot, flags, fd, offs);

    if (length <= 0) {
        return MAP_FAILED;
    }
    void* ret= malloc(length);
    memset(ret, 0, length);
    return ret;
}

int munmap(void *addr, size_t length) {
    if (addr) free(addr);
    return 0;
}
