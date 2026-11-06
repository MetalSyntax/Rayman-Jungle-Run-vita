# Rayman Jungle Run — PS Vita Port — Release v1.2.0

---

## What's New in v1.2.0

### Native PS Vita Trophies
- **19 trophies (12 Bronze, 3 Silver, 3 Gold, 1 Platinum)** mapped 1:1 to the game's 18 achievements: total Lums, Lums in one run, Teeth of Death per world, and the "Stubborn!" retry/death ones. The Platinum is awarded by the system once the other 18 are earned.
- The trophy set installs on first launch and appears in the Trophies app. Unlocks show the standard system notification.
- How it works: the engine's `Pasta::AndroidScoreMgr::unlockAchievement` always calls `GameActivity.unlockAchievement(String)` over JNI. The FalsoJNI stub in `java.c` catches the achievement ID (the GREE numeric ID, e.g. `10002958` = 100 Lums) and forwards it to `sceNpTrophy`.
- **Existing saves are synced:** the game reports each achievement only once and remembers it in its save, so achievements earned before v1.2.0 would never reach the trophy system. After every `ScoreManager::load()/save()` the port walks the engine's own unlocked list and sends any missing trophy.

---

## Requirements

- Same plugins as before (kubridge, fd_fix, libshacccg).
- **For trophies:** [NoTrpDrm](https://github.com/Rinnegatamante/NoTrpDrm/releases) under `*main` in `ur0:tai/config.txt`, then reboot. Without it the game still runs normally, just without trophies.

---

## Upgrading from v1.1.x

- Install the new VPK from VitaShell. Replacing only `eboot.bin` is **not** enough the first time: the trophy pack lives in `sce_sys/trophy/` inside the VPK.
- Your `ux0:data/raymanjunglerun/` folder and saves are kept. Achievements already in your save unlock their trophies the next time the game loads or saves.

---

## Release Artifacts (v1.2.0)

- **VPK:** `raymanjunglerun.vpk`
  - SHA256: `7b89cf12e2921c137e15375088dc0ac2ffe0f78ccff2f350e347866a4f34b103`
- **EBOOT:** `eboot.bin`
  - SHA256: `f7f53ad0f17f84ad821d7707800df1cddb14c5c628bfe6f481e0f091d01ef087`
