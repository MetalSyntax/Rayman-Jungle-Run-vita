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

### Hito 5.10 — Jugable (run 008)
- Menú → partida OK con el `malloc` a cero (5.9). Se jugaron 3 niveles. Estado commiteado (`099fcdc`).

### Bug 5.11 — Botones: CROSS no salta ni pulsa "GO"; CIRCLE abre el menú (run 008)
- `controls.c` mandaba CROSS → `nativePressKey(2)`, que solo loguea `Press xperia key` (camino Xperia Play) y no salta.
- El motor tiene soporte real de gamepad (Android TV / Moga): `Pasta::AndroidInputMgr::execute` (0x2ad710) llama a Java `isGamePadKeyPressed(0, keycode)` por cada `JoyButton`, con la tabla de keycodes Android en 0x5d7134: A=96 B=97 X=99 Y=100 L2=104 R2=105 START=108 ←21 →22 ↑19 ↓20 L3=106 R3=107 L1=102 R1=103 (BACK=4 y 12/13 se saltan); stick vía `getGamePadAxisValues(0,2)` → `nativeSetPadAxisValues`. Todo estaba en `retFalse`/no-op.
- Fix: `isGamePadConnected` → true; `isGamePadKeyPressed` lee el estado de sceCtrl (CROSS=A, CIRCLE=B, SQUARE=X, TRIANGLE=Y, L=L1, R=R1, cruceta); `getGamePadAxisValues` empuja el stick izquierdo (zona muerta 0.25). START → `nativePressBack` es el **único** botón de pausa/menú (CIRCLE/SELECT/TRIANGLE ya no lo llaman; START no se reporta además como pad 108 para no abrir+cerrar en la misma pulsación). Se quitó `nativePressKey/ReleaseKey`.

### Bug 5.7 (Resuelto) — Causa raíz de FMOD error 25: Bionic stdio inline flags en libfmodex.so (run 009)
- Diagnóstico vía desensamblado de `libfmodex.so`: en offset `0xab4f0` (`FMOD_File_Stdio_Read`), la librería fue compilada con Android Bionic NDK donde `feof()` y `ferror()` son macros inlined como `ldrh r3, [handle, #12]`, evaluando bits `0x20` (EOF) y `0x40` (ERROR) directamente contra `struct __sFILE._flags` de Bionic.
- En PS Vita (`USE_SCELIBC_IO`), `handle` es un `FILE*` nativo de Sony `SceLibc`. El offset 12 contiene campos internos de Sony donde bits 0x20/0x40 se activaban en lecturas de buffers de Vorbis (ej. libvorbis pidiendo 8500 bytes y recibiendo menos en EOF o lecturas intermedias).
- `FMOD_File_Stdio_Read` interpretaba erróneamente esto como `FMOD_ERR_FILE_BAD` (19) o `FMOD_ERR_FILE_EOF` (22) y retornaba sin actualizar `*bytesread`, causando que `_ov_open1` recibiera 0 bytes y retornara `-132` (`OV_ENOTVORBIS`) → FMOD lanzaba error 25 ("Unsupported file or audio format").
- Fix: Hook ARM en `so_mod_fmodex.text_base + 0xab4f0` reemplazando la función con `fmod_file_stdio_read_hook` (`source/patch.c`), llamando limpiamente a `sceLibcBridge_fread()`, `sceLibcBridge_feof()` y `sceLibcBridge_ferror()`. Todos los audios OGG cargan y reproducen perfectamente.

### Bug 5.12 (Resuelto) — Bajones severos de FPS y stuttering en gameplay (run 009)
- Causa 1 (GPU Fillrate): `source/utils/glutil.c` inicializaba vitaGL con `SCE_GXM_MULTISAMPLE_4X`. El motor 2D UbiArt utiliza overdraw extensivo con múltiples capas translúcidas de paralaje y partículas. 4X MSAA cuatriplicaba la carga de blending y ancho de banda en la SGX543, colapsando el framerate. Fix: cambiado a `SCE_GXM_MULTISAMPLE_NONE`.
- Causa 2 (I/O Stalls): `source/utils/logger.c` realizaba escrituras síncronas a disco (`sceIoWrite`) en cada línea de log si `ENABLE_FILE_LOG` estaba activo. Al fallar el audio y ejecutarse el bucle del juego, se escribían cientos de líneas por segundo en la tarjeta de memoria. Fix: los mensajes `LT_DEBUG` ahora se omiten del archivo de log en disco (solo consola) para evitar bloqueos del hilo del juego.
- Causa 3 (Shader Cache Miss): La carpeta `DATA_PATH "gxp"` no existía y `file_save` (`utils.c`) no creaba directorios padre (`file_mkpath`), por lo que el volcado de shaders GXP precompilados fallaba silenciosamente y forzaba recompilaciones constantes en runtime. Fix: `DATA_PATH "gxp"` se asegura en `init.c` y `file_save` invoca `file_mkpath` antes de abrir archivos destino.
- Limpieza: Removidas las funciones instrumentales de rastreo temporal (`fread_trace`, etc.) en `io.c` y `dynlib.c`.

### Bug 5.13 (Resuelto) — Data abort en `MainGameState::loadScene+0x27c` por agotamiento de descriptores de fichero (run 010)
- Crash dump: `raymanjunglerun-psp2core-1791944033-0x00047530f3-eboot.bin.psp2dmp`, PC=`0x98c29c28` (`libRO1Mobile+0x429c28`), idéntico al Bug 5.3 (`loadScene` desreferenciando puntero de escena NULL).
- Síntoma en log 010: ráfaga de `FMOD error 23 (File not found)` seguida por el fallo de apertura de TODOS los archivos del juego (`AndroidFileMgr::load can not open: Ray_font1.fnt`, `Main_menu_elements.png`, `accueil.isc.ckd`).
- Causa raíz: Al corregir el Bug 5.7, FMOD logró abrir y parsear los archivos Ogg Vorbis. El motor (`AndroidAudioPlayer::load`) llama a `FMOD_System_CreateStream` para TODOS los sonidos y pistas de música. En Android, abrir cientos de streams concurrentes no es problema, pero en PS Vita el kernel de Sony impone un límite estricto de descriptores de archivos simultáneos por proceso (~32/64). Al abrir los primeros ~30 sonidos en streaming, se agotaron los descriptores disponibles (`EMFILE`). Todas las llamadas posteriores a `fopen` fallaron silenciosamente o retornaron NULL, impidiendo cargar `accueil.isc.ckd` y desatando el crash.
- Solución: Se implementó un wrapper `FMOD_System_CreateStream_soloader` en `source/reimpl/audio.c` registrado en `source/dynlib.c`. Solo la música de fondo (`/music/`) se crea como stream continuo (1 descriptor activo). Todos los efectos de sonido (`/waves/`) se crean mediante `FMOD_System_CreateSound`, que decodifica/carga la muestra en RAM (los 500 efectos de sonido pesan solo 1.5 MB en total) y cierra el `FILE*` inmediatamente, manteniendo la tabla de descriptores de archivos del sistema prácticamente vacía para las texturas, shaders y escenas del juego.

## Fase 5 en adelante: Estado Actual
- F5 gráficos: 60 FPS estables sin MSAA 4X, shaders GXP cacheados en disco.
- F6/F7 input: Completo — touch screen y soporte nativo de gamepad del motor mapeado a los botones físicos de Vita.
- F8 audio: Completo — FMOD Ex Ogg Vorbis funcional sin error 25 y con gestión de descriptores de fichero (CreateSound para SFX, CreateStream para música).
- F9 vídeo: N/A (no hay vídeos en assets).
- F10 assets: Resueltos y completos en `ux0:data/raymanjunglerun/files/`.
- F11–F15: Overclock 444/222/222/166 MHz habilitado. VPK y eboot.bin generados exitosamente.
