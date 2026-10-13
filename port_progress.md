# Registro de Progreso — Rayman Jungle Run (PS Vita)

## Fase 1: Configuración y Preparación (Completada — 2026-10-10)
- Repo creado desde soloader-boilerplate, `.gitignore` anti-DMCA.
- APK `Rayman Jungle Run.apk` copiado y extraído.
- ABI detectada: armeabi-v7a, x86 (elegida: armeabi-v7a).
- GLES detectado: valor no estándar en manifest: 0x20000 (declarado en AndroidManifest.xml)

## Fase 2: Decompilación (Completada — 2026-10-13)
- jadx: SÍ estaba corrido (el registro anterior estaba desactualizado): `decompiled/apk_jadx/sources` (com.pastagames.*, com.ubisoft.*, org.fmod.*, ...) y `decompiled/apk_jadx/resources` (assets/LocalAssets, lib/armeabi-v7a, res) verificados en disco.
- Ghidra (.so): NO corrido — `psvita-toolkit doctor` reporta docker daemon caído (único FAIL). Sustituido por análisis `arm-vita-eabi-nm/readelf/strings` (ver Fase 3). Reintentar Ghidra cuando Docker Desktop esté arriba.

## Fase 3: Análisis del Motor Real (Completada — 2026-10-13)
- Motor: **propio de PastaGames ("ITF/Pasta")**, nada que ver con Unity/Cocos. Evidencia `nm -D libRO1Mobile.so` (28929 símbolos): namespaces `_ZN3ITF*` / `_ZN5Pasta*`, `NVCompressToDXT`/`NVHHDDSLoad`, `TiXml`, `_Z17ReadPNGFromMemory`, `ANIM_DESCRIPTIONS`/`PlayerData`/`Ray_GameManager`. Ningún port hermano comparte motor (Riptide-GP-vita solo se usó como referencia de patrón de loader, no de código de motor).
- Ciclo de vida (jadx `GameActivity.java:397/1506/1429` + `RO1GameActivity.java`): `onCreate` (setters) → `onSurfaceChanged` (width/height + `nativeResume`) → `onDrawFrame` (primer frame `nativeCreate`+`nativeStart`, luego `nativeRun` por frame). **Sin `JNI_OnLoad`** (`nm -D | grep JNI_OnLoad` vacío), sin NativeActivity, sin `RegisterNatives`. Callbacks nativo→Java vía `GetMethodID` sobre `com/pastagames/android/GameActivity` + `setJavaBridge` (Input/Score/Browser/BehaviourLogger).
- GLES: **2.0** (`glEsVersion 0x20000`, 4 ocurrencias en AndroidManifest.xml) + mezcla GLES1 (fixed pipeline) / GLES2 (shaders, VBO, FBO-OES) en los U imports y DT_NEEDED (`libGLESv1_CM.so` + `libGLESv2.so`). vitaGL cubre ambos.
- Middleware: FMOD Ex (`libfmodex.so`, 16/16 `FMOD_*` verificados) → se carga el .so REAL como segundo módulo; zlib/log/AAssetManager → tabla reimpl; tienda (Google/Amazon), push GCM/ADM/PushWoosh, Flurry, Chartboost, Moga/BT → stubs FalsoJNI no-op/false/NULL (en Vita no existen).
- `jni-analyze`/`align-check` del toolkit hacen timeout con este .so (28k símbolos) — se omitieron; `align-check` queda pendiente para el primer triage de crash.

## Fase 4: Loader y Bootstrap JNI (Completada salvo hardware — 2026-10-13)
- `source/main.c`: implementa el ciclo de vida real (setters → `nativeCreate`+`nativeStart` → `nativeUpdateSize`+`nativeResume` → loop `nativeRun`+`gl_swap` a 960x544, idioma "en", todo bajo `DATA_PATH`). `JNI_OnLoad` con guarda NULL (el boilerplate lo llamaba incondicional → salto a NULL).
- `source/utils/init.c`: carga dual (`LOAD_ADDRESS_FMODEX 0x98000000`, engine en `0x98800000`; FMOD se reloca/resuelve primero para el fallback de `so_resolve`). `CMakeLists.txt`: `SO_PATH`=libRO1Mobile.so, nuevo `SO_PATH_FMODEX`=libfmodex.so, opción `ENABLE_FILE_LOG`.
- `source/dynlib.c`: diff U-vs-tabla (304 imports) → solo faltaban `inet_addr`, `writev` (implementado como bucle sobre `write`, Vita no lo trae) y `__aeabi_ldiv0` (trampa con abort). `_Unwind_*` quedan al fallback del toolchain, igual que el port Riptide que sí funciona.
- `source/java.c`: 51 nombres de método (de `strings`+jadx) con implementaciones reales en ruta de arranque (`makeDir`→sceIoMkdir, `assetExists`, `getDLCName`→"no", `ignoreCasePath` passthrough, `displayWarningDialog`→log no fatal, `quitApp`→exit, pad/score/red→false, tienda/push/Flurry/FMOD-device→no-op). `readAsset`→NULL documentado (vamos por rama offline con ficheros en disco, sin `nativeSetAssetManager`, igual que la rama `resourcesInAsset=false` de `onCreate`).
- `lib/falso_jni/`: estaba VACÍO (submódulo nunca bajado) → restaurado desde la copia vendoreada de Riptide (misma familia de boilerplate). `converter.c` añadido al build; `reimpl/egl.h` con renombres `_soloader` (choque con `egl.o` de vitaGL).
- Logger §6 desde el día uno: `raymanjunglerun_NNN.log` (001–999 vía `next.idx`, colores ANSI stripados) en `source/utils/logger.c`.
- **Primer build OK** (`psvita-toolkit build --preset debug`, 2026-10-13): `build/eboot.bin` (574 KB) + `build/raymanjunglerun.vpk` (657 KB). Fixes de build aplicados: duplicado de cabecera en main.c, `jni->` en vez de `jni.`, include `FalsoJNI.h` en java.c, cast de `ReleaseStringUTFChars`.
- Pendiente de hardware: deploy (`vita_ip` 192.168.3.15) + primer arranque hasta `nativeCreate` sin crashear (usar `psvita-toolkit analyze` + skill `so-crash-triage` si cae). **Un bug confirmado a la vez a partir del log real.**

## Fase 5: Primeras corridas en consola (2026-10-13)

### Bug 5.1 — Prefetch abort PC=0 en `AndroidCrossPromotionManager::registerDeviceForUpn` (run 003)
- Dump `raymanjunglerun-psp2core-1791866318-...`: PC=0x0, LR=0x98aa95b0 → `libRO1Mobile+0x2a95b0` = `registerDeviceForUpn()+0x20` (código **ARM**, no Thumb: `objdump` sin `force-thumb`).
- Call site: `GameBehaviourLogger::getSingleton()->vtable[3]("register_upn", ...)` (`ldr r3,[r0]; ldr r3,[r3,#12]; blx r3`). Llegó vía `MainGameState::createMenu` → `ArcadeMainMenu::start`.
- Causa: el singleton dummy que instala `patch.c` (0x5dae78, confirmado vía GOT `0x5d3d64` R_ARM_RELATIVE) tenía la vtable en ceros. Vtable real de la base (`_ZTV19GameBehaviourLogger` @0x5b04f0): slot0 init, 1 logEvent(int,…), 2 logEndOfEvent(int,…), 3 logEvent(const char*,int,…), 4 logEndOfEvent(const char*,…).
- Fix: todos los slots de la vtable dummy → función no-op que devuelve 0 (`source/patch.c`).

### Bug 5.2 — Todos los assets "inexistent"/"can not open" (run 003)
- El log muestra `gfx/*.png`, `pasta/*.ckd`, `sfx/*.ogg`, `*.lproj/Localizable.strings` sin encontrar (y por eso `STR_FONT_FILENAME` sin traducir).
- Causa: el APK (mod) guarda los datos reales en `assets/LUGndna...` (zip, 5306 entradas `files/...`, 170 MB). `LaunchActivity.unZip` lo extrae en `getFilesDir()` sin el sufijo `/files` → los datos viven en `<app>/files/`. En la Vita **no se había subido `ux0:data/raymanjunglerun/files/`** (solo `assets/`, `res/`).
- Fix: `main.c` → root/writeTemp/browse = `DATA_PATH "files/"` (igual que `browseDir` en Android); persistence (saves) sigue en `DATA_PATH`. Subidos los 4684 ficheros de `ux0_data/raymanjunglerun/files` (sin `._*` de macOS) por FTP. `io.c` ya probaba `files/` como candidato para rutas relativas.

### Bug 5.3 — Pantalla negra + Data abort en `MainGameState::loadScene+0x27c` (run 004)
- Dump `...1791921219-...`: PC=`libRO1Mobile+0x429c28` (`ldr r3,[r0,#4]`, R0=NULL), LR en `ITF::Cell::addObject`.
- Log: `AndroidFileMgr::load can not open: .../pasta/final/accueil.isc.ckd` → `WARNING Empty file` → la escena del menú principal es NULL y `loadScene` la desreferencia. La corrida se hizo con la subida de datos al ~16% (750/4683), así que faltaban casi todos los `pasta/*.ckd` y `sfx/`. La pantalla negra era el motor cargando sin nada que dibujar.
- No es bug del loader: las rutas ya resuelven bien (`gfx/`, `*.lproj`, `pasta/factionconfig` abren OK desde `files/`).
- Mejora: `init.c` ahora comprueba 4 ficheros centinela de `files/` (lproj, gfx, pasta/final, sfx) y muestra un diálogo "Game data is missing or incomplete" en vez de crashear.
- Subida: el FTP de VitaShell se cae a ratos; script de subida reanudable (salta ficheros con igual tamaño, crea directorios padre).

### Mejora 5.4 — vitaGL vendorizado
- `vendor/vitaGL` copiado del port de Prince of Persia (upstream cd3791e + defines `SCE_GXM_*`; ver `vendor/vitaGL/VENDORED.md`), compilado vía `ExternalProject` con `SOFTFP_ABI=1 NO_DEBUG=1 NO_SPLASHSCREEN=1` (`VITAGL_MAKE_FLAGS_STR`), `build_vitagl.sh` hace `make clean` si cambian los flags. Ya no depende del `libvitaGL.a` instalado en el vitasdk.
- `.gitignore`: `Makefile`/`CMakeFiles/`/`cmake_install.cmake` anclados a la raíz (antes ignoraban también `vendor/vitaGL/Makefile`, y el build del toolkit, que copia respetando `.gitignore`, fallaba).

### Hito 5.5 — Primera imagen en pantalla (run 006)
- Con los datos completos, se ven los logos Ubisoft / UbiArt / Pastagames a 60 fps (screenshot `screenshots/ji/2026-10-13/2026-10-13-163143.jpg`). Texturas `.pvr`/`.png`, fuentes y `Localizable.strings` cargan bien.

### Bug 5.6 — Stack overflow del hilo principal cargando `pasta/final/accueil.isc` (run 006)
- Dump `...1791923531-...`: stop reason `0x10006`, PC/LR en SceLibKernel, SP=`0x814e49d0` **por debajo** de la base del stack del hilo principal (región del dump `0x81501000-0x81541000`, 256 KB por defecto de vitasdk) → ~114 KB de desborde. Ocurre justo después de `loadScene = pasta/final/accueil.isc` (carga recursiva de actores ITF).
- En Android esto corre en el hilo de render de GLSurfaceView (stack Java, muy mayor a 256 KB).
- Fix: `main.c` ejecuta todo (init + bucle) en un pthread con stack de 4 MB (`GAME_THREAD_STACK_SIZE`); el hilo principal solo hace join.

### Bug 5.7 — FMOD error 25 (Unsupported format) en todos los `.ogg` (run 006)
- Los `.ogg` son Vorbis válidos (`file`: Ogg Vorbis 1.3.3). `AndroidAudioPlayer::load` → `FMOD_System_CreateStream(path)`; FMOD abre con `fopen` (→ `FILE*` de SceLibc por `USE_SCELIBC_IO`) pero busca con `fseeko`/`ftello` (`nm -D libfmodex.so`), que `dynlib.c` mandaba a **newlib** (había un TODO) → seek/tamaño basura → FMOD_ERR_FORMAT.
- Fix: `fseeko`/`ftello` → `sceLibcBridge_fseek`/`ftell` bajo `USE_SCELIBC_IO` (en Bionic 32-bit `off_t` = `long`, misma ABI).
- Pendiente (latente, sin síntoma): `__sF_fake` copia `FILE` de newlib pero `fprintf` va al bridge, y el stride de `__sF` de Bionic (84 bytes) no coincide con `sizeof(FILE)` de newlib — revisar si algo escribe en stdout/stderr.

### Hito 5.8 — Menú principal funcionando (run 007)

### Bug 5.9 — Data abort en `ArcadeMainMenu::~ArcadeMainMenu+0x74` al iniciar partida (run 007)
- Dump `...1791924261-...`: hilo `pthread` (el del juego), `ldr r3,[r0]` con R0=`0x61002f00` (basura) = miembro `+0x64` del menú.
- Ctor (`0x430a10..0x430b78`): `+0x64`/`+0x68` son `ButtonWithSound` creados según 2 flags globales (byte en `.data` 0x5f4c64 y otro vía GOT). Si flag1=1 y flag2=0 salta a `0x430aec` y **nunca inicializa `+0x64`**; el `else` que lo pone a 0 (`0x430b70`) solo cubre flag1=0. Bug latente del motor tapado por la memoria a cero de Android.
- Todos los `operator new/new[]` → `Pasta::MemoryMgr::allocate` → import `malloc`. Fix: import `malloc` → `malloc_soloader` = `calloc(1,n)` (`source/reimpl/mem.c`).

### Bug 5.7 (cont.) — FMOD error 25 persiste
- `fseeko`/`ftello` no se llaman en realidad (0 call sites en `libfmodex`); FMOD usa `fopen/fseek/ftell/fread` (ya puenteados), el codec Ogg existe ("FMOD Ogg Vorbis Codec"), libm es softfp, no faltan imports. `FMOD_Debug_SetLevel` es stub (build sin logging).
- Instrumentación temporal (`DEBUG_SOLOADER`): `fread/fseek/ftell` → `*_trace` en `io.c`, loguea las ops sobre los 3 primeros `.ogg` (`ogg-trace ...`). Siguiente corrida decide.

## Fase 5 en adelante: Pendiente de hardware real
- F5 gráficos: shaders GLSL→vitaGL con caché en disco (ya andamiado en `glutil.c`); texturas vía `NVHHDDSLoad`/DDS/DXT + `ReadPNGFromMemory`/`loadTGATexture` — vigilar ETC1/PVRTC si aparecen.
- F6/F7 input: alimentar `nativeTouchScrStart/Move/End` desde sceTouch y `nativePressKey/ReleaseKey` (0=izq,1=der,2=saltar,3=?) + `nativeSetPadAxisValues` desde sceCtrl en el loop de `main.c`.
- F8 audio: puente FMOD→sceAudioOut (los `FMOD_System_*` corren contra el .so real; el pump `FMODAudioDevice` Java está en no-op) + `nativeMusicStopStart` en suspend.
- F9 vídeo: no hay vídeos en assets (solo `beeHub.bin`/`goog.bin`/`data.save`) — probable "skip gracioso" directo.
- F10 assets: `LocalAssets/<idioma>` + `gfx/fonts` van a `DATA_PATH`; saves en `saves/`; idioma = sistema con fallback inglés (hoy hardcoded "en" — pasar a `sceAppUtilSystemParamGetById`).
- F11–F15: clocks 444/222/222/166 ya fijados en `init.c`; settings.bin/configurador/trofeos/LiveArea/VPK release según `PORTING_PLAN.md` cuando el juego arranque en consola.
