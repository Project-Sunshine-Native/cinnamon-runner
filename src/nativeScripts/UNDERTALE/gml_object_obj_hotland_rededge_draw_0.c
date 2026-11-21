#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding topEdgeBinding = NATIVE_ASSET_BINDING_INIT;

static bool hotlandRededgeDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &topEdgeBinding, "spr_hotland_rededge_top")) return false;

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));

    float alpha = (float)GMLReal_fabs(GMLReal_sin(siner / 16.0));
    int32_t frame = (int32_t)self->imageIndex;
    int32_t i;
    for (i = 0; (GMLReal)i < self->imageYscale; ++i) {
        int32_t sprite = i == 0 ? topEdgeBinding.assetIndex : self->spriteIndex;
        Renderer_drawSpriteExt(renderer, sprite, frame, self->x, self->y + i * 40.0f,
            1.0f, 1.0f, 0.0f, 0xFFFFFFu, alpha);
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeHotlandRededgeDraw_register(VMContext* ctx) {
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    topEdgeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_hotland_rededge_Draw_0", hotlandRededgeDraw);
}
