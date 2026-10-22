/**
 * @file  java.c
 * @brief FalsoJNI tables for Rayman Jungle Run (com.pastagames.ro1mobile).
 *
 * Method names come from the strings libRO1Mobile.so passes to GetMethodID
 * (see `strings libRO1Mobile.so | grep -E "makeDir|readAsset|..."`), from the
 * _Z11javaMakeDirPKc / _Z13javaReadAssetPKc / ... wrappers inside the .so,
 * and cross-checked against jadx:
 * decompiled/apk_jadx/sources/com/pastagames/android/GameActivity.java.
 * FalsoJNI resolves method IDs by name only, so one entry per name is enough.
 *
 * Boot-path methods (engine really calls these before the first frame) have
 * REAL implementations below: makeDir, assetExists, getDLCName,
 * ignoreCasePath, isTablet, shouldClearData, isNetworkAvailable,
 * isGamePadConnected / isGamePadKeyPressed / IsRemote, scoringEnabled /
 * shareScore, isBillingTransactionsRestorable, displayWarningDialog(+FromRes),
 * quitApp. Everything else (store, push/GCM, Flurry, Chartboost, Moga,
 * browser, wallpaper) is a safe no-op/false/NULL: Vita has no Play Store,
 * no GCM, no browser intent system.
 *
 * NOTE: readAsset returns NULL. The loader drives the
 * resourcesInAsset=false branch of GameActivity.onCreate (files on disk, no
 * nativeSetAssetManager call), so the engine reads game files with plain
 * fopen() and never needs the AssetManager byte[] path. If the first-boot
 * log ever shows readAsset being called, wire it with NewByteArray +
 * SetByteArrayRegion.
 */

#include <falso_jni/FalsoJNI.h>
#include <falso_jni/FalsoJNI_Impl.h>
#include <falso_jni/FalsoJNI_Logger.h>

#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include "utils/logger.h"
#include "controls.h"

enum {
    M_MAKE_DIR = 1,
    M_ASSET_EXISTS,
    M_READ_ASSET,
    M_GET_DLC_NAME,
    M_IS_BILLING_RESTORABLE,
    M_RESTORE_TRANSACTIONS,
    M_STORE_BUY_PRODUCT,
    M_OPEN_PRODUCT_PAGE,
    M_SET_ASSET_AS_WALLPAPER,
    M_IGNORE_CASE_PATH,
    M_SET_CAN_QUIT,
    M_SHOW_TEXT_FIELD,
    M_HIDE_TEXT_FIELD,
    M_IS_TABLET,
    M_SHOULD_CLEAR_DATA,
    M_GET_GAMEPAD_AXIS,
    M_IS_GAMEPAD_CONNECTED,
    M_IS_GAMEPAD_KEY_PRESSED,
    M_IS_MOGA_CONNECTED,
    M_IS_MOGA_ACTIVE,
    M_IS_REMOTE,
    M_OPEN_URL_BROWSER,
    M_DISPLAY_WARNING,
    M_DISPLAY_WARNING_RES,
    M_SCORING_ENABLED,
    M_SHARE_SCORE,
    M_SHOW_LEADERBOARD,
    M_SHOW_LEADERBOARDS,
    M_SHOW_ACHIEVEMENTS,
    M_DISPLAY_DASHBOARD,
    M_UNLOCK_ACHIEVEMENT,
    M_QUIT_APP,
    M_IS_NETWORK_AVAILABLE,
    M_SET_PLAYER_NAME,
    M_GET_INPUT_DEVICE_STATE,
    M_REGISTER_UPN,
    M_GET_REGISTRATION_ID,
    M_MAKE_SERVER_REG,
    M_SET_REGISTRATION_ID,
    M_REGISTER_PUSH,
    M_INIT_PUSH,
    M_HAS_PHONE_STATE,
    M_HAS_ACCOUNTS,
    M_CREATE_STORE,
    M_CREATE_LOGGER,
    M_FMOD_START,
    M_FMOD_STOP,
    M_FMOD_RUN,
    M_FMOD_SHUTDOWN,
    M_FMOD_IS_MIXING,
    M_FMOD_FETCH_CONFIG,

    M_VOID_FIRST = 100,
    M_VOID_NOOP = M_VOID_FIRST, // bucket for every other void Java method
};

NameToMethodID nameToMethodId[] = {
    { M_MAKE_DIR,               "makeDir",                 METHOD_TYPE_BOOLEAN },
    { M_ASSET_EXISTS,           "assetExists",             METHOD_TYPE_BOOLEAN },
    { M_READ_ASSET,             "readAsset",               METHOD_TYPE_OBJECT },
    { M_GET_DLC_NAME,           "getDLCName",              METHOD_TYPE_OBJECT },
    { M_IS_BILLING_RESTORABLE,  "isBillingTransactionsRestorable", METHOD_TYPE_BOOLEAN },
    { M_RESTORE_TRANSACTIONS,   "restoreTransactions",     METHOD_TYPE_VOID },
    { M_STORE_BUY_PRODUCT,      "storeBuyProduct",         METHOD_TYPE_VOID },
    { M_OPEN_PRODUCT_PAGE,      "openProductPage",         METHOD_TYPE_VOID },
    { M_SET_ASSET_AS_WALLPAPER, "setAssetFileAsWallPaper", METHOD_TYPE_VOID },
    { M_IGNORE_CASE_PATH,       "ignoreCasePath",          METHOD_TYPE_OBJECT },
    { M_SET_CAN_QUIT,           "setCanQuit",              METHOD_TYPE_VOID },
    { M_SHOW_TEXT_FIELD,        "showTextFieldAndKeyboard", METHOD_TYPE_VOID },
    { M_HIDE_TEXT_FIELD,        "hideTextFieldAndKeyboard", METHOD_TYPE_VOID },
    { M_IS_TABLET,              "isTablet",                METHOD_TYPE_BOOLEAN },
    { M_SHOULD_CLEAR_DATA,      "shouldClearData",         METHOD_TYPE_BOOLEAN },
    { M_GET_GAMEPAD_AXIS,       "getGamePadAxisValues",    METHOD_TYPE_VOID },
    { M_IS_GAMEPAD_CONNECTED,   "isGamePadConnected",      METHOD_TYPE_BOOLEAN },
    { M_IS_GAMEPAD_KEY_PRESSED, "isGamePadKeyPressed",     METHOD_TYPE_BOOLEAN },
    { M_IS_MOGA_CONNECTED,      "isMogaControllerConnected", METHOD_TYPE_BOOLEAN },
    { M_IS_MOGA_ACTIVE,         "isMogaControllerActive",  METHOD_TYPE_BOOLEAN },
    { M_IS_REMOTE,              "IsRemote",                METHOD_TYPE_BOOLEAN },
    { M_OPEN_URL_BROWSER,       "openURLInBrowser",        METHOD_TYPE_VOID },
    { M_DISPLAY_WARNING,        "displayWarningDialog",    METHOD_TYPE_VOID },
    { M_DISPLAY_WARNING_RES,    "displayWarningDialogFromRes", METHOD_TYPE_VOID },
    { M_SCORING_ENABLED,        "scoringEnabled",          METHOD_TYPE_BOOLEAN },
    { M_SHARE_SCORE,            "shareScore",              METHOD_TYPE_BOOLEAN },
    { M_SHOW_LEADERBOARD,       "showLeaderboard",         METHOD_TYPE_VOID },
    { M_SHOW_LEADERBOARDS,      "showLeaderboards",        METHOD_TYPE_VOID },
    { M_SHOW_ACHIEVEMENTS,      "showAchievements",        METHOD_TYPE_VOID },
    { M_DISPLAY_DASHBOARD,      "displayDashboard",        METHOD_TYPE_VOID },
    { M_UNLOCK_ACHIEVEMENT,     "unlockAchievement",       METHOD_TYPE_VOID },
    { M_QUIT_APP,               "quitApp",                 METHOD_TYPE_VOID },
    { M_IS_NETWORK_AVAILABLE,   "isNetworkAvailable",      METHOD_TYPE_BOOLEAN },
    { M_SET_PLAYER_NAME,        "setPlayerName",           METHOD_TYPE_VOID },
    { M_GET_INPUT_DEVICE_STATE, "getInputDeviceState",     METHOD_TYPE_OBJECT },
    { M_REGISTER_UPN,           "registerUpn",             METHOD_TYPE_OBJECT },
    { M_GET_REGISTRATION_ID,    "getRegistrationId",       METHOD_TYPE_OBJECT },
    { M_MAKE_SERVER_REG,        "MakeServerRegistration",  METHOD_TYPE_BOOLEAN },
    { M_SET_REGISTRATION_ID,    "setRegistrationId",       METHOD_TYPE_VOID },
    { M_REGISTER_PUSH,          "registerPushNotification", METHOD_TYPE_VOID },
    { M_INIT_PUSH,              "initPushNotifications",   METHOD_TYPE_VOID },
    { M_HAS_PHONE_STATE,        "HasPhoneStatePermission", METHOD_TYPE_BOOLEAN },
    { M_HAS_ACCOUNTS,           "HasAccountsPermission",   METHOD_TYPE_BOOLEAN },
    { M_CREATE_STORE,           "createStore",             METHOD_TYPE_OBJECT },
    { M_CREATE_LOGGER,          "createGameBehaviourLogger", METHOD_TYPE_OBJECT },
    { M_FMOD_START,             "start",                   METHOD_TYPE_VOID },
    { M_FMOD_STOP,              "stop",                    METHOD_TYPE_VOID },
    { M_FMOD_RUN,               "run",                     METHOD_TYPE_VOID },
    { M_FMOD_SHUTDOWN,          "shutDown",                METHOD_TYPE_VOID },
    { M_FMOD_IS_MIXING,         "isMixing",                METHOD_TYPE_BOOLEAN },
    { M_FMOD_FETCH_CONFIG,      "fetchChannelConfigFromCount", METHOD_TYPE_INT },
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static const char *jstr(jstring s) {
    if (!s)
        return NULL;
    return jni->GetStringUTFChars(&jni, s, NULL);
}

static void jstr_rel(jstring s, const char *c) {
    if (s && c)
        jni->ReleaseStringUTFChars(&jni, s, (char *)c);
}

static jboolean retFalse(jmethodID id, va_list args) {
    (void)id; (void)args;
    return JNI_FALSE;
}

static void voidNoop(jmethodID id, va_list args) {
    (void)args;
    fjni_logv_dbg("JNI: void method %i ignored", (int)id);
}

/* ------------------------------------------------------------------ */
/* boot-path implementations                                          */
/* ------------------------------------------------------------------ */

// GameActivity.makeDir(String): engine creates saves/ + data dirs.
// Mirror it with sceIoMkdir (single level is enough: parents are created
// in order by the engine, same as mkdirs() on the happy path).
static jboolean java_makeDir(jmethodID id, va_list args) {
    (void)id;
    jstring name = va_arg(args, jstring);
    const char *c = jstr(name);
    if (!c)
        return JNI_FALSE;
    int ret = sceIoMkdir(c, 0777);
    // EEXIST == already there == success for mkdirs() semantics.
    jboolean ok = (ret >= 0 || ret == 0x80010011) ? JNI_TRUE : JNI_FALSE;
    l_debug("JNI makeDir(%s) -> %s (0x%x)", c, ok ? "true" : "false", ret);
    jstr_rel(name, c);
    return ok;
}

// GameActivity.assetExists(String): file-on-disk check (offline branch).
static jboolean java_assetExists(jmethodID id, va_list args) {
    (void)id;
    jstring name = va_arg(args, jstring);
    const char *c = jstr(name);
    if (!c)
        return JNI_FALSE;
    SceUID f = sceIoOpen(c, SCE_O_RDONLY, 0);
    jboolean ok = (f >= 0) ? JNI_TRUE : JNI_FALSE;
    if (f >= 0)
        sceIoClose(f);
    l_debug("JNI assetExists(%s) -> %s", c, ok ? "true" : "false");
    jstr_rel(name, c);
    return ok;
}

// GameActivity.getDLCName(int): return standard product IDs for DLC items
// (index 0 = "skins1", index 1 = "rjr_unlock_all") so the engine can query
// DLC status properly offline.
static jobject java_getDLCName(jmethodID id, va_list args) {
    (void)id;
    jint index = va_arg(args, jint);
    if (index == 0)
        return (jobject)jni->NewStringUTF(&jni, "skins1");
    if (index == 1)
        return (jobject)jni->NewStringUTF(&jni, "rjr_unlock_all");
    return (jobject)jni->NewStringUTF(&jni, "no");
}

// GameActivity.ignoreCasePath(String): Vita FS is case-sensitive and the
// data files ship with exact case, so the input path is already correct.
static jobject java_ignoreCasePath(jmethodID id, va_list args) {
    (void)id;
    return va_arg(args, jobject);
}

// GameActivity.displayWarningDialog(String,String): never fatal -- the
// engine shows these for no-network/no-store conditions which are the
// normal state on Vita. Log them so the boot log keeps the evidence.
static void java_displayWarning(jmethodID id, va_list args) {
    (void)id;
    jstring t = va_arg(args, jstring);
    jstring m = va_arg(args, jstring);
    const char *ct = jstr(t);
    const char *cm = jstr(m);
    l_error("JNI displayWarningDialog: [%s] %s",
            ct ? ct : "", cm ? cm : "");
    jstr_rel(t, ct);
    jstr_rel(m, cm);
}

// GameActivity.quitApp(): the engine asked to quit (back button with an
// empty stack). The honest mapping on Vita is exiting the process.
static void java_quitApp(jmethodID id, va_list args) {
    (void)id; (void)args;
    l_info("JNI quitApp(): engine requested quit, exiting.");
    sceKernelExitProcess(0);
}

// Push/UPN registration: no GCM on Vita. Return NULL/empty so the engine
// takes its offline branch (pushMessage stays NULL, nativeNotifyPush... is
// never called with data).
static jobject java_nullObject(jmethodID id, va_list args) {
    (void)id; (void)args;
    return NULL;
}

static jobject java_emptyString(jmethodID id, va_list args) {
    (void)id; (void)args;
    return (jobject)jni->NewStringUTF(&jni, "");
}

// Gamepad: the Vita buttons are the engine's "connected pad" (controls.c).
static jboolean java_isGamePadKeyPressed(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jint); // pad index, Vita has one
    int keycode = va_arg(args, jint);
    return controls_pad_key_pressed(keycode);
}

static void java_getGamePadAxisValues(jmethodID id, va_list args) {
    (void)id;
    int pad = va_arg(args, jint);
    controls_pad_update_axis(pad);
}

static jboolean retTrue(jmethodID id, va_list args) {
    (void)id; (void)args;
    return JNI_TRUE;
}

// FMODAudioDevice.fetchChannelConfigFromCount: silent stub (Phase 8 will
// bridge FMOD -> sceAudioOut; 0 channels keeps the mixer idle).
static jint java_fmodFetchConfig(jmethodID id, va_list args) {
    (void)id;
    (void)va_arg(args, jint);
    return 0;
}

MethodsBoolean methodsBoolean[] = {
    { M_MAKE_DIR,              java_makeDir },
    { M_ASSET_EXISTS,          java_assetExists },
    { M_IS_TABLET,             retFalse },
    { M_SHOULD_CLEAR_DATA,     retFalse },
    { M_IS_GAMEPAD_CONNECTED,  retTrue },
    { M_IS_GAMEPAD_KEY_PRESSED, java_isGamePadKeyPressed },
    { M_IS_MOGA_CONNECTED,     retFalse },
    { M_IS_MOGA_ACTIVE,        retFalse },
    { M_IS_REMOTE,             retFalse },
    { M_SCORING_ENABLED,       retFalse },
    { M_SHARE_SCORE,           retFalse },
    { M_IS_NETWORK_AVAILABLE,  retFalse },
    { M_IS_BILLING_RESTORABLE, retFalse },
    { M_MAKE_SERVER_REG,       retFalse },
    { M_HAS_PHONE_STATE,       retFalse },
    { M_HAS_ACCOUNTS,          retFalse },
    { M_FMOD_IS_MIXING,        retFalse },
};
MethodsByte methodsByte[] = {};
MethodsChar methodsChar[] = {};
MethodsDouble methodsDouble[] = {};
MethodsFloat methodsFloat[] = {};
MethodsInt methodsInt[] = {
    { M_FMOD_FETCH_CONFIG, java_fmodFetchConfig },
};
MethodsLong methodsLong[] = {};
MethodsObject methodsObject[] = {
    { M_READ_ASSET,            java_nullObject }, // offline file mode, see note above
    { M_GET_DLC_NAME,          java_getDLCName },
    { M_IGNORE_CASE_PATH,      java_ignoreCasePath },
    { M_GET_INPUT_DEVICE_STATE, java_nullObject }, // no BT/gamepad scan on Vita (Phase 7)
    { M_REGISTER_UPN,          java_nullObject },
    { M_GET_REGISTRATION_ID,   java_emptyString },
    { M_CREATE_STORE,          java_nullObject }, // no Play/Amazon store (Phase 10 docs)
    { M_CREATE_LOGGER,         java_nullObject }, // no Flurry analytics
};
MethodsShort methodsShort[] = {};
MethodsVoid methodsVoid[] = {
    { M_RESTORE_TRANSACTIONS,  voidNoop },
    { M_STORE_BUY_PRODUCT,     voidNoop },
    { M_OPEN_PRODUCT_PAGE,     voidNoop },
    { M_SET_ASSET_AS_WALLPAPER, voidNoop },
    { M_SET_CAN_QUIT,          voidNoop },
    { M_SHOW_TEXT_FIELD,       voidNoop },
    { M_HIDE_TEXT_FIELD,       voidNoop },
    { M_GET_GAMEPAD_AXIS,      java_getGamePadAxisValues },
    { M_OPEN_URL_BROWSER,      voidNoop },
    { M_DISPLAY_WARNING,       java_displayWarning },
    { M_DISPLAY_WARNING_RES,   voidNoop },
    { M_SHOW_LEADERBOARD,      voidNoop },
    { M_SHOW_LEADERBOARDS,     voidNoop },
    { M_SHOW_ACHIEVEMENTS,     voidNoop },
    { M_DISPLAY_DASHBOARD,     voidNoop },
    { M_UNLOCK_ACHIEVEMENT,    voidNoop }, // Phase 13: sceNpTrophyUnlock hook
    { M_QUIT_APP,              java_quitApp },
    { M_SET_PLAYER_NAME,       voidNoop },
    { M_SET_REGISTRATION_ID,   voidNoop },
    { M_REGISTER_PUSH,         voidNoop },
    { M_INIT_PUSH,             voidNoop },
    { M_FMOD_START,            voidNoop },
    { M_FMOD_STOP,             voidNoop },
    { M_FMOD_RUN,              voidNoop },
    { M_FMOD_SHUTDOWN,         voidNoop },
};

/*
 * JNI Fields
 */

// System-wide constant that applications sometimes request
// https://developer.android.com/reference/android/content/Context.html#WINDOW_SERVICE
char WINDOW_SERVICE[] = "window";

// System-wide constant that's often used to determine Android version
// https://developer.android.com/reference/android/os/Build.VERSION.html#SDK_INT
// Possible values: https://developer.android.com/reference/android/os/Build.VERSION_CODES
const int SDK_INT = 19; // Android 4.4 / KitKat

NameToFieldID nameToFieldId[] = {
		{ 0, "WINDOW_SERVICE", FIELD_TYPE_OBJECT }, 
		{ 1, "SDK_INT", FIELD_TYPE_INT },
};

FieldsBoolean fieldsBoolean[] = {};
FieldsByte fieldsByte[] = {};
FieldsChar fieldsChar[] = {};
FieldsDouble fieldsDouble[] = {};
FieldsFloat fieldsFloat[] = {};
FieldsInt fieldsInt[] = {
		{ 1, SDK_INT },
};
FieldsObject fieldsObject[] = {
		{ 0, WINDOW_SERVICE },
};
FieldsLong fieldsLong[] = {};
FieldsShort fieldsShort[] = {};

__FALSOJNI_IMPL_CONTAINER_SIZES

/* ---------------------------------------------------------------------------
 * Runtime overrides of stock FalsoJNI behaviour
 * ------------------------------------------------------------------------ */

extern struct JNINativeInterface *_jni;

// FMOD's Java_org_fmod_FMODAudioDevice_fmodProcess() writes the mix into
// GetDirectBufferAddress(byteBuffer). audio.c passes a raw malloc'ed buffer
// as the "ByteBuffer", so hand the pointer straight back (stock returns NULL).
static void *GetDirectBufferAddress_passthrough(JNIEnv *env, jobject buf) {
    (void)env;
    return (void *)buf;
}

void java_overrides_init(void) {
    _jni->GetDirectBufferAddress = GetDirectBufferAddress_passthrough;
}

