#include "default_font_renderer.h"

#include "default_font/default_font.h"
#include "../utils.h"

#include <gccore.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#define WII_FONT_CLUT_ENTRIES 16u
#define WII_FONT_CI4_BLOCK_W 8u
#define WII_FONT_CI4_BLOCK_H 8u

struct WiiDefaultFontRenderer {
    uint8_t* textureData;
    uint32_t textureSize;
    uint16_t* paletteData;
    GXTexObj texObj;
    GXTlutObj tlutObj;
    float spacing;
    uint32_t outlineColor;
    uint8_t outlineAlpha;
    float outlineRadius;
};

static uint16_t packRgb5a3White(uint8_t alpha) {
    if (alpha >= 255u) return 0xFFFFu;
    uint16_t a = (uint16_t)((uint32_t)alpha * 7u / 255u);
    return (uint16_t)((a << 12u) | 0x0FFFu);
}

static bool uploadDefaultFontTexture(WiiDefaultFontRenderer* renderer) {
    if (renderer == NULL) return false;

    renderer->textureSize =
        GX_GetTexBufferSize(DEFAULTFONT_ATLAS_W, DEFAULTFONT_ATLAS_H, GX_TF_CI4, GX_FALSE, 0);
    renderer->textureData = memalign(32, renderer->textureSize);
    renderer->paletteData = memalign(32, sizeof(uint16_t) * WII_FONT_CLUT_ENTRIES);
    if (renderer->textureData == NULL || renderer->paletteData == NULL) return false;

    memset(renderer->textureData, 0, renderer->textureSize);
    for (uint32_t i = 0u; i < WII_FONT_CLUT_ENTRIES; ++i) {
        renderer->paletteData[i] = packRgb5a3White((uint8_t)(i * 255u / 15u));
    }

    uint8_t* out = renderer->textureData;
    for (uint32_t blockY = 0u; blockY < DEFAULTFONT_ATLAS_H; blockY += WII_FONT_CI4_BLOCK_H) {
        for (uint32_t blockX = 0u; blockX < DEFAULTFONT_ATLAS_W;
            blockX += WII_FONT_CI4_BLOCK_W) {
            for (uint32_t row = 0u; row < WII_FONT_CI4_BLOCK_H; ++row) {
                for (uint32_t col = 0u; col < WII_FONT_CI4_BLOCK_W; col += 2u) {
                    uint32_t index = (blockY + row) * DEFAULTFONT_ATLAS_W + blockX + col;
                    uint8_t left = JetBrainsMonoExtraBoldPixels[index] >> 4u;
                    uint8_t right = JetBrainsMonoExtraBoldPixels[index + 1u] >> 4u;
                    *out++ = (uint8_t)((left << 4u) | right);
                }
            }
        }
    }

    DCFlushRange(renderer->textureData, renderer->textureSize);
    DCFlushRange(renderer->paletteData, sizeof(uint16_t) * WII_FONT_CLUT_ENTRIES);

    GX_InitTlutObj(&renderer->tlutObj, renderer->paletteData, GX_TL_RGB5A3, WII_FONT_CLUT_ENTRIES);
    GX_InitTexObjCI(&renderer->texObj, renderer->textureData, DEFAULTFONT_ATLAS_W, DEFAULTFONT_ATLAS_H,
        GX_TF_CI4, GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT1);
    GX_InitTexObjFilterMode(&renderer->texObj, GX_LINEAR, GX_LINEAR);
    GX_InvalidateTexAll();
    return true;
}

WiiDefaultFontRenderer* WiiFont_create(void) {
    WiiDefaultFontRenderer* renderer = calloc(1, sizeof(WiiDefaultFontRenderer));
    if (renderer == NULL) return NULL;
    renderer->spacing = 1.0f;
    renderer->outlineColor = 0x000000u;
    renderer->outlineAlpha = 160u;
    renderer->outlineRadius = 1.0f;
    if (!uploadDefaultFontTexture(renderer)) {
        WiiFont_destroy(renderer);
        return NULL;
    }
    return renderer;
}

void WiiFont_destroy(WiiDefaultFontRenderer* renderer) {
    if (renderer == NULL) return;
    free(renderer->textureData);
    free(renderer->paletteData);
    free(renderer);
}

void WiiFont_beginFrame(WiiDefaultFontRenderer* renderer, float width, float height) {
    if (renderer == NULL) return;
    if (width <= 0.0f) width = 640.0f;
    if (height <= 0.0f) height = 480.0f;

    Mtx identity;
    guMtxIdentity(identity);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    Mtx44 projection;
    guOrtho(projection, 0.0f, height, 0.0f, width, 0.0f, 1.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_SetViewport(0.0f, 0.0f, width + 0.05f, height + 0.05f, 0.0f, 1.0f);
    GX_SetScissor(0, 0, (uint32_t)width, (uint32_t)height);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(
        GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_LoadTlut(&renderer->tlutObj, GX_TLUT1);
    GX_LoadTexObj(&renderer->texObj, GX_TEXMAP0);
}

float WiiFont_measureText(const WiiDefaultFontRenderer* renderer, float scale, const char* text) {
    if (renderer == NULL || text == NULL || scale <= 0.0f) return 0.0f;
    float width = 0.0f;
    float longest = 0.0f;
    for (const unsigned char* cursor = (const unsigned char*)text; *cursor != '\0'; ++cursor) {
        if (*cursor == '\n') {
            if (width > longest) longest = width;
            width = 0.0f;
            continue;
        }
        const DefaultFontGlyph* glyph = DefaultFont_lookupGlyph(*cursor);
        if (glyph != NULL) width += (float)glyph->xadvance;
    }
    if (width > longest) longest = width;
    return longest * renderer->spacing * scale * DEFAULT_FONT_UI_SCALE;
}

static void drawPass(WiiDefaultFontRenderer* renderer, float x, float y, float scale, uint32_t color,
    uint8_t alpha, const char* text, int32_t len) {
    if (renderer == NULL || text == NULL || alpha == 0u) return;

    uint8_t r = (uint8_t)(color & 0xFFu);
    uint8_t g = (uint8_t)((color >> 8u) & 0xFFu);
    uint8_t b = (uint8_t)((color >> 16u) & 0xFFu);
    float fontScale = scale * DEFAULT_FONT_UI_SCALE;
    float cursorY = y;
    int32_t lineStart = 0;

    for (int32_t i = 0; i <= len; ++i) {
        if (i != len && text[i] != '\n') continue;

        float pen = x;
        int32_t lineLen = i - lineStart;
        repeat((uint32_t)lineLen, j) {
            const DefaultFontGlyph* glyph = DefaultFont_lookupGlyph((uint8_t)text[lineStart + (int32_t)j]);
            if (glyph == NULL) continue;

            if (glyph->w > 0u && glyph->h > 0u) {
                float x0 = pen + (float)glyph->xoffset * fontScale;
                float y0 = cursorY + (float)glyph->yoffset * fontScale;
                float x1 = x0 + (float)glyph->w * fontScale;
                float y1 = y0 + (float)glyph->h * fontScale;
                float u0 = (float)glyph->x / (float)DEFAULTFONT_ATLAS_W;
                float v0 = (float)glyph->y / (float)DEFAULTFONT_ATLAS_H;
                float u1 = (float)(glyph->x + glyph->w) / (float)DEFAULTFONT_ATLAS_W;
                float v1 = (float)(glyph->y + glyph->h) / (float)DEFAULTFONT_ATLAS_H;

                GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
                GX_Position3f32(x0, y0, 0.0f);
                GX_Color4u8(r, g, b, alpha);
                GX_TexCoord2f32(u0, v0);
                GX_Position3f32(x1, y0, 0.0f);
                GX_Color4u8(r, g, b, alpha);
                GX_TexCoord2f32(u1, v0);
                GX_Position3f32(x1, y1, 0.0f);
                GX_Color4u8(r, g, b, alpha);
                GX_TexCoord2f32(u1, v1);
                GX_Position3f32(x0, y1, 0.0f);
                GX_Color4u8(r, g, b, alpha);
                GX_TexCoord2f32(u0, v1);
                GX_End();
            }

            pen += (float)glyph->xadvance * renderer->spacing * fontScale;
        }

        cursorY += DEFAULT_FONT_OVERLAY_LINE_HEIGHT * scale;
        lineStart = i + 1;
    }
}

void WiiFont_printScaled(WiiDefaultFontRenderer* renderer, float x, float y, float scale,
    uint32_t color, uint8_t alpha, const char* text) {
    if (renderer == NULL || text == NULL || scale <= 0.0f) return;
    int32_t len = (int32_t)strlen(text);

    if (renderer->outlineAlpha != 0u && renderer->outlineRadius > 0.0f) {
        static const float OFFSETS[8][2] = {
            {-1.0f, -1.0f},
            {0.0f, -1.0f},
            {1.0f, -1.0f},
            {-1.0f, 0.0f},
            {1.0f, 0.0f},
            {-1.0f, 1.0f},
            {0.0f, 1.0f},
            {1.0f, 1.0f},
        };
        for (uint32_t i = 0u; i < 8u; ++i) {
            drawPass(renderer, x + OFFSETS[i][0] * renderer->outlineRadius,
                y + OFFSETS[i][1] * renderer->outlineRadius, scale, renderer->outlineColor,
                renderer->outlineAlpha, text, len);
        }
    }

    drawPass(renderer, x, y, scale, color, alpha, text, len);
}

void WiiFont_setOutline(
    WiiDefaultFontRenderer* renderer, uint32_t color, uint8_t alpha, float radius) {
    if (renderer == NULL) return;
    renderer->outlineColor = color;
    renderer->outlineAlpha = alpha;
    renderer->outlineRadius = radius;
}
