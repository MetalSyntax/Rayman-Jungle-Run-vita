/*
 * Copyright (C) 2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  patch.c
 * @brief Patching some of the .so internal functions or bridging them to native
 *        for better compatibility.
 */

#include "utils/logger.h"
#include <stdio.h>
#include <kubridge.h>
#include <so_util/so_util.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

extern so_module so_mod;
extern so_module so_mod_fmodex;

/**
 * Hook for FMOD's internal stdio read function at 0xab4f0.
 * In libfmodex.so, this function reads [handle, #12] directly to check for
 * EOF (bit 0x20) and ERROR (bit 0x40), assuming Bionic libc's struct __sFILE.
 * On PS Vita with SceLibc, handle is a Sony SceLibc FILE* where offset 12 has
 * internal SceLibc fields. If bit 0x40 is set, FMOD thinks an error occurred
 * and returns 19 (FMOD_ERR_FILE_BAD), completely breaking all Vorbis/OGG
 * reading with FMOD error 25 ("Unsupported format").
 * By replacing it with standard feof/ferror checks, FMOD reads .ogg streams cleanly.
 */
static int fmod_file_stdio_read_hook(FILE *handle, void *buffer, unsigned int sizebytes,
                                     unsigned int *bytesread, void *userdata) {
    (void)userdata;
    if (!handle)
        return 37; // FMOD_ERR_INVALID_PARAM

#ifdef USE_SCELIBC_IO
    size_t r = sceLibcBridge_fread(buffer, 1, sizebytes, handle);
    if (bytesread)
        *bytesread = (unsigned int)r;

    if (r < sizebytes) {
        if (sceLibcBridge_feof(handle))
            return 22; // FMOD_ERR_FILE_EOF
        if (sceLibcBridge_ferror(handle))
            return 19; // FMOD_ERR_FILE_BAD
    }
#else
    size_t r = fread(buffer, 1, sizebytes, handle);
    if (bytesread)
        *bytesread = (unsigned int)r;

    if (r < sizebytes) {
        if (feof(handle))
            return 22; // FMOD_ERR_FILE_EOF
        if (ferror(handle))
            return 19; // FMOD_ERR_FILE_BAD
    }
#endif

    return 0; // FMOD_OK
}

// Dummy function returning 0 for any call on the dummy logger instance
static int dummy_logger_noop(void) {
    return 0;
}

// Dummy GameBehaviourLogger vtable and instance to satisfy any calls to getSingleton()
static uintptr_t dummy_vtable[128];
static uintptr_t dummy_logger_instance[16] = {0};

void so_patch(void) {
    // Rayman Jungle Run - nativeRun calls GameBehaviourLogger::getSingleton()
    // at 0x255b6c without checking if the singleton is initialized, causing
    // a Data Abort (null pointer dereference) on the very first frame.
    // Patch: replace 'beq 255c14' at 0x255b68 with unconditional branch 'b 255c14' (0xea000029).
    uint32_t b_255c14 = 0xea000029;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x255b68), &b_255c14, sizeof(uint32_t));
    l_info("Patched nativeRun at 0x255b68: bypassed uninitialized GameBehaviourLogger.");

    // OptionsMenu::loadVolumeValues calls GameBehaviourLogger::getSingleton()->logEvent(2, ...)
    // at 0x4405d0. If singleton is NULL, this causes a crash (PC = 0x4405d8).
    // Patch: replace 'bl 0x26cf38' at 0x4405d0 with branch over the logger call to 0x4405e4 ('b 0x4405e4' = 0xea000003).
    uint32_t b_4405e4 = 0xea000003;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x4405d0), &b_4405e4, sizeof(uint32_t));
    l_info("Patched OptionsMenu::loadVolumeValues at 0x4405d0: bypassed GameBehaviourLogger.");

    // Populate GameBehaviourLogger::singleton (0x5dae78) with a dummy object
    // pointing to dummy_vtable filled with dummy_logger_noop.
    // This prevents prefetch abort crashes (e.g. at 0x2a95ac registerDeviceForUpnEv)
    // when the engine makes virtual calls on the logger singleton.
    for (int i = 0; i < 128; ++i) {
        dummy_vtable[i] = (uintptr_t)&dummy_logger_noop;
    }
    dummy_logger_instance[0] = (uintptr_t)dummy_vtable;
    uintptr_t inst_ptr = (uintptr_t)dummy_logger_instance;
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x5dae78), &inst_ptr, sizeof(uintptr_t));
    l_info("Initialized GameBehaviourLogger::singleton at 0x5dae78 with active no-op vtable.");

    // Hook FMOD's internal stdio read (0xab4f0) to use SceLibc feof/ferror instead
    // of inlined Bionic struct __sFILE flags offset [handle, #12].
    hook_arm(so_mod_fmodex.text_base + 0xab4f0, (uintptr_t)&fmod_file_stdio_read_hook);
    l_info("Hooked FMOD stdio read (0xab4f0) for SceLibc compatibility.");

    // Patch PlayerData::isUnlockAllPurchased() at 0x2e3704:
    // Make it unconditionally return 1 (true) so the full game, all worlds, and levels
    // are permanently unlocked, eliminating in-app purchase prompts for nonexistent servers.
    // 'mov r0, #1' = 0xe3a00001, 'bx lr' = 0xe12fff1e
    uint32_t patch_unlock[2] = { 0xe3a00001, 0xe12fff1e };
    kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2e3704), patch_unlock, sizeof(patch_unlock));
    l_info("Patched PlayerData::isUnlockAllPurchased at 0x2e3704: unlocked full game.");
}


