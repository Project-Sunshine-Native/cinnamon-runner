#define _GNU_SOURCE

#include "../data_win.h"
#include "../json_reader.h"
#include "../json_writer.h"
#include "../log.h"
#include "../runner.h"
#include "../runner_keyboard.h"
#include "../utils.h"
#include "../vm.h"
#include "../audio/wii/wii_audio_system.h"
#include "default_font_renderer.h"
#include "wii_log.h"
#include "wii_file_system.h"
#include "wii_gamepad.h"
#include "wii_overlay.h"
#include "wii_renderer.h"
#include "stb_ds.h"
#include <fat.h>
#include <gccore.h>
#include <ogc/conf.h>
#include <ogc/pad.h>
#include <ogc/system.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef WII_SCRIPT_OS_TYPE
#define WII_SCRIPT_OS_TYPE OS_WINDOWS
#endif

#ifndef WII_VI_WIDTH
#define WII_VI_WIDTH 672
#endif

#ifndef WII_VI_HEIGHT
#define WII_VI_HEIGHT 480
#endif

#define WII_GX_FIFO_SIZE (256u * 1024u)
#define WII_16_9_CONTENT_SCALE_X 0.80f
#define WII_DEFAULT_GAME_DIRECTORY "cinnamon"
#define WII_SETTINGS_PATH "cinnamon_settings.json"
#define WII_ATLAS_TEXTURE_CACHE_SIZE_MAX_MB 32

typedef enum {
    WII_EXIT_NONE = 0,
    WII_EXIT_HOMEBREW_CHANNEL,
    WII_EXIT_WII_MENU,
} WiiExitMode;

typedef enum {
    WII_HOME_MENU_ACTION_NONE = 0,
    WII_HOME_MENU_ACTION_RESUME,
    WII_HOME_MENU_ACTION_SETTINGS_CHANGED,
    WII_HOME_MENU_ACTION_HOMEBREW_CHANNEL,
    WII_HOME_MENU_ACTION_WII_MENU,
} WiiHomeMenuAction;

typedef enum {
    WII_HOME_MENU_SCREEN_MAIN = 0,
    WII_HOME_MENU_SCREEN_RUNNER_SETTINGS,
} WiiHomeMenuScreen;

typedef enum {
    WII_240P_MODE_DISABLED = 0,
    WII_240P_MODE_AUTOMATIC,
    WII_240P_MODE_FORCE_ON,
} Wii240pMode;

typedef enum {
    WII_ASPECT_RATIO_STRETCH = 0,
    WII_ASPECT_RATIO_4_3,
} WiiAspectRatioMode;

typedef struct {
    Wii240pMode p240Mode;
    WiiAspectRatioMode aspectRatioMode;
    bool debugStatsEnabled;
    bool debugLogEnabled;
    bool gamepadApiEnabled;
    bool cacheAtlas;
    uint32_t atlasTextureCacheBytes;
    WiiGamepadConfig gamepadConfig;
    char gameDirectory[128];
    YoYoOperatingSystem osType;
} WiiRunnerSettings;

typedef struct {
    bool up;
    bool down;
    bool left;
    bool right;
    bool confirm;
    bool cancel;
} WiiHomeMenuInput;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} WiiPresentationRect;

static uint16_t gHomeMenuPadHeld[4];
static bool gHomeMenuPadStartSuppressed[4];

typedef struct {
    float fps;
    float tickMs;
    float stepMs;
    float drawMs;
    float audioMs;
    float presentMs;
    uint64_t fpsWindowStartTicks;
    uint32_t fpsWindowFrames;
} WiiDebugStats;

typedef struct {
    uint32_t refreshRate;
    uint32_t gameSpeed;
    uint32_t phase;
} WiiFramePacer;

static volatile uint32_t gWiiRetraceCount;

static void trackWiiRetrace(u32 retraceCount) {
    gWiiRetraceCount = retraceCount;
}

static float ticksToMs(uint64_t startTicks, uint64_t endTicks) {
    return endTicks >= startTicks
        ? (float)((double)ticks_to_microsecs(endTicks - startTicks) / 1000.0)
        : 0.0f;
}

static void updateDebugFps(WiiDebugStats* stats, uint64_t frameStartTicks) {
    if (stats == NULL) return;
    if (stats->fpsWindowStartTicks == 0u) {
        stats->fpsWindowStartTicks = frameStartTicks;
        stats->fpsWindowFrames = 0u;
        stats->fps = 0.0f;
    }

    stats->fpsWindowFrames++;
    uint64_t elapsedUs = ticks_to_microsecs(frameStartTicks - stats->fpsWindowStartTicks);
    if (elapsedUs >= 500000u) {
        stats->fps =
            elapsedUs > 0u ? ((float)stats->fpsWindowFrames * 1000000.0f) / (float)elapsedUs : 0.0f;
        stats->fpsWindowStartTicks = frameStartTicks;
        stats->fpsWindowFrames = 0u;
    }
}

static void drawDebugStatsOverlay(WiiDefaultFontRenderer* font, WiiRenderer* renderer,
    const Runner* runner, const WiiDebugStats* stats) {
    if (font == NULL || runner == NULL || stats == NULL) return;

    const char* roomName = runner->currentRoom != NULL && runner->currentRoom->name != NULL
        ? runner->currentRoom->name
        : "?";
    uint32_t roomSpeed = runner->currentRoom != NULL && runner->currentRoom->speed > 0u
        ? runner->currentRoom->speed
        : 30u;
    struct mallinfo memInfo = mallinfo();
    uint32_t atlasLoaded = GX_getLoadedAtlasPages(renderer);
    uint32_t atlasTotal = GX_getAtlasPages(renderer);
    uint32_t atlasKb = GX_getAtlasBytes(renderer) / 1024u;
    uint32_t surfaceCount = GX_getSurfaceCount(renderer);

    char text[512];
    snprintf(text, sizeof(text),
        "Room: %s\nFPS: %.1f\nTick: %.2fms\nStep: %.2fms\nDraw: %.2fms\nAudio: %.2fms\nPresent: "
        "%.2fms\nHeap: %lu KB\nRoom Speed: %lu\nAtlas: %lu/%lu %lu KB\nSurfaces: %lu\nInstances: "
        "%ld\nStructs: %ld",
        roomName, (double)stats->fps, (double)stats->tickMs, (double)stats->stepMs,
        (double)stats->drawMs, (double)stats->audioMs, (double)stats->presentMs,
        (unsigned long)((uint32_t)memInfo.uordblks / 1024u), (unsigned long)roomSpeed,
        (unsigned long)atlasLoaded, (unsigned long)atlasTotal, (unsigned long)atlasKb,
        (unsigned long)surfaceCount, (long)arrlen(runner->instances),
        (long)arrlen(runner->structInstances));

    WiiFont_beginFrame(font, WII_FRAMEBUFFER_WIDTH, WII_FRAMEBUFFER_HEIGHT);
    WiiFont_printScaled(font, 10.0f, 10.0f, 0.42f, 0xFFFFFFu, 220u, text);
}

static void syncKey(RunnerKeyboardState* keyboard, bool* held, int32_t key, bool down) {
    if (keyboard == NULL || held == NULL || key < 0 || key >= GML_KEY_COUNT) return;
    if (down && !held[key]) RunnerKeyboard_onKeyDown(keyboard, key);
    if (!down && held[key]) RunnerKeyboard_onKeyUp(keyboard, key);
    held[key] = down;
}

static void syncMouseButton(RunnerMouseState* mouse, bool* held, int32_t button, bool down) {
    if (mouse == NULL || held == NULL) return;
    if (down && !*held) RunnerMouse_onButtonDown(mouse, button);
    if (!down && *held) RunnerMouse_onButtonUp(mouse, button);
    *held = down;
}

static void syncWiimoteMouse(Runner* runner, bool* leftHeld) {
    if (runner == NULL || runner->mouse == NULL) return;

    RunnerMouse_beginFrame(runner->mouse);

    for (uint32_t channel = 0u; channel < WII_INPUT_CONTROLLER_COUNT; ++channel) {
        uint32_t type = WPAD_EXP_NONE;
        if (WPAD_Probe((s32)channel, &type) != WPAD_ERR_NONE) continue;
        if (type == WPAD_EXP_CLASSIC) continue;

        ir_t ir;
        memset(&ir, 0, sizeof(ir));
        WPAD_IR((s32)channel, &ir);
        if (ir.valid) {
            Runner_updateMousePosition(
                runner, WII_FRAMEBUFFER_WIDTH, WII_FRAMEBUFFER_HEIGHT, ir.x, ir.y);
        }

        syncMouseButton(runner->mouse, leftHeld, GML_MB_LEFT,
            (WPAD_ButtonsHeld((s32)channel) & WPAD_BUTTON_A) != 0u);
        return;
    }

    syncMouseButton(runner->mouse, leftHeld, GML_MB_LEFT, false);
}

static bool pollInput(Runner* runner, bool* held, const WiiRunnerSettings* settings) {
    static bool mouseLeftHeld = false;
    bool desired[GML_KEY_COUNT];
    memset(desired, 0, sizeof(desired));
    bool systemHomePressed = false;
    RunnerGamepadState* gamepads =
        runner != NULL && (settings == NULL || settings->gamepadApiEnabled) ? runner->gamepads
                                                                            : NULL;
    if (runner != NULL && runner->gamepads != NULL && gamepads == NULL)
        RunnerGamepad_beginFrame(runner->gamepads);

    WiiPad_poll(
        gamepads, desired, &systemHomePressed, settings != NULL ? &settings->gamepadConfig : NULL);
    syncWiimoteMouse(runner, &mouseLeftHeld);

    for (int32_t key = 0; key < GML_KEY_COUNT; ++key) {
        syncKey(runner != NULL ? runner->keyboard : NULL, held, key, desired[key]);
    }
    return systemHomePressed;
}

static void clearGameInputsForFrame(Runner* runner, bool* held) {
    if (held != NULL) memset(held, 0, sizeof(bool) * GML_KEY_COUNT);
    if (runner == NULL) return;

    if (runner->keyboard != NULL) RunnerKeyboard_clear(runner->keyboard, VK_ANYKEY);
    if (runner->mouse != NULL) RunnerMouse_clear(runner->mouse, GML_MB_ANY);
    if (runner->gamepads == NULL) return;

    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        GamepadSlot* slot = &runner->gamepads->slots[i];
        memset(slot->buttonDownPrev, 0, sizeof(slot->buttonDownPrev));
        memset(slot->buttonDown, 0, sizeof(slot->buttonDown));
        memset(slot->buttonPressed, 0, sizeof(slot->buttonPressed));
        memset(slot->buttonReleased, 0, sizeof(slot->buttonReleased));
        memset(slot->buttonValue, 0, sizeof(slot->buttonValue));
        memset(slot->axisValue, 0, sizeof(slot->axisValue));
    }
}

static void absorbSuppressedInputs(
    Runner* runner, bool* held, const WiiRunnerSettings* settings) {
    bool desired[GML_KEY_COUNT];
    memset(desired, 0, sizeof(desired));
    bool ignoredHomePressed = false;

    WiiPad_poll(runner != NULL ? runner->gamepads : NULL, desired, &ignoredHomePressed,
        settings != NULL ? &settings->gamepadConfig : NULL);

    for (int32_t key = 0; key < GML_KEY_COUNT; ++key) {
        syncKey(runner != NULL ? runner->keyboard : NULL, held, key, desired[key]);
    }
    if (runner != NULL && runner->keyboard != NULL) RunnerKeyboard_beginFrame(runner->keyboard);
    if (runner != NULL && runner->gamepads != NULL) RunnerGamepad_beginFrame(runner->gamepads);
}

static void setRunnerAudioSuspended(Runner* runner, bool suspended) {
    if (runner == NULL || runner->audioSystem == NULL || runner->audioSystem->vtable == NULL)
        return;
    if (suspended) {
        if (runner->audioSystem->vtable->suspend != NULL)
            runner->audioSystem->vtable->suspend(runner->audioSystem);
    } else {
        if (runner->audioSystem->vtable->resume != NULL)
            runner->audioSystem->vtable->resume(runner->audioSystem);
    }
}

static const char* settingsP240Label(Wii240pMode mode) {
    switch (mode) {
    case WII_240P_MODE_FORCE_ON:
        return "Force on";
    case WII_240P_MODE_AUTOMATIC:
        return "Automatic";
    default:
        return "Disabled";
    }
}

static const char* settingsAspectLabel(WiiAspectRatioMode mode) {
    switch (mode) {
    case WII_ASPECT_RATIO_4_3:
        return "4:3";
    default:
        return "16:9";
    }
}

static const char* settingsBoolLabel(bool enabled) {
    return enabled ? "On" : "Off";
}

static bool stringIsBlank(const char* value) {
    if (value == NULL) return true;
    while (*value != '\0') {
        if (*value != ' ' && *value != '\t' && *value != '\r' && *value != '\n') return false;
        ++value;
    }
    return true;
}

static void copyTrimmedString(char* dst, size_t dstSize, const char* src) {
    if (dst == NULL || dstSize == 0u) return;
    dst[0] = '\0';
    if (src == NULL) return;

    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n')
        ++src;
    size_t len = strlen(src);
    while (len > 0u &&
        (src[len - 1u] == ' ' || src[len - 1u] == '\t' || src[len - 1u] == '\r' ||
            src[len - 1u] == '\n'))
        --len;
    if (len >= dstSize) len = dstSize - 1u;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void settingsSetGameDir(WiiRunnerSettings* settings, const char* value) {
    if (settings == NULL) return;
    copyTrimmedString(settings->gameDirectory, sizeof(settings->gameDirectory), value);
    WiiFS_normalizeSeparators(settings->gameDirectory);
    if (stringIsBlank(settings->gameDirectory)) {
        snprintf(settings->gameDirectory, sizeof(settings->gameDirectory), "%s",
            WII_DEFAULT_GAME_DIRECTORY);
        return;
    }

    char* colon = strchr(settings->gameDirectory, ':');
    if (colon != NULL && colon[1] != '/') {
        char fixed[sizeof(settings->gameDirectory)];
        size_t prefixLen = (size_t)(colon - settings->gameDirectory + 1);
        snprintf(fixed, sizeof(fixed), "%.*s/%s", (int)prefixLen, settings->gameDirectory,
            colon + 1);
        snprintf(settings->gameDirectory, sizeof(settings->gameDirectory), "%s", fixed);
    }

    size_t len = strlen(settings->gameDirectory);
    while (len > 0u && settings->gameDirectory[len - 1u] == '/') {
        if (len >= 3u && settings->gameDirectory[len - 2u] == ':') break;
        settings->gameDirectory[--len] = '\0';
    }
    if (stringIsBlank(settings->gameDirectory)) {
        snprintf(settings->gameDirectory, sizeof(settings->gameDirectory), "%s",
            WII_DEFAULT_GAME_DIRECTORY);
    }
}

static const char* settingsGameDir(const WiiRunnerSettings* settings) {
    return settings != NULL && !stringIsBlank(settings->gameDirectory)
        ? settings->gameDirectory
        : WII_DEFAULT_GAME_DIRECTORY;
}

static char* readWiiSettings(const char* settingsPath) {
    char* text = WiiFS_readText(settingsPath);
    if (text != NULL) return text;

    char* appRoot = WiiFS_getParentDir(settingsPath);
    char* appsRoot = WiiFS_getParentDir(appRoot);
    char* legacyPath = WiiFS_joinPath(appsRoot, "cinnamon_settings.json");
    if (strcmp(settingsPath, legacyPath) != 0) text = WiiFS_readText(legacyPath);

    if (text == NULL) {
        char* deviceRoot = WiiFS_getParentDir(appsRoot);
        char* rootLegacyPath = WiiFS_joinPath(deviceRoot, "cinnamon_settings.json");
        if (strcmp(settingsPath, rootLegacyPath) != 0 &&
            strcmp(legacyPath, rootLegacyPath) != 0) {
            text = WiiFS_readText(rootLegacyPath);
        }
        free(rootLegacyPath);
        free(deviceRoot);
    }

    free(legacyPath);
    free(appsRoot);
    free(appRoot);
    return text;
}

static bool settingsJsonBool(
    const JsonValue* object, const char* key, bool defaultValue) {
    if (!JsonReader_isObject(object)) return defaultValue;
    JsonValue* value = JsonReader_getJsonValueByKey(object, key);
    return JsonReader_isBool(value) ? JsonReader_getBool(value) : defaultValue;
}

static int32_t settingsJsonInt(
    const JsonValue* object, const char* key, int32_t defaultValue) {
    if (!JsonReader_isObject(object)) return defaultValue;
    JsonValue* value = JsonReader_getJsonValueByKey(object, key);
    return JsonReader_isNumber(value) ? (int32_t)JsonReader_getInt(value) : defaultValue;
}

static const char* settingsJsonString(const JsonValue* object, const char* key) {
    if (!JsonReader_isObject(object)) return NULL;
    JsonValue* value = JsonReader_getJsonValueByKey(object, key);
    return JsonReader_isString(value) ? JsonReader_getString(value) : NULL;
}

static void settingsLoadMappings(
    const JsonValue* object, const char* key, int32_t* mappings, uint32_t count) {
    JsonValue* array =
        JsonReader_isObject(object) ? JsonReader_getJsonValueByKey(object, key) : NULL;
    if (!JsonReader_isArray(array) || JsonReader_arrayLength(array) != (int)count) return;

    for (uint32_t i = 0; i < count; ++i) {
        JsonValue* value = JsonReader_getArrayElement(array, (int)i);
        if (JsonReader_isNumber(value)) mappings[i] = (int32_t)JsonReader_getInt(value);
    }
}

static void settingsLoadGamepad(WiiGamepadConfig* config, const JsonValue* root) {
    WiiPadCfg_init(config);
    JsonValue* controllers =
        JsonReader_isObject(root) ? JsonReader_getJsonValueByKey(root, "controllers") : NULL;
    if (!JsonReader_isArray(controllers)) return;

    int count = JsonReader_arrayLength(controllers);
    if (count > (int)WII_INPUT_CONTROLLER_COUNT) count = (int)WII_INPUT_CONTROLLER_COUNT;
    for (int i = 0; i < count; ++i) {
        JsonValue* controller = JsonReader_getArrayElement(controllers, i);
        if (!JsonReader_isObject(controller)) continue;

        int32_t kind = settingsJsonInt(controller, "kind", config->controllerKind[i]);
        if (kind >= WII_CONTROLLER_WIIMOTE && kind <= WII_CONTROLLER_GAMECUBE) {
            config->controllerKind[i] = (WiiControllerKind)kind;
        }
        config->controller[i].wiimoteSideways = settingsJsonBool(
            controller, "wiimoteSideways", config->controller[i].wiimoteSideways);
        settingsLoadMappings(
            controller, "wiimote", config->controller[i].wiimote, WII_WIIMOTE_MAPPING_COUNT);
        settingsLoadMappings(
            controller, "classic", config->controller[i].classic, WII_CLASSIC_MAPPING_COUNT);
        settingsLoadMappings(
            controller, "gamecube", config->controller[i].gamecube, WII_GAMECUBE_MAPPING_COUNT);
    }
}

static WiiRunnerSettings settingsLoad(const char* settingsPath) {
    WiiRunnerSettings settings = {
        .p240Mode = WII_240P_MODE_DISABLED,
        .aspectRatioMode = WII_ASPECT_RATIO_STRETCH,
        .debugStatsEnabled = false,
        .debugLogEnabled = false,
        .gamepadApiEnabled = true,
        .cacheAtlas = true,
        .atlasTextureCacheBytes = 0u,
        .gameDirectory = WII_DEFAULT_GAME_DIRECTORY,
        .osType = OS_WINDOWS,
    };
    WiiPadCfg_init(&settings.gamepadConfig);
    char* text = readWiiSettings(settingsPath != NULL ? settingsPath : WII_SETTINGS_PATH);
    JsonValue* root = text != NULL ? JsonReader_parse(text) : NULL;
    free(text);
    if (!JsonReader_isObject(root)) {
        JsonReader_free(root);
        return settings;
    }

    JsonValue* runner = JsonReader_getJsonValueByKey(root, "runner");
    int32_t p240Mode = settingsJsonInt(runner, "p240Mode", settings.p240Mode);
    if (p240Mode >= WII_240P_MODE_DISABLED && p240Mode <= WII_240P_MODE_FORCE_ON) {
        settings.p240Mode = (Wii240pMode)p240Mode;
    }
    int32_t aspectRatio =
        settingsJsonInt(runner, "aspectRatio", settings.aspectRatioMode);
    if (aspectRatio >= WII_ASPECT_RATIO_STRETCH && aspectRatio <= WII_ASPECT_RATIO_4_3) {
        settings.aspectRatioMode = (WiiAspectRatioMode)aspectRatio;
    }
    settings.debugStatsEnabled = settingsJsonBool(runner, "debugStats", false);
    settings.debugLogEnabled = settingsJsonBool(runner, "debugLog", false);
    settings.gamepadApiEnabled = settingsJsonBool(runner, "gamepadApi", true);
    settings.cacheAtlas = settingsJsonBool(runner, "cacheAtlas", true);

    int32_t cacheMb = settingsJsonInt(runner, "atlasTextureCacheMb", 0);
    if (cacheMb < 0) cacheMb = 0;
    if (cacheMb > WII_ATLAS_TEXTURE_CACHE_SIZE_MAX_MB) {
        cacheMb = WII_ATLAS_TEXTURE_CACHE_SIZE_MAX_MB;
    }
    settings.atlasTextureCacheBytes = (uint32_t)cacheMb * 1024u * 1024u;
    settingsSetGameDir(
        &settings, settingsJsonString(runner, "gameDirectory"));
    int32_t osType = settingsJsonInt(runner, "osType", OS_WINDOWS);
    settings.osType = osType == OS_LINUX ? OS_LINUX : OS_WINDOWS;
    settingsLoadGamepad(&settings.gamepadConfig, root);
    JsonReader_free(root);
    return settings;
}

static void settingsWriteMappings(
    JsonWriter* writer, const char* key, const int32_t* mappings, uint32_t count) {
    JsonWriter_key(writer, key);
    JsonWriter_beginArray(writer);
    for (uint32_t i = 0; i < count; ++i)
        JsonWriter_int(writer, mappings[i]);
    JsonWriter_endArray(writer);
}

static void settingsSave(const WiiRunnerSettings* settings, const char* settingsPath) {
    if (settings == NULL) return;
    if (settingsPath == NULL || settingsPath[0] == '\0') settingsPath = WII_SETTINGS_PATH;

    JsonWriter writer = JsonWriter_create();
    JsonWriter_beginObject(&writer);
    JsonWriter_key(&writer, "runner");
    JsonWriter_beginObject(&writer);
    JsonWriter_propertyInt(&writer, "p240Mode", settings->p240Mode);
    JsonWriter_propertyInt(&writer, "aspectRatio", settings->aspectRatioMode);
    JsonWriter_propertyBool(&writer, "debugStats", settings->debugStatsEnabled);
    JsonWriter_propertyBool(&writer, "debugLog", settings->debugLogEnabled);
    JsonWriter_propertyBool(&writer, "gamepadApi", settings->gamepadApiEnabled);
    JsonWriter_propertyBool(&writer, "cacheAtlas", settings->cacheAtlas);
    JsonWriter_propertyInt(
        &writer, "atlasTextureCacheMb", settings->atlasTextureCacheBytes / (1024u * 1024u));
    JsonWriter_propertyString(
        &writer, "gameDirectory", settingsGameDir(settings));
    JsonWriter_propertyInt(&writer, "osType", settings->osType);
    JsonWriter_endObject(&writer);

    JsonWriter_key(&writer, "controllers");
    JsonWriter_beginArray(&writer);
    for (uint32_t i = 0; i < WII_INPUT_CONTROLLER_COUNT; ++i) {
        const WiiControllerMappingSettings* controller = &settings->gamepadConfig.controller[i];
        JsonWriter_beginObject(&writer);
        JsonWriter_propertyInt(&writer, "kind", settings->gamepadConfig.controllerKind[i]);
        JsonWriter_propertyBool(&writer, "wiimoteSideways", controller->wiimoteSideways);
        settingsWriteMappings(
            &writer, "wiimote", controller->wiimote, WII_WIIMOTE_MAPPING_COUNT);
        settingsWriteMappings(
            &writer, "classic", controller->classic, WII_CLASSIC_MAPPING_COUNT);
        settingsWriteMappings(
            &writer, "gamecube", controller->gamecube, WII_GAMECUBE_MAPPING_COUNT);
        JsonWriter_endObject(&writer);
    }
    JsonWriter_endArray(&writer);
    JsonWriter_endObject(&writer);

    FILE* f = fopen(settingsPath, "wb");
    if (f != NULL) {
        fwrite(JsonWriter_getOutput(&writer), 1, JsonWriter_getLength(&writer), f);
        fclose(f);
    }
    JsonWriter_free(&writer);
}

static void settingsStepP240(WiiRunnerSettings* settings, int direction) {
    if (settings == NULL) return;
    int mode = (int)settings->p240Mode;
    if (mode < (int)WII_240P_MODE_DISABLED || mode > (int)WII_240P_MODE_FORCE_ON)
        mode = (int)WII_240P_MODE_AUTOMATIC;
    mode += direction < 0 ? -1 : 1;
    if (mode < (int)WII_240P_MODE_DISABLED) mode = (int)WII_240P_MODE_FORCE_ON;
    if (mode > (int)WII_240P_MODE_FORCE_ON) mode = (int)WII_240P_MODE_DISABLED;
    settings->p240Mode = (Wii240pMode)mode;
}

static void settingsStepAspect(WiiRunnerSettings* settings, int direction) {
    if (settings == NULL) return;
    (void)direction;
    settings->aspectRatioMode = settings->aspectRatioMode == WII_ASPECT_RATIO_4_3
                                    ? WII_ASPECT_RATIO_STRETCH
                                    : WII_ASPECT_RATIO_4_3;
}

static void settingsToggleDebugStats(WiiRunnerSettings* settings) {
    if (settings == NULL) return;
    settings->debugStatsEnabled = !settings->debugStatsEnabled;
}

static void settingsToggleDebugLog(WiiRunnerSettings* settings) {
    if (settings == NULL) return;
    settings->debugLogEnabled = !settings->debugLogEnabled;
}

static void primeHomePad(void) {
    PADStatus padStatus[4];
    memset(padStatus, 0, sizeof(padStatus));
    PAD_Read(padStatus);
    PAD_Clamp(padStatus);

    for (uint32_t channel = 0u; channel < 4u; ++channel) {
        gHomeMenuPadHeld[channel] =
            padStatus[channel].err == PAD_ERR_NONE ? padStatus[channel].button : 0u;
        gHomeMenuPadStartSuppressed[channel] = true;
    }
}

static void pollHomeMenuControls(WiiHomeMenuInput* input) {
    if (input == NULL) return;
    memset(input, 0, sizeof(*input));

    WPAD_ScanPads();
    PADStatus padStatus[4];
    memset(padStatus, 0, sizeof(padStatus));
    PAD_Read(padStatus);
    PAD_Clamp(padStatus);

    for (uint32_t channel = 0u; channel < 4u; ++channel) {
        uint32_t wpadDown = WPAD_ButtonsDown((s32)channel);
        input->up = input->up || (wpadDown & WPAD_BUTTON_RIGHT) != 0u ||
                    (wpadDown & WPAD_CLASSIC_BUTTON_UP) != 0u;
        input->down = input->down || (wpadDown & WPAD_BUTTON_LEFT) != 0u ||
            (wpadDown & WPAD_CLASSIC_BUTTON_DOWN) != 0u;
        input->left = input->left || (wpadDown & WPAD_BUTTON_DOWN) != 0u ||
            (wpadDown & WPAD_CLASSIC_BUTTON_LEFT) != 0u;
        input->right = input->right || (wpadDown & WPAD_BUTTON_UP) != 0u ||
            (wpadDown & WPAD_CLASSIC_BUTTON_RIGHT) != 0u;
        input->confirm = input->confirm || (wpadDown & WPAD_BUTTON_A) != 0u ||
            (wpadDown & WPAD_BUTTON_2) != 0u || (wpadDown & WPAD_CLASSIC_BUTTON_A) != 0u ||
            (wpadDown & WPAD_CLASSIC_BUTTON_PLUS) != 0u;
        input->cancel = input->cancel || (wpadDown & WPAD_BUTTON_HOME) != 0u ||
                        (wpadDown & WPAD_BUTTON_B) != 0u || (wpadDown & WPAD_BUTTON_1) != 0u ||
                        (wpadDown & WPAD_CLASSIC_BUTTON_HOME) != 0u ||
                        (wpadDown & WPAD_CLASSIC_BUTTON_B) != 0u;

        uint16_t padHeld = padStatus[channel].err == PAD_ERR_NONE ? padStatus[channel].button : 0u;
        uint16_t padDown = (uint16_t)(padHeld & (uint16_t)~gHomeMenuPadHeld[channel]);
        gHomeMenuPadHeld[channel] = padHeld;
        bool padStartHeld = (padHeld & PAD_BUTTON_START) != 0u;
        if (!padStartHeld) gHomeMenuPadStartSuppressed[channel] = false;
        bool padStartDown =
            (padDown & PAD_BUTTON_START) != 0u && !gHomeMenuPadStartSuppressed[channel];
        input->up = input->up || (padDown & PAD_BUTTON_UP) != 0u;
        input->down = input->down || (padDown & PAD_BUTTON_DOWN) != 0u;
        input->left = input->left || (padDown & PAD_BUTTON_LEFT) != 0u;
        input->right = input->right || (padDown & PAD_BUTTON_RIGHT) != 0u;
        input->confirm = input->confirm || (padDown & PAD_BUTTON_A) != 0u;
        input->cancel = input->cancel || padStartDown || (padDown & PAD_BUTTON_B) != 0u;
    }
}

static WiiHomeMenuAction pollHomeMenuInput(
    WiiHomeMenuScreen* screen, int* selectedIndex, WiiRunnerSettings* settings) {
    int optionCount = 5;
    int selected = selectedIndex != NULL ? *selectedIndex : 0;
    if (selected < 0) selected = 0;
    if (selected >= optionCount) selected = optionCount - 1;

    WiiHomeMenuInput input;
    pollHomeMenuControls(&input);

    if (input.up && !input.down) selected = (selected + optionCount - 1) % optionCount;
    if (input.down && !input.up) selected = (selected + 1) % optionCount;
    if (selectedIndex != NULL) *selectedIndex = selected;

    if (screen != NULL && *screen == WII_HOME_MENU_SCREEN_RUNNER_SETTINGS) {
        if (input.cancel) {
            *screen = WII_HOME_MENU_SCREEN_MAIN;
            if (selectedIndex != NULL) *selectedIndex = 1;
            return WII_HOME_MENU_ACTION_NONE;
        }
        if ((input.left || input.right || input.confirm) && selected == 0) {
            settingsStepP240(
                settings, input.left && !input.right && !input.confirm ? -1 : 1);
            return WII_HOME_MENU_ACTION_SETTINGS_CHANGED;
        }
        if ((input.left || input.right || input.confirm) && selected == 1) {
            settingsStepAspect(
                settings, input.left && !input.right && !input.confirm ? -1 : 1);
            return WII_HOME_MENU_ACTION_SETTINGS_CHANGED;
        }
        if ((input.left || input.right || input.confirm) && selected == 2) {
            settingsToggleDebugStats(settings);
            return WII_HOME_MENU_ACTION_SETTINGS_CHANGED;
        }
        if ((input.left || input.right || input.confirm) && selected == 3) {
            settingsToggleDebugLog(settings);
            return WII_HOME_MENU_ACTION_SETTINGS_CHANGED;
        }
        if (input.confirm && selected == 4) {
            *screen = WII_HOME_MENU_SCREEN_MAIN;
            if (selectedIndex != NULL) *selectedIndex = 1;
        }
        return WII_HOME_MENU_ACTION_NONE;
    }

    if (input.cancel) return WII_HOME_MENU_ACTION_RESUME;
    if (!input.confirm) return WII_HOME_MENU_ACTION_NONE;
    switch (selected) {
    case 1:
        if (screen != NULL) *screen = WII_HOME_MENU_SCREEN_RUNNER_SETTINGS;
        if (selectedIndex != NULL) *selectedIndex = 0;
        return WII_HOME_MENU_ACTION_NONE;
    case 2:
        return WII_HOME_MENU_ACTION_HOMEBREW_CHANNEL;
    case 3:
        return WII_HOME_MENU_ACTION_WII_MENU;
    default:
        return WII_HOME_MENU_ACTION_RESUME;
    }
}

typedef struct {
    char* dataWinPath;
    DataWin* dataWin;
    VMContext* vm;
    Renderer* renderer;
    WiiFileSystem* fileSystem;
    WiiAudioSystem* audio;
    Runner* runner;
} WiiGameSession;

static void freeStringArray(char** values) {
    repeat(arrlen(values), i) free(values[i]);
    arrfree(values);
}

static char** extractRunnerArguments(const char* rawArguments) {
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

static char* resolveGameRoot(const WiiRunnerSettings* settings, const char* appsRoot) {
    const char* value = settingsGameDir(settings);
    char normalized[128];
    WiiRunnerSettings temp = {0};
    settingsSetGameDir(&temp, value);
    snprintf(normalized, sizeof(normalized), "%s", settingsGameDir(&temp));

    if (WiiFS_pathHasScheme(normalized) || normalized[0] == '/')
        return safeStrdup(normalized);
    if (strcmp(normalized, WII_DEFAULT_GAME_DIRECTORY) == 0) {
        char* deviceRoot = WiiFS_getParentDir(appsRoot);
        char* gameRoot = WiiFS_joinPath(deviceRoot, normalized);
        free(deviceRoot);
        return gameRoot;
    }
    return WiiFS_joinPath(appsRoot, normalized);
}

static void getCurrentFrameSize(Runner* runner, int32_t* outW, int32_t* outH) {
    int32_t width = 0;
    int32_t height = 0;

    if (runner != NULL && runner->applicationWidth > 0 && runner->applicationHeight > 0) {
        width = runner->applicationWidth;
        height = runner->applicationHeight;
    } else if (runner != NULL && runner->dataWin != NULL &&
        runner->dataWin->gen8.defaultWindowWidth > 0u &&
        runner->dataWin->gen8.defaultWindowHeight > 0u) {
        width = (int32_t)runner->dataWin->gen8.defaultWindowWidth;
        height = (int32_t)runner->dataWin->gen8.defaultWindowHeight;
    } else if (runner != NULL && runner->currentRoom != NULL && runner->currentRoom->width > 0u &&
        runner->currentRoom->height > 0u) {
        width = (int32_t)runner->currentRoom->width;
        height = (int32_t)runner->currentRoom->height;
    }

    if (width <= 0) width = WII_FRAMEBUFFER_WIDTH;
    if (height <= 0) height = WII_FRAMEBUFFER_HEIGHT;
    *outW = width;
    *outH = height;
}

static bool wiiFrameTooLarge(int32_t gameW, int32_t gameH) {
    return gameW > WII_FRAMEBUFFER_WIDTH || gameH > WII_FRAMEBUFFER_HEIGHT;
}

static int startupFailure(const char* message) {
    logError("%s\nReturning to loader in 10 seconds.\n", message);
    sleep(10);
    return 1;
}

static uint32_t clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static uint32_t getViMaxWidth(const GXRModeObj* mode) {
    switch (mode != NULL ? (mode->viTVMode >> 2u) : VI_NTSC) {
    case VI_PAL:
    case VI_DEBUG_PAL:
        return VI_MAX_WIDTH_PAL;
    case VI_EURGB60:
        return VI_MAX_WIDTH_EURGB60;
    case VI_MPAL:
        return VI_MAX_WIDTH_MPAL;
    default:
        return VI_MAX_WIDTH_NTSC;
    }
}

static uint32_t getViMaxHeight(const GXRModeObj* mode) {
    switch (mode != NULL ? (mode->viTVMode >> 2u) : VI_NTSC) {
    case VI_PAL:
    case VI_DEBUG_PAL:
        return VI_MAX_HEIGHT_PAL;
    case VI_EURGB60:
        return VI_MAX_HEIGHT_EURGB60;
    case VI_MPAL:
        return VI_MAX_HEIGHT_MPAL;
    default:
        return VI_MAX_HEIGHT_NTSC;
    }
}

static uint32_t getRefreshRate(const GXRModeObj* mode) {
    switch (mode != NULL ? (mode->viTVMode >> 2u) : VI_NTSC) {
    case VI_PAL:
    case VI_DEBUG_PAL:
        return 50u;
    default:
        return 60u;
    }
}

static void waitForGameFrameVsyncs(WiiFramePacer* pacer, const WiiVideoState* video,
    const Runner* runner, uint32_t frameStartRetrace) {
    if (pacer == NULL || video == NULL || runner == NULL) return;

    uint32_t refreshRate = getRefreshRate(video->mode);
    uint32_t gameSpeed = runner->currentRoom != NULL && runner->currentRoom->speed > 0u
        ? runner->currentRoom->speed
        : 30u;
    if (gameSpeed > refreshRate) gameSpeed = refreshRate;

    if (pacer->refreshRate != refreshRate || pacer->gameSpeed != gameSpeed) {
        pacer->refreshRate = refreshRate;
        pacer->gameSpeed = gameSpeed;
        pacer->phase = 0u;
    }

    pacer->phase += refreshRate;
    uint32_t targetRetraces = pacer->phase / gameSpeed;
    pacer->phase %= gameSpeed;
    if (targetRetraces == 0u) targetRetraces = 1u;

    while ((uint32_t)(gWiiRetraceCount - frameStartRetrace) < targetRetraces) {
        VIDEO_WaitVSync();
    }
}

static uint32_t getLowResTvMode(const GXRModeObj* preferred) {
    switch (preferred != NULL ? (preferred->viTVMode >> 2u) : VI_NTSC) {
    case VI_PAL:
    case VI_DEBUG_PAL:
        return VI_TVMODE_PAL_DS;
    case VI_EURGB60:
        return VI_TVMODE_EURGB60_DS;
    case VI_MPAL:
        return VI_TVMODE_MPAL_DS;
    default:
        return VI_TVMODE_NTSC_DS;
    }
}

static void configureLowResMode(GXRModeObj* mode, const GXRModeObj* preferred) {
    if (mode == NULL) return;

    memset(mode, 0, sizeof(*mode));
    mode->viTVMode = getLowResTvMode(preferred);
    mode->fbWidth = WII_LOW_RES_FRAMEBUFFER_WIDTH;
    mode->efbHeight = WII_LOW_RES_FRAMEBUFFER_HEIGHT;
    mode->xfbHeight = WII_LOW_RES_FRAMEBUFFER_HEIGHT;

    uint32_t maxViWidth = getViMaxWidth(mode);
    uint32_t maxViHeight = getViMaxHeight(mode);
    mode->viWidth = clampU32(644u, 640u, maxViWidth);
    mode->viHeight = clampU32(WII_VI_HEIGHT, 480u, maxViHeight);
    mode->viXOrigin = (uint16_t)((maxViWidth - mode->viWidth) / 2u);

    uint32_t maxFieldHeight = maxViHeight / 2u;
    uint32_t viFieldHeight = mode->viHeight / 2u;
    mode->viYOrigin =
        (uint16_t)(maxFieldHeight > viFieldHeight ? (maxFieldHeight - viFieldHeight) / 2u : 0u);

    mode->xfbMode = VI_XFBMODE_SF;
    mode->field_rendering = GX_FALSE;
    mode->aa = GX_FALSE;
    for (uint32_t i = 0u; i < 12u; ++i) {
        mode->sample_pattern[i][0] = 6;
        mode->sample_pattern[i][1] = 6;
    }
    mode->vfilter[0] = 0;
    mode->vfilter[1] = 0;
    mode->vfilter[2] = 21;
    mode->vfilter[3] = 22;
    mode->vfilter[4] = 21;
    mode->vfilter[5] = 0;
    mode->vfilter[6] = 0;
}

static bool prepareWiiVideoModes(WiiVideoState* video) {
    if (video == NULL) return false;
    GXRModeObj preferred;
    GXRModeObj* preferredMode = VIDEO_GetPreferredMode(&preferred);
    if (preferredMode == NULL) return false;

    video->normalMode = *preferredMode;
    memset(&video->lowResMode, 0, sizeof(video->lowResMode));
    video->lowResModeReady = false;
    video->lowResAllowed = VIDEO_GetVideoScanMode() != VI_PROGRESSIVE;
    return true;
}

static void configureGxForVideoMode(WiiVideoState* video) {
    if (video == NULL || video->mode == NULL) return;
    GXRModeObj* rmode = video->mode;
    float yScale = (float)rmode->xfbHeight / (float)rmode->efbHeight;
    GX_SetDispCopyYScale(yScale);
    video->xfbHeight = rmode->xfbHeight;
    GX_SetViewport(
        0.0f, 0.0f, (float)rmode->fbWidth + 0.05f, (float)rmode->efbHeight + 0.05f, 0.0f, 1.0f);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
    GX_SetCopyFilter(GX_FALSE, rmode->sample_pattern, GX_FALSE, rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
        (rmode->viHeight == 2u * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_SetCopyClear((GXColor){0, 0, 0, 255}, 0x00FFFFFFu);
}

static bool wiiVideoIsProgressive(const GXRModeObj* mode) {
    if (mode == NULL) return false;
    uint32_t scanMode = mode->viTVMode & 3u;
    return scanMode == VI_NON_INTERLACE || scanMode == VI_PROGRESSIVE;
}

static void updateWiiScale(WiiVideoState* video) {
    if (video == NULL) return;
    video->horizontalScale = 1.0f;
    video->horizontalOffset = 0.0f;
    if (video->mode == NULL || video->lowResActive || CONF_GetAspectRatio() != CONF_ASPECT_16_9)
        return;
    video->horizontalScale = WII_16_9_CONTENT_SCALE_X;
    video->horizontalOffset = (float)video->mode->fbWidth * (1.0f - video->horizontalScale) * 0.5f;
}

static void clearWiiFramebuffers(WiiVideoState* video) {
    if (video == NULL || video->framebufferSize == 0u) return;
    uint32_t wordCount = video->framebufferSize / sizeof(uint32_t);
    for (uint32_t fb = 0u; fb < 2u; ++fb) {
        uint32_t* pixels = (uint32_t*)video->framebuffers[fb];
        if (pixels == NULL) continue;
        for (uint32_t i = 0u; i < wordCount; ++i)
            pixels[i] = COLOR_BLACK;
        DCFlushRange(pixels, video->framebufferSize);
    }
}

static void applyWiiVideoMode(WiiVideoState* video, bool lowRes) {
    if (video == NULL) return;
    if (lowRes && !video->lowResAllowed) lowRes = false;
    if (lowRes && !video->lowResModeReady) {
        configureLowResMode(&video->lowResMode, &video->normalMode);
        video->lowResModeReady = true;
    }
    if (video->mode != NULL && video->lowResActive == lowRes) return;
    GXRModeObj* previousMode = video->mode;
    video->mode = lowRes ? &video->lowResMode : &video->normalMode;
    video->lowResActive = lowRes;
    updateWiiScale(video);
    bool scanModeSwitch =
        wiiVideoIsProgressive(previousMode) != wiiVideoIsProgressive(video->mode);
    GX_InvVtxCache();
    GX_DrawDone();
    if (scanModeSwitch) {
        VIDEO_SetBlack(TRUE);
        VIDEO_Flush();
        VIDEO_WaitVSync();
    }
    VIDEO_Configure(video->mode);
    clearWiiFramebuffers(video);
    VIDEO_SetNextFramebuffer(video->framebuffers[video->framebufferIndex]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    configureGxForVideoMode(video);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

static bool isUndertaleSession(const WiiGameSession* session) {
    if (session == NULL || session->dataWin == NULL) return false;
    const char* displayName = session->dataWin->gen8.displayName;
    const char* name = session->dataWin->gen8.name;
    return (displayName != NULL && strcasestr(displayName, "undertale") != NULL) ||
        (name != NULL && strcasestr(name, "undertale") != NULL);
}

static bool useLowResGameplay(
    const WiiGameSession* session, const WiiRunnerSettings* settings) {
    if (settings == NULL) return false;
    if (settings->p240Mode == WII_240P_MODE_DISABLED) return false;
    if (settings->p240Mode == WII_240P_MODE_FORCE_ON) return true;
    if (session == NULL || session->runner == NULL || !isUndertaleSession(session)) return false;
    const char* roomName =
        session->runner->currentRoom != NULL ? session->runner->currentRoom->name : NULL;
    return roomName == NULL || strcmp(roomName, "room_battle") != 0;
}

static bool useFourThreeOutput(
    const WiiVideoState* video, const WiiRunnerSettings* settings) {
    (void)settings;
    if (video == NULL || video->lowResActive) return false;
    return CONF_GetAspectRatio() == CONF_ASPECT_16_9;
}

static WiiPresentationRect getWiiPresentationRect(const WiiVideoState* video,
    const WiiRunnerSettings* settings, int32_t outputW, int32_t outputH, int32_t gameW,
    int32_t gameH) {
    WiiPresentationRect base = {0, 0, outputW, outputH};
    if (base.width <= 0) base.width = WII_FRAMEBUFFER_WIDTH;
    if (base.height <= 0) base.height = WII_FRAMEBUFFER_HEIGHT;

    if (useFourThreeOutput(video, settings)) {
        base.width = (int32_t)((float)base.width * video->horizontalScale);
        base.x = (int32_t)video->horizontalOffset;
        return base;
    }

    if (gameW <= 0 || gameH <= 0) return base;

    WiiPresentationRect rect = base;
    if (((int64_t)gameW * (int64_t)base.height) < ((int64_t)base.width * (int64_t)gameH)) {
        rect.width = (int32_t)(((int64_t)gameW * (int64_t)base.height) / (int64_t)gameH);
        if (rect.width < 1) rect.width = 1;
        rect.x = base.x + (base.width - rect.width) / 2;
    } else {
        rect.height = (int32_t)(((int64_t)gameH * (int64_t)base.width) / (int64_t)gameW);
        if (rect.height < 1) rect.height = 1;
        rect.y = base.y + (base.height - rect.height) / 2;
    }
    return rect;
}

static void applyWiiAspect(WiiRunnerSettings* settings) {
    if (settings == NULL) return;
    if (CONF_GetAspectRatio() == CONF_ASPECT_16_9)
        settings->aspectRatioMode = WII_ASPECT_RATIO_STRETCH;
}

static bool initVideo(WiiVideoState* video) {
    if (video == NULL) return false;
    memset(video, 0, sizeof(*video));

    VIDEO_Init();
    VIDEO_SetPostRetraceCallback(trackWiiRetrace);
    if (!prepareWiiVideoModes(video)) return false;
    video->mode = &video->normalMode;
    updateWiiScale(video);
    GXRModeObj* rmode = video->mode;
    video->framebufferSize = VIDEO_GetFrameBufferSize(rmode);

    for (uint32_t i = 0u; i < 2u; ++i) {
        video->framebuffers[i] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
        if (video->framebuffers[i] == NULL) return false;
        VIDEO_ClearFrameBuffer(rmode, video->framebuffers[i], COLOR_BLACK);
    }

    VIDEO_Configure(rmode);
    VIDEO_SetNextFramebuffer(video->framebuffers[video->framebufferIndex]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    video->fifo = memalign(32, WII_GX_FIFO_SIZE);
    if (video->fifo == NULL) return false;
    memset(video->fifo, 0, WII_GX_FIFO_SIZE);
    GX_Init(video->fifo, WII_GX_FIFO_SIZE);

    configureGxForVideoMode(video);

    GX_CopyDisp(video->framebuffers[video->framebufferIndex], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(video->framebuffers[video->framebufferIndex]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    video->framebufferIndex ^= 1u;
    return true;
}

static void presentVideo(
    WiiVideoState* video, WiiRenderer* renderer, float* presentMs) {
    if (presentMs != NULL) *presentMs = 0.0f;
    if (video == NULL || video->mode == NULL ||
        video->framebuffers[video->framebufferIndex] == NULL)
        return;
    uint64_t presentStartTicks = presentMs != NULL ? gettime() : 0u;
    GX_present(renderer, video->framebuffers[video->framebufferIndex],
        video->mode->fbWidth, video->mode->efbHeight, video->mode->xfbHeight);
    if (presentMs != NULL) *presentMs = ticksToMs(presentStartTicks, gettime());

    VIDEO_SetNextFramebuffer(video->framebuffers[video->framebufferIndex]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    video->framebufferIndex ^= 1u;
}

static void sessionDestroy(WiiGameSession* session) {
    if (session == NULL) return;
    if (session->audio != NULL) {
        AudioSystem* audio = (AudioSystem*)session->audio;
        audio->vtable->destroy(audio);
    }
    if (session->renderer != NULL) session->renderer->vtable->destroy(session->renderer);
    Runner_free(session->runner);
    WiiFS_destroy(session->fileSystem);
    VM_free(session->vm);
    DataWin_free(session->dataWin);
    free(session->dataWinPath);
    memset(session, 0, sizeof(*session));
}

static bool sessionLoad(WiiGameSession* session, const char* dataWinPath, char** gameArgs,
    const WiiRunnerSettings* settings, WiiOverlay* overlay) {
    if (session == NULL || dataWinPath == NULL) return false;
    memset(session, 0, sizeof(*session));
    session->dataWinPath = safeStrdup(dataWinPath);
    if (overlay != NULL) WiiOverlay_drawStatusScreen(overlay, NULL, "Loading data.win...", false);

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
        .progressCallback = overlay != NULL ? WiiOverlay_statusCallback : NULL,
        .progressCallbackUserData = overlay,
    };
    session->dataWin = DataWin_parse(dataWinPath, parserOptions);
    if (session->dataWin == NULL) {
        sessionDestroy(session);
        return false;
    }
    const char* gameName =
        session->dataWin->gen8.displayName != NULL ? session->dataWin->gen8.displayName : NULL;
    if (overlay != NULL) WiiOverlay_drawStatusScreen(overlay, gameName, "Creating VM...", true);

    session->vm = VM_create(session->dataWin);
    if (overlay != NULL)
        WiiOverlay_drawStatusScreen(overlay, gameName, "Creating filesystem...", true);

    session->fileSystem = WiiFS_create(dataWinPath);
    if (overlay != NULL)
        WiiOverlay_drawStatusScreen(overlay, gameName, "Creating renderer...", true);

    session->renderer = GX_create(dataWinPath, settings == NULL || settings->cacheAtlas,
        settings != NULL ? settings->atlasTextureCacheBytes : 0u);
    if (overlay != NULL) WiiOverlay_drawStatusScreen(overlay, gameName, "Creating audio...", true);

    session->audio = WiiAudio_create(dataWinPath);
    if (overlay != NULL) WiiOverlay_drawStatusScreen(overlay, gameName, "Creating runner...", true);

    session->runner = Runner_create(session->dataWin, session->vm, session->renderer,
        (FileSystem*)session->fileSystem, (AudioSystem*)session->audio, 0);
    session->runner->osType = settings != NULL ? settings->osType : WII_SCRIPT_OS_TYPE;
    if (gameArgs != NULL) Runner_setGameArgs(session->runner, gameArgs, (int32_t)arrlen(gameArgs));

    if (overlay != NULL)
        WiiOverlay_drawStatusScreen(overlay, gameName, "Entering first room...", true);
    Runner_initFirstRoom(session->runner);
    GX_syncRoom((WiiRenderer*)session->renderer, session->runner->currentRoomIndex);
    if (overlay != NULL)
        WiiOverlay_drawStatusScreen(overlay, gameName, "Starting frame loop...", true);

    return true;
}

static bool sessionChangeGame(WiiGameSession* session, char* workingDirectory,
    char* launchParameters, const WiiRunnerSettings* settings, WiiOverlay* overlay) {
    if (session == NULL || workingDirectory == NULL || launchParameters == NULL) return false;

    char** newArguments = extractRunnerArguments(launchParameters);
    char* newDataWinPath =
        WiiFS_resolveGamePath(session->dataWinPath, workingDirectory, newArguments);
    if (newDataWinPath == NULL) {
        freeStringArray(newArguments);
        return false;
    }
    logEvent("game_change", "loading data.win path=%s", newDataWinPath);

    char** gameArgs = NULL;
    const char* argv0 = "butterscotch-wii";
    if (session->runner != NULL && arrlen(session->runner->gameArgs) > 0 &&
        session->runner->gameArgs[0] != NULL) {
        argv0 = session->runner->gameArgs[0];
    }
    arrput(gameArgs, safeStrdup(argv0));
    repeat(arrlen(newArguments), i) arrput(gameArgs, safeStrdup(newArguments[i]));

    sessionDestroy(session);
    bool ok = sessionLoad(session, newDataWinPath, gameArgs, settings, overlay);
    logEvent("game_change", "data.win load %s", ok ? "succeeded" : "failed");

    free(newDataWinPath);
    freeStringArray(gameArgs);
    freeStringArray(newArguments);
    return ok;
}

typedef struct {
    WiiVideoState video;
    WiiOverlay overlay;
    WiiOverlay* overlayPtr;
    WiiRunnerSettings settings;
    WiiGameSession session;
    char* settingsPath;
    bool held[GML_KEY_COUNT];
    uint64_t lastTicks;
    WiiExitMode exitMode;
    int exitCode;
    bool homeMenuOpen;
    WiiHomeMenuScreen homeMenuScreen;
    bool audioSuspendedForHomeMenu;
    int homeMenuSelection;
    int suppressGameInputFrames;
    WiiDefaultFontRenderer* debugFont;
    WiiDebugStats debugStats;
    bool debugStatsVisible;
    WiiFramePacer framePacer;
} WiiApplication;

typedef struct {
    Runner* runner;
    Renderer* renderer;
    WiiRenderer* wiiRenderer;
    uint64_t frameStartTicks;
    uint32_t frameStartRetrace;
    bool suppressInput;
    int32_t outputW;
    int32_t outputH;
    int32_t windowW;
    int32_t windowH;
    int32_t gameW;
    int32_t gameH;
    bool autoDrawAppSurface;
    bool roomChangePending;
    uint64_t stepStartTicks;
    uint64_t stepEndTicks;
    uint64_t audioStartTicks;
    uint64_t audioEndTicks;
} WiiGameplayFrame;

typedef enum {
    WII_FRAME_RUN,
    WII_FRAME_SKIP,
    WII_FRAME_EXIT,
} WiiFrameAction;

static void appInitControllers(void) {
    PAD_Init();
    WPAD_Init();
    for (uint32_t channel = 0u; channel < WII_INPUT_CONTROLLER_COUNT; ++channel) {
        WPAD_SetDataFormat((s32)channel, WPAD_FMT_BTNS_ACC_IR);
        WPAD_SetVRes((s32)channel, WII_FRAMEBUFFER_WIDTH, WII_FRAMEBUFFER_HEIGHT);
    }
}

static int selectInitialGame(WiiOverlay* overlay, char* const* gamePaths, int gameCount) {
    if (gameCount <= 1) return gameCount == 1 ? 0 : -1;
    if (overlay == NULL) return -1;

    int selected = 0;
    primeHomePad();
    WPAD_ScanPads();
    for (;;) {
        WiiOverlay_drawGameSelector(overlay, gamePaths, gameCount, selected);
        WiiHomeMenuInput input;
        pollHomeMenuControls(&input);
        if (input.cancel) return -1;
        if (input.up && !input.down) selected = (selected + gameCount - 1) % gameCount;
        if (input.down && !input.up) selected = (selected + 1) % gameCount;
        if (input.confirm) return selected;
    }
}

static bool appInit(WiiApplication* app, int argc, char** argv) {
    if (!initVideo(&app->video)) {
        startupFailure("Failed to initialize Wii video.");
        return false;
    }
    appInitControllers();
    if (WiiOverlay_init(&app->overlay, &app->video)) app->overlayPtr = &app->overlay;
    if (app->overlayPtr != NULL) {
        WiiOverlay_drawStatusScreen(
            app->overlayPtr, NULL, "Cinnamon Wii starting...", false);
    }

    if (!fatInitDefault()) {
        WiiOverlay_drawStatusScreen(
            app->overlayPtr, NULL, "Failed to mount SD/USB FAT storage.", false);
        WiiOverlay_destroy(app->overlayPtr);
        startupFailure("Failed to mount SD/USB FAT storage.");
        return false;
    }
    app->settingsPath = WiiFS_resolveSettingsPath(argc, argv, WII_SETTINGS_PATH);
    app->settings = settingsLoad(app->settingsPath);
    char* appRoot = WiiFS_getParentDir(app->settingsPath);
    char* appsRoot = WiiFS_getParentDir(appRoot);
    logSetDirectory(appRoot);
    logSetEnabled(app->settings.debugLogEnabled);
    logInit();
    logEvent("boot", "main start argc=%ld", (long)argc);
    applyWiiAspect(&app->settings);
    if (app->overlayPtr != NULL) {
        WiiOverlay_drawStatusScreen(app->overlayPtr, NULL, "Locating data.win...", false);
    }

    char* configuredRoot = resolveGameRoot(&app->settings, appsRoot);
    char** gamePaths = WiiFS_resolveDataWins(argc, argv, configuredRoot, appRoot);
    free(appsRoot);
    free(appRoot);
    free(configuredRoot);
    int selectedGame = selectInitialGame(app->overlayPtr, gamePaths, (int)arrlen(gamePaths));
    if (arrlen(gamePaths) > 1 && selectedGame < 0) {
        freeStringArray(gamePaths);
        WiiOverlay_destroy(app->overlayPtr);
        free(app->settingsPath);
        logClose();
        if (app->overlayPtr != NULL) app->exitMode = WII_EXIT_HOMEBREW_CHANNEL;
        return false;
    }
    const char* dataWinPath = selectedGame >= 0 ? gamePaths[selectedGame] : NULL;
    logEvent("boot", "data.win path=%s", dataWinPath != NULL ? dataWinPath : "<null>");

    char** initialGameArgs = NULL;
    arrput(initialGameArgs, safeStrdup(argc > 0 && argv[0] != NULL ? argv[0] : "butterscotch-wii"));

    bool sessionOk = sessionLoad(
        &app->session, dataWinPath, initialGameArgs, &app->settings, app->overlayPtr);
    freeStringArray(gamePaths);
    freeStringArray(initialGameArgs);
    if (!sessionOk) {
        WiiOverlay_drawStatusScreen(app->overlayPtr, NULL, "Failed to parse data.win.", true);
        WiiOverlay_destroy(app->overlayPtr);
        free(app->settingsPath);
        logEvent("boot", "data.win load failed");
        logClose();
        startupFailure("Failed to parse data.win.");
        return false;
    }
    logEvent("boot", "data.win loaded");

    app->lastTicks = gettime();
    app->homeMenuScreen = WII_HOME_MENU_SCREEN_MAIN;
    app->debugFont = WiiFont_create();
    app->debugStatsVisible = app->settings.debugStatsEnabled;
    return true;
}

static bool appRunHomeMenuFrame(WiiApplication* app) {
    Runner* runner = app->session.runner;
    Renderer* renderer = app->session.renderer;
    WiiRenderer* wiiRenderer = (WiiRenderer*)renderer;
    applyWiiVideoMode(&app->video, false);
    if (renderer != NULL) {
        GX_setOutputSize(wiiRenderer, WII_FRAMEBUFFER_WIDTH, WII_FRAMEBUFFER_HEIGHT);
        if (app->video.horizontalScale < 1.0f) {
            int32_t contentW =
                (int32_t)((float)WII_FRAMEBUFFER_WIDTH * app->video.horizontalScale);
            GX_setOutputRect(wiiRenderer, (int32_t)app->video.horizontalOffset, 0,
                contentW, WII_FRAMEBUFFER_HEIGHT);
        }
    }

    WiiHomeMenuAction action = pollHomeMenuInput(
        &app->homeMenuScreen, &app->homeMenuSelection, &app->settings);
    if (action == WII_HOME_MENU_ACTION_RESUME) {
        if (app->audioSuspendedForHomeMenu) {
            setRunnerAudioSuspended(runner, false);
            app->audioSuspendedForHomeMenu = false;
        }
        app->homeMenuOpen = false;
        app->homeMenuScreen = WII_HOME_MENU_SCREEN_MAIN;
        memset(app->held, 0, sizeof(app->held));
        app->suppressGameInputFrames = 1;
        memset(&app->framePacer, 0, sizeof(app->framePacer));
        app->lastTicks = gettime();
        return true;
    }
    if (action == WII_HOME_MENU_ACTION_SETTINGS_CHANGED) {
        applyWiiAspect(&app->settings);
        settingsSave(&app->settings, app->settingsPath);
        if (app->debugStatsVisible != app->settings.debugStatsEnabled) {
            memset(&app->debugStats, 0, sizeof(app->debugStats));
        }
        app->debugStatsVisible = app->settings.debugStatsEnabled;
        logSetEnabled(app->settings.debugLogEnabled);
    } else if (action == WII_HOME_MENU_ACTION_HOMEBREW_CHANNEL) {
        app->exitMode = WII_EXIT_HOMEBREW_CHANNEL;
        return false;
    } else if (action == WII_HOME_MENU_ACTION_WII_MENU) {
        app->exitMode = WII_EXIT_WII_MENU;
        return false;
    }

    if (renderer != NULL) {
        renderer->vtable->endFrameInit(renderer);
        renderer->vtable->flush(renderer);
    }
    if (app->homeMenuScreen == WII_HOME_MENU_SCREEN_RUNNER_SETTINGS) {
        WiiOverlay_drawSettings(app->overlayPtr, app->homeMenuSelection,
            settingsP240Label(app->settings.p240Mode),
            settingsAspectLabel(app->settings.aspectRatioMode),
            settingsBoolLabel(app->settings.debugStatsEnabled),
            settingsBoolLabel(app->settings.debugLogEnabled));
    } else {
        WiiOverlay_drawHomeMenu(app->overlayPtr, app->homeMenuSelection);
    }
    presentVideo(&app->video, wiiRenderer, NULL);
    WiiAudio_pump((WiiAudioSystem*)runner->audioSystem);
    app->lastTicks = gettime();
    return true;
}

static WiiFrameAction appBeginGameplayFrame(
    WiiApplication* app, WiiGameplayFrame* frame) {
    frame->runner = app->session.runner;
    frame->renderer = app->session.renderer;
    frame->wiiRenderer = (WiiRenderer*)frame->renderer;
    frame->frameStartTicks = gettime();
    frame->frameStartRetrace = gWiiRetraceCount;
    frame->suppressInput = app->suppressGameInputFrames > 0;
    bool homePressed = false;
    if (frame->suppressInput) {
        app->suppressGameInputFrames--;
        clearGameInputsForFrame(frame->runner, app->held);
    } else {
        if (app->debugStatsVisible) {
            updateDebugFps(&app->debugStats, frame->frameStartTicks);
        }
        homePressed = pollInput(frame->runner, app->held, &app->settings);
    }
    if (homePressed) {
        app->homeMenuOpen = true;
        app->homeMenuScreen = WII_HOME_MENU_SCREEN_MAIN;
        app->homeMenuSelection = 0;
        if (!app->audioSuspendedForHomeMenu) {
            setRunnerAudioSuspended(frame->runner, true);
            app->audioSuspendedForHomeMenu = true;
        }
        primeHomePad();
        app->lastTicks = gettime();
        return WII_FRAME_SKIP;
    }
    return frame->runner->shouldExit ? WII_FRAME_EXIT : WII_FRAME_RUN;
}

static void appUpdateGameplayFrame(WiiApplication* app, WiiGameplayFrame* frame) {
    Runner* runner = frame->runner;

    applyWiiVideoMode(&app->video, useLowResGameplay(&app->session, &app->settings));
    frame->outputW = app->video.mode != NULL && app->video.mode->fbWidth > 0u
        ? (int32_t)app->video.mode->fbWidth
        : WII_FRAMEBUFFER_WIDTH;
    frame->outputH = app->video.mode != NULL && app->video.mode->efbHeight > 0u
        ? (int32_t)app->video.mode->efbHeight
        : WII_FRAMEBUFFER_HEIGHT;
    frame->windowW = app->video.lowResActive ? WII_LOW_RES_LOGICAL_WIDTH : frame->outputW;
    frame->windowH = app->video.lowResActive ? WII_LOW_RES_LOGICAL_HEIGHT : frame->outputH;
    runner->deltaTime = ticks_to_microsecs(frame->frameStartTicks - app->lastTicks);
    app->lastTicks = frame->frameStartTicks;

    frame->stepStartTicks = app->debugStatsVisible ? gettime() : 0u;
    Runner_step(runner);
    frame->stepEndTicks = app->debugStatsVisible ? gettime() : 0u;
    frame->audioStartTicks = frame->stepEndTicks;
    if (runner->audioSystem != NULL) {
        runner->audioSystem->vtable->update(
            runner->audioSystem, (float)runner->deltaTime / 1000000.0f);
    }
    frame->audioEndTicks = app->debugStatsVisible ? gettime() : 0u;

    frame->gameW = WII_FRAMEBUFFER_WIDTH;
    frame->gameH = WII_FRAMEBUFFER_HEIGHT;
    getCurrentFrameSize(runner, &frame->gameW, &frame->gameH);
    bool directToScreen = wiiFrameTooLarge(frame->gameW, frame->gameH);
    runner->applicationWidth = frame->gameW;
    runner->applicationHeight = frame->gameH;
    runner->renderGameW = (uint32_t)frame->gameW;
    runner->renderGameH = (uint32_t)frame->gameH;
    runner->usingAppSurface = runner->appSurfaceEnabled && !directToScreen;
    runner->applicationSurfaceId = frame->renderer != NULL
        ? frame->renderer->vtable->ensureApplicationSurface(
            frame->renderer, frame->gameW, frame->gameH)
        : APPLICATION_SURFACE_ID;
    frame->autoDrawAppSurface = runner->usingAppSurface && runner->appSurfaceAutoDraw;
    WiiPresentationRect rect = frame->autoDrawAppSurface
        ? getWiiPresentationRect(&app->video, &app->settings, frame->outputW, frame->outputH,
            frame->gameW, frame->gameH)
        : (WiiPresentationRect){0, 0, frame->outputW, frame->outputH};
    if (frame->renderer != NULL) {
        GX_setOutputSize(frame->wiiRenderer, frame->outputW, frame->outputH);
        GX_setOutputRect(
            frame->wiiRenderer, rect.x, rect.y, rect.width, rect.height);
    }
}

static void appDrawGameplayFrame(WiiApplication* app, WiiGameplayFrame* frame) {
    Runner* runner = frame->runner;
    Renderer* renderer = frame->renderer;
    uint64_t drawStartTicks = app->debugStatsVisible ? gettime() : 0u;
    Runner_drawPre(runner, frame->gameW, frame->gameH);
    Runner_beginFrame(runner, frame->gameW, frame->gameH, frame->windowW, frame->windowH,
        frame->outputW, frame->outputH);
    Runner_drawViews(runner, frame->gameW, frame->gameH, false);
    renderer->vtable->endFrameInit(renderer);
    Runner_drawPost(runner, frame->autoDrawAppSurface ? frame->gameW : frame->windowW,
        frame->autoDrawAppSurface ? frame->gameH : frame->windowH);
    Runner_drawGUI(runner, frame->windowW, frame->windowH, frame->gameW, frame->gameH);
    renderer->vtable->endFrameEnd(renderer);
    frame->roomChangePending = runner->pendingRoom != -1;
    uint64_t drawEndTicks = app->debugStatsVisible ? gettime() : 0u;
    if (app->debugStatsVisible) {
        app->debugStats.stepMs = ticksToMs(frame->stepStartTicks, frame->stepEndTicks);
        app->debugStats.audioMs = ticksToMs(frame->audioStartTicks, frame->audioEndTicks);
        app->debugStats.drawMs = ticksToMs(drawStartTicks, drawEndTicks);
        app->debugStats.tickMs = ticksToMs(frame->frameStartTicks, drawEndTicks);

        renderer->vtable->flush(renderer);
        drawDebugStatsOverlay(app->debugFont, frame->wiiRenderer, runner, &app->debugStats);
    }
    if (frame->roomChangePending) GX_clearOutput(frame->wiiRenderer);

    presentVideo(&app->video, frame->wiiRenderer,
        app->debugStatsVisible ? &app->debugStats.presentMs : NULL);

    if (frame->roomChangePending) {
        logEvent("room", "change begin from=%ld to=%ld", (long)runner->currentRoomIndex,
            (long)runner->pendingRoom);
    }
    Runner_handlePendingRoomChange(runner);
    if (frame->roomChangePending)
        logEvent("room", "entered room=%ld; sync textures", (long)runner->currentRoomIndex);

    GX_syncRoom(frame->wiiRenderer, runner->currentRoomIndex);
    if (frame->roomChangePending)
        logEvent("room", "textures ready room=%ld", (long)runner->currentRoomIndex);
    if (frame->suppressInput) absorbSuppressedInputs(runner, app->held, &app->settings);
}

static bool appFinishGameplayFrame(WiiApplication* app, WiiGameplayFrame* frame) {
    Runner* runner = frame->runner;
    if (runner->pendingWorkingDirectory != NULL && runner->pendingLaunchParameters != NULL) {
        char* workingDirectory = runner->pendingWorkingDirectory;
        char* launchParameters = runner->pendingLaunchParameters;
        runner->pendingWorkingDirectory = NULL;
        runner->pendingLaunchParameters = NULL;
        applyWiiVideoMode(&app->video, false);
        bool changed = sessionChangeGame(&app->session, workingDirectory,
            launchParameters, &app->settings, app->overlayPtr);
        free(workingDirectory);
        free(launchParameters);
        if (!changed) {
            app->exitCode = 1;
            return false;
        }
        memset(app->held, 0, sizeof(app->held));
        memset(&app->framePacer, 0, sizeof(app->framePacer));
        app->lastTicks = gettime();
        return true;
    }

    RunnerKeyboard_beginFrame(runner->keyboard);
    if (!frame->roomChangePending) {

        waitForGameFrameVsyncs(
            &app->framePacer, &app->video, runner, frame->frameStartRetrace);

    }

    WiiAudio_pump((WiiAudioSystem*)runner->audioSystem);

    return true;
}

static void appRunApplication(WiiApplication* app) {
    while (app->session.runner != NULL) {
        if (app->homeMenuOpen) {
            if (!appRunHomeMenuFrame(app)) break;
            continue;
        }

        WiiGameplayFrame frame = {0};
        WiiFrameAction action = appBeginGameplayFrame(app, &frame);
        if (action == WII_FRAME_EXIT) break;
        if (action == WII_FRAME_SKIP) continue;
        appUpdateGameplayFrame(app, &frame);
        appDrawGameplayFrame(app, &frame);
        if (!appFinishGameplayFrame(app, &frame)) break;
    }
}

static int appShutdown(WiiApplication* app) {
    WiiFont_destroy(app->debugFont);
    WiiOverlay_destroy(app->overlayPtr);
    sessionDestroy(&app->session);
    if (app->exitMode == WII_EXIT_HOMEBREW_CHANNEL) {
        VIDEO_SetBlack(TRUE);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        free(app->settingsPath);
        logEvent("exit", "returning to Homebrew Channel");
        logClose();
        return 0;
    }
    if (app->exitMode == WII_EXIT_WII_MENU) {
        VIDEO_SetBlack(TRUE);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        free(app->settingsPath);
        logEvent("exit", "returning to Wii Menu");
        logClose();
        SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
        return 0;
    }
    free(app->settingsPath);
    logEvent("exit", "main exit code=%d", app->exitCode);
    logClose();
    return app->exitCode;
}

int main(int argc, char** argv) {
    WiiApplication app = {0};
    if (!appInit(&app, argc, argv))
        return app.exitMode == WII_EXIT_HOMEBREW_CHANNEL ? 0 : 1;
    appRunApplication(&app);
    return appShutdown(&app);
}
