#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#ifdef PLATFORM_3DS
#include "3ds/3ds_renderer.h"

#include <string.h>
#endif

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

enum { GRASS_TILE_SIZE = 40 };

static struct {
    int32_t index, length, height, i, j;
#ifdef PLATFORM_3DS
    int32_t cullSprite;
#endif
} cache;

static int32_t grassLoopCount(RValue value, GMLReal epsilon) {
    if (value.type != RVALUE_REAL && value.type != RVALUE_INT32) return -1;
    GMLReal limit = RValue_toReal(value);
    if (NativeScript_compareReals(0, limit, epsilon) >= 0) return 0;
    if (limit >= (GMLReal)16777216) return -1;

    int32_t count = (int32_t)limit;
    return count + (NativeScript_compareReals((GMLReal)count, limit, epsilon) < 0);
}

#ifdef PLATFORM_3DS
static int32_t grassCullIndex(GMLReal offset, int32_t count) {
    GMLReal cell = offset * ((GMLReal)1 / GRASS_TILE_SIZE);
    if (cell <= 0) return 0;
    if (cell >= (GMLReal)count) return count;
    return (int32_t)cell;
}

static int32_t grassCullSprite(const DataWin* dataWin) {
    for (uint32_t i = 0; i < dataWin->sprt.count; ++i) {
        const Sprite* sprite = &dataWin->sprt.sprites[i];
        if (!sprite->present || strcmp(sprite->name, "spr_tile_darkgrass_middle") != 0) continue;
        if (sprite->originX != 0 || sprite->originY != 0) return -1;
        for (uint32_t j = 0; j < sprite->textureCount; ++j) {
            int32_t index = sprite->tpagIndices[j];
            if ((uint32_t)index >= dataWin->tpag.count) return -1;
            const TexturePageItem* tpag = &dataWin->tpag.items[index];
            if (tpag->targetX != 0 || tpag->targetY != 0 ||
                tpag->targetWidth != GRASS_TILE_SIZE || tpag->targetHeight != GRASS_TILE_SIZE) {
                return -1;
            }
        }
        return (int32_t)i;
    }
    return -1;
}
#endif

static bool purplegrassDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    GMLReal epsilon = ctx->runner->mathEpsilon;
    int32_t columns = grassLoopCount(Instance_getSelfVar(self, cache.length), epsilon);
    if (columns < 0) return false;
    int32_t rows = columns > 0 ? grassLoopCount(Instance_getSelfVar(self, cache.height), epsilon) : 0;
    RValue index = Instance_getSelfVar(self, cache.index);
    if (rows < 0 || (index.type != RVALUE_REAL && index.type != RVALUE_INT32)) return false;

    GMLReal phase = RValue_toReal(index) + (GMLReal)0.2;
    Instance_setSelfVar(self, cache.index, RValue_makeReal(phase));
    Instance_setSelfVar(self, cache.i, RValue_makeReal((GMLReal)columns));
    if (columns > 0) Instance_setSelfVar(self, cache.j, RValue_makeReal((GMLReal)rows));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);

    int32_t spriteIndex = self->spriteIndex;
    if (columns == 0 || rows == 0 || (uint32_t)spriteIndex >= renderer->dataWin->sprt.count) return true;
    const Sprite* sprite = &renderer->dataWin->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0) return true;

    GMLReal x = self->x, y = self->y;
    int32_t firstI = 0, firstJ = 0;
    int32_t endI = columns, endJ = rows;
#ifdef PLATFORM_3DS
    float view[4];
    if (spriteIndex == cache.cullSprite && isfinite(x) && isfinite(y) && C3D_getTileCullRect(renderer, view)) {
        float padX = 1.0f, padY = 1.0f;
        if (renderer->CPortW > 0) padX += (view[2] - view[0]) / (float)renderer->CPortW;
        if (renderer->CPortH > 0) padY += (view[3] - view[1]) / (float)renderer->CPortH;
        firstI = grassCullIndex((GMLReal)view[0] - x - padX - GRASS_TILE_SIZE, columns);
        firstJ = grassCullIndex((GMLReal)view[1] - y - padY - GRASS_TILE_SIZE, rows);
        endI = grassCullIndex((GMLReal)view[2] - x + padX + GRASS_TILE_SIZE, columns);
        endJ = grassCullIndex((GMLReal)view[3] - y + padY + GRASS_TILE_SIZE, rows);
    }
#endif
    if (firstI >= endI || firstJ >= endJ) return true;

    GMLReal phaseX = phase + x / (GMLReal)320;
    GMLReal phaseY = y / (GMLReal)320;
    int32_t previousFrame = 0;
    int32_t tpag = sprite->tpagIndices[0];
    float originX = (float)sprite->originX, originY = (float)sprite->originY;
    float alpha = renderer->drawAlpha;
    for (int32_t i = firstI; i < endI; ++i) {
        float drawX = (float)(x + (GMLReal)i * GRASS_TILE_SIZE);
        GMLReal columnPhase = phaseX + (GMLReal)i * (GMLReal)0.125;
        for (int32_t j = firstJ; j < endJ; ++j) {
            int32_t frame = (int32_t)((columnPhase + (GMLReal)j * (GMLReal)0.125) + phaseY);
            if (frame < 0) frame = (int32_t)self->imageIndex;
            if (frame != previousFrame) {
                tpag = Renderer_resolveTPAGIndex(renderer->dataWin, spriteIndex, frame);
                previousFrame = frame;
            }
            if (tpag < 0) continue;

            float drawY = (float)(y + (GMLReal)j * GRASS_TILE_SIZE);
            renderer->vtable->drawSprite(renderer, tpag, drawX, drawY,
                originX, originY, 1.0f, 1.0f, 0.0f, 0xFFFFFFu, alpha);
        }
    }
    return true;
}

void NativePurplegrassDraw_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_purplegrass_Draw_0", purplegrassDraw)) return;

    cache.index = NativeScript_getSelfVarID(ctx, "index");
    cache.length = NativeScript_getSelfVarID(ctx, "length");
    cache.height = NativeScript_getSelfVarID(ctx, "height");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
    cache.j = NativeScript_getSelfVarID(ctx, "j");
#ifdef PLATFORM_3DS
    cache.cullSprite = grassCullSprite(ctx->dataWin);
#endif
}
