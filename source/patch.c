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

    // Pasta::AndroidFileMgr::load(ulong*, int, FILE*) inlines bionic ferror():
    // 'ldrh r3,[FILE,#12]; tst r3,#0x40; beq ok'. Same SceLibc FILE layout issue as
    // FMOD above: offset 12 isn't bionic _flags, so every save read was dropped (NULL)
    // and progress reset on boot. 0x2a7794 'beq 0x2a77ac' -> 'b 0x2a77ac' (0xea000004).
    // Only caller is AndroidPersistentMgr::readData (slot1.dat, ROselectedSlot, ...).
    {
        uint32_t b_ok = 0xea000004;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2a7794), &b_ok, sizeof(b_ok));
        l_info("Patched AndroidFileMgr::load at 0x2a7794: skip bionic ferror() on SceLibc FILE.");
    }

    // Neutralize Pasta::PlayBillingStore::buyProduct(int,int) at 0x2b0228:
    // first instruction -> 'bx lr' (0xe12fff1e, ARM verified: push {r4-r7,lr} ...).
    // Without a Play Store on Vita the original flow logs buyProduct(0/1,1),
    // resolves product "no", calls Java storeBuyProduct (no-op here), leaves the
    // transaction PENDING forever and spams the dead iTunes URL
    // (itms-appss://buy.itunes.apple.com ... salableAdamId=537931449, see run 011).
    // Early return: tapping a store button does nothing instead of hanging.
    {
        uint32_t bx_lr = 0xe12fff1e;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2b0228), &bx_lr, sizeof(bx_lr));
        l_info("Patched PlayBillingStore::buyProduct at 0x2b0228: store disabled (no-op).");
    }

    // =========================================================================
    // STORE & IN-APP PURCHASE BYPASS
    // =========================================================================
    // Patch Pasta::Store::getIndexOfExistingTransactionForProduct(wchar_t*) at 0x26d2a4:
    // return 0 (valid transaction index >= 0 found).
    // 'mov r0, #0' = 0xe3a00000, 'bx lr' = 0xe12fff1e.
    // This satisfies all engine checks asking if a product is purchased.
    {
        uint32_t patch_store_tx[2] = { 0xe3a00000, 0xe12fff1e };
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x26d2a4), patch_store_tx, sizeof(patch_store_tx));
        l_info("Patched Pasta::Store::getIndexOfExistingTransactionForProduct at 0x26d2a4: transaction index 0.");
    }

    // Patch PlayerData::isUnlockAllPurchased() at 0x2e3704:
    // Make it unconditionally return 1 (true) so the full game, all worlds, and levels
    // are permanently unlocked, eliminating in-app purchase prompts for nonexistent servers.
    {
        uint32_t patch_unlock[2] = { 0xe3a00001, 0xe12fff1e };
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2e3704), patch_unlock, sizeof(patch_unlock));
        l_info("Patched PlayerData::isUnlockAllPurchased at 0x2e3704: unlocked full game.");
    }

    // =========================================================================
    // ALL 70 LEVELS UNLOCK
    // =========================================================================
    // 1. In PlayerData::ResetLevels (0x2e2fd8):
    //    Patch 0x2e3030 from 'movne r1, #1' (0x13a01001) to 'mov r1, #0' (0xe3a01000).
    //    All 70 levels start with isLocked = 0 instead of only level % 10 == 0.
    {
        uint32_t mov_r1_0 = 0xe3a01000;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2e3030), &mov_r1_0, sizeof(mov_r1_0));
        l_info("Patched PlayerData::ResetLevels at 0x2e3030: default all 70 levels unlocked.");
    }

    // 2. In PlayerData::LoadInfos (0x2e3074):
    //    If no save file exists, branch at 0x2e30b8 was 'b 0x2e31c8' (skipping unlock).
    //    Patch 0x2e30b8 to 'b 0x2e31c0' (0xea000040) so it flows into our UnlockAll call.
    //    At 0x2e31c0: replace 'beq 0x2e31c8' (0x0a000000) with 'nop' (0xe1a00000).
    //    At 0x2e31c4: replace 'bl SaveInfos' (0xebffff40) with 'bl PlayerData::UnlockAll' (0xeb000006).
    //    Whenever LoadInfos runs, it unconditionally calls PlayerData::UnlockAll (0x2e31e4),
    //    forcing all 70 levels to isLocked = 0 in memory regardless of what is on disk.
    {
        uint32_t b_2e31c0 = 0xea000040;
        uint32_t nop = 0xe1a00000;
        uint32_t bl_unlockall = 0xeb000006;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2e30b8), &b_2e31c0, sizeof(b_2e31c0));
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2e31c0), &nop, sizeof(nop));
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x2e31c4), &bl_unlockall, sizeof(bl_unlockall));
        l_info("Patched PlayerData::LoadInfos at 0x2e31c0-0x2e31c4: auto-calls PlayerData::UnlockAll.");
    }

    // NOTE: We deliberately do NOT pre-seed PlayerData::is_unlock_all_purchased (0x5ee1ae) = 1.
    // Leaving it 0 allows LevelsMenu::execute and ManyWorldsMenu::execute to run their internal
    // unlock loop, mark is_unlock_all_purchased = 1, and save the unlocked state to disk.

    // =========================================================================
    // HEROES / SKINS DLC UNLOCK
    // =========================================================================
    // Patch HeroesSelectionMenu::DLCDownloaded() at 0x438714 -> return 1.
    // Patch HeroesSelectionMenu::DLCBoughtLocal() at 0x4387dc -> return 1.
    // Patch HeroesSelectionMenu::DLCBought() at 0x438928 -> return 1.
    {
        uint32_t ret_true[2] = { 0xe3a00001, 0xe12fff1e };
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x438714), ret_true, sizeof(ret_true));
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x4387dc), ret_true, sizeof(ret_true));
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x438928), ret_true, sizeof(ret_true));
        l_info("Patched HeroesSelectionMenu DLCDownloaded/DLCBought(Local) -> return true.");

        uint8_t one = 1;
        uint8_t zero = 0;
        // CHANGE_HERO_DISPLAYED at 0x005d700f = 1
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x005d700f), &one, sizeof(one));
        // PURCHASE_SKINS at 0x005d700e = 0
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x005d700e), &zero, sizeof(zero));
        l_info("Configured CHANGE_HERO_DISPLAYED=1, PURCHASE_SKINS=0.");
    }

    // =========================================================================
    // WORLDS 6 & 7 (CASA DLC) UNLOCK & CLEAN CAROUSEL
    // =========================================================================
    // ManyWorldsMenu carousel loop: when world index r7 == 7 (the buy button slot),
    // patch 'beq 0x43d694' (0x0a000032) at 0x43d5c4 to 'beq 0x43d714' (0x0a000052).
    // Skips allocating and inserting the "20 New Levels !" buy button into the carousel.
    // Must land on 0x43d714 (loop increment), NOT 0x43d70c: that is ~std::string(r9) for the
    // skipped block's temp string, and r9 there is the previous page -> Data Abort at boot.
    // [this+0x1a0] stays NULL; every other reader (execute, notify, dtor) null-checks it.
    {
        uint32_t beq_43d714 = 0x0a000052;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x43d5c4), &beq_43d714, sizeof(beq_43d714));
        l_info("Patched ManyWorldsMenu at 0x43d5c4: removed '20 New Levels' purchase button.");

        uint8_t one = 1;
        uint8_t zero = 0;
        // casa_dlc1_purchased at 0x005fa28f = 1
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x005fa28f), &one, sizeof(one));
        // purchaseWorld_6_7_pressed at 0x005fa28e = 0
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x005fa28e), &zero, sizeof(zero));
        l_info("Configured casa_dlc1_purchased=1, purchaseWorld_6_7_pressed=0.");
    }

    // =========================================================================
    // GALLERY WALLPAPERS UNLOCK
    // =========================================================================
    // In ShopMenu::start:
    // Patch 0x436468 from 'movweq r6, #6300' (0x0301689c) to 'movw r6, #6300' (0xe301689c).
    // Patch 0x43646c from 'beq 0x43649c' (0x0a00000a) to 'b 0x43649c' (0xea00000a).
    // Unconditionally grants 6300 lums and unlocks all wallpapers.
    // Also seed wallpaperLocked at 0x005d7a43 = 0.
    {
        uint32_t movw_r6_6300 = 0xe301689c;
        uint32_t b_43649c = 0xea00000a;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x436468), &movw_r6_6300, sizeof(movw_r6_6300));
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x43646c), &b_43649c, sizeof(b_43649c));

        uint8_t zero = 0;
        kuKernelCpuUnrestrictedMemcpy((void *)(so_mod.text_base + 0x005d7a43), &zero, sizeof(zero));
        l_info("Patched ShopMenu::start at 0x436468 & wallpaperLocked: all gallery wallpapers unlocked.");
    }
}


