#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t con, laugh, onoff, frozen, desperate, siner, anim, op, mode, rotbonus, xbonus, ybonus;
} cache;
static NativeVarBinding debugBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding beamBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding bridgeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding edgeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding lipBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding dimpleBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding rectangleBinding = NATIVE_CALL_BINDING_INIT;

static bool floweyMouthDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.con)), 0.0, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.laugh)), 0.0, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(NativeScript_getVariable(ctx, ctx->globalScopeInstance,
            &debugBinding, "debug")), 1.0, epsilon) == 0) return false;
    if (!NativeScript_canResolveAsset(ctx, &beamBinding, "obj_mouthbeam") ||
        !NativeScript_canResolveAsset(ctx, &bridgeBinding, "spr_floweyx_mouthbridge") ||
        !NativeScript_canResolveAsset(ctx, &edgeBinding, "spr_floweyx_mouthedge") ||
        !NativeScript_canResolveAsset(ctx, &lipBinding, "spr_floweyx_mouthl") ||
        !NativeScript_canResolveAsset(ctx, &dimpleBinding, "spr_floweyx_dimple") ||
        !NativeScript_canCall(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle")) return false;

    GMLReal onoff = RValue_toReal(Instance_getSelfVar(self, cache.onoff)) + 1.0;
    if (NativeScript_compareReals(onoff, 3.0, epsilon) > 0) onoff = 0.0;
    Instance_setSelfVar(self, cache.onoff, RValue_makeReal(onoff));
    renderer->drawColor = 0;
    GMLReal mode = RValue_toReal(Instance_getSelfVar(self, cache.mode));
    bool closed = NativeScript_compareReals(mode, 0.0, epsilon) == 0;
    bool open = NativeScript_compareReals(mode, 1.0, epsilon) == 0;
    bool wide = NativeScript_compareReals(mode, 2.0, epsilon) == 0;
    if (VM_findInstanceByTarget(ctx, beamBinding.assetIndex) == nullptr && (closed || open || wide)) {
        GMLReal left = self->x;
        GMLReal right = self->x + 60.0;
        if (open) {
            left = self->x - 10.0;
            right = self->x + 70.0;
        }
        if (wide) {
            left = self->x - 18.0;
            right = self->x + 78.0;
        }
        RValue args[4] = {
            RValue_makeReal(left), RValue_makeReal(self->y + 8.0),
            RValue_makeReal(right), RValue_makeReal(self->y + 110.0),
        };
        RValue result = NativeScript_call(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle", args, 4);
        RValue_free(&result);
    }
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    GMLReal anim = RValue_toReal(Instance_getSelfVar(self, cache.anim));
    GMLReal desperate = RValue_toReal(Instance_getSelfVar(self, cache.desperate));
    bool normal = NativeScript_compareReals(desperate, 0.0, epsilon) == 0;
    bool urgent = NativeScript_compareReals(desperate, 1.0, epsilon) == 0;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.frozen)), 0.0, epsilon) == 0) {
        siner += 1.0;
        if (urgent) siner += 0.4;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
        anim += 0.25;
        Instance_setSelfVar(self, cache.anim, RValue_makeReal(anim));
    }
    Instance_setSelfVar(self, cache.op, RValue_makeReal(60.0));
    int32_t frame = (int32_t)self->imageIndex;
    Renderer_drawSpriteExt(renderer, bridgeBinding.assetIndex, frame, (float)(self->x + 10.0),
        self->y, 1.0f, 1.0f, 0.0f, self->imageBlend, self->imageAlpha);

    GMLReal rotbonus = RValue_toReal(Instance_getSelfVar(self, cache.rotbonus));
    GMLReal xbonus = RValue_toReal(Instance_getSelfVar(self, cache.xbonus));
    GMLReal ybonus = RValue_toReal(Instance_getSelfVar(self, cache.ybonus));
    if (closed) {
        rotbonus = NativeScript_compareReals(rotbonus, 0.0, epsilon) > 0 ? rotbonus - 5.0 : 0.0;
        xbonus = NativeScript_compareReals(xbonus, 0.0, epsilon) > 0 ? xbonus - 2.0 : 0.0;
        ybonus = 0.0;
    }
    if (open) {
        if (NativeScript_compareReals(ybonus, -4.0, epsilon) > 0) ybonus -= 2.0;
        if (NativeScript_compareReals(xbonus, 6.0, epsilon) < 0) xbonus += 2.0;
        if (NativeScript_compareReals(rotbonus, 15.0, epsilon) < 0) rotbonus += 5.0;
    }
    if (wide) {
        if (NativeScript_compareReals(ybonus, -8.0, epsilon) > 0) ybonus -= 4.0;
        if (NativeScript_compareReals(xbonus, 6.0, epsilon) < 0) xbonus += 2.0;
        if (NativeScript_compareReals(rotbonus, 24.0, epsilon) < 0) rotbonus += 8.0;
    }
    if (closed || open || wide) {
        Instance_setSelfVar(self, cache.rotbonus, RValue_makeReal(rotbonus));
        Instance_setSelfVar(self, cache.xbonus, RValue_makeReal(xbonus));
        Instance_setSelfVar(self, cache.ybonus, RValue_makeReal(ybonus));
    }
    GMLReal sine = GMLReal_sin(siner / 2.0);
    GMLReal cosine = GMLReal_cos(siner / 2.0);
    GMLReal sine4 = GMLReal_sin(siner / 4.0);
    if (normal || urgent) {
        GMLReal offset = sine * (normal ? 3.0 : 4.0);
        GMLReal edgeScale = 1.0 + sine4 * (normal ? 0.03 : 0.06);
        GMLReal lipScale = 1.0 + sine4 * (normal ? 0.03 : 0.05);
        GMLReal leftAngle = 0.0 - cosine - rotbonus;
        GMLReal rightAngle = 0.0 + cosine + rotbonus;
        Renderer_drawSpriteExt(renderer, edgeBinding.assetIndex, frame,
            (float)(((self->x + offset) - 20.0) + xbonus), (float)(((self->y + cosine) - 5.0) + ybonus),
            1.0f, (float)edgeScale, (float)leftAngle, self->imageBlend, 1.0f);
        Renderer_drawSpriteExt(renderer, lipBinding.assetIndex, frame,
            (float)(self->x + offset + xbonus), (float)(self->y + cosine + ybonus),
            1.0f, (float)lipScale, (float)leftAngle, self->imageBlend, 1.0f);
        Renderer_drawSpriteExt(renderer, edgeBinding.assetIndex, frame,
            (float)(((self->x - offset) + 60.0 + 20.0) - xbonus), (float)(((self->y + cosine) - 5.0) + ybonus),
            -1.0f, (float)edgeScale, (float)rightAngle, self->imageBlend, 1.0f);
        Renderer_drawSpriteExt(renderer, lipBinding.assetIndex, frame,
            (float)((self->x + 60.0) - offset - xbonus), (float)(self->y + cosine + ybonus),
            -1.0f, (float)lipScale, (float)rightAngle, self->imageBlend, 1.0f);
    }
    int32_t animFrame = (int32_t)anim;
    if (animFrame < 0) animFrame = frame;
    Renderer_drawSpriteExt(renderer, dimpleBinding.assetIndex, animFrame,
        (float)(((self->x + sine * 3.0) - 5.0) + xbonus), (float)(self->y - 10.0),
        1.0f, (float)(1.0 + sine4 * 0.03), (float)(0.0 + cosine), self->imageBlend, 1.0f);
    Renderer_drawSpriteExt(renderer, dimpleBinding.assetIndex, animFrame,
        (float)((((self->x + 60.0) - sine * 3.0) + 5.0) - xbonus), (float)(self->y - 10.0),
        -1.0f, (float)(1.0 + sine4 * 0.03), (float)(0.0 + cosine), self->imageBlend, 1.0f);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyMouthDraw_register(VMContext* ctx) {
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.laugh = NativeScript_getSelfVarID(ctx, "laugh");
    cache.onoff = NativeScript_getSelfVarID(ctx, "onoff");
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.desperate = NativeScript_getSelfVarID(ctx, "desperate");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.anim = NativeScript_getSelfVarID(ctx, "anim");
    cache.op = NativeScript_getSelfVarID(ctx, "op");
    cache.mode = NativeScript_getSelfVarID(ctx, "mode");
    cache.rotbonus = NativeScript_getSelfVarID(ctx, "rotbonus");
    cache.xbonus = NativeScript_getSelfVarID(ctx, "xbonus");
    cache.ybonus = NativeScript_getSelfVarID(ctx, "ybonus");
    debugBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    beamBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    bridgeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    edgeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    lipBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    dimpleBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rectangleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_mouth_Draw_0", floweyMouthDraw);
}
