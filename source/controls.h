/*
 * Copyright (C) 2026 Rayman Jungle Run Vita port contributors
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef SOLOADER_CONTROLS_H
#define SOLOADER_CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

void controls_init(void);
void controls_poll(void);

// Backing for GameActivity.isGamePadKeyPressed / getGamePadAxisValues.
unsigned char controls_pad_key_pressed(int keycode);
void controls_pad_update_axis(int pad);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_CONTROLS_H
