#include "wii_overlay.h"
#include "wii_renderer.h"

#include "../utils.h"

#include <stdio.h>
#include <string.h>

#define WII_OVERLAY_TITLE_COLOR 0x52A5E8u
#define WII_OVERLAY_WHITE 0xFFFFFFu
#define WII_OVERLAY_GRAY 0xAAAAAAu
#define WII_OVERLAY_DARK_GRAY 0x707070u

static const GXColor WII_MENU_BG = {24, 24, 24, 230};
static const GXColor WII_MENU_BORDER = {255, 255, 255, 210};

static void overlayDrawSolidRect(float x0, float y0, float x1, float y1, GXColor color) {
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x0, y0, 0.0f);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_Position3f32(x1, y0, 0.0f);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_Position3f32(x1, y1, 0.0f);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_Position3f32(x0, y1, 0.0f);
    GX_Color4u8(color.r, color.g, color.b, color.a);
    GX_End();
}

static void overlayDrawFramedRect(
    float x, float y, float w, float h, float thickness, GXColor bg, GXColor border) {
    float x2 = x + w;
    float y2 = y + h;
    overlayDrawSolidRect(x, y, x2, y2, bg);
    overlayDrawSolidRect(x, y, x2, y + thickness, border);
    overlayDrawSolidRect(x, y2 - thickness, x2, y2, border);
    overlayDrawSolidRect(x, y + thickness, x + thickness, y2 - thickness, border);
    overlayDrawSolidRect(x2 - thickness, y + thickness, x2, y2 - thickness, border);
}

static void overlayPrintCentered(WiiOverlay* overlay, float centerX, float y, float scale,
    uint32_t color, uint8_t alpha, const char* text) {
    if (overlay == NULL || overlay->font == NULL || text == NULL) return;
    float x = centerX - WiiFont_measureText(overlay->font, scale, text) * 0.5f;
    WiiFont_printScaled(overlay->font, x, y, scale, color, alpha, text);
}

static void overlayBeginFontFrame(WiiOverlay* overlay) {
    if (overlay == NULL || overlay->font == NULL) return;
    WiiFont_beginFrame(overlay->font, WII_FRAMEBUFFER_WIDTH, WII_FRAMEBUFFER_HEIGHT);
    if (overlay->video == NULL) return;

    float scale = overlay->video->horizontalScale > 0.0f ? overlay->video->horizontalScale : 1.0f;
    float offset = overlay->video->horizontalOffset;
    float width = (float)WII_FRAMEBUFFER_WIDTH * scale;
    Mtx44 projection;
    guOrtho(projection, 0.0f, (float)WII_FRAMEBUFFER_HEIGHT, 0.0f, (float)WII_FRAMEBUFFER_WIDTH,
        0.0f, 1.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_SetViewport(offset, 0.0f, width + 0.05f, (float)WII_FRAMEBUFFER_HEIGHT + 0.05f, 0.0f, 1.0f);
    GX_SetScissor((uint32_t)offset, 0u, (uint32_t)width, WII_FRAMEBUFFER_HEIGHT);
}

static void overlayDrawCredits(WiiOverlay* overlay) {
    WiiFont_printScaled(overlay->font, 10.0f, 460.0f, 0.36f, WII_OVERLAY_DARK_GRAY,
        255u,
        "Cinnamon created by Grayforz2468/Project Sunhine, fork of Butterscotch by MrPowerGamerBR "
        "(https://mrpowergamerbr.com/)");
}

static void overlayDrawChunkStats(WiiOverlay* overlay) {
    if (overlay == NULL || overlay->font == NULL || overlay->loadingState.statCount <= 0) return;
    char line[40];
    float y = 10.0f;
    for (int i = 0; i < overlay->loadingState.statCount; ++i) {
        snprintf(line, sizeof(line), "%lu %s", (unsigned long)overlay->loadingState.stats[i].count,
            overlay->loadingState.stats[i].label);
        WiiFont_printScaled(
            overlay->font, 10.0f, y, 0.34f, WII_OVERLAY_GRAY, 255u, line);
        y += 12.0f;
    }
}

static void overlayPresent(WiiOverlay* overlay) {
    if (overlay == NULL || overlay->video == NULL ||
        overlay->video->framebuffers[overlay->video->framebufferIndex] == NULL)
        return;
    GX_SetCopyClear((GXColor){0, 0, 0, 255}, 0x00FFFFFFu);
    GX_CopyDisp(overlay->video->framebuffers[overlay->video->framebufferIndex], GX_TRUE);
    GX_DrawDone();
    VIDEO_SetNextFramebuffer(overlay->video->framebuffers[overlay->video->framebufferIndex]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    overlay->video->framebufferIndex ^= 1u;
}

static void overlayBeginFrame(WiiOverlay* overlay) {
    GX_SetCopyClear((GXColor){0, 0, 0, 255}, 0x00FFFFFFu);
    GX_SetViewport(0.0f, 0.0f, (float)WII_FRAMEBUFFER_WIDTH + 0.05f,
        (float)WII_FRAMEBUFFER_HEIGHT + 0.05f, 0.0f, 1.0f);
    GX_SetScissor(0, 0, WII_FRAMEBUFFER_WIDTH, WII_FRAMEBUFFER_HEIGHT);
    GX_InvalidateTexAll();
    overlayBeginFontFrame(overlay);
}

static void overlayRecordChunkStats(WiiOverlay* overlay, DataWin* dataWin) {
    if (overlay == NULL || dataWin == NULL) return;
    typedef struct {
        uint32_t* countPtr;
        const char* label;
    } CountSource;
    CountSource sources[] = {
        {&dataWin->sond.count, "sounds"},
        {&dataWin->sprt.count, "sprites"},
        {&dataWin->bgnd.count, "backgrounds"},
        {&dataWin->font.count, "fonts"},
        {&dataWin->objt.count, "objects"},
        {&dataWin->room.count, "rooms"},
        {&dataWin->code.count, "code entries"},
        {&dataWin->txtr.count, "textures"},
    };

    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        if (*sources[i].countPtr == 0u) continue;
        bool found = false;
        for (int statIndex = 0; statIndex < overlay->loadingState.statCount; ++statIndex) {
            if (strcmp(overlay->loadingState.stats[statIndex].label, sources[i].label) == 0) {
                found = true;
                break;
            }
        }
        if (!found &&
            overlay->loadingState.statCount < (int)(sizeof(overlay->loadingState.stats) /
                sizeof(overlay->loadingState.stats[0]))) {
            WiiOverlayChunkStat* stat =
                &overlay->loadingState.stats[overlay->loadingState.statCount++];
            snprintf(stat->label, sizeof(stat->label), "%s", sources[i].label);
            stat->count = *sources[i].countPtr;
        }
    }
}

bool WiiOverlay_init(WiiOverlay* overlay, WiiVideoState* video) {
    if (overlay == NULL || video == NULL) return false;
    memset(overlay, 0, sizeof(*overlay));
    overlay->video = video;
    overlay->font = WiiFont_create();
    if (overlay->font == NULL) return false;
    WiiFont_setOutline(overlay->font, 0x000000u, 160u, 1.0f);
    overlay->initialized = true;
    return true;
}

void WiiOverlay_destroy(WiiOverlay* overlay) {
    if (overlay == NULL) return;
    WiiFont_destroy(overlay->font);
    memset(overlay, 0, sizeof(*overlay));
}

void WiiOverlay_drawStatusScreen(
    WiiOverlay* overlay, const char* gameName, const char* statusText, bool includeChunkStats) {
    if (overlay == NULL || !overlay->initialized || overlay->font == NULL) return;
    overlayBeginFrame(overlay);

    overlayPrintCentered(
        overlay, 320.0f, 178.0f, 0.76f, WII_OVERLAY_TITLE_COLOR, 255u, "Cinnamon");
    if (gameName != NULL && gameName[0] != '\0') {
        overlayPrintCentered(overlay, 320.0f, 210.0f, 0.46f, WII_OVERLAY_GRAY, 255u, gameName);
    }
    if (statusText != NULL && statusText[0] != '\0') {
        overlayPrintCentered(
            overlay, 320.0f, 300.0f, 0.46f, WII_OVERLAY_GRAY, 255u, statusText);
    }

    if (includeChunkStats) overlayDrawChunkStats(overlay);
    overlayDrawCredits(overlay);
    overlayPresent(overlay);
}

void WiiOverlay_drawGameSelector(
    WiiOverlay* overlay, char* const* gamePaths, int gameCount, int selectedIndex) {
    if (overlay == NULL || !overlay->initialized || overlay->font == NULL) return;
    enum { visibleGames = 8 };
    int first = (selectedIndex / visibleGames) * visibleGames;
    overlayBeginFrame(overlay);
    overlayPrintCentered(
        overlay, 320.0f, 42.0f, 0.76f, WII_OVERLAY_TITLE_COLOR, 255u, "Cinnamon");
    overlayPrintCentered(
        overlay, 320.0f, 82.0f, 0.48f, WII_OVERLAY_WHITE, 255u, "Select a game");

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
        char line[80];
        snprintf(line, sizeof(line), "%s %.*s%s", index == selectedIndex ? ">" : " ",
            nameLength > 64 ? 61 : nameLength, name, nameLength > 64 ? "..." : "");
        WiiFont_printScaled(overlay->font, 58.0f, 130.0f + (float)row * 30.0f, 0.60f,
            index == selectedIndex ? WII_OVERLAY_TITLE_COLOR : WII_OVERLAY_GRAY, 255u, line);
    }

    char position[48];
    snprintf(position, sizeof(position), "%d / %d", selectedIndex + 1, gameCount);
    overlayPrintCentered(overlay, 320.0f, 382.0f, 0.46f, WII_OVERLAY_GRAY, 255u, position);
    overlayDrawCredits(overlay);
    overlayPresent(overlay);
}

void WiiOverlay_drawHomeMenu(WiiOverlay* overlay, int selectedIndex) {
    if (overlay == NULL || !overlay->initialized || overlay->font == NULL) return;
    static const char* options[] = {
        "Resume",
        "Runner settings",
        "Return to Homebrew Channel",
        "Return to Wii Menu",
    };
    const int optionCount = (int)(sizeof(options) / sizeof(options[0]));
    if (selectedIndex < 0) selectedIndex = 0;
    if (selectedIndex >= optionCount) selectedIndex = optionCount - 1;

    overlayBeginFrame(overlay);
    overlayDrawSolidRect(0.0f, 0.0f, (float)WII_FRAMEBUFFER_WIDTH,
        (float)WII_FRAMEBUFFER_HEIGHT, (GXColor){0, 0, 0, 150});

    float menuX = 112.0f;
    float menuY = 138.0f;
    float menuW = 416.0f;
    float menuH = 204.0f;
    overlayDrawFramedRect(menuX, menuY, menuW, menuH, 2.0f, WII_MENU_BG, WII_MENU_BORDER);

    overlayBeginFontFrame(overlay);
    overlayPrintCentered(
        overlay, 320.0f, menuY + 20.0f, 0.58f, WII_OVERLAY_WHITE, 255u, "Paused");

    float optionY = menuY + 60.0f;
    for (int i = 0; i < optionCount; ++i) {
        bool selected = i == selectedIndex;
        if (selected) {
            overlayDrawSolidRect(menuX + 18.0f, optionY - 5.0f, menuX + menuW - 18.0f,
                optionY + 23.0f, (GXColor){48, 48, 48, 230});
            overlayBeginFontFrame(overlay);
        }
        uint32_t color = selected ? WII_OVERLAY_TITLE_COLOR : WII_OVERLAY_GRAY;
        WiiFont_printScaled(
            overlay->font, menuX + 38.0f, optionY, 0.48f, color, 255u, selected ? ">" : " ");
        WiiFont_printScaled(
            overlay->font, menuX + 62.0f, optionY, 0.48f, color, 255u, options[i]);
        optionY += 32.0f;
    }
}

void WiiOverlay_drawSettings(WiiOverlay* overlay, int selectedIndex,
    const char* p240ModeLabel, const char* aspectRatioLabel, const char* debugStatsLabel,
    const char* debugLogLabel) {
    if (overlay == NULL || !overlay->initialized || overlay->font == NULL) return;
    enum { optionCount = 5 };
    if (selectedIndex < 0) selectedIndex = 0;
    if (selectedIndex >= optionCount) selectedIndex = optionCount - 1;
    if (p240ModeLabel == NULL || p240ModeLabel[0] == '\0') p240ModeLabel = "Automatic";
    if (aspectRatioLabel == NULL || aspectRatioLabel[0] == '\0') aspectRatioLabel = "16:9";
    if (debugStatsLabel == NULL || debugStatsLabel[0] == '\0') debugStatsLabel = "Off";
    if (debugLogLabel == NULL || debugLogLabel[0] == '\0') debugLogLabel = "Off";

    overlayBeginFrame(overlay);
    overlayDrawSolidRect(0.0f, 0.0f, (float)WII_FRAMEBUFFER_WIDTH,
        (float)WII_FRAMEBUFFER_HEIGHT, (GXColor){0, 0, 0, 150});

    float menuX = 96.0f;
    float menuY = 108.0f;
    float menuW = 448.0f;
    float menuH = 264.0f;
    overlayDrawFramedRect(menuX, menuY, menuW, menuH, 2.0f, WII_MENU_BG, WII_MENU_BORDER);

    overlayBeginFontFrame(overlay);
    overlayPrintCentered(
        overlay, 320.0f, menuY + 20.0f, 0.58f, WII_OVERLAY_WHITE, 255u, "Runner settings");

    char p240Line[64];
    snprintf(p240Line, sizeof(p240Line), "240p mode: %s", p240ModeLabel);
    char aspectRatioLine[64];
    snprintf(aspectRatioLine, sizeof(aspectRatioLine), "Aspect ratio: %s", aspectRatioLabel);
    char debugStatsLine[64];
    snprintf(debugStatsLine, sizeof(debugStatsLine), "Debug stats: %s", debugStatsLabel);
    char debugLogLine[64];
    snprintf(debugLogLine, sizeof(debugLogLine), "Debug log: %s", debugLogLabel);
    const char* options[] = {
        p240Line,
        aspectRatioLine,
        debugStatsLine,
        debugLogLine,
        "Back",
    };

    float optionY = menuY + 66.0f;
    for (int i = 0; i < optionCount; ++i) {
        bool selected = i == selectedIndex;
        if (selected) {
            overlayDrawSolidRect(menuX + 18.0f, optionY - 5.0f, menuX + menuW - 18.0f,
                optionY + 23.0f, (GXColor){48, 48, 48, 230});
            overlayBeginFontFrame(overlay);
        }
        uint32_t color = selected ? WII_OVERLAY_TITLE_COLOR : WII_OVERLAY_GRAY;
        WiiFont_printScaled(
            overlay->font, menuX + 38.0f, optionY, 0.48f, color, 255u, selected ? ">" : " ");
        WiiFont_printScaled(
            overlay->font, menuX + 62.0f, optionY, 0.48f, color, 255u, options[i]);
        optionY += 32.0f;
    }
}

void WiiOverlay_statusCallback(
    const char* chunkName, int chunkIndex, int totalChunks, DataWin* dataWin, void* userData) {
    WiiOverlay* overlay = (WiiOverlay*)userData;
    if (overlay == NULL || !overlay->initialized) return;
    if (totalChunks <= 0) totalChunks = 1;
    if (chunkIndex < 0) chunkIndex = 0;
    if (chunkIndex >= totalChunks) chunkIndex = totalChunks - 1;

    overlayRecordChunkStats(overlay, dataWin);

    const char* gameName =
        dataWin != NULL && dataWin->gen8.displayName != NULL ? dataWin->gen8.displayName : NULL;
    overlayBeginFrame(overlay);

    overlayPrintCentered(
        overlay, 320.0f, 170.0f, 0.76f, WII_OVERLAY_TITLE_COLOR, 255u, "Cinnamon");
    if (gameName != NULL && gameName[0] != '\0') {
        overlayPrintCentered(overlay, 320.0f, 202.0f, 0.46f, WII_OVERLAY_GRAY, 255u, gameName);
    }

    float progress = (float)(chunkIndex + 1) / (float)totalChunks;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float barX = 120.0f;
    float barY = 300.0f;
    float barW = 400.0f;
    float barH = 20.0f;
    overlayDrawSolidRect(barX, barY, barX + barW, barY + barH, (GXColor){64, 64, 64, 255});
    overlayDrawSolidRect(
        barX, barY, barX + barW * progress, barY + barH, (GXColor){255, 204, 0, 255});
    overlayBeginFontFrame(overlay);

    char percentText[8];
    snprintf(percentText, sizeof(percentText), "%d%%", (int)(progress * 100.0f));
    overlayPrintCentered(
        overlay, 320.0f, barY + 2.0f, 0.38f, WII_OVERLAY_WHITE, 255u, percentText);

    char statusText[48];
    snprintf(statusText, sizeof(statusText), "Loading %.4s... (%d/%d)",
        chunkName != NULL ? chunkName : "data", chunkIndex + 1, totalChunks);
    overlayPrintCentered(
        overlay, 320.0f, barY + barH + 12.0f, 0.46f, WII_OVERLAY_WHITE, 255u, statusText);

    overlayDrawChunkStats(overlay);
    overlayDrawCredits(overlay);
    overlayPresent(overlay);
}
