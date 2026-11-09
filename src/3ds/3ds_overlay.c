#include "3ds_overlay.h"
#include "3ds_c3d_draw.h"
#include "3ds_platform_config.h"
#include "3ds_renderer.h"
#include "../runner.h"
#include "../utils.h"
#include <3ds/allocator/vram.h>
#include <citro2d.h>
#include <stb/ds/stb_ds.h>
#include <stdio.h>
#include <string.h>

#define N3DS_LOADING_SCREEN_WIDTH 400.0f
#define N3DS_DISPLAY_TRANSFER_FLAGS                                                                \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |               \
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |                                             \
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

static void n3ds_overlayLerpUV(
    float ua, float va, float ub, float vb, float t, float* outU, float* outV) {
    *outU = ua + (ub - ua) * t;
    *outV = va + (vb - va) * t;
}

static void n3ds_overlayGetSubTexRectUVs(const Tex3DS_SubTexture* subtex, float srcX0,
    float srcY0, float srcX1, float srcY1, float* outUVs) {
    float tlU, tlV, trU, trV, blU, blV, brU, brV;
    float topLeftU, topLeftV, topRightU, topRightV;
    float bottomLeftU, bottomLeftV, bottomRightU, bottomRightV;
    float sx0 = subtex->width > 0 ? srcX0 / (float)subtex->width : 0.0f;
    float sy0 = subtex->height > 0 ? srcY0 / (float)subtex->height : 0.0f;
    float sx1 = subtex->width > 0 ? srcX1 / (float)subtex->width : 0.0f;
    float sy1 = subtex->height > 0 ? srcY1 / (float)subtex->height : 0.0f;

    Tex3DS_SubTextureTopLeft(subtex, &tlU, &tlV);
    Tex3DS_SubTextureTopRight(subtex, &trU, &trV);
    Tex3DS_SubTextureBottomLeft(subtex, &blU, &blV);
    Tex3DS_SubTextureBottomRight(subtex, &brU, &brV);

    n3ds_overlayLerpUV(tlU, tlV, trU, trV, sx0, &topLeftU, &topLeftV);
    n3ds_overlayLerpUV(blU, blV, brU, brV, sx0, &bottomLeftU, &bottomLeftV);
    n3ds_overlayLerpUV(topLeftU, topLeftV, bottomLeftU, bottomLeftV, sy0,
        &outUVs[0], &outUVs[1]);
    n3ds_overlayLerpUV(tlU, tlV, trU, trV, sx1, &topRightU, &topRightV);
    n3ds_overlayLerpUV(blU, blV, brU, brV, sx1, &bottomRightU, &bottomRightV);
    n3ds_overlayLerpUV(topRightU, topRightV, bottomRightU, bottomRightV, sy0,
        &outUVs[2], &outUVs[3]);
    n3ds_overlayLerpUV(topLeftU, topLeftV, bottomLeftU, bottomLeftV, sy1,
        &outUVs[4], &outUVs[5]);
    n3ds_overlayLerpUV(topRightU, topRightV, bottomRightU, bottomRightV, sy1,
        &outUVs[6], &outUVs[7]);
}

static void n3ds_loadDraw(N3DSLoadingScreen* screen);

void n3ds_loadFree(N3DSLoadingScreen* screen) {
    if (screen == NULL) return;
    if (screen->textBuffer != NULL) {
        C2D_TextBufDelete(screen->textBuffer);
        screen->textBuffer = NULL;
    }
    if (screen->target != NULL) {
        C3D_RenderTargetDelete(screen->target);
        screen->target = NULL;
    }
}

void n3ds_loadInit(N3DSLoadingScreen* screen, bool useCitro2D) {
    if (screen == NULL || !useCitro2D) return;
    screen->target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
    if (screen->target != NULL) {
        C3D_RenderTargetSetOutput(
            screen->target, GFX_TOP, GFX_LEFT, N3DS_DISPLAY_TRANSFER_FLAGS);
        screen->textBuffer = C2D_TextBufNew(1024);
        if (screen->textBuffer != NULL) return;
        C3D_RenderTargetDelete(screen->target);
        screen->target = NULL;
    }
}

static bool n3ds_loadBeginFrame(N3DSLoadingScreen* screen) {
    if (screen == NULL || screen->target == NULL) return false;
    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return false;
    C3D_RenderTargetSetOutput(
        screen->target, GFX_TOP, GFX_LEFT, N3DS_DISPLAY_TRANSFER_FLAGS);
    C2D_Prepare();
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
        GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0xFF);
    C2D_TargetClear(screen->target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(screen->target);
    return true;
}

static void n3ds_loadDrawCenteredText(
    N3DSLoadingScreen* screen, const char* text, float y, float scale, uint32_t color) {
    if (screen == NULL || screen->textBuffer == NULL || text == NULL) return;
    C2D_Text parsed = {0};
    if (C2D_TextParse(&parsed, screen->textBuffer, text) == NULL) return;
    C2D_DrawText(&parsed, C2D_WithColor | C2D_AlignCenter, N3DS_LOADING_SCREEN_WIDTH * 0.5f, y,
        0.25f, scale, scale, color);
}

static void n3ds_loadDraw(N3DSLoadingScreen* screen) {
    if (screen == NULL) return;

    float progress = 0.0f;
    if (screen->totalChunks > 0) {
        progress = (float)screen->chunkIndex / (float)screen->totalChunks;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
    }

    if (screen->target != NULL) {
        const float barX = 50.0f;
        const float barY = 150.0f;
        const float barW = 300.0f;
        const float barH = 18.0f;
        const float fillW = barW * progress;

        char percentText[8];
        snprintf(percentText, sizeof(percentText), "%d%%", (int)(progress * 100.0f));

        if (!n3ds_loadBeginFrame(screen)) return;
        C2D_DrawRectSolid(barX, barY, 0.75f, barW, barH, C2D_Color32(64, 64, 64, 255));
        if (fillW > 0.0f)
            C2D_DrawRectSolid(
                barX, barY, 0.5f, fillW, barH, C2D_Color32(255, 204, 0, 255));

        C2D_TextBufClear(screen->textBuffer);
        n3ds_loadDrawCenteredText(
            screen, "Cinnamon", 57.0f, 0.9f, C2D_Color32(82, 165, 232, 255));
        if (screen->gameName[0] != '\0') {
            n3ds_loadDrawCenteredText(
                screen, screen->gameName, 90.0f, 0.55f, C2D_Color32(170, 170, 170, 255));
        }
        n3ds_loadDrawCenteredText(
            screen, screen->statusLine, 118.0f, 0.55f, C2D_Color32(170, 170, 170, 255));
        n3ds_loadDrawCenteredText(
            screen, percentText, barY + 2.0f, 0.45f, C2D_Color32(255, 255, 255, 255));

        C2D_Flush();
        C3D_FrameEnd(0);
        gspWaitForVBlank();
        return;
    }
}

void n3ds_loadSetGameName(N3DSLoadingScreen* screen, const char* gameName) {
    if (screen == NULL) return;
    snprintf(screen->gameName, sizeof(screen->gameName), "%s", gameName != NULL ? gameName : "");
}

void n3ds_loadSet(
    N3DSLoadingScreen* screen, const char* statusLine, int chunkIndex, int totalChunks) {
    if (screen == NULL) return;
    snprintf(
        screen->statusLine, sizeof(screen->statusLine), "%s", statusLine != NULL ? statusLine : "");
    screen->chunkIndex = chunkIndex;
    screen->totalChunks = totalChunks;
    n3ds_loadDraw(screen);
}

void n3ds_loadWaitForStart(N3DSLoadingScreen* screen, const char* statusLine) {
    n3ds_loadSet(screen, statusLine, 1, 1);
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

int n3ds_loadSelectGame(N3DSLoadingScreen* screen, char* const* gamePaths, int gameCount) {
    if (screen == NULL || screen->target == NULL || screen->textBuffer == NULL || gameCount <= 0)
        return -1;
    enum { visibleGames = 6 };
    int selected = 0;
    hidScanInput();
    while (aptMainLoop()) {
        if (!n3ds_loadBeginFrame(screen)) return -1;
        C2D_TextBufClear(screen->textBuffer);
        n3ds_loadDrawCenteredText(
            screen, "Cinnamon", 8.0f, 0.8f, C2D_Color32(82, 165, 232, 255));
        n3ds_loadDrawCenteredText(
            screen, "Select a game", 36.0f, 0.55f, C2D_Color32(255, 255, 255, 255));

        int first = (selected / visibleGames) * visibleGames;
        for (int row = 0; row < visibleGames && first + row < gameCount; ++row) {
            int index = first + row;
            const char* end = strrchr(gamePaths[index], '/');
            const char* name = end;
            if (name != NULL) {
                while (name > gamePaths[index] && name[-1] != '/') --name;
            } else {
                name = gamePaths[index];
                end = name + strlen(name);
            }
            int nameLength = (int)(end - name);
            char line[64];
            snprintf(line, sizeof(line), "%s %.*s%s", index == selected ? ">" : " ",
                nameLength > 50 ? 47 : nameLength, name, nameLength > 50 ? "..." : "");
            C2D_Text text = {0};
            C2D_TextParse(&text, screen->textBuffer, line);
            float width = 0.0f;
            C2D_TextGetDimensions(&text, 0.55f, 0.55f, &width, NULL);
            float scale = width > 352.0f ? 0.55f * 352.0f / width : 0.55f;
            uint32_t color = index == selected ? C2D_Color32(82, 165, 232, 255)
                                               : C2D_Color32(170, 170, 170, 255);
            C2D_DrawText(&text, C2D_WithColor, 24.0f, 66.0f + (float)row * 21.0f,
                0.25f, scale, scale, color);
        }

        char position[48];
        snprintf(position, sizeof(position), "%d / %d", selected + 1, gameCount);
        n3ds_loadDrawCenteredText(
            screen, position, 199.0f, 0.45f, C2D_Color32(170, 170, 170, 255));
        C2D_Flush();
        C3D_FrameEnd(0);
        gspWaitForVBlank();

        hidScanInput();
        u32 down = hidKeysDown();
        if (down & (KEY_B | KEY_START)) return -1;
        if ((down & KEY_UP) && !(down & KEY_DOWN)) selected = (selected + gameCount - 1) % gameCount;
        if ((down & KEY_DOWN) && !(down & KEY_UP)) selected = (selected + 1) % gameCount;
        if (down & KEY_A) return selected;
    }
    return -1;
}

typedef struct {
    bool ready;
    bool failed;
    C2D_SpriteSheet sheet;
    C3D_Tex* texture;
    float glyphUVs[95][8];
} N3DSDebugTinyFontAtlas;

static N3DSDebugTinyFontAtlas gN3DSDebugTinyFontAtlas = {0};

typedef struct {
    bool initialized;
    u64 fpsWindowStartTick;
    uint32_t fpsWindowFrames;
    float fps;
    float tickMs;
    float stepMs;
    float drawMs;
    float audioMs;
} N3DSFrameStatsOverlay;

static N3DSFrameStatsOverlay gN3DSFrameStatsOverlay = {0};

static float n3ds_ticksToMsF(u64 ticks) {
    return (float)((double)ticks * 1000.0 / (double)SYSCLOCK_ARM11);
}

static bool n3ds_fontAtlasEnsureReady(void) {
    N3DSDebugTinyFontAtlas* atlas = &gN3DSDebugTinyFontAtlas;
    if (atlas->ready) return true;
    if (atlas->failed) return false;

    enum {
        glyphWidth = 6,
        glyphHeight = 10,
        glyphsPerRow = 16,
        glyphCount = 95,
        atlasWidth = glyphWidth * glyphsPerRow,
        atlasHeight = glyphHeight * 6,
    };

    atlas->sheet = C2D_SpriteSheetLoad("romfs:/gfx/debugTinyFont.t3x");
    if (atlas->sheet == NULL) {
        atlas->failed = true;
        return false;
    }
    C2D_Image image = C2D_SpriteSheetGetImage(atlas->sheet, 0u);
    if (image.tex == NULL || image.subtex == NULL || image.subtex->width < atlasWidth ||
        image.subtex->height < atlasHeight) {
        C2D_SpriteSheetFree(atlas->sheet);
        atlas->sheet = NULL;
        atlas->failed = true;
        return false;
    }
    atlas->texture = image.tex;

    repeat(glyphCount, glyphIndex) {
        uint32_t gridX = (uint32_t)glyphIndex % glyphsPerRow;
        uint32_t gridY = (uint32_t)glyphIndex / glyphsPerRow;
        uint32_t glyphX = gridX * glyphWidth;
        uint32_t glyphY = gridY * glyphHeight;

        n3ds_overlayGetSubTexRectUVs(image.subtex, (float)glyphX, (float)glyphY,
            (float)(glyphX + glyphWidth), (float)(glyphY + glyphHeight),
            atlas->glyphUVs[glyphIndex]);
    }

    C3D_TexSetFilter(atlas->texture, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(atlas->texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    atlas->ready = true;
    return true;
}

void n3ds_overlayFree(void) {
    N3DSDebugTinyFontAtlas* atlas = &gN3DSDebugTinyFontAtlas;
    if (atlas->sheet != NULL) C2D_SpriteSheetFree(atlas->sheet);
    memset(atlas, 0, sizeof(*atlas));
}

static void n3ds_fontWriteVertex(
    N3DSC3DVertex* vertex, float x, float y, float u, float v, uint32_t color) {
    vertex->x = x;
    vertex->y = y;
    vertex->u = u;
    vertex->v = v;
    vertex->r = (uint8_t)(color & 0xFFu);
    vertex->g = (uint8_t)((color >> 8) & 0xFFu);
    vertex->b = (uint8_t)((color >> 16) & 0xFFu);
    vertex->a = (uint8_t)((color >> 24) & 0xFFu);
}

void n3ds_overlayDrawText(Renderer* renderer,
    const char* text, float x, float y, float scale, float maxWidth, uint32_t color) {
    if (renderer == NULL) return;
    if (text == NULL || text[0] == '\0') return;
    if (!n3ds_fontAtlasEnsureReady()) return;

    const float lineStartX = x;
    const float lineStartY = y;
    const float glyphAdvance = 6.0f * scale;
    const float glyphHeight = 10.0f * scale;
    uint32_t glyphCount = 0u;

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        char glyphChar = *cursor;
        if (glyphChar == '\n') {
            x = lineStartX;
            y += glyphHeight;
            continue;
        }
        if (glyphChar == '\t') {
            x += glyphAdvance * 4.0f;
            continue;
        }
        if (maxWidth > 0.0f && x + glyphAdvance > lineStartX + maxWidth) break;
        if (glyphChar == ' ') {
            x += glyphAdvance;
            continue;
        }
        glyphCount++;
        x += glyphAdvance;
    }
    if (glyphCount == 0u || glyphCount > UINT32_MAX / 6u) return;

    N3DSC3DVertex* vertices = C3D_beginVertices(
        renderer, gN3DSDebugTinyFontAtlas.texture, glyphCount * 6u);
    if (vertices == NULL) return;

    x = lineStartX;
    y = lineStartY;
    uint32_t vertexIndex = 0u;

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        char glyphChar = *cursor;
        if (glyphChar == '\n') {
            x = lineStartX;
            y += glyphHeight;
            continue;
        }
        if (glyphChar == '\t') {
            x += glyphAdvance * 4.0f;
            continue;
        }
        if (maxWidth > 0.0f && x + glyphAdvance > lineStartX + maxWidth) break;
        if (glyphChar == ' ') {
            x += glyphAdvance;
            continue;
        }
        if (glyphChar < 32 || glyphChar > 126) glyphChar = '?';

        uint32_t glyphIndex = (uint32_t)((uint8_t)glyphChar - 32u);
        const float* uv = gN3DSDebugTinyFontAtlas.glyphUVs[glyphIndex];
        n3ds_fontWriteVertex(&vertices[vertexIndex++], x, y, uv[0], uv[1], color);
        n3ds_fontWriteVertex(
            &vertices[vertexIndex++], x + glyphAdvance, y, uv[2], uv[3], color);
        n3ds_fontWriteVertex(
            &vertices[vertexIndex++], x + glyphAdvance, y + glyphHeight, uv[6], uv[7], color);
        n3ds_fontWriteVertex(&vertices[vertexIndex++], x, y, uv[0], uv[1], color);
        n3ds_fontWriteVertex(
            &vertices[vertexIndex++], x + glyphAdvance, y + glyphHeight, uv[6], uv[7], color);
        n3ds_fontWriteVertex(
            &vertices[vertexIndex++], x, y + glyphHeight, uv[4], uv[5], color);
        x += glyphAdvance;
    }
    C3D_commitVertices(renderer, vertexIndex);
}

void n3ds_overlayReset(void) {
    memset(&gN3DSFrameStatsOverlay, 0, sizeof(gN3DSFrameStatsOverlay));
}

void n3ds_overlayUpdate(const N3DSOverlayTimings* timing) {
    N3DSFrameStatsOverlay* stats = &gN3DSFrameStatsOverlay;
    if (!stats->initialized) {
        stats->initialized = true;
        stats->fpsWindowStartTick = timing->frameStartTick;
        stats->fpsWindowFrames = 0u;
        stats->fps = 0.0f;
    }

    stats->tickMs = 0.0f;
    stats->stepMs = 0.0f;
    stats->drawMs = 0.0f;
    stats->audioMs = 0.0f;

    stats->fpsWindowFrames++;
    u64 fpsWindowTicks = timing->frameStartTick >= stats->fpsWindowStartTick
        ? timing->frameStartTick - stats->fpsWindowStartTick
        : 0u;
    if (fpsWindowTicks >= (SYSCLOCK_ARM11 / 4u)) {
        stats->fps = fpsWindowTicks > 0u
            ? (float)(((double)stats->fpsWindowFrames * (double)SYSCLOCK_ARM11) /
                (double)fpsWindowTicks)
            : 0.0f;
        stats->fpsWindowStartTick = timing->frameStartTick;
        stats->fpsWindowFrames = 0u;
    }

    stats->tickMs = timing->workEndTick >= timing->workStartTick
                        ? n3ds_ticksToMsF(timing->workEndTick - timing->workStartTick)
                        : 0.0f;
    stats->stepMs = timing->stepEndTick >= timing->stepStartTick
                        ? n3ds_ticksToMsF(timing->stepEndTick - timing->stepStartTick)
                        : 0.0f;
    stats->audioMs = timing->audioEndTick >= timing->audioStartTick
        ? n3ds_ticksToMsF(timing->audioEndTick - timing->audioStartTick)
        : 0.0f;
    stats->drawMs = timing->drawEndTick >= timing->drawStartTick
                        ? n3ds_ticksToMsF(timing->drawEndTick - timing->drawStartTick)
                        : 0.0f;
}

void n3ds_overlayDraw(Renderer* renderer, const Runner* runner, bool gameOnBottomScreen) {
#if !N3DS_HAS_BOTTOM_TARGET
    (void)renderer;
    (void)runner;
    (void)gameOnBottomScreen;
    return;
#endif
    if (renderer == NULL) return;
#if !N3DS_DISABLE_GAME_BOTTOM_SCREEN
    if (!gameOnBottomScreen && C3D_bottomScreenUsed(renderer)) return;
#endif

    const N3DSFrameStatsOverlay* stats = &gN3DSFrameStatsOverlay;
    const Room* room = runner != NULL ? runner->currentRoom : NULL;
    const char* roomName = room != NULL && room->name != NULL ? room->name : "?";
    uint32_t roomSpeed = room != NULL ? room->speed : 0u;
    uint32_t residentAtlasPages = C3D_getAtlasPages(renderer);
    uint32_t atlasPageCount = C3D_getAtlasPageLimit(renderer);
    uint32_t residentAtlasKb = C3D_getAtlasVramBytes(renderer) / 1024u;

    N3DSRendererAtlasSizeBucket sizeBuckets[8];
    memset(sizeBuckets, 0, sizeof(sizeBuckets));
    uint32_t sizeBucketCount = C3D_getAtlasSizeBuckets(
        renderer, sizeBuckets, (uint32_t)(sizeof(sizeBuckets) / sizeof(sizeBuckets[0])));
    uint32_t bestBucket = UINT32_MAX;
    for (uint32_t i = 0u; i < sizeBucketCount; ++i) {
        if (bestBucket == UINT32_MAX ||
            sizeBuckets[i].residentPages > sizeBuckets[bestBucket].residentPages ||
            (sizeBuckets[i].residentPages == sizeBuckets[bestBucket].residentPages &&
                sizeBuckets[i].totalPages > sizeBuckets[bestBucket].totalPages)) {
            bestBucket = i;
        }
    }

    uint32_t trackedVramFreeBytes = vramSpaceFree();
    uint32_t trackedAppFreeKb = n3ds_getFreeHeapBudgetBytes() / 1024u;
    uint32_t trackedVramFreeKb = trackedVramFreeBytes / 1024u;

    char text[1024];
    if (bestBucket != UINT32_MAX) {
        snprintf(text, sizeof(text),
            "Room: %.40s\n"
            "FPS: %.1f\n"
            "Tick: %.2fms\n"
            "Step: %.2fms\n"
            "Draw: %.2fms\n"
            "Audio: %.2fms\n"
            "Heap Free: %lu KB\n"
            "VRAM Free: %lu KB\n"
            "Room Speed: %lu\n"
            "Atlas: %lu/%lu %lu KB\n"
            "  %lux%lu: %lu/%lu\n"
            "Instances: %ld\n"
            "Structs: %ld",
            roomName, (double)stats->fps, (double)stats->tickMs, (double)stats->stepMs,
            (double)stats->drawMs, (double)stats->audioMs, (unsigned long)trackedAppFreeKb,
            (unsigned long)trackedVramFreeKb, (unsigned long)roomSpeed,
            (unsigned long)residentAtlasPages, (unsigned long)atlasPageCount,
            (unsigned long)residentAtlasKb, (unsigned long)sizeBuckets[bestBucket].width,
            (unsigned long)sizeBuckets[bestBucket].height,
            (unsigned long)sizeBuckets[bestBucket].residentPages,
            (unsigned long)sizeBuckets[bestBucket].totalPages,
            (long)(runner != NULL ? arrlen(runner->instances) : 0),
            (long)(runner != NULL ? arrlen(runner->structInstances) : 0));
    } else {
        snprintf(text, sizeof(text),
            "Room: %.40s\n"
            "FPS: %.1f\n"
            "Tick: %.2fms\n"
            "Step: %.2fms\n"
            "Draw: %.2fms\n"
            "Audio: %.2fms\n"
            "Heap Free: %lu KB\n"
            "VRAM Free: %lu KB\n"
            "Room Speed: %lu\n"
            "Atlas: %lu/%lu %lu KB\n"
            "Instances: %ld\n"
            "Structs: %ld",
            roomName, (double)stats->fps, (double)stats->tickMs, (double)stats->stepMs,
            (double)stats->drawMs, (double)stats->audioMs, (unsigned long)trackedAppFreeKb,
            (unsigned long)trackedVramFreeKb, (unsigned long)roomSpeed,
            (unsigned long)residentAtlasPages, (unsigned long)atlasPageCount,
            (unsigned long)residentAtlasKb,
            (long)(runner != NULL ? arrlen(runner->instances) : 0),
            (long)(runner != NULL ? arrlen(runner->structInstances) : 0));
    }

    if (gameOnBottomScreen)
        C3D_beginTopOverlay(renderer, 400, 240);
    else
        C3D_beginBottomOverlay(renderer, 320, 240, true);
    n3ds_overlayDrawText(
        renderer, text, 8.0f, 8.0f, 1.0f, 0.0f, C2D_Color32(255, 255, 255, 255));
    C3D_endBottomScreenGUI(renderer);
}
