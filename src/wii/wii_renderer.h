#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../renderer.h"

#define WII_FRAMEBUFFER_WIDTH 640
#define WII_FRAMEBUFFER_HEIGHT 480
#define WII_LOW_RES_LOGICAL_WIDTH 320
#define WII_LOW_RES_LOGICAL_HEIGHT 240
#define WII_LOW_RES_FRAMEBUFFER_WIDTH 320
#define WII_LOW_RES_FRAMEBUFFER_HEIGHT 240

typedef struct WiiRenderer WiiRenderer;

Renderer* GX_create(
    const char* dataWinPath, bool cacheAtlas, uint32_t atlasTextureCacheBytes);
void GX_syncRoom(WiiRenderer* renderer, int32_t roomIndex);
void GX_setOutputSize(WiiRenderer* renderer, int32_t width, int32_t height);
void GX_setOutputRect(
    WiiRenderer* renderer, int32_t x, int32_t y, int32_t width, int32_t height);
void GX_clearOutput(WiiRenderer* renderer);
void GX_present(
    WiiRenderer* renderer, void* xfb, uint32_t fbWidth, uint32_t efbHeight, uint32_t xfbHeight);
uint32_t GX_getAtlasBytes(const WiiRenderer* renderer);
uint32_t GX_getLoadedAtlasPages(const WiiRenderer* renderer);
uint32_t GX_getAtlasPages(const WiiRenderer* renderer);
uint32_t GX_getSurfaceCount(const WiiRenderer* renderer);
