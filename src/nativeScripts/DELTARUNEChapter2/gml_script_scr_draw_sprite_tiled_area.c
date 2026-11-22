#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#ifdef PLATFORM_3DS
#include "3ds/3ds_renderer.h"
#endif

#include <math.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static bool tiledArea(VMContext* ctx, RValue* outResult) {
    if (ctx->currentCallFrame->scriptArgsCount < 10) return false;
    GMLReal arguments[10];
    for (int32_t i = 0; i < 10; ++i)
        arguments[i] = RValue_toReal(NativeScript_getArgument(ctx, i));
    int32_t index = RValue_toInt32(NativeScript_getArgument(ctx, 0));
    if ((uint32_t)index >= ctx->dataWin->sprt.count) return false;
    const Sprite* sprite = &ctx->dataWin->sprt.sprites[index];
    GMLReal originX = arguments[2], originY = arguments[3];
    GMLReal x1 = arguments[4], y1 = arguments[5];
    GMLReal x2 = arguments[6], y2 = arguments[7];
    GMLReal scaleX = arguments[8], scaleY = arguments[9];
    GMLReal sw = sprite->width * scaleX, sh = sprite->height * scaleY;
    if (!sprite->present || sw <= 0 || sh <= 0 || !isfinite(sw) || !isfinite(sh))
        return false;
    for (int32_t i = 1; i < 10; ++i) {
        if (!isfinite(arguments[i])) return false;
    }
    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal xmod = GMLReal_fmod(x1, sw), originXmod = GMLReal_fmod(originX, sw);
    GMLReal ymod = GMLReal_fmod(y1, sh), originYmod = GMLReal_fmod(originY, sh);
    GMLReal startX = x1 - (xmod - originXmod) -
        sw * (NativeScript_compareReals(xmod, originXmod, epsilon) < 0);
    GMLReal startY = y1 - (ymod - originYmod) -
        sh * (NativeScript_compareReals(ymod, originYmod, epsilon) < 0);
    if (startX + sw == startX || startY + sh == startY) return false;
    Renderer* renderer = ctx->runner->renderer;
    *outResult = RValue_makeReal(0);
    if (renderer == nullptr) return true;
    int32_t frame = RValue_toInt32(NativeScript_getArgument(ctx, 1));
    if (frame < 0 && ctx->currentCallFrame->self != nullptr)
        frame = (int32_t)ctx->currentCallFrame->self->imageIndex;
    uint32_t color = ctx->currentCallFrame->scriptArgsCount > 10 ?
        RValue_toColour(NativeScript_getArgument(ctx, 10)) : 0xFFFFFF;
    float alpha = ctx->currentCallFrame->scriptArgsCount > 11 ?
        RValue_toReal(NativeScript_getArgument(ctx, 11)) : 1;
    GMLReal endX = x2, endY = y2;
#ifdef PLATFORM_3DS
    float view[4];
    if (scaleX == 1 && scaleY == 1 && C3D_getTileCullRect(renderer, view)) {
        GMLReal skipX = GMLReal_floor(((GMLReal)view[0] - 2 - startX) / sw) - 1;
        GMLReal skipY = GMLReal_floor(((GMLReal)view[1] - 2 - startY) / sh) - 1;
        if (skipX > 0) startX += skipX * sw;
        if (skipY > 0) startY += skipY * sh;
        if (endX > view[2] + 2) endX = view[2] + 2;
        if (endY > view[3] + 2) endY = view[3] + 2;
    }
#endif
    for (GMLReal x = startX; NativeScript_compareReals(x, endX, epsilon) <= 0; x += sw) {
        GMLReal left = NativeScript_compareReals(x, x1, epsilon) <= 0 ? x1 - x : 0;
        GMLReal width = NativeScript_compareReals(x2, x + sw, epsilon) <= 0 ?
            ((sw - ((x + sw) - x2)) + 1) - left : sw - left;
        for (GMLReal y = startY; NativeScript_compareReals(y, endY, epsilon) <= 0; y += sh) {
            GMLReal top = NativeScript_compareReals(y, y1, epsilon) <= 0 ? y1 - y : 0;
            GMLReal height = NativeScript_compareReals(y2, y + sh, epsilon) <= 0 ?
                ((sh - ((y + sh) - y2)) + 1) - top : sh - top;
            Renderer_drawSpritePartExt(renderer, index, frame, left, top, width, height,
                x + left, y + top, scaleX, scaleY, 0, 0, 0, color, alpha);
        }
    }
    return true;
}

void NativeChapter2TiledArea_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_draw_sprite_tiled_area", tiledArea);
}
