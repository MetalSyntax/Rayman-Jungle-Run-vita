# Rayman Jungle Run — PS Vita Port

<p align="center">
  <img src="extras/livearea/icon0.png" alt="Rayman Jungle Run Vita Icon" width="128" height="128">
</p>

An enhanced, native port of **Rayman Jungle Run** (Android) for the **PlayStation Vita** and **PlayStation TV**, powered by an ARMv7 dynamic shared library loader (`so-loader`) and custom `FalsoJNI` bridge.

---

## Features

- **Native Resolution & 60 FPS target:** Runs at full Vita native resolution (960x544) targeting 60 FPS (some heavy scenes still drop frames, see Known Issues).
- **Full High-Fidelity Audio:** Native implementation of FMOD Ex with Ogg Vorbis streaming and sample caching, playing all original music tracks and sound effects without distortion or missing audio.
- **Full Game Unlocked:** All 70 levels, worlds 6 & 7 (CASA DLC), character skins, gallery wallpapers and dead-man stages are fully unlocked out of the box (in-app purchase prompts bypassed).
- **Persistent Saves:** Level progress, selected hero and settings are saved and restored between sessions (fixed in v1.1.0).
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

## Save Data

Saves are stored in `ux0:data/raymanjunglerun/saves/` (`slot1.dat`, `secondarySave`, `ROHeroe`, `Achievements`, ...).

- **v1.0.1 and later** create the `saves/` folder automatically on launch.
- **v1.0 did not create it**, so the game silently failed to save. If you are on v1.0, update, or create the folder manually with VitaShell: `ux0:data/raymanjunglerun/saves/`.
- **v1.0.x wrote saves but never loaded them back** (progress appeared reset on every launch). **v1.1.0 fixes this**; existing `slot1.dat` files load as-is.
- Progress played on v1.0 *before* that folder existed was never written to disk and cannot be recovered.
- On first launch with a new save, select your save slot from the main menu so the game loads it.

---

## Known Issues

- **Frame drops in some scenes** (e.g. World 2 during the Choplifter / flying sequences). v1.1.0 raises the CPU clock to 500 MHz to reduce them; still under investigation.
- If progress still resets on v1.1.0, please open an issue attaching the latest file from `ux0:data/raymanjunglerun/logs/` and your `saves/` folder.

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
