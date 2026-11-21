#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding jBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding singleTopBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding leftTopBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding middleTopBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding rightTopBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding middleBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding leftMiddleBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding rightMiddleBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding leftBottomBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding middleBottomBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding rightBottomBinding = NATIVE_ASSET_BINDING_INIT;

static bool brightwaterfallDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &singleTopBinding, "spr_waterfall_singletop") ||
        !NativeScript_canResolveAsset(ctx, &leftTopBinding, "spr_waterfall_bright_lt") ||
        !NativeScript_canResolveAsset(ctx, &middleTopBinding, "spr_waterfall_bright_mt") ||
        !NativeScript_canResolveAsset(ctx, &rightTopBinding, "spr_waterfall_bright_rt") ||
        !NativeScript_canResolveAsset(ctx, &middleBinding, "spr_waterfall_bright_mm") ||
        !NativeScript_canResolveAsset(ctx, &leftMiddleBinding, "spr_waterfall_bright_lm") ||
        !NativeScript_canResolveAsset(ctx, &rightMiddleBinding, "spr_waterfall_bright_rm") ||
        !NativeScript_canResolveAsset(ctx, &leftBottomBinding, "spr_waterfall_bright_bl") ||
        !NativeScript_canResolveAsset(ctx, &middleBottomBinding, "spr_waterfall_bright_bm") ||
        !NativeScript_canResolveAsset(ctx, &rightBottomBinding, "spr_waterfall_bright_br")) return false;

    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));

    int32_t frame = (int32_t)(siner / 5.0);
    if (frame < 0) frame = (int32_t)self->imageIndex;
    GMLReal xscale = self->imageXscale;
    GMLReal yscale = self->imageYscale;

    int xscaleRelation = NativeScript_compareReals(xscale, 1.0, epsilon);
    bool tall = NativeScript_compareReals(yscale, 1.0, epsilon) > 0;

    if (xscaleRelation == 0)
        Renderer_drawSprite(renderer, singleTopBinding.assetIndex, frame, self->x, self->y);

    if (xscaleRelation > 0) {
        Renderer_drawSprite(renderer, leftTopBinding.assetIndex, frame, self->x, self->y);

        int32_t i;
        for (i = 1; NativeScript_compareReals(i, xscale + 1.0, epsilon) < 0; ++i) {
            bool middle = NativeScript_compareReals(i, xscale, epsilon) < 0;
            int32_t sprite = middle ? middleTopBinding.assetIndex : rightTopBinding.assetIndex;
            float x = (float)(self->x + i * 20.0 - (middle ? 0.0 : 20.0));
            Renderer_drawSprite(renderer, sprite, frame, x, self->y);
            if (!middle) break;
        }
        NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));
    }

    if (tall && xscaleRelation == 0) {
        int32_t i;
        for (i = 1; NativeScript_compareReals(i, yscale, epsilon) <= 0; ++i)
            Renderer_drawSprite(renderer, middleBinding.assetIndex, frame,
                self->x, (float)(self->y + i * 20.0));
        NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));
    }

    if (tall && xscaleRelation > 0) {
        int32_t j;
        for (j = 1; NativeScript_compareReals(j, yscale, epsilon) <= 0; ++j) {
            bool bottomRow = NativeScript_compareReals(j, yscale, epsilon) == 0;
            int32_t leftSprite = bottomRow ? leftBottomBinding.assetIndex : leftMiddleBinding.assetIndex;
            int32_t middleSprite = bottomRow ? middleBottomBinding.assetIndex : middleBinding.assetIndex;
            int32_t rightSprite = bottomRow ? rightBottomBinding.assetIndex : rightMiddleBinding.assetIndex;
            float y = (float)(self->y + j * 20.0 - (bottomRow ? 20.0 : 0.0));

            Renderer_drawSprite(renderer, leftSprite, frame, self->x, y);

            int32_t i;
            for (i = 1; NativeScript_compareReals(i, xscale, epsilon) <= 0; ++i) {
                bool rightEdge = NativeScript_compareReals(i, xscale, epsilon) == 0;
                int32_t sprite = rightEdge ? rightSprite : middleSprite;
                float x = (float)(self->x + i * 20.0 - (rightEdge ? 20.0 : 0.0));
                Renderer_drawSprite(renderer, sprite, frame, x, y);
            }
            NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));
        }
        NativeScript_setVariable(ctx, self, &jBinding, "j", RValue_makeReal(j));
    }

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeBrightwaterfallDraw_register(VMContext* ctx) {
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    jBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    singleTopBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    leftTopBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    middleTopBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rightTopBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    middleBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    leftMiddleBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rightMiddleBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    leftBottomBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    middleBottomBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rightBottomBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_brightwaterfall_Draw_0", brightwaterfallDraw);
}
