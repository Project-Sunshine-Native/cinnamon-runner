#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t desperate, acon, frozen, siner, growth, ssx, ssy, xr;
} cache;

static bool floweyarmDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (RValue_toReal(Instance_getSelfVar(self, cache.acon)) != 0.0) return false;

    GMLReal desperate = RValue_toReal(Instance_getSelfVar(self, cache.desperate));
    if (desperate == 0.0 || desperate == 1.0) {
        GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
        if (RValue_toReal(Instance_getSelfVar(self, cache.frozen)) == 0.0) {
            siner += desperate == 0.0 ? 0.8 : 1.2;
            Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
        }
        GMLReal growth = 1.0 + GMLReal_cos(siner / 6.0) * 0.03;
        GMLReal ssx = GMLReal_sin(siner / 3.0) * (desperate == 0.0 ? 4.0 : 5.0) * self->imageXscale;
        GMLReal ssy = GMLReal_cos(siner / 3.0) * (desperate == 0.0 ? 4.0 : 8.0);
        Instance_setSelfVar(self, cache.growth, RValue_makeReal(growth));
        Instance_setSelfVar(self, cache.ssx, RValue_makeReal(ssx));
        Instance_setSelfVar(self, cache.ssy, RValue_makeReal(ssy));
        GMLReal y = self->y + ssy;
        if (desperate == 1.0) y += 2.0;
        Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
            (float)(self->x - ssx), (float)y, self->imageXscale,
            (float)growth, 0.0f, self->imageBlend, 1.0f);
    }
    Instance_setSelfVar(self, cache.xr, RValue_makeReal(-(GMLReal)self->imageXscale));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyarmDraw_register(VMContext* ctx) {
    cache.desperate = NativeScript_getSelfVarID(ctx, "desperate");
    cache.acon = NativeScript_getSelfVarID(ctx, "acon");
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.growth = NativeScript_getSelfVarID(ctx, "growth");
    cache.ssx = NativeScript_getSelfVarID(ctx, "ssx");
    cache.ssy = NativeScript_getSelfVarID(ctx, "ssy");
    cache.xr = NativeScript_getSelfVarID(ctx, "xr");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyarm_Draw_0", floweyarmDraw);
}
