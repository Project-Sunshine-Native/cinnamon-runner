#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t frozen, siner, desperate;
} cache;
static NativeAssetBinding nostrilsBinding = NATIVE_ASSET_BINDING_INIT;

static bool fleshfaceDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &nostrilsBinding, "spr_nostrils")) return false;

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    if (RValue_toReal(Instance_getSelfVar(self, cache.frozen)) == 0.0) {
        siner += 1.2;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
    GMLReal desperate = RValue_toReal(Instance_getSelfVar(self, cache.desperate));
    if (desperate == 0.0 || desperate == 1.0) {
        if (desperate == 0.0) {
            siner -= 0.2;
            Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
        }
        GMLReal sine = GMLReal_sin(siner / 2.0);
        Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
            self->x, (float)(self->y + sine * 3.0), 1.0f, 1.0f, 0.0f, self->imageBlend, 1.0f);
        int32_t frame = (int32_t)(siner / 8.0);
        if (frame < 0) frame = (int32_t)self->imageIndex;
        GMLReal nostrilY = desperate == 0.0
            ? self->y + 12.0 + sine * 6.0 : self->y + 14.0 + sine * 7.0;
        Renderer_drawSpriteExt(renderer, nostrilsBinding.assetIndex, frame,
            (float)(self->x + 32.0), (float)nostrilY, 1.0f, 1.0f, 0.0f, self->imageBlend, 1.0f);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFleshfaceDraw_register(VMContext* ctx) {
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.desperate = NativeScript_getSelfVarID(ctx, "desperate");
    nostrilsBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_fleshface_Draw_0", fleshfaceDraw);
}
