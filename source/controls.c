/*
 * Copyright (C) 2026 Rayman Jungle Run Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  controls.c
 * @brief Vita touch + buttons mapped directly to engine native methods:
 *        - Front touch -> nativeTouchScrStart / nativeTouchScrMove / nativeTouchScrEnd
 *        - Physical buttons (X, Square, R1, D-pad, Circle/Triangle, Start) ->
 *          nativePressKey / nativeReleaseKey & nativePressBack
 */

#include "controls.h"
#include "utils/logger.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

#include <string.h>

extern so_module so_mod;

// Engine native touch functions
static void (*nativeTouchScrStart)(void *env, void *obj, int x, int y);
static void (*nativeTouchScrMove)(void *env, void *obj, int oldX, int oldY, int newX, int newY);
static void (*nativeTouchScrEnd)(void *env, void *obj, int x, int y);

// Engine native key functions
static void (*nativePressKey)(void *env, void *obj, int key);
static void (*nativeReleaseKey)(void *env, void *obj, int key);
static int  (*nativePressBack)(void *env, void *obj);
static void (*nativePressMenu)(void *env, void *obj);

#define MAX_TOUCH_REPORTS 4

typedef struct {
    int id;
    int x;
    int y;
    int active;
} ActiveTouch;

static ActiveTouch g_touches[MAX_TOUCH_REPORTS];
static uint32_t g_prev_buttons = 0;

void controls_init(void) {
    *(void **)&nativeTouchScrStart = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeTouchScrStart");
    *(void **)&nativeTouchScrMove  = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeTouchScrMove");
    *(void **)&nativeTouchScrEnd   = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeTouchScrEnd");

    *(void **)&nativePressKey   = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativePressKey");
    *(void **)&nativeReleaseKey = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeReleaseKey");
    *(void **)&nativePressBack  = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativePressBack");
    *(void **)&nativePressMenu  = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativePressMenu");

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);

    memset(g_touches, 0, sizeof(g_touches));
    for (int i = 0; i < MAX_TOUCH_REPORTS; i++) {
        g_touches[i].id = -1;
    }

    l_info("controls initialized (touch & buttons wired).");
}

static void poll_touch(void) {
    if (!nativeTouchScrStart || !nativeTouchScrMove || !nativeTouchScrEnd)
        return;

    SceTouchData touch;
    int ret = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (ret <= 0)
        return;

    // 1. Check for ended touches (previously active, now missing)
    for (int i = 0; i < MAX_TOUCH_REPORTS; i++) {
        if (!g_touches[i].active)
            continue;

        int found = 0;
        for (int r = 0; r < touch.reportNum; r++) {
            if (touch.report[r].id == g_touches[i].id) {
                found = 1;
                break;
            }
        }

        if (!found) {
            nativeTouchScrEnd(&jni, NULL, g_touches[i].x, g_touches[i].y);
            g_touches[i].active = 0;
            g_touches[i].id = -1;
        }
    }

    // 2. Process current reports (start or move)
    for (int r = 0; r < touch.reportNum && r < MAX_TOUCH_REPORTS; r++) {
        // Vita front touch panel resolution: 1920x1088 -> map to 960x544
        int screen_x = (touch.report[r].x * 960) / 1920;
        int screen_y = (touch.report[r].y * 544) / 1088;

        int existing_slot = -1;
        for (int i = 0; i < MAX_TOUCH_REPORTS; i++) {
            if (g_touches[i].active && g_touches[i].id == touch.report[r].id) {
                existing_slot = i;
                break;
            }
        }

        if (existing_slot >= 0) {
            // Touch moved
            if (screen_x != g_touches[existing_slot].x || screen_y != g_touches[existing_slot].y) {
                int old_x = g_touches[existing_slot].x;
                int old_y = g_touches[existing_slot].y;
                g_touches[existing_slot].x = screen_x;
                g_touches[existing_slot].y = screen_y;
                nativeTouchScrMove(&jni, NULL, old_x, old_y, screen_x, screen_y);
            }
        } else {
            // New touch: find free slot
            for (int i = 0; i < MAX_TOUCH_REPORTS; i++) {
                if (!g_touches[i].active) {
                    g_touches[i].active = 1;
                    g_touches[i].id = touch.report[r].id;
                    g_touches[i].x = screen_x;
                    g_touches[i].y = screen_y;
                    nativeTouchScrStart(&jni, NULL, screen_x, screen_y);
                    break;
                }
            }
        }
    }
}

static void poll_buttons(void) {
    SceCtrlData pad;
    int ret = sceCtrlPeekBufferPositive(0, &pad, 1);
    if (ret < 0)
        return;

    uint32_t pressed = pad.buttons & ~g_prev_buttons;
    uint32_t released = ~pad.buttons & g_prev_buttons;

    // In Rayman Jungle Run (GameActivity.java):
    // Action / Jump:
    //   Key 2 (KEYCODE_DPAD_CENTER=23 or KEYCODE_BUTTON_A=96 / 103)
    // Attack / Punch / Helicopter:
    //   Key 3 (KEYCODE_BUTTON_X=99 or KEYCODE_BUTTON_L1=102)
    // Left/Right:
    //   Key 0 (KEYCODE_DPAD_LEFT=21), Key 1 (KEYCODE_DPAD_RIGHT=22)
    // Pause / Back:
    //   nativePressBack() (KEYCODE_BACK=4)
    // Menu:
    //   nativePressMenu() (KEYCODE_MENU=82)

    // Button mapping table
    static const struct {
        uint32_t vita_mask;
        int game_key;
    } k_mappings[] = {
        // Jump / Action
        { SCE_CTRL_CROSS,    2 },
        { SCE_CTRL_UP,       2 },
        // Attack / Helicopter
        { SCE_CTRL_SQUARE,   3 },
        { SCE_CTRL_RTRIGGER, 3 },
        { SCE_CTRL_LTRIGGER, 3 },
        // Directions
        { SCE_CTRL_LEFT,     0 },
        { SCE_CTRL_RIGHT,    1 },
    };

    if (nativePressKey && nativeReleaseKey) {
        for (size_t i = 0; i < sizeof(k_mappings)/sizeof(k_mappings[0]); i++) {
            if (pressed & k_mappings[i].vita_mask) {
                nativePressKey(&jni, NULL, k_mappings[i].game_key);
            }
            if (released & k_mappings[i].vita_mask) {
                nativeReleaseKey(&jni, NULL, k_mappings[i].game_key);
            }
        }
    }

    // Start / Select / Circle / Triangle -> Back / Pause
    if ((pressed & SCE_CTRL_START) || (pressed & SCE_CTRL_CIRCLE) || (pressed & SCE_CTRL_SELECT)) {
        if (nativePressBack) {
            nativePressBack(&jni, NULL);
        }
    }

    if (pressed & SCE_CTRL_TRIANGLE) {
        if (nativePressMenu) {
            nativePressMenu(&jni, NULL);
        }
    }

    g_prev_buttons = pad.buttons;
}

void controls_poll(void) {
    poll_touch();
    poll_buttons();
}
