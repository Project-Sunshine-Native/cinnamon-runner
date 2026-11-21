#include "3ds_overlay.h"
#include "3ds_file_system.h"
#include "3ds_settings.h"
#include "3ds_menu_icon.h"
#include "3ds_menu_intro.h"
#include "../audio/3ds/3ds_audio_system.h"
#include "../utils.h"
#include "../log.h"
#include <3ds/applets/swkbd.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define N3DS_MENU_ROOT "romfs:/launcher/"
#define N3DS_MENU_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

enum { N3DS_MENU_VISIBLE_GAMES = 3, N3DS_MENU_ROW_HEIGHT = 45 };
enum { N3DS_MENU_GAMES, N3DS_MENU_SETTINGS, N3DS_MENU_GAME_OPTIONS,
    N3DS_MENU_DEV_OPTIONS, N3DS_MENU_KEYS, N3DS_MENU_LAUNCHER_OPTIONS, N3DS_MENU_RENAME };
enum {
    N3DS_GAME_OPTION_KEYS = N3DS_GAME_SETTING_COUNT,
    N3DS_GAME_OPTION_DEV,
    N3DS_GAME_OPTION_RENAME,
    N3DS_GAME_OPTION_RESET,
    N3DS_GAME_OPTION_SAVE,
    N3DS_GAME_OPTION_COUNT,
};

typedef struct {
    C2D_Image image;
    Tex3DS_SubTexture subtex;
    int advance;
    int offset;
} N3DSMenuGlyph;

typedef struct {
    char name[256];
    char folder[1024];
    const char* path;
    int pathIndex;
    int recentOrder;
    uint32_t playtimeSeconds;
} N3DSMenuGame;

typedef struct {
    C3D_RenderTarget* target;
    C2D_SpriteSheet font;
    C2D_SpriteSheet door;
    C2D_SpriteSheet soul;
    C2D_SpriteSheet defaultIcon;
    N3DSMenuGlyph glyphs[128];
    N3DSFileSystem* fileSystem;
    DataWin audioData;
    AudioSystem* audio;
    int32_t sounds[4];
    int32_t musicInstance;
    N3DSLauncherSettings launcher;
    N3DSLauncherUI uiStyle;
    N3DSRunnerSettings defaults;
    N3DSRunnerSettings edit;
    N3DSMenuGame* editingGame;
    char editName[256];
    char nameInput[256];
    bool nameEdited;
    int nameKey;
    int nameAlphabet;
    N3DSMenuGame* games;
    N3DSMenuIcon* icons;
    int gameCount;
    int loadedGames;
    C2D_TextBuf infoTextBuffer;
    int infoGameIndex;
    float infoTime;
    char settingsPath[1100];
    char settingsMessage[128];
    int page;
    int option;
    int optionFirst;
    float optionRepeat;
    bool editingDefaults;
    bool resetPending;
    bool optionTouching;
    bool optionDragged;
    int optionTouchX;
    int optionTouchY;
    int optionTouchFirst;
    float optionSoulY;
} N3DSMenu;

static int n3ds_menuVisibleRows(const N3DSMenu* menu) {
    return menu->uiStyle == N3DS_LAUNCHER_BASIC ? 6 : N3DS_MENU_VISIBLE_GAMES;
}

static int n3ds_menuLauncherSettingCount(const N3DSMenu* menu) {
    return N3DS_LAUNCHER_SETTING_COUNT - (menu->launcher.uiStyle == N3DS_LAUNCHER_BASIC);
}

static int n3ds_menuRowHeight(const N3DSMenu* menu) {
    return menu->uiStyle == N3DS_LAUNCHER_BASIC ? 21 : N3DS_MENU_ROW_HEIGHT;
}

static int n3ds_menuListTop(const N3DSMenu* menu) {
    return menu->uiStyle == N3DS_LAUNCHER_BASIC ? 64 : 53;
}

static bool n3ds_menuTouchInList(const N3DSMenu* menu, const touchPosition* touch) {
    int left = menu->uiStyle == N3DS_LAUNCHER_BASIC ? 12 : 53;
    int bottom = menu->uiStyle == N3DS_LAUNCHER_BASIC ? 190 : 186;
    return touch->px >= left && touch->px < 267 &&
        touch->py >= n3ds_menuListTop(menu) && touch->py < bottom;
}

static int n3ds_menuCompareGames(const void* left, const void* right) {
    const N3DSMenuGame* a = left;
    const N3DSMenuGame* b = right;
    if (a->recentOrder != b->recentOrder) return a->recentOrder < b->recentOrder ? -1 : 1;
    int order = strcasecmp(a->name, b->name);
    return order != 0 ? order : strcmp(a->path, b->path);
}

static void n3ds_menuSortGames(N3DSMenu* menu, N3DSMenuGame* games, int count) {
    for (int i = 0; i < count; ++i) {
        games[i].recentOrder = 0;
        if (!menu->launcher.sortRecent) continue;
        int recent = 0;
        while (recent < menu->launcher.recentCount &&
            strcmp(games[i].path, menu->launcher.recentGames[recent]) != 0) ++recent;
        games[i].recentOrder = recent;
    }
    qsort(games, (size_t)count, sizeof(*games), n3ds_menuCompareGames);
}

static uint32_t n3ds_menuReadU32(const uint8_t* bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static bool n3ds_menuReadName(FILE* file, long fileSize, uint32_t offset, char* name, size_t capacity) {
    if (offset == 0 || offset >= (uint32_t)fileSize || fseek(file, (long)offset, SEEK_SET) != 0)
        return false;
    size_t length = 0;
    while (length + 1 < capacity && length < (size_t)(fileSize - (long)offset)) {
        int c = fgetc(file);
        if (c == EOF) break;
        if (c == 0) {
            name[length] = '\0';
            return length > 0;
        }
        name[length++] = (char)c;
    }
    name[length] = '\0';
    return length + 1 == capacity;
}

static void n3ds_menuLoadGame(N3DSMenuGame* game, const char* path) {
    if (!n3ds_fsGetParentDir(path, game->folder, sizeof(game->folder)))
        snprintf(game->folder, sizeof(game->folder), "%s", path);
    const char* folderName = strrchr(game->folder, '/');
    snprintf(game->name, sizeof(game->name), "%s", folderName != NULL ? folderName + 1 : game->folder);
    if (n3ds_settingsGameName(path, game->name, sizeof(game->name))) return;

    FILE* file = fopen(path, "rb");
    if (file == NULL) return;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return;
    }
    long fileSize = ftell(file);
    long chunk = 8;
    uint8_t header[8];
    while (fileSize >= 8 && chunk <= fileSize - 8) {
        if (fseek(file, chunk, SEEK_SET) != 0 || fread(header, 1, 8, file) != 8) break;
        uint32_t size = n3ds_menuReadU32(header + 4);
        if (size > (uint32_t)(fileSize - chunk - 8)) break;
        if (memcmp(header, "GEN8", 4) == 0) {
            uint8_t gen8[104];
            size_t bytes = size < sizeof(gen8) ? size : sizeof(gen8);
            if (fread(gen8, 1, bytes, file) != bytes || bytes < 8) break;
            // wad ver 8 and 9 dont have a name in GEN8
            bool compact = gen8[1] <= 8 && size < 108;
            uint32_t nameOffset = compact ? 4 : 40;
            uint32_t displayOffset = gen8[1] >= 10 && bytes >= 104
                ? n3ds_menuReadU32(gen8 + 100) : 0;
            char name[sizeof(game->name)];
            bool found = n3ds_menuReadName(file, fileSize, displayOffset, name, sizeof(name));
            if (!found && bytes >= nameOffset + 4)
                found = n3ds_menuReadName(file, fileSize, n3ds_menuReadU32(gen8 + nameOffset), name, sizeof(name));
            if (found) memcpy(game->name, name, strlen(name) + 1);
            break;
        }
        chunk += 8 + (long)size;
    }
    fclose(file);
}

static bool n3ds_menuLoadFont(N3DSMenu* menu) {
    menu->font = C2D_SpriteSheetLoad(N3DS_MENU_ROOT "font.t3x");
    if (menu->font == NULL) return false;
    C2D_Image image = C2D_SpriteSheetGetImage(menu->font, 0);
    C3D_TexSetFilter(image.tex, GPU_NEAREST, GPU_NEAREST);
    FILE* file = fopen(N3DS_MENU_ROOT "font.csv", "r");
    if (file == NULL) return false;
    char line[256];
    fgets(line, sizeof(line), file);
    while (fgets(line, sizeof(line), file) != NULL) {
        int code, x, y, width, height, advance, offset;
        if (sscanf(line, "%d;%d;%d;%d;%d;%d;%d", &code, &x, &y, &width, &height, &advance, &offset) != 7)
            continue;
        if (code < 0 || code >= 128) continue;
        N3DSMenuGlyph* glyph = &menu->glyphs[code];
        glyph->advance = advance;
        glyph->offset = offset;
        glyph->subtex = *image.subtex;
        float u = (image.subtex->right - image.subtex->left) / image.subtex->width;
        float v = (image.subtex->bottom - image.subtex->top) / image.subtex->height;
        glyph->subtex.width = width;
        glyph->subtex.height = height;
        glyph->subtex.left = image.subtex->left + x * u;
        glyph->subtex.right = glyph->subtex.left + width * u;
        glyph->subtex.top = image.subtex->top + y * v;
        glyph->subtex.bottom = glyph->subtex.top + height * v;
        glyph->image = (C2D_Image){image.tex, &glyph->subtex};
    }
    fclose(file);
    return menu->glyphs['?'].advance > 0;
}

static void n3ds_menuDrawText(N3DSMenu* menu, N3DSLoadingScreen* screen, const char* text,
    float x, float y, float scale, float maxWidth, uint32_t color) {
    bool ascii = true;
    float width = 0.0f;
    for (const unsigned char* c = (const unsigned char*)text; *c != 0; ++c) {
        if (*c >= 128) ascii = false;
        else width += menu->glyphs[*c].advance * scale;
    }
    if (!ascii || menu->uiStyle == N3DS_LAUNCHER_BASIC) {
        C2D_Text parsed = {0};
        if (C2D_TextParse(&parsed, screen->textBuffer, text) == NULL) return;
        scale *= menu->uiStyle == N3DS_LAUNCHER_BASIC ? 0.55f : 0.6f;
        C2D_TextGetDimensions(&parsed, scale, scale, &width, NULL);
        float scaleX = width > maxWidth ? scale * maxWidth / width : scale;
        C2D_DrawText(&parsed, C2D_WithColor, x, y, 0.1f, scaleX, scale, color);
        return;
    }
    float scaleX = width > maxWidth ? scale * maxWidth / width : scale;
    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    for (const unsigned char* c = (const unsigned char*)text; *c != 0; ++c) {
        N3DSMenuGlyph* glyph = &menu->glyphs[*c];
        if (glyph->image.tex != NULL)
            C2D_DrawImageAt(glyph->image, x + glyph->offset * scaleX, y, 0.1f, &tint, scaleX, scale);
        x += glyph->advance * scaleX;
    }
}

static void n3ds_menuSetFontFiltering(N3DSMenu* menu, bool linear) {
    if (menu->uiStyle == N3DS_LAUNCHER_BASIC) return;
    C3D_Tex* texture = C2D_SpriteSheetGetImage(menu->font, 0).tex;
    C2D_Flush();
    C3D_TexSetFilter(texture, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
    C3D_TexBind(0, texture);
}

static float n3ds_menuTextWidth(N3DSMenu* menu, N3DSLoadingScreen* screen,
    const char* text, float scale) {
    float width = 0;
    bool ascii = true;
    for (const unsigned char* c = (const unsigned char*)text; *c != 0; ++c) {
        if (*c >= 128) ascii = false;
        else width += menu->glyphs[*c].advance * scale;
    }
    if (!ascii || menu->uiStyle == N3DS_LAUNCHER_BASIC) {
        C2D_Text parsed = {0};
        if (C2D_TextParse(&parsed, screen->textBuffer, text) == NULL) return -1;
        float fontScale = scale * (menu->uiStyle == N3DS_LAUNCHER_BASIC ? 0.55f : 0.6f);
        C2D_TextGetDimensions(&parsed, fontScale, fontScale, &width, NULL);
    }
    return width;
}

static void n3ds_menuDrawCenteredText(N3DSMenu* menu, N3DSLoadingScreen* screen,
    const char* text, float y, float scale, uint32_t color) {
    float width = n3ds_menuTextWidth(menu, screen, text, scale);
    if (width < 0) return;
    n3ds_menuDrawText(menu, screen, text, (400 - fminf(width, 368)) / 2,
        y, scale, 368, color);
}

static void n3ds_menuDrawMessage(N3DSMenu* menu, N3DSLoadingScreen* screen,
    const char* text, float x, float y, float scale, float maxWidth, int maxLines, uint32_t color) {
    for (int row = 0; row < maxLines && *text != '\0'; ++row) {
        while (*text == ' ') ++text;
        if (*text == '\0') break;
        const char* end = text;
        const char* lastSpace = NULL;
        float width = 0;
        char line[128];
        while (*end != '\0' && *end != '\n' && end - text < (int)sizeof(line) - 1) {
            unsigned char code = (unsigned char)*end;
            float advance = menu->uiStyle == N3DS_LAUNCHER_BASIC ? 9 * scale
                : menu->glyphs[code < 128 ? code : '?'].advance * scale;
            if (width + advance > maxWidth) break;
            width += advance;
            if (*end == ' ') lastSpace = end;
            ++end;
        }
        if (*end != '\0' && *end != '\n' && lastSpace != NULL) end = lastSpace;
        if (end == text && *end != '\n') ++end;
        size_t length = (size_t)(end - text);
        memcpy(line, text, length);
        line[length] = '\0';
        n3ds_menuDrawText(menu, screen, line, x, y + row * (17 * scale), scale, maxWidth, color);
        text = *end == '\n' ? end + 1 : end;
    }
}

static void n3ds_menuPlaySound(N3DSMenu* menu, int sound) {
    if (menu->uiStyle == N3DS_LAUNCHER_BASIC || menu->sounds[sound] < 0) return;
    menu->audio->vtable->stopSound(menu->audio, menu->sounds[sound]);
    int32_t instance = menu->audio->vtable->playSound(menu->audio, menu->sounds[sound], 0, sound == 0);
    if (sound == 0) menu->musicInstance = instance;
}

static void n3ds_menuApplyVolumes(N3DSMenu* menu) {
    for (int i = 0; i < 4; ++i) {
        if (menu->sounds[i] < 0) continue;
        float gain = (float)(i == 0 ? menu->launcher.musicVolume : menu->launcher.soundVolume) / 100.0f;
        if (i == 0) gain *= 0.95f;
        menu->audio->vtable->setSoundGain(menu->audio, menu->sounds[i], gain, 0);
        if (i == 0 && menu->musicInstance >= 0)
            menu->audio->vtable->setSoundGain(menu->audio, menu->musicInstance, gain, 0);
    }
}

static void n3ds_menuInitAudio(N3DSMenu* menu) {
    menu->fileSystem = n3ds_fsCreate(N3DS_MENU_ROOT, N3DS_MENU_ROOT);
    menu->audio = (AudioSystem*)n3ds_audioCreate();
    menu->musicInstance = -1;
    for (int i = 0; i < 4; ++i) menu->sounds[i] = -1;
    menu->audio->vtable->init(menu->audio, &menu->audioData, &menu->fileSystem->base);
}

static bool n3ds_menuPreload(void* context) {
    N3DSMenu* menu = context;
    if (menu->loadedGames < menu->gameCount) {
        N3DSMenuGame* game = &menu->games[menu->loadedGames++];
        n3ds_menuLoadGame(game, game->path);
        game->playtimeSeconds = n3ds_settingsPlaytime(game->path);
        n3ds_menuIconLoad(&menu->icons[game->pathIndex], game->folder);
        return false;
    }
    static const char* files[] = {"menu.ogg", "move.wav", "select.wav", "back.wav"};
    for (int i = 1; i < 4; ++i) {
        menu->sounds[i] = menu->audio->vtable->createStream(menu->audio, files[i]);
    }
    return true;
}

static void n3ds_menuFree(N3DSMenu* menu) {
    for (int i = 0; i < menu->gameCount; ++i) n3ds_menuIconFree(&menu->icons[i]);
    free(menu->icons);
    free(menu->games);
    if (menu->infoTextBuffer != NULL) C2D_TextBufDelete(menu->infoTextBuffer);
    if (menu->audio != NULL) menu->audio->vtable->destroy(menu->audio);
    n3ds_fsDestroy(menu->fileSystem);
    if (menu->font != NULL) C2D_SpriteSheetFree(menu->font);
    if (menu->door != NULL) C2D_SpriteSheetFree(menu->door);
    if (menu->soul != NULL) C2D_SpriteSheetFree(menu->soul);
    if (menu->defaultIcon != NULL) C2D_SpriteSheetFree(menu->defaultIcon);
    if (menu->target != NULL) C3D_RenderTargetDelete(menu->target);
}

static void n3ds_menuDrawBackground(N3DSMenu* menu, N3DSLoadingScreen* screen, float phase) {
    if (menu->uiStyle == N3DS_LAUNCHER_DELTARUNE && menu->door != NULL) {
        C2D_ImageTint tint;
        C2D_Image door = C2D_SpriteSheetGetImage(menu->door, 0);
        float alpha = fmaxf(0, 0.03f + sinf(phase / 20.0f) * 0.04f);
        C2D_AlphaImageTint(&tint, alpha);
        // spr_giantdarkdoor (the door in the launcher bg) has an x origin of 35 pixels at 2x scale
        C2D_DrawImageAt(door, -27, 48, 0.9f, &tint, 2, 2);
        C2D_DrawImageAt(door, -23, 48, 0.9f, &tint, 2, 2);
        C2D_DrawImageAt(door, -27, 52, 0.9f, &tint, 2, 2);
        C2D_DrawImageAt(door, -23, 52, 0.9f, &tint, 2, 2);
        C2D_AlphaImageTint(&tint, 0.25f);
        C2D_DrawImageAt(door, -25, 50, 0.9f, &tint, 2, 2);
    }
    C2D_TextBufClear(screen->textBuffer);
}

static void n3ds_menuDrawBox(float y, uint32_t color) {
    C2D_DrawRectSolid(55, y, 0.8f, 210, 40, C2D_Color32(0, 0, 0, 128));
    C2D_DrawRectSolid(53, y - 2, 0.5f, 214, 2, color);
    C2D_DrawRectSolid(53, y + 39, 0.5f, 214, 2, color);
    C2D_DrawRectSolid(53, y - 2, 0.5f, 2, 43, color);
    C2D_DrawRectSolid(265, y - 2, 0.5f, 2, 43, color);
}

static void n3ds_menuSetPage(N3DSMenu* menu, int page) {
    menu->page = page;
    menu->option = 0;
    menu->optionFirst = 0;
    menu->optionSoulY = 72;
    menu->optionRepeat = 0;
    menu->resetPending = false;
    menu->optionTouching = false;
    menu->settingsMessage[0] = '\0';
}

static void n3ds_menuOpenSettings(N3DSMenu* menu) {
    menu->defaults = n3ds_settingsDefaults();
    n3ds_settingsLoad(N3DS_SETTINGS_PATH, &menu->defaults, NULL);
    n3ds_menuSetPage(menu, N3DS_MENU_SETTINGS);
}

static int n3ds_menuOptionCount(const N3DSMenu* menu) {
    switch (menu->page) {
    case N3DS_MENU_SETTINGS: return 3;
    case N3DS_MENU_GAME_OPTIONS: return N3DS_GAME_OPTION_COUNT - (menu->editingGame == NULL);
    case N3DS_MENU_DEV_OPTIONS: return N3DS_DEV_SETTING_COUNT;
    case N3DS_MENU_KEYS: return N3DS_KEY_MAPPING_COUNT + 1;
    case N3DS_MENU_LAUNCHER_OPTIONS: return n3ds_menuLauncherSettingCount(menu) + 2;
    default: return 0;
    }
}

static int n3ds_menuGameOption(const N3DSMenu* menu, int option) {
    if (menu->editingGame == NULL && option >= N3DS_GAME_OPTION_RENAME) ++option;
    return option;
}

static const N3DSSetting* n3ds_menuOptionSetting(N3DSMenu* menu, int option) {
    if (menu->page == N3DS_MENU_GAME_OPTIONS && option < N3DS_GAME_SETTING_COUNT)
        return &N3DS_GAME_SETTINGS[option];
    if (menu->page == N3DS_MENU_DEV_OPTIONS)
        return &N3DS_GAME_SETTINGS[N3DS_GAME_SETTING_COUNT + option];
    if (menu->page == N3DS_MENU_LAUNCHER_OPTIONS && option < n3ds_menuLauncherSettingCount(menu))
        return &N3DS_LAUNCHER_SETTINGS[option];
    return NULL;
}

static const int n3ds_menuMappingKeys[] = {
    -1, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', 16, 32, 13,
};

static void n3ds_menuKeyName(int key, char* name, size_t capacity) {
    if (key == -1) snprintf(name, capacity, "Unmapped");
    else if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z'))
        snprintf(name, capacity, "%c", key);
    else if (key == 16) snprintf(name, capacity, "Shift");
    else if (key == 32) snprintf(name, capacity, "Space");
    else if (key == 13) snprintf(name, capacity, "Enter");
    else {
        const char* legacy = NULL;
        switch (key) {
        case 9: legacy = "Tab"; break;
        case 27: legacy = "Escape"; break;
        case 37: legacy = "Left"; break;
        case 38: legacy = "Up"; break;
        case 39: legacy = "Right"; break;
        case 40: legacy = "Down"; break;
        }
        if (legacy != NULL) snprintf(name, capacity, "%s (existing)", legacy);
        else snprintf(name, capacity, "Existing binding (%d)", key);
    }
}

static const char* n3ds_menuOptionLabel(N3DSMenu* menu, const N3DSMenuGame* game,
    int option, char* value, size_t capacity) {
    value[0] = '\0';
    const N3DSSetting* setting = n3ds_menuOptionSetting(menu, option);
    if (setting != NULL) {
        void* settings = menu->page == N3DS_MENU_LAUNCHER_OPTIONS ? (void*)&menu->launcher : (void*)&menu->edit;
        int number = *n3ds_settingValue(settings, setting);
        if (setting->choices != NULL) snprintf(value, capacity, "%s", setting->choices[number - setting->min]);
        else if (strstr(setting->key, "Volume") != NULL || strcmp(setting->key, "volume") == 0)
            snprintf(value, capacity, "%d%%", number);
        else if (strcmp(setting->key, "atlasTextureCacheMb") == 0 && number == 0)
            snprintf(value, capacity, "Automatic");
        else if (strcmp(setting->key, "osType") == 0 && number == 0)
            snprintf(value, capacity, "Windows");
        else snprintf(value, capacity, "%d", number);
        return setting->label;
    }
    if (menu->page == N3DS_MENU_SETTINGS) {
        static const char* labels[] = {"This game", "Game defaults", "Launcher"};
        static const char* details[] = {NULL, "Defaults for all games", "UI, music, game list"};
        snprintf(value, capacity, "%s", option == 0 ? game->name : details[option]);
        return labels[option];
    }
    if (menu->page == N3DS_MENU_KEYS && option < N3DS_KEY_MAPPING_COUNT) {
        n3ds_menuKeyName(menu->edit.keyMappings[option], value, capacity);
        return N3DS_KEY_BINDINGS[option].label;
    }
    if (menu->page == N3DS_MENU_GAME_OPTIONS) {
        switch (n3ds_menuGameOption(menu, option)) {
        case N3DS_GAME_OPTION_KEYS:
            snprintf(value, capacity, "Keyboard controls");
            return "Button mapping";
        case N3DS_GAME_OPTION_DEV:
            snprintf(value, capacity, "Diagnostics and compatibility");
            return "Dev";
        case N3DS_GAME_OPTION_RENAME:
            snprintf(value, capacity, "%s", menu->editName[0] != '\0' ? menu->editName : "Original title");
            return "Rename game";
        case N3DS_GAME_OPTION_SAVE:
            return "Save & return";
        }
    }
    if (menu->page == N3DS_MENU_LAUNCHER_OPTIONS && option == n3ds_menuLauncherSettingCount(menu) + 1) {
        return "Save & return";
    }
    snprintf(value, capacity, "%s", menu->resetPending && option == menu->option ? "Press A again to confirm" : "Requires confirmation");
    return menu->page == N3DS_MENU_KEYS ? "Restore controls" : "Restore defaults";
}

static bool n3ds_menuLeaveSettings(N3DSMenu* menu, bool save) {
    if (menu->page == N3DS_MENU_DEV_OPTIONS || menu->page == N3DS_MENU_KEYS) {
        n3ds_menuSetPage(menu, N3DS_MENU_GAME_OPTIONS);
        return true;
    }
    bool saved = true;
    if (menu->page == N3DS_MENU_GAME_OPTIONS && save) {
        const N3DSRunnerSettings* defaults = menu->editingDefaults ? NULL : &menu->defaults;
        saved = n3ds_settingsSaveGame(menu->settingsPath, &menu->edit, defaults,
            menu->editingGame != NULL && menu->nameEdited ? menu->editName : NULL);
        if (saved && menu->editingDefaults) menu->defaults = menu->edit;
        if (saved && menu->editingGame != NULL && menu->nameEdited) {
            n3ds_menuLoadGame(menu->editingGame, menu->editingGame->path);
            snprintf(menu->editName, sizeof(menu->editName), "%s", menu->editingGame->name);
            menu->nameEdited = false;
            menu->infoTime = 0;
        }
    }
    if (menu->page == N3DS_MENU_LAUNCHER_OPTIONS) {
        if (save) saved = n3ds_settingsSave(N3DS_SETTINGS_PATH, NULL, NULL, &menu->launcher);
        else n3ds_settingsLoad(N3DS_SETTINGS_PATH, NULL, &menu->launcher);
        n3ds_menuApplyVolumes(menu);
        if (saved && menu->uiStyle != (N3DSLauncherUI)menu->launcher.uiStyle) {
            menu->uiStyle = (N3DSLauncherUI)menu->launcher.uiStyle;
            if (menu->uiStyle == N3DS_LAUNCHER_BASIC) {
                menu->audio->vtable->stopAll(menu->audio);
                menu->musicInstance = -1;
            } else n3ds_menuPlaySound(menu, 0);
        }
    }
    if (!saved) {
        snprintf(menu->settingsMessage, sizeof(menu->settingsMessage), "%s", n3ds_settingsError());
        return false;
    }
    int page = menu->page == N3DS_MENU_SETTINGS ? N3DS_MENU_GAMES : N3DS_MENU_SETTINGS;
    n3ds_menuSetPage(menu, page);
    return true;
}

static const char* const n3ds_menuNameRows[3][3] = {
    {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ"},
    {"abcdefghij", "klmnopqrst", "uvwxyz"},
    {"0123456789", "-_'.,!?():", "/&+@#%"},
};

static void n3ds_menuFinishRename(N3DSMenu* menu, bool accept) {
    if (accept) {
        snprintf(menu->editName, sizeof(menu->editName), "%s", menu->nameInput);
        menu->nameEdited = true;
    }
    n3ds_menuPlaySound(menu, accept ? 2 : 3);
    n3ds_menuSetPage(menu, N3DS_MENU_GAME_OPTIONS);
    menu->option = N3DS_GAME_OPTION_RENAME;
    int visible = n3ds_menuVisibleRows(menu);
    menu->optionFirst = menu->option >= visible ? menu->option - visible + 1 : 0;
    menu->optionSoulY = 72 + (menu->option - menu->optionFirst) * n3ds_menuRowHeight(menu);
}

static void n3ds_menuOpenRename(N3DSMenu* menu) {
    snprintf(menu->nameInput, sizeof(menu->nameInput), "%s", menu->editName);
    menu->optionTouching = false;
    if (menu->uiStyle == N3DS_LAUNCHER_BASIC) {
        SwkbdState keyboard;
        swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, 64);
        swkbdSetValidation(&keyboard, SWKBD_ANYTHING, 0, 0);
        swkbdSetHintText(&keyboard, "Game name (blank restores original)");
        swkbdSetInitialText(&keyboard, menu->editName);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
        swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "Done", true);
        SwkbdButton button = swkbdInputText(&keyboard, menu->nameInput, sizeof(menu->nameInput));
        n3ds_menuFinishRename(menu, button == SWKBD_BUTTON_RIGHT);
        hidScanInput();
        return;
    }
    menu->nameKey = 0;
    menu->nameAlphabet = 0;
    n3ds_menuSetPage(menu, N3DS_MENU_RENAME);
}

static void n3ds_menuDeleteName(N3DSMenu* menu) {
    size_t length = strlen(menu->nameInput);
    if (length > 0) {
        do --length;
        while (length > 0 && ((unsigned char)menu->nameInput[length] & 0xc0) == 0x80);
        menu->nameInput[length] = '\0';
        n3ds_menuPlaySound(menu, 3);
    }
}

static void n3ds_menuAppendName(N3DSMenu* menu, char character) {
    size_t length = strlen(menu->nameInput);
    int characters = 0;
    for (size_t i = 0; i < length; ++i)
        if (((unsigned char)menu->nameInput[i] & 0xc0) != 0x80) ++characters;
    if (characters >= 64 || length + 1 >= sizeof(menu->nameInput)) {
        n3ds_menuPlaySound(menu, 3);
        return;
    }
    menu->nameInput[length] = character;
    menu->nameInput[length + 1] = '\0';
    n3ds_menuPlaySound(menu, 2);
}

static int n3ds_menuNameKeyAt(int row, int column) {
    if (row < 2) return row * 10 + column;
    if (column < 6) return 20 + column;
    return column < 8 ? 26 : 27;
}

static void n3ds_menuRenameInput(N3DSMenu* menu, u32 down, u32 held, float dt) {
    int previous = menu->nameKey;
    u32 direction = held & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
    bool move = (down & direction) != 0;
    if (move) menu->optionRepeat = 0.35f;
    else if (direction != 0) {
        menu->optionRepeat -= dt;
        if (menu->optionRepeat <= 0) {
            move = true;
            menu->optionRepeat = 0.1f;
        }
    }
    if (move) {
        int row = menu->nameKey / 10;
        if (direction & (KEY_LEFT | KEY_RIGHT)) {
            int count = row == 2 ? 8 : 10;
            int step = direction & KEY_LEFT ? -1 : 1;
            menu->nameKey = row * 10 + (menu->nameKey % 10 + step + count) % count;
        } else {
            int column = menu->nameKey == 27 ? 8 : menu->nameKey % 10;
            row = (row + (direction & KEY_UP ? 2 : 1)) % 3;
            menu->nameKey = n3ds_menuNameKeyAt(row, column);
        }
    }
    if (menu->nameKey != previous) n3ds_menuPlaySound(menu, 1);
    if (down & KEY_X) {
        n3ds_menuFinishRename(menu, false);
        return;
    }
    if (down & KEY_START) {
        n3ds_menuFinishRename(menu, true);
        return;
    }
    if (down & KEY_B) n3ds_menuDeleteName(menu);
    if (down & KEY_Y) n3ds_menuAppendName(menu, ' ');
    if (down & KEY_SELECT) menu->nameInput[0] = '\0';
    if (down & (KEY_L | KEY_R)) {
        menu->nameAlphabet = (menu->nameAlphabet + (down & KEY_L ? 2 : 1)) % 3;
        n3ds_menuPlaySound(menu, 1);
    }
    bool activate = (down & KEY_A) != 0;
    if (down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        if (touch.px >= 64 && touch.px < 264 && touch.py >= 68 && touch.py < 128) {
            int column = (touch.px - 64) / 20;
            int row = (touch.py - 68) / 20;
            menu->nameKey = n3ds_menuNameKeyAt(row, column);
            activate = true;
        } else if (touch.py >= 138 && touch.py < 162 && touch.px >= 24 && touch.px < 296) {
            menu->nameAlphabet = (touch.px - 24) / 91;
            n3ds_menuPlaySound(menu, 1);
        } else if (touch.py >= 164 && touch.py < 184 && touch.px >= 120 && touch.px < 200) {
            n3ds_menuAppendName(menu, ' ');
        } else if (touch.py >= 190 && touch.py < 214) {
            if (touch.px >= 16 && touch.px < 108) {
                n3ds_menuFinishRename(menu, false);
                return;
            }
            if (touch.px >= 112 && touch.px < 200) menu->nameInput[0] = '\0';
            if (touch.px >= 216 && touch.px < 304) {
                n3ds_menuFinishRename(menu, true);
                return;
            }
        }
    }
    if (activate) {
        if (menu->nameKey == 26) n3ds_menuDeleteName(menu);
        else if (menu->nameKey == 27) n3ds_menuFinishRename(menu, true);
        else n3ds_menuAppendName(menu,
            n3ds_menuNameRows[menu->nameAlphabet][menu->nameKey / 10][menu->nameKey % 10]);
    }
}

static void n3ds_menuSettingsInput(N3DSMenu* menu, N3DSMenuGame* game, u32 down, u32 held, float dt) {
    if (menu->page == N3DS_MENU_RENAME) {
        n3ds_menuRenameInput(menu, down, held, dt);
        return;
    }
    int count = n3ds_menuOptionCount(menu);
    int visible = n3ds_menuVisibleRows(menu);
    int rowHeight = n3ds_menuRowHeight(menu);
    int previous = menu->option;
    bool move = (down & (KEY_UP | KEY_DOWN)) != 0;
    if (move) menu->optionRepeat = 0.35f;
    else if (held & (KEY_UP | KEY_DOWN)) {
        menu->optionRepeat -= dt;
        if (menu->optionRepeat <= 0) {
            move = true;
            menu->optionRepeat = 0.1f;
        }
    }
    if (move) {
        bool up = (held & KEY_UP) != 0;
        bool downHeld = (held & KEY_DOWN) != 0;
        if (up != downHeld) menu->option = (menu->option + (up ? count - 1 : 1)) % count;
    }
    bool activate = (down & KEY_A) != 0;
    bool cancel = (down & KEY_B) != 0;
    bool save = (down & KEY_START) != 0;
    bool left = (down & KEY_LEFT) != 0;
    bool right = (down & KEY_RIGHT) != 0;
    if (down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        menu->optionTouching = n3ds_menuTouchInList(menu, &touch);
        menu->optionDragged = false;
        menu->optionTouchX = touch.px;
        menu->optionTouchY = touch.py;
        menu->optionTouchFirst = menu->optionFirst;
        if (touch.px >= 270 && touch.px < 305) {
            if (touch.py >= 45 && touch.py < 85 && menu->optionFirst > 0)
                menu->option = menu->optionFirst - 1;
            if (touch.py >= 150 && touch.py < 190 && menu->optionFirst + visible < count)
                menu->option = menu->optionFirst + visible;
        }
        if (touch.py >= 190 && touch.py < 211) {
            if (touch.px >= 53 && touch.px < 165) cancel = true;
            if (touch.px >= 175 && touch.px < 267 && menu->page != N3DS_MENU_SETTINGS) save = true;
        }
    }
    if (menu->optionTouching && (held & KEY_TOUCH)) {
        touchPosition touch;
        hidTouchRead(&touch);
        int distance = menu->optionTouchY - touch.py;
        if (abs(distance) > 8 || abs((int)touch.px - menu->optionTouchX) > 8) menu->optionDragged = true;
        if (menu->optionDragged) {
            int first = menu->optionTouchFirst + distance / rowHeight;
            int maxFirst = count > visible ? count - visible : 0;
            if (first < 0) first = 0;
            if (first > maxFirst) first = maxFirst;
            menu->optionFirst = first;
            if (menu->option < first) menu->option = first;
            if (menu->option >= first + visible) menu->option = first + visible - 1;
        }
    }
    if (menu->optionTouching && !(held & KEY_TOUCH)) {
        menu->optionTouching = false;
        if (!menu->optionDragged) {
            int row = (menu->optionTouchY - n3ds_menuListTop(menu)) / rowHeight;
            if (row < visible && menu->optionFirst + row < count) {
                menu->option = menu->optionFirst + row;
                activate = true;
            }
        }
    }
    if (menu->option < menu->optionFirst) menu->optionFirst = menu->option;
    if (menu->option >= menu->optionFirst + visible) menu->optionFirst = menu->option - visible + 1;
    float targetY = 72 + (menu->option - menu->optionFirst) * rowHeight;
    menu->optionSoulY += (targetY - menu->optionSoulY) * (1.0f - powf(0.5f, dt * 30));
    if (previous != menu->option) {
        menu->resetPending = false;
        menu->settingsMessage[0] = '\0';
        n3ds_menuPlaySound(menu, 1);
    }
    if (cancel && menu->resetPending) {
        menu->resetPending = false;
        n3ds_menuPlaySound(menu, 3);
        return;
    }
    if (cancel || save) {
        if (n3ds_menuLeaveSettings(menu, save)) n3ds_menuPlaySound(menu, cancel ? 3 : 2);
        return;
    }
    if (!activate && left == right && !(down & (KEY_X | KEY_L | KEY_R))) return;
    int direction = left && !right ? -1 : 1;
    const N3DSSetting* setting = n3ds_menuOptionSetting(menu, menu->option);
    if (setting != NULL && (activate || left != right)) {
        void* object = menu->page == N3DS_MENU_LAUNCHER_OPTIONS ? (void*)&menu->launcher : (void*)&menu->edit;
        int32_t* value = n3ds_settingValue(object, setting);
        *value += direction * setting->step;
        if (*value < setting->min) *value = setting->max;
        if (*value > setting->max) *value = setting->min;
        if (menu->page == N3DS_MENU_LAUNCHER_OPTIONS) n3ds_menuApplyVolumes(menu);
        n3ds_menuPlaySound(menu, 1);
    } else if (menu->page == N3DS_MENU_KEYS && menu->option < N3DS_KEY_MAPPING_COUNT) {
        int32_t* key = &menu->edit.keyMappings[menu->option];
        if (down & KEY_X) *key = -1;
        else {
            int step = (down & KEY_L) ? -10 : ((down & KEY_R) ? 10 : direction);
            int count = (int)(sizeof(n3ds_menuMappingKeys) / sizeof(n3ds_menuMappingKeys[0]));
            int index = 0;
            while (index < count && n3ds_menuMappingKeys[index] != *key) ++index;
            if (index == count) index = 0;
            *key = n3ds_menuMappingKeys[(index + step + count) % count];
        }
        menu->edit.gamepadApiEnabled = 0;
        n3ds_menuPlaySound(menu, 1);
    } else if (activate) {
        n3ds_menuPlaySound(menu, 2);
        int option = n3ds_menuGameOption(menu, menu->option);
        if (menu->page == N3DS_MENU_SETTINGS) {
            if (menu->option == 2) n3ds_menuSetPage(menu, N3DS_MENU_LAUNCHER_OPTIONS);
            else {
                menu->editingDefaults = menu->option == 1;
                menu->editingGame = menu->editingDefaults ? NULL : game;
                snprintf(menu->editName, sizeof(menu->editName), "%s", game->name);
                menu->nameEdited = false;
                menu->edit = menu->defaults;
                if (menu->editingDefaults) snprintf(menu->settingsPath, sizeof(menu->settingsPath), "%s", N3DS_SETTINGS_PATH);
                else if (!n3ds_settingsGamePath(game->path, menu->settingsPath, sizeof(menu->settingsPath))) {
                    snprintf(menu->settingsMessage, sizeof(menu->settingsMessage), "Could not locate the game's settings folder.");
                    return;
                }
                if (strcmp(menu->settingsPath, N3DS_SETTINGS_PATH) == 0) menu->editingDefaults = true;
                if (!menu->editingDefaults) n3ds_settingsLoad(menu->settingsPath, &menu->edit, NULL);
                n3ds_menuSetPage(menu, N3DS_MENU_GAME_OPTIONS);
            }
        } else if (menu->page == N3DS_MENU_GAME_OPTIONS && option == N3DS_GAME_OPTION_KEYS) {
            n3ds_menuSetPage(menu, N3DS_MENU_KEYS);
        } else if (menu->page == N3DS_MENU_GAME_OPTIONS && option == N3DS_GAME_OPTION_DEV) {
            n3ds_menuSetPage(menu, N3DS_MENU_DEV_OPTIONS);
        } else if (menu->page == N3DS_MENU_GAME_OPTIONS && option == N3DS_GAME_OPTION_RENAME) {
            n3ds_menuOpenRename(menu);
        } else if ((menu->page == N3DS_MENU_GAME_OPTIONS && option == N3DS_GAME_OPTION_SAVE) ||
            (menu->page == N3DS_MENU_LAUNCHER_OPTIONS && menu->option == n3ds_menuLauncherSettingCount(menu) + 1)) {
            n3ds_menuLeaveSettings(menu, true);
        } else if (!menu->resetPending) menu->resetPending = true;
        else {
            if (menu->page == N3DS_MENU_LAUNCHER_OPTIONS) {
                menu->launcher.uiStyle = N3DS_LAUNCHER_DELTARUNE;
                menu->launcher.musicVolume = 100;
                menu->launcher.soundVolume = 100;
                menu->launcher.sortRecent = 0;
                menu->launcher.rememberSelection = 1;
                menu->launcher.showPaths = 1;
                menu->launcher.showIntro = 1;
                n3ds_menuApplyVolumes(menu);
            } else {
                N3DSRunnerSettings defaults = menu->editingDefaults ? n3ds_settingsDefaults() : menu->defaults;
                if (menu->page == N3DS_MENU_KEYS)
                    memcpy(menu->edit.keyMappings, defaults.keyMappings, sizeof(defaults.keyMappings));
                else menu->edit = defaults;
            }
            menu->resetPending = false;
        }
    }
}

static void n3ds_menuDrawSettings(N3DSMenu* menu, N3DSLoadingScreen* screen,
    const N3DSMenuGame* game, float phase) {
    n3ds_menuDrawBackground(menu, screen, phase);
    bool basic = menu->uiStyle == N3DS_LAUNCHER_BASIC;
    int visible = n3ds_menuVisibleRows(menu);
    uint32_t white = C2D_Color32(255, 255, 255, 255);
    uint32_t dim = basic ? C2D_Color32(170, 170, 170, 255) : C2D_Color32(179, 153, 153, 255);
    uint32_t selectedColor = basic ? C2D_Color32(82, 165, 232, 255) : white;
    const char* title = "Settings";
    if (menu->page == N3DS_MENU_GAME_OPTIONS) title = menu->editingGame == NULL ? "Game defaults" : "Game settings";
    if (menu->page == N3DS_MENU_DEV_OPTIONS) title = "Dev";
    if (menu->page == N3DS_MENU_KEYS) title = "Button mapping";
    if (menu->page == N3DS_MENU_LAUNCHER_OPTIONS) title = "Launcher settings";
    n3ds_menuDrawText(menu, screen, "Cinnamon", 8, 4, basic ? 1.4f : 1, 304, selectedColor);
    n3ds_menuDrawText(menu, screen, title, basic ? 24 : 40, basic ? 36 : 30, 1, 270, white);
    int count = n3ds_menuOptionCount(menu);
    char value[256];
    if (basic) {
        n3ds_menuOptionLabel(menu, game, menu->option, value, sizeof(value));
        char detail[84];
        snprintf(detail, sizeof(detail), "%s", value);
        n3ds_menuDrawText(menu, screen, detail, 24, 50, 0.65f, 272, dim);
    }
    for (int row = 0; row < visible && menu->optionFirst + row < count; ++row) {
        int option = menu->optionFirst + row;
        const char* label = n3ds_menuOptionLabel(menu, game, option, value, sizeof(value));
        uint32_t color = option == menu->option ? selectedColor : dim;
        if (basic) {
            float y = 66 + row * n3ds_menuRowHeight(menu);
            bool hasValue = n3ds_menuOptionSetting(menu, option) != NULL ||
                (menu->page == N3DS_MENU_KEYS && option < N3DS_KEY_MAPPING_COUNT);
            n3ds_menuDrawText(menu, screen, option == menu->option ? ">" : " ", 12, y, 1, 14, color);
            n3ds_menuDrawText(menu, screen, label, 28, y, 0.95f, hasValue ? 155 : 234, color);
            if (hasValue) n3ds_menuDrawText(menu, screen, value, 196, y, 0.85f, 70, color);
            continue;
        }
        float y = 55 + row * N3DS_MENU_ROW_HEIGHT;
        n3ds_menuDrawBox(y, color);
        n3ds_menuDrawText(menu, screen, label, 80, y + 5, 1, 180, color);
        n3ds_menuDrawText(menu, screen, value, 80, y + 22, 0.85f, 180, color);
    }
    if (!basic)
        C2D_DrawImageAt(C2D_SpriteSheetGetImage(menu->soul, 0), 65, menu->optionSoulY, 0.1f, NULL, 1, 1);
    if (menu->optionFirst > 0) n3ds_menuDrawText(menu, screen, "^", 278, 55, 1, 16, white);
    if (menu->optionFirst + visible < count) n3ds_menuDrawText(menu, screen, "v", 278, 165, 1, 16, selectedColor);
    bool scope = menu->page == N3DS_MENU_GAME_OPTIONS || menu->page == N3DS_MENU_LAUNCHER_OPTIONS;
    n3ds_menuDrawText(menu, screen, scope ? "Cancel" : "Back", 55, 193, 1, 100, dim);
    if (menu->page != N3DS_MENU_SETTINGS)
        n3ds_menuDrawText(menu, screen, scope ? "Save" : "Done", 185, 193, 1, 80, dim);
    if (menu->settingsMessage[0] != '\0') {
        n3ds_menuSetFontFiltering(menu, true);
        n3ds_menuDrawMessage(menu, screen, menu->settingsMessage, 8, 215, 0.65f, 304, 2, white);
        n3ds_menuSetFontFiltering(menu, false);
    }
}

static void n3ds_menuDrawCredits(N3DSMenu* menu, N3DSLoadingScreen* screen) {
    static const char* credits[] = {
        "Cinnamon Launcher v1.04",
        "by Project Sunshine/grayforz24682, casrielasriel",
    };
    n3ds_menuSetFontFiltering(menu, true);
    for (int i = 0; i < 2; ++i) {
        float width = n3ds_menuTextWidth(menu, screen, credits[i], 0.6f);
        if (width < 0) continue;
        n3ds_menuDrawText(menu, screen, credits[i], 312 - fminf(width, 304),
            215 + i * 11, 0.6f, 304, C2D_Color32(255, 255, 255, 153));
    }
    n3ds_menuSetFontFiltering(menu, false);
}

static void n3ds_menuDrawScrollingText(N3DSMenu* menu, N3DSLoadingScreen* screen, const char* text,
    float x, float y, float scale, float maxWidth, int screenWidth, bool centered,
    float elapsed, uint32_t color) {
    float width = 0;
    bool ascii = true;
    for (const unsigned char* c = (const unsigned char*)text; *c != 0; ++c) {
        if (*c >= 128) ascii = false;
        else width += menu->glyphs[*c].advance * scale;
    }
    C2D_Text parsed = {0};
    float fontScale = scale * (menu->uiStyle == N3DS_LAUNCHER_BASIC ? 0.55f : 0.6f);
    bool systemFont = !ascii || menu->uiStyle == N3DS_LAUNCHER_BASIC;
    if (systemFont) {
        if (C2D_TextParse(&parsed, screen->textBuffer, text) == NULL) return;
        C2D_TextGetDimensions(&parsed, fontScale, fontScale, &width, NULL);
    }
    float drawX = centered ? x + (maxWidth - width) / 2 : x;
    if (width > maxWidth) {
        float travel = (width - maxWidth) / 24;
        float time = fmodf(fmaxf(0, elapsed - 1), 2 * travel + 2);
        float offset;
        if (time < travel) offset = time * 24;
        else if (time < travel + 1) offset = width - maxWidth;
        else if (time < 2 * travel + 1) offset = (2 * travel + 1 - time) * 24;
        else offset = 0;
        drawX = x - offset;
    }
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)(240 - y - ceilf(17 * scale)),
        (u32)(screenWidth - x - maxWidth), (u32)(240 - y), (u32)(screenWidth - x));
    if (systemFont)
        C2D_DrawText(&parsed, C2D_WithColor, drawX, y, 0.1f, fontScale, fontScale, color);
    else n3ds_menuDrawText(menu, screen, text, drawX, y, scale, fmaxf(width, 1), color);
    C2D_Flush();
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

static void n3ds_menuDrawKeyboard(N3DSMenu* menu, N3DSLoadingScreen* screen) {
    C2D_TextBufClear(screen->textBuffer);
    uint32_t white = C2D_Color32(255, 255, 255, 255);
    uint32_t yellow = C2D_Color32(255, 255, 0, 255);
    uint32_t dim = C2D_Color32(179, 153, 153, 255);
    n3ds_menuDrawText(menu, screen, "RENAME YOUR GAME.", 68, 14, 1, 248, white);
    const char* tail = menu->nameInput;
    size_t length = strlen(tail);
    if (length > 24) {
        tail += length - 24;
        while (((unsigned char)*tail & 0xc0) == 0x80) ++tail;
    }
    char preview[32];
    snprintf(preview, sizeof(preview), "%s%s", tail != menu->nameInput ? "..." : "",
        *tail != '\0' ? tail : "_");
    n3ds_menuDrawScrollingText(menu, screen, preview, 24, 40, 1, 272, 320, true, 0, white);
    for (int i = 0; i < 28; ++i) {
        int column = i == 27 ? 8 : i % 10;
        float x = 68 + column * 20;
        float y = 70 + (i / 10) * 20;
        char character[2] = {0};
        const char* label;
        if (i == 26) label = "BACK";
        else if (i == 27) label = "END";
        else {
            character[0] = n3ds_menuNameRows[menu->nameAlphabet][i / 10][i % 10];
            label = character;
        }
        if (i == menu->nameKey && menu->soul != NULL) {
            float advance = 0, right = 0, height = 0;
            float left = menu->glyphs[(unsigned char)label[0]].offset;
            for (const unsigned char* c = (const unsigned char*)label; *c != 0; ++c) {
                const N3DSMenuGlyph* glyph = &menu->glyphs[*c];
                left = fminf(left, advance + glyph->offset);
                right = fmaxf(right, advance + glyph->offset + glyph->subtex.width);
                height = fmaxf(height, glyph->subtex.height);
                advance += glyph->advance;
            }
            C2D_Image soul = C2D_SpriteSheetGetImage(menu->soul, 0);
            C2D_ImageTint tint;
            C2D_AlphaImageTint(&tint, 0.5f);
            C2D_DrawImageAt(soul, roundf(x + (left + right - soul.subtex->width) / 2),
                roundf(y + (height - soul.subtex->height) / 2), 0.1f, &tint, 1, 1);
        }
        n3ds_menuDrawText(menu, screen, label, x, y, 1, i >= 26 ? 40 : 20,
            i == menu->nameKey ? yellow : white);
    }
    static const char* const alphabets[] = {"ABC", "abc", "123"};
    for (int i = 0; i < 3; ++i)
        n3ds_menuDrawText(menu, screen, alphabets[i], 56 + i * 88, 145, 0.8f, 64,
            i == menu->nameAlphabet ? yellow : white);
    n3ds_menuDrawText(menu, screen, "SPACE", 128, 169, 0.8f, 80, white);
    n3ds_menuDrawText(menu, screen, "Cancel", 24, 195, 0.8f, 84, dim);
    n3ds_menuDrawText(menu, screen, "Clear", 136, 195, 0.8f, 64, dim);
    n3ds_menuDrawText(menu, screen, "Done", 248, 195, 0.8f, 56, white);
    n3ds_menuDrawText(menu, screen, "X: cancel  Y: space  START: done", 8, 220, 0.55f, 304, dim);
}

static void n3ds_menuDrawPlaytime(N3DSMenu* menu, N3DSLoadingScreen* screen,
    uint32_t seconds, float right, float y, uint32_t color) {
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu32 ":%02" PRIu32, seconds / 3600, (seconds / 60) % 60);
    const float scale = 1, maxWidth = 70;
    float width = n3ds_menuTextWidth(menu, screen, text, scale);
    if (width < 0) return;
    n3ds_menuDrawText(menu, screen, text, right - fminf(width, maxWidth), y, scale, maxWidth, color);
}

static void n3ds_menuDrawBasic(N3DSMenu* menu, N3DSLoadingScreen* screen,
    const N3DSMenuGame* games, int gameCount, int selected, int first, int confirmChoice, int footerChoice) {
    C2D_TextBufClear(screen->textBuffer);
    uint32_t blue = C2D_Color32(82, 165, 232, 255);
    uint32_t white = C2D_Color32(255, 255, 255, 255);
    uint32_t gray = C2D_Color32(170, 170, 170, 255);
    n3ds_menuDrawText(menu, screen, "Cinnamon", 8, 4, 1.4f, 304, blue);
    n3ds_menuDrawText(menu, screen, "Select a game", 24, 36, 1, 272, white);
    if (menu->launcher.showPaths) {
        const char* folder = games[selected].folder;
        if (strncmp(folder, "sdmc:/", 6) == 0) folder += 6;
        n3ds_menuDrawScrollingText(menu, screen, folder, 24, 50, 0.65f, 272, 320, false,
            menu->infoTime, gray);
    }
    int visible = n3ds_menuVisibleRows(menu);
    for (int row = 0; row < visible && first + row < gameCount; ++row) {
        int index = first + row;
        bool highlighted = index == selected && footerChoice < 0;
        float y = 66 + row * n3ds_menuRowHeight(menu);
        uint32_t color = highlighted ? blue : gray;
        if (highlighted) n3ds_menuDrawText(menu, screen, ">", 12, y, 1, 14, color);
        n3ds_menuDrawScrollingText(menu, screen, games[index].name, 28, y, 1, 152, 320, false,
            highlighted ? menu->infoTime : 0, color);
        n3ds_menuDrawPlaytime(menu, screen, games[index].playtimeSeconds, 264, y, color);
    }
    if (first > 0) n3ds_menuDrawText(menu, screen, "^", 278, 66, 1, 16, blue);
    if (first + visible < gameCount) n3ds_menuDrawText(menu, screen, "v", 278, 171, 1, 16, blue);
    char position[48];
    snprintf(position, sizeof(position), "%d/%d", selected + 1, gameCount);
    n3ds_menuDrawText(menu, screen, position, 278, 193, 0.6f, 34, gray);
    n3ds_menuDrawText(menu, screen, confirmChoice >= 0 ? "Continue" : "Settings", 55, 193, 1, 110,
        footerChoice == 0 || confirmChoice == 0 ? blue : gray);
    n3ds_menuDrawText(menu, screen, confirmChoice >= 0 ? "Back" : "Exit", 185, 193, 1, 80,
        footerChoice == 1 || confirmChoice == 1 ? blue : gray);
    n3ds_menuDrawCredits(menu, screen);
}

static void n3ds_menuDraw(N3DSMenu* menu, N3DSLoadingScreen* screen,
    const N3DSMenuGame* games, int gameCount, int selected, int first, int confirmChoice,
    int footerChoice, float soulX, float soulY, float phase) {
    if (menu->uiStyle == N3DS_LAUNCHER_BASIC) {
        n3ds_menuDrawBasic(menu, screen, games, gameCount, selected, first, confirmChoice, footerChoice);
        return;
    }
    n3ds_menuDrawBackground(menu, screen, phase);
    uint32_t white = C2D_Color32(255, 255, 255, 255);
    uint32_t dim = C2D_Color32(179, 153, 153, 255);
    n3ds_menuDrawText(menu, screen, "Cinnamon", 8, 4, 1, 304, white);
    n3ds_menuDrawText(menu, screen, "Please select a game.", 40, 30, 1, 270, white);

    for (int row = 0; row < N3DS_MENU_VISIBLE_GAMES && first + row < gameCount; ++row) {
        int index = first + row;
        float y = 55 + row * N3DS_MENU_ROW_HEIGHT;
        uint32_t color = index == selected && footerChoice < 0 ? white : dim;
        n3ds_menuDrawBox(y, color);
        n3ds_menuDrawScrollingText(menu, screen, games[index].name, 80, y + 5, 1, 102, 320, false,
            index == selected && footerChoice < 0 ? menu->infoTime : 0, color);
        n3ds_menuDrawPlaytime(menu, screen, games[index].playtimeSeconds, 260, y + 5, color);
        if (index == selected && confirmChoice >= 0) {
            n3ds_menuDrawText(menu, screen, "Continue", 90, y + 22, 1, 80,
                confirmChoice == 0 ? white : dim);
            n3ds_menuDrawText(menu, screen, "Back", 180, y + 22, 1, 80,
                confirmChoice == 1 ? white : dim);
        } else if (menu->launcher.showPaths) {
            const char* folder = games[index].folder;
            if (strncmp(folder, "sdmc:/", 6) == 0) folder += 6;
            n3ds_menuDrawScrollingText(menu, screen, folder, 80, y + 22, 1, 180, 320, false,
                menu->infoTime, color);
        }
    }
    if (menu->soul != NULL)
        C2D_DrawImageAt(C2D_SpriteSheetGetImage(menu->soul, 0), soulX, soulY, 0.1f, NULL, 1, 1);
    if (confirmChoice < 0 && first > 0)
        n3ds_menuDrawText(menu, screen, "^", 278, 55, 1, 16, white);
    if (confirmChoice < 0 && first + N3DS_MENU_VISIBLE_GAMES < gameCount)
        n3ds_menuDrawText(menu, screen, "v", 278, 165, 1, 16, white);
    char position[48];
    snprintf(position, sizeof(position), "%d/%d", selected + 1, gameCount);
    n3ds_menuDrawText(menu, screen, position, 278, 193, 0.6f, 34, dim);
    n3ds_menuDrawText(menu, screen, confirmChoice >= 0 ? "A: confirm" : "Settings",
        55, 193, 1, 110, footerChoice == 0 ? white : dim);
    n3ds_menuDrawText(menu, screen, confirmChoice >= 0 ? "B: back" : "Exit",
        185, 193, 1, 80, footerChoice == 1 ? white : dim);

    n3ds_menuDrawCredits(menu, screen);
}

static void n3ds_menuDrawInfo(N3DSMenu* menu, const N3DSMenuGame* game) {
    N3DSLoadingScreen screen = {.textBuffer = menu->infoTextBuffer};
    C2D_TextBufClear(screen.textBuffer);
    uint32_t dim = C2D_Color32(170, 170, 170, 255);
    uint32_t accent = menu->uiStyle == N3DS_LAUNCHER_BASIC
        ? C2D_Color32(82, 165, 232, 255) : C2D_Color32(255, 230, 150, 255);
    C2D_Image image = {0};
    N3DSMenuIcon* icon = &menu->icons[game->pathIndex];
    if (icon->loaded) image = (C2D_Image){&icon->texture, &icon->subtex};
    else if (menu->defaultIcon != NULL) image = C2D_SpriteSheetGetImage(menu->defaultIcon, 0);
    if (image.tex != NULL) {
        float scale = 160.0f / fmaxf(image.subtex->width, image.subtex->height);
        C2D_DrawImageAt(image, 120 + (160 - image.subtex->width * scale) / 2,
            40 + (160 - image.subtex->height * scale) / 2, 0.1f, NULL, scale, scale);
    } else {
        C2D_DrawRectSolid(120, 40, 0, 160, 160, C2D_Color32(28, 28, 32, 255));
        n3ds_menuDrawCenteredText(menu, &screen, "No icon", 113, 0.8f, dim);
    }
    n3ds_menuDrawCenteredText(menu, &screen, game->name, 8, 1.4f, accent);
    n3ds_menuSetFontFiltering(menu, true);
    n3ds_menuDrawScrollingText(menu, &screen, game->path, 16, 212, 0.8f, 368, 400, true,
        menu->infoTime, C2D_Color32(255, 255, 255, 255));
    n3ds_menuSetFontFiltering(menu, false);
}

int n3ds_loadSelectGame(N3DSLoadingScreen* screen, char* const* gamePaths, int gameCount) {
    if (screen == NULL || screen->target == NULL || screen->textBuffer == NULL || gameCount <= 0)
        return -1;
    N3DSMenu menu = {0};
    menu.target = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, -1);
    if (menu.target == NULL) return -1;
    C3D_RenderTargetSetOutput(menu.target, GFX_BOTTOM, GFX_LEFT, N3DS_MENU_TRANSFER_FLAGS);
    if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) {
        C2D_Prepare();
        C2D_TargetClear(menu.target, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(menu.target);
        C2D_TargetClear(screen->target, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(screen->target);
        C3D_FrameEnd(0);
    }
    menu.infoTextBuffer = C2D_TextBufNew(4096);
    menu.infoGameIndex = -1;
    if (menu.infoTextBuffer == NULL) {
        n3ds_menuFree(&menu);
        return -1;
    }
    if (!n3ds_menuLoadFont(&menu)) {
        n3ds_loadWaitForStart(screen, "Missing launcher font. Press START.");
        n3ds_menuFree(&menu);
        return -1;
    }
    menu.door = C2D_SpriteSheetLoad(N3DS_MENU_ROOT "door.t3x");
    menu.soul = C2D_SpriteSheetLoad(N3DS_MENU_ROOT "soul.t3x");
    if (menu.door == NULL || menu.soul == NULL) {
        n3ds_loadWaitForStart(screen, "Missing launcher sprites. Press START.");
        n3ds_menuFree(&menu);
        return -1;
    }
    C3D_TexSetFilter(C2D_SpriteSheetGetImage(menu.door, 0).tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetFilter(C2D_SpriteSheetGetImage(menu.soul, 0).tex, GPU_NEAREST, GPU_NEAREST);
    menu.defaultIcon = C2D_SpriteSheetLoad(N3DS_MENU_ROOT "270767334.t3x");
    if (menu.defaultIcon != NULL)
        C3D_TexSetFilter(C2D_SpriteSheetGetImage(menu.defaultIcon, 0).tex, GPU_LINEAR, GPU_LINEAR);

    N3DSMenuGame* games = safeCalloc((size_t)gameCount, sizeof(*games));
    menu.games = games;
    menu.icons = safeCalloc((size_t)gameCount, sizeof(*menu.icons));
    menu.gameCount = gameCount;
    n3ds_settingsLoad(N3DS_SETTINGS_PATH, NULL, &menu.launcher);
    menu.uiStyle = (N3DSLauncherUI)menu.launcher.uiStyle;
    for (int i = 0; i < gameCount; ++i) {
        games[i].path = gamePaths[i];
        games[i].pathIndex = i;
    }
    n3ds_menuInitAudio(&menu);
    if (!n3ds_menuIntro(screen->target, menu.target, menu.audio, menu.launcher.musicVolume,
            menu.uiStyle == N3DS_LAUNCHER_DELTARUNE && menu.launcher.showIntro, n3ds_menuPreload, &menu)) {
        n3ds_menuFree(&menu);
        return -1;
    }
    n3ds_menuSortGames(&menu, games, gameCount);
    menu.sounds[0] = menu.audio->vtable->createStream(menu.audio, "menu.ogg");
    n3ds_menuApplyVolumes(&menu);
    n3ds_menuPlaySound(&menu, 0);
    int selected = 0;
    if (menu.launcher.rememberSelection && menu.launcher.recentCount > 0) {
        for (int i = 0; i < gameCount; ++i)
            if (strcmp(games[i].path, menu.launcher.recentGames[0]) == 0) selected = i;
    }
    int first = 0;
    if (selected >= n3ds_menuVisibleRows(&menu)) first = selected - n3ds_menuVisibleRows(&menu) + 1;
    int result = -1;
    int confirmChoice = -1;
    int footerChoice = -1;
    float soulX = 65;
    float soulY = 72;
    float phase = 0;
    bool touching = false;
    bool dragged = false;
    int touchStartX = 0;
    int touchStartY = 0;
    int touchFirst = 0;
    int finishFrames = 0;
    u64 lastTick = svcGetSystemTick();
    float repeatTime = 0;
    hidScanInput();
    while (aptMainLoop()) {
        u64 tick = svcGetSystemTick();
        float dt = (float)((double)(tick - lastTick) / SYSCLOCK_ARM11);
        lastTick = tick;
        if (dt > 0.1f) dt = 0.1f;
        menu.audio->vtable->update(menu.audio, dt);
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        int visible = n3ds_menuVisibleRows(&menu);
        int rowHeight = n3ds_menuRowHeight(&menu);
        int previous = selected;
        int previousChoice = confirmChoice;
        int previousFooter = footerChoice;
        if (finishFrames > 0) {
            if (--finishFrames == 0) break;
        } else if (menu.page != N3DS_MENU_GAMES) {
            int pathIndex = games[selected].pathIndex;
            n3ds_menuSettingsInput(&menu, &games[selected], down, held, dt);
            if (menu.page == N3DS_MENU_GAMES) {
                n3ds_menuSortGames(&menu, games, gameCount);
                for (int i = 0; i < gameCount; ++i)
                    if (games[i].pathIndex == pathIndex) selected = i;
                first = selected >= visible ? selected - visible + 1 : 0;
                touching = false;
            }
        } else {
            bool cancel = (down & KEY_B) != 0;
            bool quit = (down & KEY_START) != 0;
            bool openSettings = (down & KEY_X) != 0;
            u32 direction = held & (KEY_UP | KEY_DOWN);
            bool move = (down & (KEY_UP | KEY_DOWN)) != 0;
            if (move) repeatTime = 0.35f;
            else if (direction != 0) {
                repeatTime -= dt;
                if (repeatTime <= 0) {
                    move = true;
                    repeatTime = 0.1f;
                }
            }
            if (move && confirmChoice < 0) {
                bool up = (direction & KEY_UP) != 0;
                bool downHeld = (direction & KEY_DOWN) != 0;
                if (up != downHeld) {
                    if (up) {
                        if (footerChoice == 1) footerChoice = 0;
                        else if (footerChoice == 0) {
                            footerChoice = -1;
                            selected = gameCount - 1;
                        } else if (selected == 0) footerChoice = 1;
                        else --selected;
                    } else {
                        if (footerChoice == 0) footerChoice = 1;
                        else if (footerChoice == 1) {
                            footerChoice = -1;
                            selected = 0;
                        } else if (selected + 1 == gameCount) footerChoice = 0;
                        else ++selected;
                    }
                }
            }
            if (confirmChoice >= 0) {
                bool left = (down & KEY_LEFT) != 0;
                bool right = (down & KEY_RIGHT) != 0;
                if (left != right) confirmChoice = 1 - confirmChoice;
            }
            if (footerChoice >= 0 && (down & (KEY_LEFT | KEY_RIGHT))) footerChoice = 1 - footerChoice;
            bool activate = (down & KEY_A) != 0;
            if (activate && footerChoice >= 0) {
                if (footerChoice == 0) openSettings = true;
                else quit = true;
                activate = false;
            }
            if (down & KEY_TOUCH) {
                touchPosition touch;
                hidTouchRead(&touch);
                touching = n3ds_menuTouchInList(&menu, &touch);
                dragged = false;
                touchStartX = touch.px;
                touchStartY = touch.py;
                touchFirst = first;
                if (confirmChoice < 0 && touch.px >= 270 && touch.px < 305) {
                    footerChoice = -1;
                    if (touch.py >= 45 && touch.py < 85 && first > 0)
                        selected = first - 1;
                    if (touch.py >= 150 && touch.py < 190 && first + visible < gameCount)
                        selected = first + visible;
                }
                if (touch.py >= 190 && touch.py < 211) {
                    if (touch.px >= 53 && touch.px < 165) {
                        if (confirmChoice < 0) openSettings = true;
                        else {
                            if (menu.uiStyle == N3DS_LAUNCHER_BASIC) confirmChoice = 0;
                            activate = true;
                        }
                    }
                    if (touch.px >= 175 && touch.px < 267) {
                        if (confirmChoice < 0) quit = true;
                        else cancel = true;
                    }
                }
            }
            if (touching && (held & KEY_TOUCH)) {
                touchPosition touch;
                hidTouchRead(&touch);
                int distance = touchStartY - touch.py;
                if (abs(distance) > 8 || abs((int)touch.px - touchStartX) > 8) dragged = true;
                if (dragged && confirmChoice < 0) {
                    footerChoice = -1;
                    first = touchFirst + distance / rowHeight;
                    int maxFirst = gameCount > visible ? gameCount - visible : 0;
                    if (first < 0) first = 0;
                    if (first > maxFirst) first = maxFirst;
                    if (selected < first) selected = first;
                    if (selected >= first + visible) selected = first + visible - 1;
                }
            }
            if (touching && !(held & KEY_TOUCH)) {
                touching = false;
                if (!dragged) {
                    int row = (touchStartY - n3ds_menuListTop(&menu)) / rowHeight;
                    int index = first + row;
                    if (row < visible && index < gameCount) {
                        if (confirmChoice < 0) {
                            footerChoice = -1;
                            selected = index;
                            activate = true;
                        } else if (menu.uiStyle == N3DS_LAUNCHER_DELTARUNE && index == selected &&
                            touchStartY >= 75 + row * rowHeight) {
                            if (touchStartX >= 80 && touchStartX < 170) {
                                confirmChoice = 0;
                                activate = true;
                            } else if (touchStartX >= 170 && touchStartX < 260) {
                                confirmChoice = 1;
                                activate = true;
                            }
                        }
                    }
                }
            }
            if (selected < first) first = selected;
            if (selected >= first + visible)
                first = selected - visible + 1;
            if (selected != previous || previousFooter != footerChoice ||
                (previousChoice >= 0 && confirmChoice != previousChoice))
                n3ds_menuPlaySound(&menu, 1);
            if (quit || cancel) {
                n3ds_menuPlaySound(&menu, 3);
                touching = false;
                if (!quit && confirmChoice >= 0) confirmChoice = -1;
                else finishFrames = 12;
            } else if (openSettings) {
                n3ds_menuPlaySound(&menu, 2);
                touching = false;
                confirmChoice = -1;
                footerChoice = 0;
                n3ds_menuOpenSettings(&menu);
            } else if (activate) {
                n3ds_menuPlaySound(&menu, confirmChoice == 1 ? 3 : 2);
                touching = false;
                if (confirmChoice < 0) confirmChoice = 0;
                else if (confirmChoice == 1) confirmChoice = -1;
                else {
                    result = games[selected].pathIndex;
                    n3ds_settingsRememberGame(&menu.launcher, games[selected].path);
                    if (!n3ds_settingsSave(N3DS_SETTINGS_PATH, NULL, NULL, &menu.launcher))
                        logWarn("Could not save launcher game history\n");
                    finishFrames = 12;
                }
            }
        }
        float targetX = confirmChoice < 0 ? 65 : (confirmChoice == 0 ? 75 : 165);
        float targetY = (confirmChoice < 0 ? 72 : 81) + (selected - first) * rowHeight;
        if (footerChoice >= 0) {
            targetX = footerChoice == 0 ? 40 : 170;
            targetY = 195;
        }
        float soulStep = 1.0f - powf(0.5f, dt * 30.0f);
        soulX += (targetX - soulX) * soulStep;
        soulY += (targetY - soulY) * soulStep;
        if (fabsf(targetX - soulX) <= 2) soulX = targetX;
        if (fabsf(targetY - soulY) <= 2) soulY = targetY;
        phase += dt * 30;
        if (menu.infoGameIndex != games[selected].pathIndex) {
            menu.infoGameIndex = games[selected].pathIndex;
            menu.infoTime = 0;
        } else menu.infoTime += dt;
        if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) break;
        C2D_Prepare();
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
            GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0xFF);
        C2D_TargetClear(screen->target, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(screen->target);
        if (finishFrames == 0) n3ds_menuDrawInfo(&menu, &games[selected]);
        C2D_TargetClear(menu.target, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(menu.target);
        if (finishFrames == 0) {
            if (menu.page == N3DS_MENU_GAMES)
                n3ds_menuDraw(&menu, screen, games, gameCount, selected, first, confirmChoice,
                    footerChoice, soulX, soulY, phase);
            else if (menu.page == N3DS_MENU_RENAME) n3ds_menuDrawKeyboard(&menu, screen);
            else n3ds_menuDrawSettings(&menu, screen, &games[selected], phase);
        }
        C2D_Flush();
        C3D_FrameEnd(0);
        gspWaitForVBlank();
    }
    if (C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) C3D_FrameEnd(0);
    n3ds_menuFree(&menu);
    return result;
}
