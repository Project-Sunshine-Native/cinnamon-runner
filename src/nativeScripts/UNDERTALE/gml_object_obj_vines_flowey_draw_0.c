#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t frozen, siner, growth, growth2, growth3, ssx, ssx2, ssx3;
} cache;
static NativeAssetBinding vinesBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding vines2Binding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding vines3Binding = NATIVE_ASSET_BINDING_INIT;

static bool vinesFloweyDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &vinesBinding, "spr_vines_flowey") ||
        !NativeScript_canResolveAsset(ctx, &vines2Binding, "spr_vines_flowey_2") ||
        !NativeScript_canResolveAsset(ctx, &vines3Binding, "spr_vines_flowey_3")) return false;

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    if (RValue_toReal(Instance_getSelfVar(self, cache.frozen)) == 0.0) {
        siner += 1.0;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
    GMLReal cosine = GMLReal_cos(siner / 6.0);
    GMLReal growth = 1.0 + GMLReal_sin(siner / 6.0) * 0.05;
    GMLReal growth2 = 1.0 + cosine * 0.05;
    GMLReal growth3 = 1.0 - GMLReal_sin(siner / 7.0) * 0.05;
    GMLReal ssx = GMLReal_sin(siner / 4.0) * 2.0 * self->imageXscale;
    GMLReal ssx2 = GMLReal_sin(siner / 5.0) * self->imageXscale;
    GMLReal ssx3 = cosine * 0.5 * self->imageXscale;
    Instance_setSelfVar(self, cache.growth, RValue_makeReal(growth));
    Instance_setSelfVar(self, cache.growth2, RValue_makeReal(growth2));
    Instance_setSelfVar(self, cache.growth3, RValue_makeReal(growth3));
    Instance_setSelfVar(self, cache.ssx, RValue_makeReal(ssx));
    Instance_setSelfVar(self, cache.ssx2, RValue_makeReal(ssx2));
    Instance_setSelfVar(self, cache.ssx3, RValue_makeReal(ssx3));

    int32_t frame = (int32_t)self->imageIndex;
    Renderer_drawSpriteExt(renderer, vines3Binding.assetIndex, frame,
        (float)((self->x - ssx3) + 20.0), self->y, self->imageXscale,
        (float)growth3, 0.0f, self->imageBlend, 1.0f);
    Renderer_drawSpriteExt(renderer, vines2Binding.assetIndex, frame,
        (float)(self->x - ssx2 - 20.0), self->y, self->imageXscale,
        (float)growth2, 0.0f, self->imageBlend, 1.0f);
    Renderer_drawSpriteExt(renderer, vinesBinding.assetIndex, frame,
        (float)(self->x - ssx), self->y, self->imageXscale,
        (float)growth, 0.0f, self->imageBlend, 1.0f);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeVinesFloweyDraw_register(VMContext* ctx) {
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.growth = NativeScript_getSelfVarID(ctx, "growth");
    cache.growth2 = NativeScript_getSelfVarID(ctx, "growth2");
    cache.growth3 = NativeScript_getSelfVarID(ctx, "growth3");
    cache.ssx = NativeScript_getSelfVarID(ctx, "ssx");
    cache.ssx2 = NativeScript_getSelfVarID(ctx, "ssx2");
    cache.ssx3 = NativeScript_getSelfVarID(ctx, "ssx3");
    vinesBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    vines2Binding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    vines3Binding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_vines_flowey_Draw_0", vinesFloweyDraw);
}
