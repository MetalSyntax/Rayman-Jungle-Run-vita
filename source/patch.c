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
#include <kubridge.h>
#include <so_util/so_util.h>

extern so_module so_mod;

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
}

