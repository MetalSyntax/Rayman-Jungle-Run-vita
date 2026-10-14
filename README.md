# Rayman Jungle Run — PS Vita Port

<p align="center">
  <img src="extras/livearea/icon0.png" alt="Rayman Jungle Run Vita Icon" width="128" height="128">
</p>

An enhanced, native port of **Rayman Jungle Run** (Android) for the **PlayStation Vita** and **PlayStation TV**, powered by an ARMv7 dynamic shared library loader (`so-loader`) and custom `FalsoJNI` bridge.

---

## Features

- **Native Resolution & Solid 60 FPS:** Runs at full Vita native resolution (960x544) at smooth, consistent 60 frames per second without frame drops.
- **Full High-Fidelity Audio:** Native implementation of FMOD Ex with Ogg Vorbis streaming and sample caching, playing all original music tracks and sound effects without distortion or missing audio.
- **Full Game Unlocked:** All worlds, levels, DLCs, characters/skins, and dead-man stages are fully unlocked out of the box (in-app purchase prompts bypassed).
- **Physical Gamepad & Front Touch:** Full integration with PS Vita hardware controls (Buttons, D-Pad, Left Analog Stick) plus native front touchscreen support.
- **Shader Cache:** Runtime GXP shader binary caching on storage (`ux0:data/raymanjunglerun/gxp/`) eliminating recompilation stutters.
- **Optimized Memory & I/O:** Uses SceLibcBridge with custom sample memory management to respect PS Vita kernel file descriptor limits (`EMFILE` safe).

---

## Controls

| Button | Action |
| :--- | :--- |
| **Cross ($\times$)** | Jump / Hover / Confirm / Start level ("GO") |
| **Square ($\square$)** | Punch / Attack |
| **Circle ($\bigcirc$)** | Back / Cancel |
| **Left Stick / D-Pad** | Navigate Menus / Move |
| **Start** | Pause Game / Return to Menu |
| **Touchscreen** | Full Touchscreen Controls & UI Interaction |

---

## Prerequisites

Make sure your PS Vita is running Henkaku / Enso on firmware 3.60 or 3.65 and has the following plugins installed:

1. **[kubridge.skprx](https://github.com/TheOfficialFloW/kubridge/releases)** (v0.3.1 or higher)
2. **[fd_fix.skprx](https://github.com/TheOfficialFloW/FdFix/releases)** (or repatch / rePatch reLoaded)
3. **libshacccg.suprx** runtime shader compiler (can be easily extracted using **[ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D)**).

---

## Setup & Installation

1. Install the latest **`raymanjunglerun.vpk`** on your PS Vita using VitaShell.
2. Obtain a legally purchased copy of **Rayman Jungle Run** (v1.3.x or compatible ARMv7 build) in `.apk` format.
3. Open the `.apk` file using an archive manager (such as 7-Zip, WinRAR, or unzip).
4. Extract the following native libraries from `lib/armeabi-v7a/`:
   - `libRO1Mobile.so`
   - `libfmodex.so`
5. Extract the game asset files:
   - In most APK versions, the main game assets are inside `assets/` (or packed inside an internal zip file such as `assets/LUGndna...`).
   - Extract the entire `files/` folder (containing `gfx/`, `pasta/`, `sfx/`, `*.lproj/`, etc.).
6. Copy the extracted files to your PS Vita storage under `ux0:data/raymanjunglerun/`:
   ```
   ux0:data/raymanjunglerun/
   ├── libRO1Mobile.so
   ├── libfmodex.so
   └── files/
       ├── en.lproj/
       ├── es.lproj/ (and other languages)
       ├── gfx/
       ├── pasta/
       └── sfx/
   ```
7. Launch **Rayman Jungle Run** from your LiveArea!

---

## Building from Source

To compile the port yourself using the official **[vitaSDK](https://vitasdk.org/)**:

```bash
git clone https://github.com/your-username/Rayman-Jungle-Run-vita.git
cd Rayman-Jungle-Run-vita

export VITASDK=/path/to/vitasdk
export PATH=$VITASDK/bin:$PATH

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The resulting `raymanjunglerun.vpk` and `eboot.bin` will be located in the `build/` directory.

---

## Credits & Acknowledgements

- **Ubisoft Montpellier & PastaGames** for creating *Rayman Jungle Run* and the UbiArt / Pasta framework.
- **TheFloW** for the original `so-loader` concept and `kubridge`.
- **Rinnegatamante** for [vitaGL](https://github.com/Rinnegatamante/vitaGL) and invaluable contributions to the PS Vita homebrew scene.
- **Volodymyr Atamanenko** for the soloader boilerplate architecture and tooling foundations.
