#pragma once

#include "../data_win.h"
#include "default_font_renderer.h"

#include <gccore.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    GXRModeObj normalMode;
    GXRModeObj lowResMode;
    GXRModeObj* mode;
    bool lowResModeReady;
    bool lowResAllowed;
    void* framebuffers[2];
    uint32_t framebufferSize;
    uint32_t framebufferIndex;
    uint32_t xfbHeight;
    void* fifo;
    bool lowResActive;
    float horizontalScale;
    float horizontalOffset;
} WiiVideoState;

typedef struct {
    char label[16];
    uint32_t count;
} WiiOverlayChunkStat;

typedef struct {
    WiiOverlayChunkStat stats[24];
    int statCount;
} WiiOverlayLoadingState;

typedef struct {
    WiiVideoState* video;
    WiiDefaultFontRenderer* font;
    WiiOverlayLoadingState loadingState;
    bool initialized;
} WiiOverlay;

bool WiiOverlay_init(WiiOverlay* overlay, WiiVideoState* video);
void WiiOverlay_destroy(WiiOverlay* overlay);
void WiiOverlay_drawStatusScreen(
    WiiOverlay* overlay, const char* gameName, const char* statusText, bool includeChunkStats);
void WiiOverlay_drawHomeMenu(WiiOverlay* overlay, int selectedIndex);
void WiiOverlay_drawGameSelector(
    WiiOverlay* overlay, char* const* gamePaths, int gameCount, int selectedIndex);
void WiiOverlay_drawSettings(WiiOverlay* overlay, int selectedIndex,
    const char* p240ModeLabel, const char* aspectRatioLabel, const char* debugStatsLabel,
    const char* debugLogLabel);
void WiiOverlay_statusCallback(
    const char* chunkName, int chunkIndex, int totalChunks, DataWin* dataWin, void* userData);
