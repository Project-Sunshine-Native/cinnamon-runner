#pragma once

#include "../renderer.h"
#include <3ds.h>
#include <citro2d.h>
#include <stdint.h>

typedef struct {
    u64 frameStartTick;
    u64 workStartTick;
    u64 stepStartTick;
    u64 stepEndTick;
    u64 audioStartTick;
    u64 audioEndTick;
    u64 drawStartTick;
    u64 drawEndTick;
    u64 workEndTick;
} N3DSOverlayTimings;

void n3ds_overlayReset(void);
void n3ds_overlayUpdate(const N3DSOverlayTimings* timing);
void n3ds_overlayDraw(Renderer* renderer, const Runner* runner, bool gameOnBottomScreen);
void n3ds_overlayDrawText(Renderer* renderer,
    const char* text, float x, float y, float scale, float maxWidth, uint32_t color);
void n3ds_overlayFree(void);

typedef struct {
    C3D_RenderTarget* target;
    C2D_TextBuf textBuffer;
    char gameName[128];
    char statusLine[128];
    int chunkIndex;
    int totalChunks;
} N3DSLoadingScreen;

void n3ds_loadFree(N3DSLoadingScreen* screen);
void n3ds_loadInit(N3DSLoadingScreen* screen, bool useCitro2D);
void n3ds_loadSetGameName(N3DSLoadingScreen* screen, const char* gameName);
void n3ds_loadSet(
    N3DSLoadingScreen* screen, const char* statusLine, int chunkIndex, int totalChunks);
void n3ds_loadWaitForStart(N3DSLoadingScreen* screen, const char* statusLine);
int n3ds_loadSelectGame(N3DSLoadingScreen* screen, char* const* gamePaths, int gameCount);
