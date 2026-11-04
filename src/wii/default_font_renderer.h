#pragma once

#include <stdint.h>

typedef struct WiiDefaultFontRenderer WiiDefaultFontRenderer;

WiiDefaultFontRenderer* WiiFont_create(void);
void WiiFont_destroy(WiiDefaultFontRenderer* renderer);
void WiiFont_beginFrame(WiiDefaultFontRenderer* renderer, float width, float height);
void WiiFont_printScaled(WiiDefaultFontRenderer* renderer, float x, float y, float scale,
    uint32_t color, uint8_t alpha, const char* text);
float WiiFont_measureText(const WiiDefaultFontRenderer* renderer, float scale, const char* text);
void WiiFont_setOutline(
    WiiDefaultFontRenderer* renderer, uint32_t color, uint8_t alpha, float radius);
