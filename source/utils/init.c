/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021-2022 Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "utils/init.h"

#include "utils/dialog.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "utils/settings.h"

#include <string.h>

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/kernel/clib.h>
#include <psp2/power.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>
#include <fios/fios.h>

// Base addresses for the two Android .so files. Memory footprints
// (extract listing): libfmodex.so ~1.0 MiB, libRO1Mobile.so ~5.8 MiB.
// 8 MiB apart leaves room for so_util's patch/cave areas. Dependency
// order matters: so_resolve() falls back to already loaded modules, so
// libfmodex.so (DT_NEEDED, provides 16x FMOD_* U symbols) must be
// relocated/resolved before libRO1Mobile.so.
#define LOAD_ADDRESS_FMODEX 0x98000000
#define LOAD_ADDRESS 0x98800000

extern so_module so_mod;         // libRO1Mobile.so (engine)
extern so_module so_mod_fmodex;  // libfmodex.so (FMOD Ex)

static void load_so(so_module *mod, const char *path, uintptr_t addr) {
    if (!file_exists(path)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", path);
    }
    if (so_file_load(mod, path, addr) < 0) {
        l_fatal("%s could not be loaded.", path);
        fatal_error("Error: could not load %s.", path);
    }
    l_success("%s loaded at 0x%08X.", path, (unsigned int)addr);
}

static void link_so(so_module *mod, const char *name) {
    so_relocate(mod);
    resolve_imports(mod);
    l_success("%s relocated and imports resolved.", name);
}

void soloader_init_all() {
	// Launch `app0:configurator.bin` on `-config` init param
    sceAppUtilInit(&(SceAppUtilInitParam){}, &(SceAppUtilBootParam){});
    SceAppUtilAppEventParam eventParam;
    sceClibMemset(&eventParam, 0, sizeof(SceAppUtilAppEventParam));
    sceAppUtilReceiveAppEvent(&eventParam);
    if (eventParam.type == 0x05) {
        char buffer[2048];
        sceAppUtilAppEventParseLiveArea(&eventParam, buffer);
        if (strstr(buffer, "-config"))
            sceAppMgrLoadExec("app0:/configurator.bin", NULL, NULL);
    }

    // Set default overclock values
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

#ifdef USE_SCELIBC_IO
    if (fios_init(DATA_PATH) == 0)
        l_success("FIOS initialized.");
#endif

    if (!module_loaded("kubridge")) {
        l_fatal("kubridge is not loaded.");
        fatal_error("Error: kubridge.skprx is not installed.");
    }
    l_success("kubridge check passed.");

    if (!file_exists(SO_PATH)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH);
    }
    if (!file_exists(SO_PATH_FMODEX)) {
        fatal_error("Looks like you haven't installed the data files for this "
                    "port, or they are in an incorrect location. Please make "
                    "sure that you have %s file exactly at that path.", SO_PATH_FMODEX);
    }
    // Game data = the APK's assets/LUGndna... zip ("files/..." entries).
    // With it missing/partial the engine only logs "Empty file" and then
    // dereferences a NULL scene in MainGameState::loadScene (run 004), so
    // check a few files spread across the tree (sfx/ comes last in an
    // alphabetical upload, so it also catches interrupted copies).
    static const char *data_sentinels[] = {
        DATA_PATH "files/en.lproj/Localizable.strings",
        DATA_PATH "files/gfx/menus/Main_menu_elements.png",
        DATA_PATH "files/pasta/final/accueil.isc.ckd",
        DATA_PATH "files/sfx/music/menu.ogg",
    };
    for (unsigned i = 0; i < sizeof(data_sentinels) / sizeof(*data_sentinels); i++) {
        if (!file_exists(data_sentinels[i])) {
            fatal_error("Game data is missing or incomplete. Extract the "
                        "\"files\" folder of the APK's data zip to "
                        DATA_PATH "files/ (missing: %s).", data_sentinels[i]);
        }
    }

    // FMOD first: engine DT_NEEDEDs it, resolve falls back to loaded modules.
    load_so(&so_mod_fmodex, SO_PATH_FMODEX, LOAD_ADDRESS_FMODEX);
    load_so(&so_mod, SO_PATH, LOAD_ADDRESS);

    settings_load();
    l_success("Settings loaded.");

    link_so(&so_mod_fmodex, "libfmodex.so");
    link_so(&so_mod, "libRO1Mobile.so");

    so_patch();
    l_success("SO patched.");

    so_flush_caches(&so_mod_fmodex);
    so_flush_caches(&so_mod);
    l_success("SO caches flushed.");

    so_initialize(&so_mod_fmodex);
    so_initialize(&so_mod);
    l_success("SO initialized (init_array run for both modules).");

    gl_preload();
    l_success("OpenGL preloaded.");

    jni_init();
    void java_overrides_init(void);
    java_overrides_init();
    l_success("FalsoJNI initialized and overrides applied.");
}

