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
    int32_t siner;
    int32_t factor;
} cache;

static bool lightfairyDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    GMLReal factor = RValue_toReal(Instance_getSelfVar(self, cache.factor));
    self->imageAlpha = (float)(GMLReal_sin(siner / 26) * factor);
    *outResult = RValue_makeUndefined();
#ifdef PLATFORM_3DS
    if (self->imageAlpha <= 0.0f) return true;
#endif

    int32_t frame = (int32_t)(siner / 10);
    if (frame < 0) frame = (int32_t)self->imageIndex;
    float scale = (float)(3 + GMLReal_sin(siner / 12));
    Renderer_drawSpriteExt(renderer, self->spriteIndex, frame, self->x, self->y,
        scale, scale, 0, 0xFFFFFF, self->imageAlpha);
    return true;
}

void NativeLightfairyDraw_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_lightfairy_Draw_0", lightfairyDraw)) return;

    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.factor = NativeScript_getSelfVarID(ctx, "factor");
}
