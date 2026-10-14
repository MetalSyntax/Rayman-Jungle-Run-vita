# Rayman Jungle Run — PS Vita Port v1.0

First official release of **Rayman Jungle Run** for the PlayStation Vita and PlayStation TV!

---

## What's New in v1.0

### Full Game Unlocked
- All worlds and levels (including World 6, 7, and Land of the Dead) are fully unlocked out-of-the-box.
- All characters/skins unlocked without needing Google Play / Amazon Store microtransactions.
- In-app purchase prompts requesting real money to nonexistent servers are completely bypassed.

### Audio Engine Fixed (FMOD Ex Ogg Vorbis)
- Fixed root cause of FMOD Error 25 by hooking inlined Bionic `struct __sFILE` dereferences with proper Sony `SceLibc` `feof`/`ferror` checks.
- Resolved PS Vita open file descriptor limit (`EMFILE`) by intelligently streaming only background music while loading short sound effects (waves) into RAM as memory samples, freeing all file descriptors immediately after reading.
- Full crystal-clear sound effects and background music now play seamlessly throughout the entire game.

### Performance & 60 FPS Stability
- Disabled 4X MSAA (`SCE_GXM_MULTISAMPLE_NONE`) in favor of native 960x544 Vita framebuffer, eliminating fillrate bottlenecks on the SGX543 GPU during heavy 2D alpha blending and parallax overdraw.
- Added persistent GXP shader binary caching (`ux0:data/raymanjunglerun/gxp/`), eliminating in-game microstutters caused by runtime GLSL recompilation.
- Filtered verbose debug logging from synchronous disk writes to prevent flash storage write stalls.
- CPU/GPU overclock set to 444/222/222/166 MHz for rock-solid 60 FPS gameplay.

### Controls & Input
- Native integration with PS Vita gamepad:
  - **Cross ($\times$)**: Jump / Hover / Confirm ("GO")
  - **Square ($\square$)**: Punch / Attack
  - **Circle ($\bigcirc$)**: Cancel / Back
  - **Left Analog Stick / D-Pad**: Directional movement & Menu navigation
  - **Start**: Pause game
- Full multi-touch front touchscreen support for both gameplay and UI navigation.

---

## Installation Guide

1. Install `raymanjunglerun.vpk` on your PS Vita using VitaShell.
2. Ensure you have `kubridge.skprx`, `fd_fix.skprx`, and `libshacccg.suprx` installed on your system.
3. Extract `libRO1Mobile.so` and `libfmodex.so` from your Android `.apk` (`lib/armeabi-v7a/`).
4. Extract the game's data folder `files/` from the APK (inside `assets/LUGndna...` zip).
5. Copy the files to your PS Vita into `ux0:data/raymanjunglerun/`:
   ```
   ux0:data/raymanjunglerun/
   ├── libRO1Mobile.so
   ├── libfmodex.so
   └── files/
       ├── gfx/
       ├── pasta/
       ├── sfx/
       └── en.lproj/ (and other languages)
   ```
6. Launch the game from LiveArea and enjoy!

---

## Release Artifacts

- **VPK:** `raymanjunglerun.vpk`
  - SHA256: `96a05b25db4903fe8a3b5f8c232d93ea5890f7c16154ab4201b47ca63bb2e996`
- **EBOOT:** `eboot.bin`
  - SHA256: `da860276d84bbd9023ca3a5708cfbbdcc1b9133deee7ce382367806517f34384`
