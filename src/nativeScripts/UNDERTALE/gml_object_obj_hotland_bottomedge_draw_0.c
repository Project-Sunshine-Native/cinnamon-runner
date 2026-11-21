#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding leftEdgeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding rightEdgeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding bottomEdgeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding blackPixelBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding fireRoomBinding = NATIVE_ASSET_BINDING_INIT;

static bool hotlandBottomedgeDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &leftEdgeBinding, "spr_hotland_leftedge") ||
        !NativeScript_canResolveAsset(ctx, &rightEdgeBinding, "spr_hotland_rightedge") ||
        !NativeScript_canResolveAsset(ctx, &bottomEdgeBinding, "spr_hotland_bottomedge") ||
        !NativeScript_canResolveAsset(ctx, &blackPixelBinding, "spr_singleblackpixel") ||
        !NativeScript_canResolveAsset(ctx, &fireRoomBinding, "room_fire_walkandbranch2")) return false;

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));
    int32_t frame = (int32_t)(siner / 2.0);
    if (frame < 0) frame = (int32_t)self->imageIndex;

    GMLReal xscale = self->imageXscale;
    int32_t i;
    if (xscale >= 0.0) {
        for (i = 0; (GMLReal)i < xscale; ++i) {
            int32_t sprite = bottomEdgeBinding.assetIndex;
            if (i == 0) sprite = leftEdgeBinding.assetIndex;
            else if (NativeScript_compareReals((GMLReal)i, xscale - 1.0, runner->mathEpsilon) == 0)
                sprite = rightEdgeBinding.assetIndex;
            Renderer_drawSprite(renderer, sprite, frame, (float)(self->x + i * 20.0), self->y);
        }
        renderer->drawColor = 0x000000u;
        if (runner->currentRoomIndex == fireRoomBinding.assetIndex) {
            Renderer_drawSpriteExt(renderer, blackPixelBinding.assetIndex, 0,
                self->x, self->y + 19.0f, (float)(20.0 * xscale), 1.0f, 0.0f, 0xFFFFFFu, 1.0f);
        }
    } else {
        Renderer_drawSprite(renderer, rightEdgeBinding.assetIndex, frame, self->x - 20.0f, self->y);
        for (i = 1; (GMLReal)i < -xscale; ++i) {
            Renderer_drawSprite(renderer, bottomEdgeBinding.assetIndex, frame,
                (float)(self->x - i * 20.0 - 20.0), self->y);
        }
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeHotlandBottomedgeDraw_register(VMContext* ctx) {
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    leftEdgeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rightEdgeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    bottomEdgeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    blackPixelBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    fireRoomBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_hotland_bottomedge_Draw_0", hotlandBottomedgeDraw);
}
