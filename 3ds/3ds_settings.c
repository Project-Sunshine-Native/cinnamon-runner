#include "3ds_settings.h"
#include "3ds_file_system.h"
#include "../json_reader.h"
#include "../json_writer.h"
#include "../log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char settingsError[128];

const char* n3ds_settingsError(void) {
    return settingsError;
}

static void n3ds_settingsSetError(const char* action, const char* path, int error) {
    snprintf(settingsError, sizeof(settingsError), "%s: %s", action, strerror(error));
    logWarn("Settings %s (%s): %s\n", action, path, strerror(error));
}

const N3DSKeyBinding N3DS_KEY_BINDINGS[N3DS_KEY_MAPPING_COUNT] = {
    {"dpadLeft", "D-pad left", KEY_DLEFT},
    {"dpadDown", "D-pad down", KEY_DDOWN},
    {"dpadRight", "D-pad right", KEY_DRIGHT},
    {"dpadUp", "D-pad up", KEY_DUP},
    {"circleLeft", "Circle pad left", KEY_CPAD_LEFT},
    {"circleDown", "Circle pad down", KEY_CPAD_DOWN},
    {"circleUp", "Circle pad up", KEY_CPAD_UP},
    {"circleRight", "Circle pad right", KEY_CPAD_RIGHT},
    {"a", "A", KEY_A}, {"b", "B", KEY_B}, {"x", "X", KEY_X}, {"y", "Y", KEY_Y},
    {"start", "START", KEY_START}, {"l", "L", KEY_L}, {"r", "R", KEY_R},
    {"cstickLeft", "C-stick left", KEY_CSTICK_LEFT},
    {"cstickDown", "C-stick down", KEY_CSTICK_DOWN},
    {"cstickUp", "C-stick up", KEY_CSTICK_UP},
    {"cstickRight", "C-stick right", KEY_CSTICK_RIGHT},
    {"zl", "ZL", KEY_ZL}, {"zr", "ZR", KEY_ZR},
};

static const char* const offOn[] = {"Off", "On"};
static const char* const scaling[] = {"OG", "Stretch", "Fullscreen"};
static const char* const filtering[] = {"Current", "Off"};
static const char* const screens[] = {"Top", "Bottom"};
static const char* const resolution[] = {"Normal", "Half"};
static const char* const sorting[] = {"Alphabetical", "Recent"};
static const char* const launcherUIs[] = {"DELTARUNE", "Basic"};

const N3DSSetting N3DS_GAME_SETTINGS[] = {
    {"scaling", "Scaling",
        offsetof(N3DSRunnerSettings, scaling), 0, 2, 1, scaling},
    {"filteringOff", "Filtering",
        offsetof(N3DSRunnerSettings, filteringOff), 0, 1, 1, filtering},
    {"bottomScreen", "Game screen",
        offsetof(N3DSRunnerSettings, bottomScreen), 0, 1, 1, screens},
    {"halfResolutionApplicationSurface", "Resolution",
        offsetof(N3DSRunnerSettings, halfResApplicationSurface), 0, 1, 1, resolution},
    {"gamepadApi", "Gamepad support",
        offsetof(N3DSRunnerSettings, gamepadApiEnabled), 0, 1, 1, offOn},
    {"touchMouse", "Touch mouse",
        offsetof(N3DSRunnerSettings, touchMouse), 0, 1, 1, offOn},
    {"volume", "Game volume",
        offsetof(N3DSRunnerSettings, volume), 0, 100, 10, NULL},
    {"mute", "Mute",
        offsetof(N3DSRunnerSettings, mute), 0, 1, 1, offOn},
    {"debugStats", "Performance overlay",
        offsetof(N3DSRunnerSettings, debugStatsEnabled), 0, 1, 1, offOn},
    {"debugLog", "Debug logging",
        offsetof(N3DSRunnerSettings, debugLogEnabled), 0, 1, 1, offOn},
    {"cacheAtlas", "Texture caching",
        offsetof(N3DSRunnerSettings, cacheAtlas), 0, 1, 1, offOn},
    {"atlasTextureCacheMb", "Texture cache MB",
        offsetof(N3DSRunnerSettings, atlasTextureCacheMb), 0, 60, 4, NULL},
    {"osType", "Reported OS",
        offsetof(N3DSRunnerSettings, osType), -1, 20, 1, NULL},
};

const N3DSSetting N3DS_LAUNCHER_SETTINGS[] = {
    {"uiStyle", "Launcher UI",
        offsetof(N3DSLauncherSettings, uiStyle), N3DS_LAUNCHER_DELTARUNE, N3DS_LAUNCHER_BASIC, 1, launcherUIs},
    {"musicVolume", "Music volume",
        offsetof(N3DSLauncherSettings, musicVolume), 0, 100, 10, NULL},
    {"soundVolume", "Sound volume",
        offsetof(N3DSLauncherSettings, soundVolume), 0, 100, 10, NULL},
    {"sortRecent", "Game order",
        offsetof(N3DSLauncherSettings, sortRecent), 0, 1, 1, sorting},
    {"rememberSelection", "Remember selection",
        offsetof(N3DSLauncherSettings, rememberSelection), 0, 1, 1, offOn},
    {"showPaths", "Show folder paths",
        offsetof(N3DSLauncherSettings, showPaths), 0, 1, 1, offOn},
    {"showIntro", "Startup intro",
        offsetof(N3DSLauncherSettings, showIntro), 0, 1, 1, offOn},
};

int32_t* n3ds_settingValue(void* settings, const N3DSSetting* option) {
    return (int32_t*)((char*)settings + option->offset);
}

N3DSRunnerSettings n3ds_settingsDefaults(void) {
    N3DSRunnerSettings settings = {.cacheAtlas = 1, .gamepadApiEnabled = 1, .touchMouse = 1, .volume = 100};
    const int32_t keys[N3DS_KEY_MAPPING_COUNT] = {
        37, 40, 39, 38, 37, 40, 38, 39, 'Z', 'X', 'C', 16, 13, 'Q', 'E', 37, 40, 38, 39, 9, 27,
    };
    memcpy(settings.keyMappings, keys, sizeof(keys));
    return settings;
}

static JsonValue* n3ds_settingsReadFile(const char* path, bool* readable) {
    if (readable != NULL) *readable = false;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || size > 256 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    char* text = malloc((size_t)size + 1);
    JsonValue* root = NULL;
    if (text != NULL && fread(text, 1, (size_t)size, file) == (size_t)size) {
        if (readable != NULL) *readable = true;
        text[size] = '\0';
        root = JsonReader_parse(text);
    }
    free(text);
    fclose(file);
    return root;
}

static JsonValue* n3ds_settingsReadBackup(const char* path) {
    char backup[1200];
    if (snprintf(backup, sizeof(backup), "%s.bak", path) >= (int)sizeof(backup)) return NULL;
    JsonValue* root = n3ds_settingsReadFile(backup, NULL);
    if (JsonReader_isObject(root)) return root;
    JsonReader_free(root);
    return NULL;
}

static JsonValue* n3ds_settingsRead(const char* path) {
    JsonValue* root = n3ds_settingsReadFile(path, NULL);
    if (JsonReader_isObject(root)) return root;
    JsonReader_free(root);
    return n3ds_settingsReadBackup(path);
}

static bool n3ds_settingsEnsureParent(const char* path) {
    char parent[1100];
    if (!n3ds_fsGetParentDir(path, parent, sizeof(parent))) {
        errno = ENAMETOOLONG;
        return false;
    }
    for (char* p = parent; ; ++p) {
        if ((*p != '/' && *p != '\0') || (*p == '/' && p > parent && p[-1] == ':')) continue;
        bool last = *p == '\0';
        *p = '\0';
        if (parent[0] != '\0' && mkdir(parent, 0777) != 0 &&
            !(errno == EEXIST && n3ds_fsPathIsDirectory(parent))) {
            if (!last) *p = '/';
            return false;
        }
        if (last) return true;
        *p = '/';
    }
}

static bool n3ds_settingsCommit(const char* path, const char* data, size_t size, bool primaryValid) {
    char temporary[1200], previous[1200];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary) ||
        snprintf(previous, sizeof(previous), "%s.%s", path, primaryValid ? "bak" : "corrupt") >= (int)sizeof(previous)) {
        n3ds_settingsSetError("Path too long", path, ENAMETOOLONG);
        return false;
    }
    if (!n3ds_settingsEnsureParent(path)) {
        n3ds_settingsSetError("Could not create settings folder", path, errno);
        return false;
    }
    FILE* file = fopen(temporary, "wb");
    if (file == NULL) {
        n3ds_settingsSetError("Could not open temporary file", path, errno);
        return false;
    }
    bool written = fwrite(data, 1, size, file) == size;
    if (written) written = fflush(file) == 0;
    if (written) written = fsync(fileno(file)) == 0;
    int error = errno;
    if (fclose(file) != 0) {
        if (written) error = errno;
        written = false;
    }
    if (!written) {
        remove(temporary);
        n3ds_settingsSetError("Could not flush settings", path, error != 0 ? error : EIO);
        return false;
    }
    bool hadPrevious = n3ds_fsPathExists(path);
    if (hadPrevious) {
        if (remove(previous) != 0 && errno != ENOENT) {
            error = errno;
            remove(temporary);
            n3ds_settingsSetError("Could not replace backup", path, error);
            return false;
        }
        if (rename(path, previous) != 0) {
            error = errno;
            remove(temporary);
            n3ds_settingsSetError("Could not back up settings", path, error);
            return false;
        }
    }
    if (rename(temporary, path) != 0) {
        error = errno;
        if (hadPrevious && rename(previous, path) != 0)
            logWarn("Could not restore settings; previous file remains at %s\n", previous);
        remove(temporary);
        n3ds_settingsSetError("Could not install settings", path, error);
        return false;
    }
    return true;
}

static void n3ds_settingsReadOptions(const JsonValue* object, void* settings,
    const N3DSSetting* options, int count) {
    if (!JsonReader_isObject(object)) return;
    for (int i = 0; i < count; ++i) {
        JsonValue* value = JsonReader_getJsonValueByKey(object, options[i].key);
        if (!JsonReader_isNumber(value) && !JsonReader_isBool(value)) continue;
        int64_t number = JsonReader_isBool(value) ? JsonReader_getBool(value) : JsonReader_getInt(value);
        if (number < options[i].min) number = options[i].min;
        if (number > options[i].max) number = options[i].max;
        *n3ds_settingValue(settings, &options[i]) = (int32_t)number;
    }
}

void n3ds_settingsLoad(const char* path, N3DSRunnerSettings* game, N3DSLauncherSettings* launcher) {
    if (launcher != NULL) {
        memset(launcher, 0, sizeof(*launcher));
        launcher->musicVolume = 100;
        launcher->soundVolume = 100;
        launcher->rememberSelection = 1;
        launcher->showPaths = 1;
        launcher->showIntro = 1;
    }
    JsonValue* root = n3ds_settingsRead(path);
    if (!JsonReader_isObject(root)) {
        JsonReader_free(root);
        return;
    }
    if (game != NULL) {
        n3ds_settingsReadOptions(JsonReader_getJsonValueByKey(root, "runner"), game,
            N3DS_GAME_SETTINGS, N3DS_GAME_SETTING_COUNT + N3DS_DEV_SETTING_COUNT);
        JsonValue* keyboard = JsonReader_getJsonValueByKey(root, "keyboard");
        if (JsonReader_isObject(keyboard)) {
            for (int i = 0; i < N3DS_KEY_MAPPING_COUNT; ++i) {
                JsonValue* value = JsonReader_getJsonValueByKey(keyboard, N3DS_KEY_BINDINGS[i].jsonKey);
                if (JsonReader_isNumber(value)) {
                    int64_t key = JsonReader_getInt(value);
                    if (key >= -1 && key <= 255) game->keyMappings[i] = (int32_t)key;
                }
            }
        }
    }
    if (launcher != NULL) {
        JsonValue* object = JsonReader_getJsonValueByKey(root, "launcher");
        n3ds_settingsReadOptions(object, launcher, N3DS_LAUNCHER_SETTINGS, N3DS_LAUNCHER_SETTING_COUNT);
        JsonValue* recent = JsonReader_isObject(object) ? JsonReader_getJsonValueByKey(object, "recentGames") : NULL;
        if (JsonReader_isArray(recent)) {
            for (int i = 0; i < JsonReader_arrayLength(recent) && launcher->recentCount < N3DS_RECENT_GAME_COUNT; ++i) {
                JsonValue* value = JsonReader_getArrayElement(recent, i);
                if (!JsonReader_isString(value)) continue;
                const char* name = JsonReader_getString(value);
                if (strlen(name) >= sizeof(launcher->recentGames[0])) continue;
                snprintf(launcher->recentGames[launcher->recentCount++], sizeof(launcher->recentGames[0]), "%s", name);
            }
        }
    }
    JsonReader_free(root);
}

static void n3ds_settingsWriteValue(JsonWriter* writer, const JsonValue* value) {
    switch (value->type) {
    case JSON_OBJECT:
        JsonWriter_beginObject(writer);
        for (int i = 0; i < JsonReader_objectLength(value); ++i) {
            JsonWriter_key(writer, JsonReader_getJsonKeyByIndex(value, i));
            n3ds_settingsWriteValue(writer, JsonReader_getJsonValueByIndex(value, i));
        }
        JsonWriter_endObject(writer);
        break;
    case JSON_ARRAY:
        JsonWriter_beginArray(writer);
        for (int i = 0; i < JsonReader_arrayLength(value); ++i)
            n3ds_settingsWriteValue(writer, JsonReader_getArrayElement(value, i));
        JsonWriter_endArray(writer);
        break;
    case JSON_STRING: JsonWriter_string(writer, JsonReader_getString(value)); break;
    case JSON_NUMBER: JsonWriter_double(writer, JsonReader_getDouble(value)); break;
    case JSON_BOOL: JsonWriter_bool(writer, JsonReader_getBool(value)); break;
    default: JsonWriter_null(writer); break;
    }
}

static void n3ds_settingsWriteOptions(JsonWriter* writer, const JsonValue* old,
    const void* settings, const void* defaults, const N3DSSetting* options, int count) {
    JsonWriter_beginObject(writer);
    if (JsonReader_isObject(old)) {
        for (int i = 0; i < JsonReader_objectLength(old); ++i) {
            const char* key = JsonReader_getJsonKeyByIndex(old, i);
            bool owned = strcmp(key, "recentGames") == 0 && options == N3DS_LAUNCHER_SETTINGS;
            for (int j = 0; j < count; ++j) owned |= strcmp(key, options[j].key) == 0;
            if (owned) continue;
            JsonWriter_key(writer, key);
            n3ds_settingsWriteValue(writer, JsonReader_getJsonValueByIndex(old, i));
        }
    }
    for (int i = 0; i < count; ++i) {
        int32_t value = *(const int32_t*)((const char*)settings + options[i].offset);
        if (defaults != NULL && value == *(const int32_t*)((const char*)defaults + options[i].offset)) continue;
        JsonWriter_key(writer, options[i].key);
        if (options[i].min == 0 && options[i].max == 1) JsonWriter_bool(writer, value != 0);
        else JsonWriter_int(writer, value);
    }
}

static uint32_t n3ds_settingsReadPlaytime(const JsonValue* root) {
    JsonValue* value = JsonReader_isObject(root) ? JsonReader_getJsonValueByKey(root, "playtimeSeconds") : NULL;
    if (!JsonReader_isNumber(value)) return 0;
    double seconds = JsonReader_getDouble(value);
    if (!(seconds > 0)) return 0;
    if (seconds >= UINT32_MAX) return UINT32_MAX;
    return (uint32_t)seconds;
}

static bool n3ds_settingsSaveFile(const char* path, const N3DSRunnerSettings* game,
    const N3DSRunnerSettings* defaults, const N3DSLauncherSettings* launcher,
    uint64_t addedSeconds, const char* name) {
    settingsError[0] = '\0';
    bool readable;
    JsonValue* root = n3ds_settingsReadFile(path, &readable);
    bool primaryValid = JsonReader_isObject(root);
    if (!readable && n3ds_fsPathExists(path)) {
        JsonReader_free(root);
        n3ds_settingsSetError("Could not read existing settings", path, errno != 0 ? errno : EIO);
        return false;
    }
    if (!primaryValid) {
        JsonReader_free(root);
        root = n3ds_settingsReadBackup(path);
    }
    JsonWriter writer = JsonWriter_create();
    JsonWriter_beginObject(&writer);
    if (JsonReader_isObject(root)) {
        for (int i = 0; i < JsonReader_objectLength(root); ++i) {
            const char* key = JsonReader_getJsonKeyByIndex(root, i);
            if (game != NULL && (strcmp(key, "runner") == 0 || strcmp(key, "keyboard") == 0)) continue;
            if (launcher != NULL && strcmp(key, "launcher") == 0) continue;
            if (addedSeconds > 0 && strcmp(key, "playtimeSeconds") == 0) continue;
            if (name != NULL && strcmp(key, "launcherName") == 0) continue;
            JsonWriter_key(&writer, key);
            n3ds_settingsWriteValue(&writer, JsonReader_getJsonValueByIndex(root, i));
        }
    }
    if (game != NULL) {
        JsonWriter_key(&writer, "runner");
        n3ds_settingsWriteOptions(&writer, root != NULL ? JsonReader_getJsonValueByKey(root, "runner") : NULL,
            game, defaults, N3DS_GAME_SETTINGS, N3DS_GAME_SETTING_COUNT + N3DS_DEV_SETTING_COUNT);
        JsonWriter_endObject(&writer);
        JsonWriter_key(&writer, "keyboard");
        JsonWriter_beginObject(&writer);
        JsonValue* keyboard = root != NULL ? JsonReader_getJsonValueByKey(root, "keyboard") : NULL;
        if (JsonReader_isObject(keyboard)) {
            for (int i = 0; i < JsonReader_objectLength(keyboard); ++i) {
                const char* key = JsonReader_getJsonKeyByIndex(keyboard, i);
                bool owned = false;
                for (int j = 0; j < N3DS_KEY_MAPPING_COUNT; ++j) owned |= strcmp(key, N3DS_KEY_BINDINGS[j].jsonKey) == 0;
                if (owned) continue;
                JsonWriter_key(&writer, key);
                n3ds_settingsWriteValue(&writer, JsonReader_getJsonValueByIndex(keyboard, i));
            }
        }
        for (int i = 0; i < N3DS_KEY_MAPPING_COUNT; ++i) {
            if (defaults != NULL && game->keyMappings[i] == defaults->keyMappings[i]) continue;
            JsonWriter_propertyInt(&writer, N3DS_KEY_BINDINGS[i].jsonKey, game->keyMappings[i]);
        }
        JsonWriter_endObject(&writer);
    }
    if (launcher != NULL) {
        JsonWriter_key(&writer, "launcher");
        n3ds_settingsWriteOptions(&writer, root != NULL ? JsonReader_getJsonValueByKey(root, "launcher") : NULL,
            launcher, NULL, N3DS_LAUNCHER_SETTINGS, N3DS_LAUNCHER_SETTING_COUNT);
        JsonWriter_key(&writer, "recentGames");
        JsonWriter_beginArray(&writer);
        for (int i = 0; i < launcher->recentCount; ++i) JsonWriter_string(&writer, launcher->recentGames[i]);
        JsonWriter_endArray(&writer);
        JsonWriter_endObject(&writer);
    }
    if (name != NULL && name[0] != '\0') JsonWriter_propertyString(&writer, "launcherName", name);
    if (addedSeconds > 0) {
        uint32_t previous = n3ds_settingsReadPlaytime(root);
        uint32_t total = addedSeconds >= UINT32_MAX - previous
            ? UINT32_MAX : previous + (uint32_t)addedSeconds;
        JsonWriter_propertyInt(&writer, "playtimeSeconds", total);
    }
    JsonWriter_endObject(&writer);
    bool unchanged = false;
    if (primaryValid) {
        JsonWriter existing = JsonWriter_create();
        n3ds_settingsWriteValue(&existing, root);
        unchanged = strcmp(JsonWriter_getOutput(&existing), JsonWriter_getOutput(&writer)) == 0;
        JsonWriter_free(&existing);
    }
    bool saved = unchanged || n3ds_settingsCommit(path, JsonWriter_getOutput(&writer),
        JsonWriter_getLength(&writer), primaryValid);
    JsonWriter_free(&writer);
    JsonReader_free(root);
    return saved;
}

bool n3ds_settingsSave(const char* path, const N3DSRunnerSettings* game,
    const N3DSRunnerSettings* defaults, const N3DSLauncherSettings* launcher) {
    return n3ds_settingsSaveFile(path, game, defaults, launcher, 0, NULL);
}

bool n3ds_settingsSaveGame(const char* path, const N3DSRunnerSettings* game,
    const N3DSRunnerSettings* defaults, const char* name) {
    if (name == NULL) return n3ds_settingsSaveFile(path, game, defaults, NULL, 0, NULL);
    while (*name == ' ') ++name;
    size_t length = strlen(name);
    while (length > 0 && name[length - 1] == ' ') --length;
    char trimmed[256];
    if (length >= sizeof(trimmed)) {
        n3ds_settingsSetError("Game name too long", path, EINVAL);
        return false;
    }
    memcpy(trimmed, name, length);
    trimmed[length] = '\0';
    return n3ds_settingsSaveFile(path, game, defaults, NULL, 0, trimmed);
}

bool n3ds_settingsGamePath(const char* dataWinPath, char* path, size_t capacity) {
    char parent[1024];
    if (!n3ds_fsGetParentDir(dataWinPath, parent, sizeof(parent))) return false;
    if (strncmp(parent, "romfs:", 6) == 0)
        return snprintf(path, capacity, "%s", N3DS_SETTINGS_PATH) < (int)capacity;
    return snprintf(path, capacity, "%s/cinnamon_settings.json", parent) < (int)capacity;
}

uint32_t n3ds_settingsPlaytime(const char* dataWinPath) {
    char path[1100];
    if (!n3ds_settingsGamePath(dataWinPath, path, sizeof(path))) return 0;
    JsonValue* root = n3ds_settingsRead(path);
    uint32_t seconds = n3ds_settingsReadPlaytime(root);
    JsonReader_free(root);
    return seconds;
}

bool n3ds_settingsAddPlaytime(const char* dataWinPath, uint64_t seconds) {
    if (seconds == 0) return true;
    char path[1100];
    if (!n3ds_settingsGamePath(dataWinPath, path, sizeof(path))) {
        n3ds_settingsSetError("Playtime path too long", dataWinPath, ENAMETOOLONG);
        return false;
    }
    return n3ds_settingsSaveFile(path, NULL, NULL, NULL, seconds, NULL);
}

bool n3ds_settingsGameName(const char* dataWinPath, char* name, size_t capacity) {
    char path[1100];
    if (!n3ds_settingsGamePath(dataWinPath, path, sizeof(path))) return false;
    JsonValue* root = n3ds_settingsRead(path);
    JsonValue* value = JsonReader_isObject(root) ? JsonReader_getJsonValueByKey(root, "launcherName") : NULL;
    bool found = false;
    if (JsonReader_isString(value)) {
        const char* custom = JsonReader_getString(value);
        size_t length = strlen(custom);
        if (length > 0 && length < capacity) {
            memcpy(name, custom, length + 1);
            found = true;
        }
    }
    JsonReader_free(root);
    return found;
}

void n3ds_settingsRememberGame(N3DSLauncherSettings* settings, const char* path) {
    int position = 0;
    while (position < settings->recentCount && strcmp(settings->recentGames[position], path) != 0) ++position;
    if (position == settings->recentCount && settings->recentCount < N3DS_RECENT_GAME_COUNT) ++settings->recentCount;
    if (position >= N3DS_RECENT_GAME_COUNT) position = N3DS_RECENT_GAME_COUNT - 1;
    for (int i = position; i > 0; --i)
        memcpy(settings->recentGames[i], settings->recentGames[i - 1], sizeof(settings->recentGames[i]));
    snprintf(settings->recentGames[0], sizeof(settings->recentGames[0]), "%s", path);
}
