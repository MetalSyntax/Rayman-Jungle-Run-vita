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
 *        - Physical buttons -> engine gamepad (isGamePadKeyPressed /
 *          getGamePadAxisValues JNI, see java.c); Start -> nativePressBack
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

// Engine native key / pad functions
static int  (*nativePressBack)(void *env, void *obj);
static void (*nativeSetPadAxisValues)(void *env, void *obj, int pad, float x, float y);

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

    *(void **)&nativePressBack        = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativePressBack");
    *(void **)&nativeSetPadAxisValues = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetPadAxisValues");

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchEnableTouchForce(SCE_TOUCH_PORT_FRONT);

    memset(g_touches, 0, sizeof(g_touches));
    for (int i = 0; i < MAX_TOUCH_REPORTS; i++) {
        g_touches[i].id = -1;
    }

    l_info("controls initialized (touch + gamepad wired).");
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

// Gamepad state sampled once per frame, read back by the engine through the
// GameActivity gamepad JNI methods (java.c). Pasta::AndroidInputMgr::execute
// (0x2ad710) polls isGamePadKeyPressed(0, keycode) for every JoyButton using
// the Android keycode table at 0x5d7134:
//   A=96 B=97 X=99 Y=100 L2=104 R2=105 START=108 LEFT=21 RIGHT=22 UP=19
//   DOWN=20 THUMBL=106 THUMBR=107 L1=102 R1=103
// and getGamePadAxisValues(0, 2) for the stick (Java answers through
// nativeSetPadAxisValues). This is the path the game uses for Android TV /
// Moga pads: A jumps and confirms menu buttons. The old Xperia Play path
// (nativePressKey) only logs "Press xperia key" and doesn't jump.
static uint32_t g_pad_buttons = 0;
static float g_pad_axis_x = 0.0f;
static float g_pad_axis_y = 0.0f;

static const struct {
    uint32_t vita_mask;
    int keycode;
} k_pad_map[] = {
    { SCE_CTRL_CROSS,    96 },  // A: jump / confirm
    { SCE_CTRL_CIRCLE,   97 },  // B: cancel
    { SCE_CTRL_SQUARE,   99 },  // X: punch
    { SCE_CTRL_TRIANGLE, 100 }, // Y
    { SCE_CTRL_LTRIGGER, 102 }, // L1
    { SCE_CTRL_RTRIGGER, 103 }, // R1
    { SCE_CTRL_LEFT,     21 },
    { SCE_CTRL_RIGHT,    22 },
    { SCE_CTRL_UP,       19 },
    { SCE_CTRL_DOWN,     20 },
};

#define STICK_DEADZONE 0.25f

static float stick_axis(uint8_t v) {
    float f = ((int)v - 128) / 128.0f;
    if (f > -STICK_DEADZONE && f < STICK_DEADZONE)
        return 0.0f;
    return f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
}

static void poll_buttons(void) {
    SceCtrlData pad;
    int ret = sceCtrlPeekBufferPositive(0, &pad, 1);
    if (ret < 0)
        return;

    uint32_t pressed = pad.buttons & ~g_prev_buttons;

    g_pad_buttons = pad.buttons;
    // Android joystick axes: +x right, +y down (same as the Vita stick).
    g_pad_axis_x = stick_axis(pad.lx);
    g_pad_axis_y = stick_axis(pad.ly);

    // START is the only pause/menu button (GameActivity.onKeyDown KEYCODE_BACK
    // -> nativePressBack). It is not reported as pad START (108) as well, or
    // a single press would open and close the pause menu.
    if ((pressed & SCE_CTRL_START) && nativePressBack)
        nativePressBack(&jni, NULL);

    g_prev_buttons = pad.buttons;
}

jboolean controls_pad_key_pressed(int keycode) {
    for (size_t i = 0; i < sizeof(k_pad_map) / sizeof(k_pad_map[0]); i++) {
        if (k_pad_map[i].keycode == keycode)
            return (g_pad_buttons & k_pad_map[i].vita_mask) ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

void controls_pad_update_axis(int pad) {
    if (nativeSetPadAxisValues)
        nativeSetPadAxisValues(&jni, NULL, pad, g_pad_axis_x, g_pad_axis_y);
}

void controls_poll(void) {
    poll_touch();
    poll_buttons();
}
