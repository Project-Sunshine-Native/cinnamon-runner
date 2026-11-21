#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "utils.h"

static struct {
    int32_t frozen, siner, msin, ysin, growth, msin2, ysin2, growth2;
    int32_t msin3, ysin3, growth3, blend3;
} cache;

static bool bgpipeDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    if (RValue_toReal(Instance_getSelfVar(self, cache.frozen)) == 0.0) {
        siner += 1.0;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
    GMLReal sine9 = GMLReal_sin(siner / 9.0);
    GMLReal msin = sine9 * 2.0;
    GMLReal ysin = GMLReal_cos(siner / 8.0);
    GMLReal growth = GMLReal_sin(siner / 7.0) * 0.012;
    GMLReal msin2 = GMLReal_sin(siner / 10.0) * 1.5;
    GMLReal ysin2 = GMLReal_cos(siner / 9.0) * 0.8;
    GMLReal growth2 = GMLReal_sin(siner / 8.0) * 0.01;
    GMLReal msin3 = GMLReal_sin(siner / 11.0);
    GMLReal ysin3 = GMLReal_cos(siner / 10.0) * 0.5;
    GMLReal growth3 = sine9 * 0.005;
    uint32_t blend3 = (uint32_t)Color_lerp((int32_t)self->imageBlend, 0, 0.4f);
    Instance_setSelfVar(self, cache.msin, RValue_makeReal(msin));
    Instance_setSelfVar(self, cache.ysin, RValue_makeReal(ysin));
    Instance_setSelfVar(self, cache.growth, RValue_makeReal(growth));
    Instance_setSelfVar(self, cache.msin2, RValue_makeReal(msin2));
    Instance_setSelfVar(self, cache.ysin2, RValue_makeReal(ysin2));
    Instance_setSelfVar(self, cache.growth2, RValue_makeReal(growth2));
    Instance_setSelfVar(self, cache.msin3, RValue_makeReal(msin3));
    Instance_setSelfVar(self, cache.ysin3, RValue_makeReal(ysin3));
    Instance_setSelfVar(self, cache.growth3, RValue_makeReal(growth3));
    Instance_setSelfVar(self, cache.blend3, RValue_makeReal(blend3));

    int32_t frame = (int32_t)self->imageIndex;
    Renderer_drawSpriteExt(renderer, self->spriteIndex, frame,
        (float)(self->x + (msin3 - 60.0) * self->imageXscale),
        (float)((self->y + ysin3) - 20.0), (float)(growth3 + self->imageXscale),
        1.0f, 0.0f, blend3, 1.0f);
    Renderer_drawSpriteExt(renderer, self->spriteIndex, frame,
        (float)(self->x + (msin2 - 40.0) * self->imageXscale),
        (float)((self->y + ysin2) - 10.0), (float)(growth2 + self->imageXscale),
        1.0f, 0.0f, blend3, 1.0f);
    Renderer_drawSpriteExt(renderer, self->spriteIndex, frame,
        (float)(self->x + msin * self->imageXscale), (float)(self->y + ysin),
        (float)(growth + self->imageXscale), 1.0f, 0.0f, self->imageBlend, 1.0f);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeBgpipeDraw_register(VMContext* ctx) {
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.msin = NativeScript_getSelfVarID(ctx, "msin");
    cache.ysin = NativeScript_getSelfVarID(ctx, "ysin");
    cache.growth = NativeScript_getSelfVarID(ctx, "growth");
    cache.msin2 = NativeScript_getSelfVarID(ctx, "msin2");
    cache.ysin2 = NativeScript_getSelfVarID(ctx, "ysin2");
    cache.growth2 = NativeScript_getSelfVarID(ctx, "growth2");
    cache.msin3 = NativeScript_getSelfVarID(ctx, "msin3");
    cache.ysin3 = NativeScript_getSelfVarID(ctx, "ysin3");
    cache.growth3 = NativeScript_getSelfVarID(ctx, "growth3");
    cache.blend3 = NativeScript_getSelfVarID(ctx, "blend3");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_bgpipe_Draw_0", bgpipeDraw);
}
