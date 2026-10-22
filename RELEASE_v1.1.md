# Rayman Jungle Run — PS Vita Port — Release v1.1.0

---

## What's New in v1.1.0

### Save Progress Now Loads Correctly
- **Fixed progress resetting on every launch.** Saves were written to `ux0:data/raymanjunglerun/saves/slot1.dat`, but the game discarded them when loading.
  - Root cause: the engine's file loader (`AndroidFileMgr::load`) inlines Android's `ferror()` by reading the error bit at offset 12 of the C `FILE` struct. On PS Vita the `FILE` comes from SceLibc, with a different layout, so every save read was treated as a failure and progress started from zero. Same class of bug as the FMOD audio fix in v1.0.
  - Fix: the bogus check is skipped (`so+0x2a7794`). This affects every persistent file: level progress (`slot1.dat`), selected slot, selected hero and volume.
- `access()` now goes through the loader's path resolution (normalizes `//`, same as `fopen`).

### More Content Unlocked
- Character skins DLC unlocked (hero selection shows all skins, buy button hidden).
- Worlds 6 & 7 (CASA DLC) unlocked; the "20 New Levels !" purchase button is removed from the world carousel.
- All gallery wallpapers unlocked.
- All 70 levels start unlocked, even on a fresh save.

### Stability
- Fixed a boot crash (Data Abort) caused by the world carousel patch jumping into a string destructor with an invalid object.

### Performance
- ARM CPU clock raised from 444 MHz to 500 MHz to reduce frame drops in physics/particle-heavy scenes (World 2 helicopter / wind sections).

---

## Upgrading from v1.0.x

- Install the new VPK (or replace `ux0:app/PSVRJR001/eboot.bin`). Your `ux0:data/raymanjunglerun/` folder is kept.
- Existing `slot1.dat` files are loaded as-is. Progress lost on earlier versions *before* it was written to disk cannot be recovered.

---

## Release Artifacts (v1.1.0)

- **VPK:** `raymanjunglerun.vpk`
  - SHA256: `268bbcbab1ceaa96f84cae062d4fa0c28480776b5d0cf1926f215b9d84722ce4`
- **EBOOT:** `eboot.bin`
  - SHA256: `047c1af9c5eca94668eda92dd4c61c6d79a1d82ad991f93fbfd2a7a3318632d8`

Installation instructions: see [README.md](README.md). Previous releases: [RELEASE_v1.0.md](RELEASE_v1.0.md).
