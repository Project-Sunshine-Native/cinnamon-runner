#pragma once

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define N3DS_SETTINGS_PATH "sdmc:/3ds/cinnamon/cinnamon_settings.json"
#define N3DS_KEY_MAPPING_COUNT 21
#define N3DS_RECENT_GAME_COUNT 16

typedef enum {
    N3DS_SCALING_ORIGINAL,
    N3DS_SCALING_STRETCH,
    N3DS_SCALING_FULLSCREEN,
} N3DSScalingMode;

typedef enum {
    N3DS_LAUNCHER_DELTARUNE,
    N3DS_LAUNCHER_BASIC,
} N3DSLauncherUI;

typedef struct {
    int32_t scaling;
    int32_t filteringOff;
    int32_t bottomScreen;
    int32_t halfResApplicationSurface;
    int32_t gamepadApiEnabled;
    int32_t touchMouse;
    int32_t volume;
    int32_t mute;
    int32_t debugStatsEnabled;
    int32_t debugLogEnabled;
    int32_t cacheAtlas;
    int32_t atlasTextureCacheMb;
    int32_t osType;
    int32_t keyMappings[N3DS_KEY_MAPPING_COUNT];
} N3DSRunnerSettings;

typedef struct {
    int32_t uiStyle;
    int32_t musicVolume;
    int32_t soundVolume;
    int32_t sortRecent;
    int32_t rememberSelection;
    int32_t showPaths;
    int32_t showIntro;
    int recentCount;
    char recentGames[N3DS_RECENT_GAME_COUNT][1024];
} N3DSLauncherSettings;

typedef struct {
    const char* jsonKey;
    const char* label;
    uint32_t button;
} N3DSKeyBinding;

typedef struct {
    const char* key;
    const char* label;
    size_t offset;
    int min;
    int max;
    int step;
    const char* const* choices;
} N3DSSetting;

enum { N3DS_GAME_SETTING_COUNT = 8, N3DS_DEV_SETTING_COUNT = 5, N3DS_LAUNCHER_SETTING_COUNT = 7 };
extern const N3DSKeyBinding N3DS_KEY_BINDINGS[N3DS_KEY_MAPPING_COUNT];
extern const N3DSSetting N3DS_GAME_SETTINGS[N3DS_GAME_SETTING_COUNT + N3DS_DEV_SETTING_COUNT];
extern const N3DSSetting N3DS_LAUNCHER_SETTINGS[N3DS_LAUNCHER_SETTING_COUNT];

N3DSRunnerSettings n3ds_settingsDefaults(void);
void n3ds_settingsLoad(const char* path, N3DSRunnerSettings* game, N3DSLauncherSettings* launcher);
bool n3ds_settingsSave(const char* path, const N3DSRunnerSettings* game,
    const N3DSRunnerSettings* defaults, const N3DSLauncherSettings* launcher);
const char* n3ds_settingsError(void);
bool n3ds_settingsGamePath(const char* dataWinPath, char* path, size_t capacity);
int32_t* n3ds_settingValue(void* settings, const N3DSSetting* option);
void n3ds_settingsRememberGame(N3DSLauncherSettings* settings, const char* path);
uint32_t n3ds_settingsPlaytime(const char* dataWinPath);
bool n3ds_settingsAddPlaytime(const char* dataWinPath, uint64_t seconds);
bool n3ds_settingsGameName(const char* dataWinPath, char* name, size_t capacity);
bool n3ds_settingsSaveGame(const char* path, const N3DSRunnerSettings* game,
    const N3DSRunnerSettings* defaults, const char* name);
