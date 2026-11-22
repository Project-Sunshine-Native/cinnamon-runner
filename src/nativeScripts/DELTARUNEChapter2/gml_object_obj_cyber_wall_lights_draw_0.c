#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t animsiner, totalWidth;
} cache;

static bool cyberWallLightsDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    int32_t spriteIndex = self->spriteIndex;
    if ((uint32_t)spriteIndex >= ctx->dataWin->sprt.count) return false;
    const Sprite* sprite = &ctx->dataWin->sprt.sprites[spriteIndex];
    if (!sprite->present || sprite->width <= 0 || sprite->height <= 0) return false;

    GMLReal phase = RValue_toReal(Instance_getSelfVar(self, cache.animsiner)) + 1;
    Instance_setSelfVar(self, cache.animsiner, RValue_makeReal(phase));
    *outResult = RValue_makeUndefined();
    Renderer* renderer = ctx->runner->renderer;
    if (renderer == nullptr) return true;

    int32_t frame = (int32_t)(phase / 16);
    if (frame < 0) frame = (int32_t)self->imageIndex;
    GMLReal x = self->x, y = self->y;
    GMLReal right = (x + RValue_toReal(Instance_getSelfVar(self, cache.totalWidth))) - 1;
    GMLReal bottom = y + 39;
    GMLReal tileWidth = sprite->width, tileHeight = sprite->height;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    for (GMLReal xx = x; NativeScript_compareReals(xx, right, epsilon) <= 0; xx += tileWidth) {
        GMLReal width = tileWidth;
        if (NativeScript_compareReals(right, xx + tileWidth, epsilon) <= 0)
            width = (tileWidth - ((xx + tileWidth) - right)) + 1;
        for (GMLReal yy = y; NativeScript_compareReals(yy, bottom, epsilon) <= 0; yy += tileHeight) {
            GMLReal height = tileHeight;
            if (NativeScript_compareReals(bottom, yy + tileHeight, epsilon) <= 0)
                height = (tileHeight - ((yy + tileHeight) - bottom)) + 1;
            Renderer_drawSpritePartExt(renderer, spriteIndex, frame, 0, 0, width, height,
                xx, yy, 1, 1, 0, 0, 0, 0xFFFFFF, 1);
        }
    }
    return true;
}

void NativeChapter2CyberWallLightsDraw_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_cyber_wall_lights_Draw_0", cyberWallLightsDraw))
        return;
    cache.animsiner = NativeScript_getSelfVarID(ctx, "animsiner");
    cache.totalWidth = NativeScript_getSelfVarID(ctx, "total_width");
}
