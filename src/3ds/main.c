#include "../data_win.h"
#include "../json_reader.h"
#include "../runner.h"
#include "../runner_gamepad.h"
#include "../runner_keyboard.h"
#include "../runner_mouse.h"
#include "../utils.h"
#include "../vm.h"
#include "../log.h"
#include "../audio/3ds/3ds_audio_system.h"
#include "3ds_overlay.h"
#include "3ds_file_system.h"
#include "3ds_gamepad.h"
#include "3ds_log.h"
#include "3ds_platform_config.h"
#include "3ds_renderer.h"
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <dirent.h>
#include <stb/ds/stb_ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define N3DS_ENABLE_TOUCH_MOUSE 1
#define N3DS_ATLAS_MAGIC 0x5441334Eu
#define N3DS_ATLAS_VERSION 2u
#define N3DS_KEY_MAPPING_COUNT 21
#define N3DS_TOP_SCREEN_WIDTH 400
#define N3DS_TOP_SCREEN_HEIGHT 240
#define N3DS_BOTTOM_SCREEN_WIDTH 320
#define N3DS_BOTTOM_SCREEN_HEIGHT 240
#define N3DS_ATLAS_TEXTURE_CACHE_SIZE_MAX_MB 60
#define N3DS_SDMC_ROOT "sdmc:/3ds/cinnamon"
#define N3DS_SETTINGS_PATH N3DS_SDMC_ROOT "/cinnamon_settings.json"
#define N3DS_ROMFS_DATA_WIN_PATH "romfs:/data.win"

typedef struct {
    bool debugLogEnabled;
    bool debugStatsEnabled;
    bool gamepadApiEnabled;
    bool cacheAtlas;
    bool halfResApplicationSurface;
    YoYoOperatingSystem osType;
    uint32_t atlasTextureCacheBytes;
    int32_t keyMappings[N3DS_KEY_MAPPING_COUNT];
} N3DSRunnerSettings;

typedef struct {
    const char* jsonKey;
    uint32_t button;
} N3DSKeyBinding;

static const N3DSKeyBinding N3DS_KEY_BINDINGS[N3DS_KEY_MAPPING_COUNT] = {
    {"dpadLeft", KEY_DLEFT},
    {"dpadDown", KEY_DDOWN},
    {"dpadRight", KEY_DRIGHT},
    {"dpadUp", KEY_DUP},
    {"circleLeft", KEY_CPAD_LEFT},
    {"circleDown", KEY_CPAD_DOWN},
    {"circleUp", KEY_CPAD_UP},
    {"circleRight", KEY_CPAD_RIGHT},
    {"a", KEY_A},
    {"b", KEY_B},
    {"x", KEY_X},
    {"y", KEY_Y},
    {"start", KEY_START},
    {"l", KEY_L},
    {"r", KEY_R},
    {"cstickLeft", KEY_CSTICK_LEFT},
    {"cstickDown", KEY_CSTICK_DOWN},
    {"cstickUp", KEY_CSTICK_UP},
    {"cstickRight", KEY_CSTICK_RIGHT},
    {"zl", KEY_ZL},
    {"zr", KEY_ZR},
};

static bool n3ds_appGetWindowSize(int32_t* outW, int32_t* outH) {
    if (outW != NULL) *outW = N3DS_TOP_SCREEN_WIDTH;
    if (outH != NULL) *outH = N3DS_TOP_SCREEN_HEIGHT;
    return true;
}

static void n3ds_appSetWindowSize(MAYBE_UNUSED int32_t width, MAYBE_UNUSED int32_t height) {}

static bool n3ds_appJsonBool(const JsonValue* object, const char* key, bool defaultValue) {
    if (!JsonReader_isObject(object)) return defaultValue;
    JsonValue* value = JsonReader_getJsonValueByKey(object, key);
    return JsonReader_isBool(value) ? JsonReader_getBool(value) : defaultValue;
}

static int32_t n3ds_appJsonInt(const JsonValue* object, const char* key, int32_t defaultValue) {
    if (!JsonReader_isObject(object)) return defaultValue;
    JsonValue* value = JsonReader_getJsonValueByKey(object, key);
    return JsonReader_isNumber(value) ? (int32_t)JsonReader_getInt(value) : defaultValue;
}

static N3DSRunnerSettings n3ds_settingsLoad(const char* path) {
    N3DSRunnerSettings settings = {
        .debugLogEnabled = true,
        .debugStatsEnabled = false,
        .gamepadApiEnabled = true,
        .cacheAtlas = true,
        .halfResApplicationSurface = false,
        .osType = OS_WINDOWS,
        .atlasTextureCacheBytes = 0u,
    };
    for (uint32_t i = 0u; i < N3DS_KEY_MAPPING_COUNT; ++i) {
        settings.keyMappings[i] = -1;
    }

    JsonValue* root = NULL;
    FILE* file = fopen(path != NULL ? path : N3DS_SETTINGS_PATH, "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        rewind(file);
        if (size >= 0) {
            char* text = safeMalloc((size_t)size + 1u);
            size_t bytesRead = fread(text, 1u, (size_t)size, file);
            text[bytesRead] = '\0';
            root = JsonReader_parse(text);
            free(text);
        }
        fclose(file);
    }
    if (!JsonReader_isObject(root)) {
        JsonReader_free(root);
        return settings;
    }

    JsonValue* runner = JsonReader_getJsonValueByKey(root, "runner");
    settings.debugLogEnabled = n3ds_appJsonBool(runner, "debugLog", true);
    settings.debugStatsEnabled = n3ds_appJsonBool(runner, "debugStats", false);
    settings.gamepadApiEnabled = n3ds_appJsonBool(runner, "gamepadApi", true);
    settings.cacheAtlas = n3ds_appJsonBool(runner, "cacheAtlas", true);
    settings.osType = (YoYoOperatingSystem)n3ds_appJsonInt(runner, "osType", settings.osType);
    settings.halfResApplicationSurface =
        n3ds_appJsonBool(runner, "halfResolutionApplicationSurface", false);
    int32_t cacheMb = n3ds_appJsonInt(runner, "atlasTextureCacheMb", 0);
    if (cacheMb < 0) cacheMb = 0;
    if (cacheMb > N3DS_ATLAS_TEXTURE_CACHE_SIZE_MAX_MB) {
        cacheMb = N3DS_ATLAS_TEXTURE_CACHE_SIZE_MAX_MB;
    }
    settings.atlasTextureCacheBytes = (uint32_t)cacheMb * 1024u * 1024u;

    JsonValue* keyboard = JsonReader_getJsonValueByKey(root, "keyboard");
    for (uint32_t i = 0u; i < N3DS_KEY_MAPPING_COUNT; ++i) {
        settings.keyMappings[i] = n3ds_appJsonInt(keyboard, N3DS_KEY_BINDINGS[i].jsonKey, -1);
    }
    JsonReader_free(root);
    return settings;
}

static void n3ds_appSyncKey(RunnerKeyboardState* keyboard, bool* held, int32_t key, bool down) {
    if (keyboard == NULL || held == NULL || key < 0 || key >= GML_KEY_COUNT) return;
    if (down && !held[key]) RunnerKeyboard_onKeyDown(keyboard, key);
    if (!down && held[key]) RunnerKeyboard_onKeyUp(keyboard, key);
    held[key] = down;
}

static void n3ds_appSyncMappedKeyboard(
    Runner* runner, bool* keyboardHeld, u32 held, const N3DSRunnerSettings* settings) {
    bool desired[GML_KEY_COUNT];
    memset(desired, 0, sizeof(desired));

    for (uint32_t i = 0u; i < N3DS_KEY_MAPPING_COUNT; ++i) {
        int32_t key = settings != NULL ? settings->keyMappings[i] : -1;
        if (key >= 0 && key < GML_KEY_COUNT && (held & N3DS_KEY_BINDINGS[i].button) != 0u)
            desired[key] = true;
    }
    for (int32_t key = 0; key < GML_KEY_COUNT; ++key) {
        n3ds_appSyncKey(runner != NULL ? runner->keyboard : NULL, keyboardHeld, key, desired[key]);
    }
}

u32 __stacksize__ = 2u * 1024u * 1024u;

static char gN3DSDataWinStatusLine[128] = "Looking for data.win";

static float n3ds_appGetRoomFrameRate(const Runner* runner) {
    uint32_t targetFps = 30u;
    if (runner != NULL && runner->currentRoom != NULL && runner->currentRoom->speed > 0u) {
        targetFps = runner->currentRoom->speed;
    }
    return (float)(targetFps > 60u ? 60u : targetFps);
}

typedef struct {
    bool initialized;
    bool touchHeld;
    double cursorX;
    double cursorY;
} N3DSTouchMouseState;

#ifndef N3DS_USE_ROMFS
static int n3ds_compareGamePaths(const void* left, const void* right) {
    const char* a = *(const char* const*)left;
    const char* b = *(const char* const*)right;
    size_t aLength = (size_t)(strrchr(a, '/') - a);
    size_t bLength = (size_t)(strrchr(b, '/') - b);
    int order = strncmp(a, b, aLength < bLength ? aLength : bLength);
    return order != 0 ? order : (aLength > bLength) - (aLength < bLength);
}
#endif

static char* n3ds_chooseDataWinPath(bool citroReady, bool* cancelled) {
    *cancelled = false;
#ifdef N3DS_USE_ROMFS
    (void)citroReady;
    if (n3ds_fsPathExists(N3DS_ROMFS_DATA_WIN_PATH)) {
        snprintf(
            gN3DSDataWinStatusLine, sizeof(gN3DSDataWinStatusLine), "Using data.win from RomFS");
        return safeStrdup(N3DS_ROMFS_DATA_WIN_PATH);
    }
    snprintf(gN3DSDataWinStatusLine, sizeof(gN3DSDataWinStatusLine), "No data.win found in RomFS");
    return NULL;
#else
    const char* root = N3DS_SDMC_ROOT;
    snprintf(gN3DSDataWinStatusLine, sizeof(gN3DSDataWinStatusLine),
        "No game data found in /3ds/cinnamon/<game>/data.win");
    DIR* dir = opendir(root);
    if (dir == NULL) return NULL;

    char** gamePaths = NULL;
    while (true) {
        struct dirent* entry = readdir(dir);
        if (entry == NULL) break;
        const char* name = entry->d_name;
        if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;

        char folderPath[1024];
        int length = snprintf(folderPath, sizeof(folderPath), "%s/%s", root, name);
        if (length < 0 || (size_t)length >= sizeof(folderPath)) continue;
        if (!n3ds_fsPathIsDirectory(folderPath)) continue;

        char dataWinPath[1024];
        length = snprintf(dataWinPath, sizeof(dataWinPath), "%s/data.win", folderPath);
        if (length < 0 || (size_t)length >= sizeof(dataWinPath)) continue;
        struct stat st;
        if (stat(dataWinPath, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        arrput(gamePaths, safeStrdup(dataWinPath));
    }

    closedir(dir);
    int selected = 0;
    if (arrlen(gamePaths) > 1) {
        qsort(gamePaths, arrlen(gamePaths), sizeof(*gamePaths), n3ds_compareGamePaths);
        N3DSLoadingScreen selector = {0};
        n3ds_loadInit(&selector, citroReady);
        if (selector.target == NULL) {
            snprintf(gN3DSDataWinStatusLine, sizeof(gN3DSDataWinStatusLine),
                "Unable to display game selector");
            selected = -1;
        } else {
            selected = n3ds_loadSelectGame(&selector, gamePaths, (int)arrlen(gamePaths));
            *cancelled = selected < 0;
        }
        n3ds_loadFree(&selector);
    }
    char* selectedPath = NULL;
    if (arrlen(gamePaths) > 0 && selected >= 0) {
        snprintf(gN3DSDataWinStatusLine, sizeof(gN3DSDataWinStatusLine),
            "Using data.win from /3ds/cinnamon");
        selectedPath = safeStrdup(gamePaths[selected]);
    }
    repeat(arrlen(gamePaths), i) free(gamePaths[i]);
    arrfree(gamePaths);
    return selectedPath;
#endif
}

static void n3ds_appGetAppSize(
    const Runner* runner, const Gen8* gen8, int32_t* outW, int32_t* outH) {
    int32_t gameW = runner != NULL && runner->applicationWidth > 0 ? runner->applicationWidth : 0;
    int32_t gameH = runner != NULL && runner->applicationHeight > 0 ? runner->applicationHeight : 0;
    if (gameW <= 0 && gen8 != NULL) gameW = (int32_t)gen8->defaultWindowWidth;
    if (gameH <= 0 && gen8 != NULL) gameH = (int32_t)gen8->defaultWindowHeight;
    if (gameW <= 0) gameW = N3DS_TOP_SCREEN_WIDTH;
    if (gameH <= 0) gameH = N3DS_TOP_SCREEN_HEIGHT;
    if (outW != NULL) *outW = gameW;
    if (outH != NULL) *outH = gameH;
}

#if N3DS_ENABLE_TOUCH_MOUSE
static double n3ds_appClampDouble(double value, double minValue, double maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void n3ds_appSetMousePos(
    Runner* runner, double cursorX, double cursorY, int32_t gameW, int32_t gameH) {
    if (runner == NULL || runner->mouse == NULL) return;
    if (gameW <= 0) gameW = 1;
    if (gameH <= 0) gameH = 1;
    runner->mouse->screenX = cursorX;
    runner->mouse->screenY = cursorY;
    runner->mouse->normalizedX = cursorX / (double)gameW;
    runner->mouse->normalizedY = cursorY / (double)gameH;
}
#endif

static void n3ds_appSyncTouchMouse(
    N3DSTouchMouseState* state, Runner* runner, int32_t gameW, int32_t gameH, u32 held) {
    if (state == NULL || runner == NULL || runner->mouse == NULL) return;
    if (gameW <= 0) gameW = 1;
    if (gameH <= 0) gameH = 1;

    RunnerMouse_beginFrame(runner->mouse);
#if !N3DS_ENABLE_TOUCH_MOUSE
    (void)held;
    if (state->touchHeld) {
        RunnerMouse_onButtonUp(runner->mouse, GML_MB_LEFT);
    }
    state->touchHeld = false;
    state->initialized = false;
    return;
#else
    if (!state->initialized) {
        state->cursorX = (double)gameW * 0.5;
        state->cursorY = (double)gameH * 0.5;
        state->initialized = true;
    }

    state->cursorX = n3ds_appClampDouble(state->cursorX, 0.0, (double)gameW - 1.0);
    state->cursorY = n3ds_appClampDouble(state->cursorY, 0.0, (double)gameH - 1.0);

    bool touchHeld = (held & KEY_TOUCH) != 0;
    touchPosition touch;
    memset(&touch, 0, sizeof(touch));
    if (touchHeld) hidTouchRead(&touch);

    if (touchHeld) {
        state->cursorX =
            ((double)touch.px + 0.5) * (double)gameW / (double)N3DS_BOTTOM_SCREEN_WIDTH;
        state->cursorY =
            ((double)touch.py + 0.5) * (double)gameH / (double)N3DS_BOTTOM_SCREEN_HEIGHT;
        state->cursorX = n3ds_appClampDouble(state->cursorX, 0.0, (double)gameW - 1.0);
        state->cursorY = n3ds_appClampDouble(state->cursorY, 0.0, (double)gameH - 1.0);
        if (!state->touchHeld) RunnerMouse_onButtonDown(runner->mouse, GML_MB_LEFT);
    } else if (state->touchHeld) {
        RunnerMouse_onButtonUp(runner->mouse, GML_MB_LEFT);
    }

    state->touchHeld = touchHeld;
    n3ds_appSetMousePos(runner, state->cursorX, state->cursorY, gameW, gameH);
#endif
}

static void n3ds_appOnDataWinProgress(const char* chunkName, int chunkIndex, int totalChunks,
    DataWin* dataWin, void* userData) {
    N3DSLoadingScreen* screen = (N3DSLoadingScreen*)userData;
    if (screen == NULL) return;
    if (totalChunks <= 0) totalChunks = 1;
    if (chunkIndex < 0) chunkIndex = 0;
    if (chunkIndex >= totalChunks) chunkIndex = totalChunks - 1;

    if (dataWin != NULL && dataWin->gen8.displayName != NULL)
        n3ds_loadSetGameName(screen, dataWin->gen8.displayName);
    char status[128];
    snprintf(status, sizeof(status), "Loading %.4s... (%d/%d)",
        chunkName != NULL ? chunkName : "data", chunkIndex + 1, totalChunks);
    n3ds_loadSet(screen, status, chunkIndex + 1, totalChunks);
}

typedef struct {
    char* dataWinPath;
    char gameRoot[1024];
    DataWin* dataWin;
    VMContext* vm;
    Renderer* renderer;
    FileSystem* fileSystem;
    AudioSystem* audioSystem;
    Runner* runner;
} N3DSGameSession;

static void n3ds_appFreeStringArray(char** values) {
    repeat(arrlen(values), i) free(values[i]);
    arrfree(values);
}

static char** n3ds_appExtractRunnerArgs(const char* rawArguments) {
    char** array = NULL;
    if (rawArguments == NULL || rawArguments[0] == '\0') return array;

    char* copy = safeStrdup(rawArguments);
    char* saveptr = NULL;
    char* token = strtok_r(copy, " \t\r\n", &saveptr);
    while (token != NULL) {
        arrput(array, safeStrdup(token));
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    free(copy);
    return array;
}

static bool n3ds_appPathHasScheme(const char* path) {
    return path != NULL && strstr(path, ":/") != NULL;
}

static void n3ds_appNormalizePath(char* path) {
    if (path == NULL) return;
    for (char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') *cursor = '/';
    }
}

static char* n3ds_appResolveGamePath(
    const char* currentDataWinPath, const char* workingDirectory, char** launchArgs) {
    const char* dataWinFilename = NULL;
    size_t argCount = arrlen(launchArgs);
    repeat(argCount, i) {
        if (strcmp(launchArgs[i], "-game") == 0 && i + 1 < argCount) {
            dataWinFilename = launchArgs[i + 1];
            break;
        }
    }

    if (dataWinFilename == NULL || dataWinFilename[0] == '\0') {
        logEvent("game_change", "missing -game in launch params");
        return NULL;
    }

    if (n3ds_appPathHasScheme(dataWinFilename)) {
        char* directPath = safeStrdup(dataWinFilename);
        n3ds_appNormalizePath(directPath);
        return directPath;
    }

    char parentDir[1024];
    if (!n3ds_fsGetParentDir(currentDataWinPath, parentDir, sizeof(parentDir))) {
        snprintf(parentDir, sizeof(parentDir), "%s", N3DS_SDMC_ROOT);
    }

    const char* work = workingDirectory != NULL ? workingDirectory : "";
    while (*work == '\\')
        ++work;

    size_t parentLen = strlen(parentDir);
    size_t workLen = strlen(work);
    size_t fileLen = strlen(dataWinFilename);
    bool workHasLeadingSlash = workLen > 0 && work[0] == '/';
    bool needSlashBeforeWork =
        workLen > 0 && !workHasLeadingSlash && parentLen > 0 && parentDir[parentLen - 1] != '/';
    bool needSlashBeforeFile = fileLen > 0 && dataWinFilename[0] != '/';
    size_t totalLen = parentLen + workLen + fileLen + (needSlashBeforeWork ? 1u : 0u) +
        (needSlashBeforeFile ? 1u : 0u) + 1u;

    char* outPath = safeMalloc(totalLen);
    snprintf(outPath, totalLen, "%s%s%s%s%s", parentDir, needSlashBeforeWork ? "/" : "", work,
        needSlashBeforeFile ? "/" : "", dataWinFilename);
    n3ds_appNormalizePath(outPath);
    return outPath;
}

static void n3ds_sessionDestroy(N3DSGameSession* session) {
    if (session == NULL) return;
    if (session->audioSystem != NULL) session->audioSystem->vtable->destroy(session->audioSystem);
    if (session->renderer != NULL) {
        C2D_Prepare();
        session->renderer->vtable->destroy(session->renderer);
    }
    Runner_free(session->runner);
    n3ds_fsDestroy((N3DSFileSystem*)session->fileSystem);
    VM_free(session->vm);
    DataWin_free(session->dataWin);
    free(session->dataWinPath);
    memset(session, 0, sizeof(*session));
}

static bool n3ds_sessionLoad(N3DSGameSession* session, const char* dataWinPath, char** gameArgs,
    const N3DSRunnerSettings* settings, bool citroReady) {
    if (session == NULL || dataWinPath == NULL) return false;
    memset(session, 0, sizeof(*session));
    session->dataWinPath = safeStrdup(dataWinPath);

    N3DSLoadingScreen loadingScreen = {0};
    n3ds_loadInit(&loadingScreen, citroReady);

    n3ds_loadSet(&loadingScreen, "Loading data.win...", 0, 1);
    snprintf(session->gameRoot, sizeof(session->gameRoot), "%s", N3DS_SDMC_ROOT);
    char dataWinParent[1024];
    if (n3ds_fsGetParentDir(dataWinPath, dataWinParent, sizeof(dataWinParent))) {
        snprintf(session->gameRoot, sizeof(session->gameRoot), "%s", dataWinParent);
    }
    n3ds_fsSetGameRoot(session->gameRoot);

    DataWinParserOptions parserOptions = {
        .parseGen8 = true,
        .parseOptn = true,
        .parseLang = true,
        .parseExtn = true,
        .parseSond = true,
        .parseAgrp = true,
        .parseSprt = true,
        .parseBgnd = true,
        .parsePath = true,
        .parseScpt = true,
        .parseGlob = true,
        .parseShdr = true,
        .parseFont = true,
        .parseTmln = true,
        .parseObjt = true,
        .parseRoom = true,
        .parseTpag = true,
        .parseCode = true,
        .parseVari = true,
        .parseFunc = true,
        .parseStrg = true,
        .parseTxtr = false,
        .parseAudo = false,
        .skipLoadingPreciseMasksForNonPreciseSprites = true,
        .lazyLoadRooms = true,
        .progressCallback = n3ds_appOnDataWinProgress,
        .progressCallbackUserData = &loadingScreen,
    };
    session->dataWin = DataWin_parse(dataWinPath, parserOptions);
    if (session->dataWin == NULL) {
        n3ds_loadWaitForStart(
            &loadingScreen, "Failed to parse data.win. Press START.");
        n3ds_loadFree(&loadingScreen);
        n3ds_sessionDestroy(session);
        return false;
    }

    const char* gameName = session->dataWin->gen8.displayName;
    n3ds_loadSetGameName(&loadingScreen, gameName);
    n3ds_loadSet(&loadingScreen, "Creating filesystem...", 1, 1);
    const char* saveRoot =
        strncmp(session->gameRoot, "romfs:", 6) == 0 ? N3DS_SDMC_ROOT : session->gameRoot;
    session->fileSystem = (FileSystem*)n3ds_fsCreate(session->gameRoot, saveRoot);

    n3ds_loadSet(&loadingScreen, "Creating audio...", 1, 1);
    session->audioSystem = (AudioSystem*)n3ds_audioCreate();

    session->audioSystem->vtable->init(session->audioSystem, session->dataWin, session->fileSystem);
    if (!n3ds_audioIsReady((N3DSAudioSystem*)session->audioSystem)) {
        const char* error =
            n3ds_audioGetStartupError((N3DSAudioSystem*)session->audioSystem);
        n3ds_loadWaitForStart(
            &loadingScreen, error != NULL ? error : "Audio init failed. Press START.");
        n3ds_loadFree(&loadingScreen);
        n3ds_sessionDestroy(session);
        return false;
    }

    n3ds_loadSet(&loadingScreen, "Creating VM...", 1, 1);
    session->vm = VM_create(session->dataWin);

    n3ds_loadSet(&loadingScreen, "Creating renderer...", 1, 1);
    session->renderer = C3D_create(settings == NULL || settings->cacheAtlas,
        settings != NULL ? settings->atlasTextureCacheBytes : 0u,
        settings != NULL && settings->halfResApplicationSurface);
    n3ds_loadSet(&loadingScreen, "Creating runner...", 1, 1);
    session->runner = Runner_create(session->dataWin, session->vm, session->renderer,
        session->fileSystem, session->audioSystem, 0);
    if (gameArgs != NULL) Runner_setGameArgs(session->runner, gameArgs, (int32_t)arrlen(gameArgs));
    session->runner->getWindowSize = n3ds_appGetWindowSize;
    session->runner->setWindowSize = n3ds_appSetWindowSize;
    session->runner->osType = settings != NULL ? settings->osType : OS_WINDOWS;
    if (!C3D_isReady(session->renderer)) {
        const char* error = C3D_getStartupError(session->renderer);
        n3ds_loadWaitForStart(
            &loadingScreen, error != NULL ? error : "Renderer init failed. Press START.");
        n3ds_loadFree(&loadingScreen);
        n3ds_sessionDestroy(session);
        return false;
    }

    n3ds_loadSet(&loadingScreen, "Entering first room...", 1, 1);
    Runner_initFirstRoom(session->runner);
    C3D_syncRoom(session->renderer, session->runner->currentRoomIndex);
    n3ds_loadSet(&loadingScreen, "Starting frame loop...", 1, 1);
    n3ds_loadFree(&loadingScreen);
    C3D_restoreTopScreenOutput(session->renderer);

    return true;
}

static bool n3ds_sessionChangeGame(N3DSGameSession* session, char* workingDirectory,
    char* launchParameters, const N3DSRunnerSettings* settings, bool citroReady) {
    if (session == NULL || workingDirectory == NULL || launchParameters == NULL) return false;

    char** newArguments = n3ds_appExtractRunnerArgs(launchParameters);
    char* newDataWinPath =
        n3ds_appResolveGamePath(session->dataWinPath, workingDirectory, newArguments);
    if (newDataWinPath == NULL) {
        n3ds_appFreeStringArray(newArguments);
        return false;
    }

    char** gameArgs = NULL;
    arrput(gameArgs, safeStrdup("butterscotch"));
    repeat(arrlen(newArguments), i) arrput(gameArgs, safeStrdup(newArguments[i]));

    logEvent("game_change", "working=%s params=%s path=%s", workingDirectory, launchParameters,
        newDataWinPath);
    n3ds_sessionDestroy(session);
    bool ok = n3ds_sessionLoad(session, newDataWinPath, gameArgs, settings, citroReady);

    free(newDataWinPath);
    n3ds_appFreeStringArray(gameArgs);
    n3ds_appFreeStringArray(newArguments);
    return ok;
}

static void n3ds_appShutdown(bool citroReady) {
    logClose();
    if (citroReady) {
        n3ds_overlayFree();
        C2D_Fini();
        C3D_Fini();
    }
    romfsExit();
    gfxExit();
}

typedef struct {
    N3DSGameSession session;
    N3DSRunnerSettings settings;
    N3DSTouchMouseState touchMouse;
    bool keyboardHeld[GML_KEY_COUNT];
    bool citroReady;
    bool debugMonitorVisible;
#if N3DS_HAS_BOTTOM_TARGET
    bool bottomScreenDisabledNeedsClear;
#endif
    float frameRate;
    u64 lastFrameStartTick;
} N3DSApplication;

static bool n3ds_appInitCitro(void) {
    bool citroReady = false;
    bool c3dInitialized = C3D_Init(N3DS_C3D_COMMAND_BUFFER_SIZE);
    if (c3dInitialized) {
        citroReady = C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
        if (citroReady) C2D_Prepare();
    }
    if (!citroReady) {
        if (c3dInitialized) C3D_Fini();
    }
    return citroReady;
}

static void n3ds_appLoadGameSettings(N3DSRunnerSettings* settings, const char* dataWinPath) {
    char gameRoot[1024];
    if (!n3ds_fsGetParentDir(dataWinPath, gameRoot, sizeof(gameRoot))) return;
    char gameSettingsPath[1100];
    snprintf(gameSettingsPath, sizeof(gameSettingsPath), "%s/cinnamon_settings.json", gameRoot);
    if (!n3ds_fsPathExists(gameSettingsPath)) return;
    *settings = n3ds_settingsLoad(gameSettingsPath);
    logSetEnabled(settings->debugLogEnabled);
}

static bool n3ds_appLoadInitialGame(N3DSApplication* app, int argc, char** argv) {
    mkdir("sdmc:/3ds", 0777);
    mkdir(N3DS_SDMC_ROOT, 0777);

    bool cancelled = false;
    char* dataWinPath = n3ds_chooseDataWinPath(app->citroReady, &cancelled);
    if (dataWinPath == NULL) {
        if (cancelled) return false;
        N3DSLoadingScreen loadingScreen = {0};
        n3ds_loadInit(&loadingScreen, app->citroReady);
        n3ds_loadWaitForStart(&loadingScreen, gN3DSDataWinStatusLine);
        n3ds_loadFree(&loadingScreen);
        return false;
    }
    n3ds_appLoadGameSettings(&app->settings, dataWinPath);

    char** initialGameArgs = NULL;
    arrput(initialGameArgs, safeStrdup(argc > 0 && argv[0] != NULL ? argv[0] : "butterscotch"));
    bool loaded = n3ds_sessionLoad(
        &app->session, dataWinPath, initialGameArgs, &app->settings, app->citroReady);
    free(dataWinPath);
    n3ds_appFreeStringArray(initialGameArgs);
    return loaded;
}

static void n3ds_appResetFrameState(N3DSApplication* app, bool showDebugMonitor) {
    app->debugMonitorVisible = showDebugMonitor;
#if N3DS_HAS_BOTTOM_TARGET
    app->bottomScreenDisabledNeedsClear = true;
#endif
    memset(&app->touchMouse, 0, sizeof(app->touchMouse));
    memset(app->keyboardHeld, 0, sizeof(app->keyboardHeld));
    app->frameRate = n3ds_appGetRoomFrameRate(app->session.runner);
    if (app->citroReady) C3D_FrameRate(60.0f);
    app->lastFrameStartTick = 0u;
}

static void n3ds_appBeginFrame(N3DSApplication* app, Runner* runner, N3DSOverlayTimings* timing) {
    app->frameRate = n3ds_appGetRoomFrameRate(runner);
    u64 period = (u64)((double)SYSCLOCK_ARM11 / (double)app->frameRate);
    u64 waitStartTick = svcGetSystemTick();
    if (app->lastFrameStartTick != 0u) {
        u64 deadline = app->lastFrameStartTick + period;
        u64 now = waitStartTick;
        while (now < deadline) {
            s64 ns = (s64)((double)(deadline - now) * 1000000000.0 / (double)SYSCLOCK_ARM11);
            svcSleepThread(ns > 0 ? ns : 1);
            now = svcGetSystemTick();
        }
    }
    C3D_FrameBegin(0);
    timing->frameStartTick = svcGetSystemTick();
    u64 deltaTicks = app->lastFrameStartTick != 0u &&
        timing->frameStartTick >= app->lastFrameStartTick
        ? timing->frameStartTick - app->lastFrameStartTick
        : period;
    runner->deltaTime = (GMLReal)((double)deltaTicks * 1000000.0 / (double)SYSCLOCK_ARM11);
    app->lastFrameStartTick = timing->frameStartTick;
    C3D_beginDrawFrame(app->session.renderer);
    timing->workStartTick = svcGetSystemTick();
}

static bool n3ds_appPollFrameInput(N3DSApplication* app, Runner* runner, const Gen8* gen8) {
    hidScanInput();
    u32 held = hidKeysHeld();
    u32 down = hidKeysDown();
    if ((held & KEY_START) && (held & KEY_SELECT) && (down & (KEY_START | KEY_SELECT))) {
        C3D_endDrawFrame(app->session.renderer);
        return false;
    }
    if ((down & KEY_SELECT) && !(held & KEY_START)) {
        app->debugMonitorVisible = !app->debugMonitorVisible;
        if (app->debugMonitorVisible) n3ds_overlayReset();
    }

    circlePosition circle;
    hidCircleRead(&circle);
    int32_t gameW = 0;
    int32_t gameH = 0;
    n3ds_appGetAppSize(runner, gen8, &gameW, &gameH);
    n3ds_appSyncTouchMouse(&app->touchMouse, runner, gameW, gameH, held);
    if (app->settings.gamepadApiEnabled) {
        n3ds_padSyncPrimary(runner->gamepads, held, circle);
    } else {
        if (runner->gamepads != NULL) RunnerGamepad_beginFrame(runner->gamepads);
        n3ds_appSyncMappedKeyboard(runner, app->keyboardHeld, held, &app->settings);
    }
    return true;
}

static void n3ds_appStepFrame(
    N3DSApplication* app, Runner* runner, Renderer* renderer, N3DSOverlayTimings* timing) {
    timing->stepStartTick = app->debugMonitorVisible ? svcGetSystemTick() : 0u;
    Runner_step(runner);
    timing->stepEndTick = app->debugMonitorVisible ? svcGetSystemTick() : 0u;
    C3D_syncRoom(renderer, runner->currentRoomIndex);
    timing->audioStartTick = app->debugMonitorVisible ? svcGetSystemTick() : 0u;
    runner->audioSystem->vtable->update(
        runner->audioSystem, (float)(runner->deltaTime / 1000000.0));
    timing->audioEndTick = app->debugMonitorVisible ? svcGetSystemTick() : 0u;
}

static void n3ds_appDrawFrame(N3DSApplication* app, Runner* runner, Renderer* renderer,
    const Gen8* gen8, N3DSOverlayTimings* timing) {
    if (!runner->appSurfaceEnabled) {
        runner->applicationWidth = N3DS_TOP_SCREEN_WIDTH;
        runner->applicationHeight = N3DS_TOP_SCREEN_HEIGHT;
        runner->usingAppSurface = false;
    } else {
        if (runner->applicationWidth <= 0 || runner->applicationHeight <= 0) {
            runner->applicationWidth = (int32_t)gen8->defaultWindowWidth;
            runner->applicationHeight = (int32_t)gen8->defaultWindowHeight;
        }
        runner->usingAppSurface = true;
    }

    int32_t gameW = runner->applicationWidth;
    int32_t gameH = runner->applicationHeight;
    runner->widescreenExtraWidth = 0;
    runner->widescreenExtraHeight = 0;
    timing->drawStartTick = app->debugMonitorVisible ? svcGetSystemTick() : 0u;
    Runner_drawPre(runner, N3DS_TOP_SCREEN_WIDTH, N3DS_TOP_SCREEN_HEIGHT);
    Runner_beginFrame(runner, gameW, gameH, N3DS_TOP_SCREEN_WIDTH, N3DS_TOP_SCREEN_HEIGHT,
        N3DS_TOP_SCREEN_WIDTH, N3DS_TOP_SCREEN_HEIGHT);
    Runner_drawViews(runner, gameW, gameH, false);
    renderer->vtable->endFrameInit(renderer);
    Runner_drawPost(runner, N3DS_TOP_SCREEN_WIDTH, N3DS_TOP_SCREEN_HEIGHT);
    renderer->vtable->endFrameEnd(renderer);
    Runner_drawGUI(runner, N3DS_TOP_SCREEN_WIDTH, N3DS_TOP_SCREEN_HEIGHT, gameW, gameH);
    renderer->vtable->flush(renderer);
    C3D_presentBottomSurface(renderer);
    timing->drawEndTick = app->debugMonitorVisible ? svcGetSystemTick() : 0u;
}

static void n3ds_appDrawFrameOverlay(
    N3DSApplication* app, Runner* runner, Renderer* renderer, N3DSOverlayTimings* timing) {
    if (app->debugMonitorVisible) {
        timing->workEndTick = svcGetSystemTick();
        n3ds_overlayUpdate(timing);
        n3ds_overlayDraw(renderer, runner);
#if N3DS_HAS_BOTTOM_TARGET
        if (C3D_bottomScreenUsed(renderer)) app->bottomScreenDisabledNeedsClear = true;
#endif
        return;
    }
#if N3DS_HAS_BOTTOM_TARGET
    if (!C3D_bottomScreenUsed(renderer)) {
        if (app->bottomScreenDisabledNeedsClear) {
            C3D_clearBottomScreen(renderer);
            app->bottomScreenDisabledNeedsClear = false;
        }
    } else {
        app->bottomScreenDisabledNeedsClear = true;
    }
#endif
}

static void n3ds_appPresentFrame(Runner* runner, Renderer* renderer) {
    bool roomChangePending = runner->pendingRoom != -1;
    if (roomChangePending) C3D_skipTopScreenPresent(renderer);
    C3D_endDrawFrame(renderer);
    if (roomChangePending) C3D_restoreTopScreenOutput(renderer);
    C3D_freePendingSurfaces(renderer);
    Runner_handlePendingRoomChange(runner);
}

static bool n3ds_appHandleGameChange(N3DSApplication* app, Runner* runner, bool* changed) {
    *changed = false;
    if (runner->pendingWorkingDirectory == NULL || runner->pendingLaunchParameters == NULL)
        return true;
    char* workingDirectory = runner->pendingWorkingDirectory;
    char* launchParameters = runner->pendingLaunchParameters;
    runner->pendingWorkingDirectory = NULL;
    runner->pendingLaunchParameters = NULL;
    bool ok = n3ds_sessionChangeGame(&app->session, workingDirectory, launchParameters,
        &app->settings, app->citroReady);
    free(workingDirectory);
    free(launchParameters);
    if (!ok) return false;
    *changed = true;
    n3ds_appResetFrameState(app, false);
    return true;
}

static bool n3ds_appRunFrame(N3DSApplication* app) {
    Runner* runner = app->session.runner;
    Renderer* renderer = app->session.renderer;
    Gen8* gen8 = &app->session.dataWin->gen8;
    N3DSOverlayTimings timing = {0};
    n3ds_appBeginFrame(app, runner, &timing);
    if (!n3ds_appPollFrameInput(app, runner, gen8)) return false;
    n3ds_appStepFrame(app, runner, renderer, &timing);
    n3ds_appDrawFrame(app, runner, renderer, gen8, &timing);
    n3ds_appDrawFrameOverlay(app, runner, renderer, &timing);
    n3ds_appPresentFrame(runner, renderer);

    bool gameChanged = false;
    if (!n3ds_appHandleGameChange(app, runner, &gameChanged)) return false;
    if (!gameChanged) {
        RunnerKeyboard_beginFrame(runner->keyboard);
        RunnerGamepad_beginFrame(runner->gamepads);
    }
    return true;
}

static void n3ds_appRunApplication(N3DSApplication* app) {
    n3ds_appResetFrameState(app, app->settings.debugStatsEnabled);
    while (aptMainLoop() && !app->session.runner->shouldExit) {
        if (!n3ds_appRunFrame(app)) break;
    }
}

int main(int argc, char** argv) {
    N3DSApplication app = {0};
    gfxInitDefault();
    romfsInit();
    osSetSpeedupEnable(true);
    app.settings = n3ds_settingsLoad(N3DS_SETTINGS_PATH);
    logSetEnabled(app.settings.debugLogEnabled);
    logInit();
    app.citroReady = n3ds_appInitCitro();
    if (!n3ds_appLoadInitialGame(&app, argc, argv)) {
        n3ds_appShutdown(app.citroReady);
        return 1;
    }

    n3ds_appRunApplication(&app);
    n3ds_sessionDestroy(&app.session);
    n3ds_appShutdown(app.citroReady);
    return 0;
}
