/**
 * @file  main.c
 * @brief Rayman Jungle Run (com.pastagames.ro1mobile) loader entry point.
 *
 * Engine: custom PastaGames "ITF/Pasta" engine in libRO1Mobile.so (no
 * JNI_OnLoad, no NativeActivity, no RegisterNatives -- confirmed with
 * `nm -D`: only Java_* exports). Audio: FMOD Ex via libfmodex.so
 * (DT_NEEDED, 16x FMOD_* U symbols) loaded as a second module first so
 * so_resolve() falls back to it (same pattern as Riptide-GP-vita).
 *
 * This main thread plays the role of the Android UI + GL threads:
 *   GameActivity.onCreate()   -> nativeSet*() setters below
 *   onSurfaceChanged(960,544) -> nativeSetWidth/Height + nativeResume
 *   onDrawFrame() first frame -> nativeCreate() + nativeStart()
 *   onDrawFrame() every frame -> nativeRun() + gl_swap()
 * See decompiled/apk_jadx/sources/com/pastagames/android/GameActivity.java
 * (onCreate/onSurfaceChanged/onDrawFrame) and RO1GameActivity.loadProperties.
 */

#include "utils/init.h"
#include "utils/glutil.h"
#include "utils/logger.h"
#include "controls.h"
#include "reimpl/audio.h"

#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <falso_jni/FalsoJNI.h>
#include <so_util/so_util.h>

int _newlib_heap_size_user = 256 * 1024 * 1024;

#ifdef USE_SCELIBC_IO
int sceLibcHeapSize = 4 * 1024 * 1024;
#endif

so_module so_mod;         // libRO1Mobile.so (engine)
so_module so_mod_fmodex;  // libfmodex.so (FMOD Ex, NEEDED by engine)

// Game data root. The APK's asset zip (assets/LUGndna...) unpacks its
// "files/..." entries into the app dir, so on Android the engine root is
// <app>/files (LaunchActivity.unZip + GameActivity.onCreate browseDir).
// Saves stay under DATA_PATH (persistence root = internal storage).
#define GAME_ROOT DATA_PATH "files/"

#define GAME_W 960
#define GAME_H 544

// Stack for the thread that runs the engine. Android runs all of this on the
// GLSurfaceView render thread (Java thread, far more than 256 KB); ITF's
// scene loading recurses deeply. Run 006 overflowed vitasdk's default 256 KB
// main-thread stack (SP 0x814e49d0 vs stack base 0x81501000, stop reason
// 0x10006) while loading pasta/final/accueil.isc.
#define GAME_THREAD_STACK_SIZE (4 * 1024 * 1024)

static void *game_main(void *arg) {
    soloader_init_all();

    // This engine has no JNI_OnLoad (verified: `nm -D` shows no such
    // export, only Java_*). Older boilerplate called it unconditionally,
    // which would jump to NULL. Guard it so a future .so with JNI_OnLoad
    // still works.
    int (*JNI_OnLoad)(void *jvm) = (void *)so_symbol(&so_mod, "JNI_OnLoad");
    if (JNI_OnLoad) {
        JNI_OnLoad(&jvm);
        l_success("JNI_OnLoad done.");
    } else {
        l_info("No JNI_OnLoad in libRO1Mobile.so (expected).");
    }

    gl_init();
    l_success("vitaGL initialized.");

    // ---- GameActivity.onCreate() equivalents (setter phase) ----
    // All symbols below verified present (`nm -D` ... T Java_...).
    void (*nativeSetLanguage)(void *, void *, void *) = NULL;
    void (*nativeSetRootDirectory)(void *, void *, void *) = NULL;
    void (*nativeSetPersistenceRootDirectory)(void *, void *, void *) = NULL;
    void (*nativeSetWriteTempRootDirectory)(void *, void *, void *) = NULL;
    void (*nativeSetBrowseDirectory)(void *, void *, void *) = NULL;
    void (*nativeSetWidth)(void *, void *, int) = NULL;
    void (*nativeSetHeight)(void *, void *, int) = NULL;
    // JNI stub signatures: (JNIEnv*, jobject, ...) -- FalsoJNI passes its
    // own env; we call with NULL jobject (no real Activity object).
    *(void **)&nativeSetLanguage = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetLanguage");
    *(void **)&nativeSetRootDirectory = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetRootDirectory");
    *(void **)&nativeSetPersistenceRootDirectory = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetPersistenceRootDirectory");
    *(void **)&nativeSetWriteTempRootDirectory = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetWriteTempRootDirectory");
    *(void **)&nativeSetBrowseDirectory = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetBrowseDirectory");
    *(void **)&nativeSetWidth = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetWidth");
    *(void **)&nativeSetHeight = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeSetHeight");

    // String args need real jstrings; FalsoJNI provides jni->NewStringUTF.
    // If any setter is missing we log and continue -- the engine keeps
    // built-in defaults (matches Android behaviour when a setter is skipped).
    // System language detection with fallback
    int sys_lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
    sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &sys_lang);
    const char *lang_str = "en";
    switch (sys_lang) {
        case SCE_SYSTEM_PARAM_LANG_FRENCH:              lang_str = "fr"; break;
        case SCE_SYSTEM_PARAM_LANG_SPANISH:             lang_str = "es"; break;
        case SCE_SYSTEM_PARAM_LANG_GERMAN:              lang_str = "de"; break;
        case SCE_SYSTEM_PARAM_LANG_ITALIAN:             lang_str = "it"; break;
        case SCE_SYSTEM_PARAM_LANG_JAPANESE:            lang_str = "ja"; break;
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT:
        case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR:       lang_str = "pt"; break;
        case SCE_SYSTEM_PARAM_LANG_RUSSIAN:             lang_str = "ru"; break;
        case SCE_SYSTEM_PARAM_LANG_KOREAN:              lang_str = "ko"; break;
        case SCE_SYSTEM_PARAM_LANG_CHINESE_T:
        case SCE_SYSTEM_PARAM_LANG_CHINESE_S:           lang_str = "zh"; break;
        case SCE_SYSTEM_PARAM_LANG_ENGLISH_US:
        case SCE_SYSTEM_PARAM_LANG_ENGLISH_GB:
        default:                                        lang_str = "en"; break;
    }

    if (nativeSetLanguage) {
        jstring lang = jni->NewStringUTF(&jni, lang_str);
        nativeSetLanguage(&jni, NULL, lang);
        l_success("nativeSetLanguage(%s) done.", lang_str);
    } else l_error("nativeSetLanguage not found");
    if (nativeSetRootDirectory) {
        jstring p = jni->NewStringUTF(&jni, GAME_ROOT);
        nativeSetRootDirectory(&jni, NULL, p);
        l_success("nativeSetRootDirectory(%s) done.", GAME_ROOT);
    } else l_error("nativeSetRootDirectory not found");
    if (nativeSetPersistenceRootDirectory) {
        jstring p = jni->NewStringUTF(&jni, DATA_PATH);
        nativeSetPersistenceRootDirectory(&jni, NULL, p);
        l_success("nativeSetPersistenceRootDirectory done.");
    } else l_error("nativeSetPersistenceRootDirectory not found");
    if (nativeSetWriteTempRootDirectory) {
        jstring p = jni->NewStringUTF(&jni, GAME_ROOT);
        nativeSetWriteTempRootDirectory(&jni, NULL, p);
        l_success("nativeSetWriteTempRootDirectory done.");
    } else l_error("nativeSetWriteTempRootDirectory not found");
    if (nativeSetBrowseDirectory) {
        jstring p = jni->NewStringUTF(&jni, GAME_ROOT);
        nativeSetBrowseDirectory(&jni, NULL, p);
        l_success("nativeSetBrowseDirectory done.");
    } else l_error("nativeSetBrowseDirectory not found");
    // onSurfaceChanged() always reports landscape (width > height branch in
    // GameActivity.java swaps otherwise).
    if (nativeSetWidth) nativeSetWidth(&jni, NULL, GAME_W);
    else l_error("nativeSetWidth not found");
    if (nativeSetHeight) nativeSetHeight(&jni, NULL, GAME_H);
    else l_error("nativeSetHeight not found");

    void (*nativeCreate)(void *, void *) = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeCreate");
    void (*nativeStart)(void *, void *) = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeStart");
    void (*nativeResume)(void *, void *) = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeResume");
    void (*nativeUpdateSize)(void *, void *) = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeUpdateSize");
    int (*nativeRun)(void *, void *) = (void *)so_symbol(&so_mod, "Java_com_pastagames_android_GameActivity_nativeRun");
    if (!nativeCreate || !nativeStart || !nativeRun) {
        l_fatal("Missing lifecycle exports (create=%p start=%p run=%p).",
                nativeCreate, nativeStart, nativeRun);
        sceKernelExitProcess(0);
    }

    // onDrawFrame() first-frame path: nativeCreate() + nativeStart().
    nativeCreate(&jni, NULL);
    l_success("nativeCreate done.");
    nativeStart(&jni, NULL);
    l_success("nativeStart done.");
    // onSurfaceChanged() tail: nativeUpdateSize() (bInited) + nativeResume().
    if (nativeUpdateSize) nativeUpdateSize(&jni, NULL);
    if (nativeResume) nativeResume(&jni, NULL);
    l_success("nativeResume done.");

    // Initialize controls (touch + buttons)
    controls_init();

    // Start native FMOD AudioTrack pump
    audio_start();
    l_success("audio started, entering frame loop.");

    // onDrawFrame() steady state: controls_poll() + nativeRun() per GL frame.
    while (1) {
        controls_poll();
        nativeRun(&jni, NULL);
        gl_swap();
    }

    return NULL;
}

int main() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GAME_THREAD_STACK_SIZE);
    pthread_t t;
    if (pthread_create(&t, &attr, game_main, NULL) != 0) {
        // Fall back to the main thread (256 KB stack) rather than not booting.
        game_main(NULL);
    } else {
        pthread_join(t, NULL);
    }
    sceKernelExitDeleteThread(0);
}
