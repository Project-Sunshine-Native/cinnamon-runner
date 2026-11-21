#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

static NativeVarBinding borderBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding leftBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding rightBinding = NATIVE_ASSET_BINDING_INIT;
static struct {
    int32_t l, t, w, h, offx, offy, ll, tt, ww, hh;
} cache;

static bool sidedFireDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &leftBinding, "obj_lborder") ||
        !NativeScript_canResolveAsset(ctx, &rightBinding, "obj_rborder")) return false;
    Instance* leftBorder = VM_findInstanceByTarget(ctx, leftBinding.assetIndex);
    Instance* rightBorder = VM_findInstanceByTarget(ctx, rightBinding.assetIndex);
    if (leftBorder == nullptr || rightBorder == nullptr) return false;

    // draw_self_border_true writes these as instance variables, not locals.
    GMLReal l = 0.0;
    GMLReal t = 0.0;
    GMLReal w = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1));
    GMLReal h = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
    RValue border = NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &borderBinding, "idealborder");
    GMLReal top = RValue_toReal(NativeScript_getIndex(border, RValue_makeReal(2.0)));
    GMLReal bottom = RValue_toReal(NativeScript_getIndex(border, RValue_makeReal(3.0)));
    GMLReal ll = (leftBorder->x - self->x) + 1.0;
    GMLReal tt = (top - self->y) + 1.0;
    GMLReal ww = (self->x + w) - rightBorder->x - 1.0;
    GMLReal hh = (self->y + h) - bottom - 1.0;
    if (ll > 0.0) l += ll;
    if (tt > 0.0) t += tt;
    if (ww > 0.0) w -= ww;
    if (hh > 0.0) h -= hh;
    w = GMLReal_bankersRound(w);
    h = GMLReal_bankersRound(h);
    l = GMLReal_bankersRound(l);
    t = GMLReal_bankersRound(t);

    Instance_setSelfVar(self, cache.l, RValue_makeReal(l));
    Instance_setSelfVar(self, cache.t, RValue_makeReal(t));
    Instance_setSelfVar(self, cache.w, RValue_makeReal(w));
    Instance_setSelfVar(self, cache.h, RValue_makeReal(h));
    Instance_setSelfVar(self, cache.offx, RValue_makeReal(0.0));
    Instance_setSelfVar(self, cache.offy, RValue_makeReal(0.0));
    Instance_setSelfVar(self, cache.ll, RValue_makeReal(ll));
    Instance_setSelfVar(self, cache.tt, RValue_makeReal(tt));
    Instance_setSelfVar(self, cache.ww, RValue_makeReal(ww));
    Instance_setSelfVar(self, cache.hh, RValue_makeReal(hh));

    if (w > 0.0 && h > 0.0 && l < w && t < h) {
        Renderer_drawSpritePart(renderer, self->spriteIndex, (int32_t)self->imageIndex,
            (float)l, (float)t, (float)(w - l), (float)(h - t),
            (float)(self->x + l), (float)(self->y + t));
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSidedFireDraw_register(VMContext* ctx) {
    borderBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    leftBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rightBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    cache.l = NativeScript_getSelfVarID(ctx, "l");
    cache.t = NativeScript_getSelfVarID(ctx, "t");
    cache.w = NativeScript_getSelfVarID(ctx, "w");
    cache.h = NativeScript_getSelfVarID(ctx, "h");
    cache.offx = NativeScript_getSelfVarID(ctx, "offx");
    cache.offy = NativeScript_getSelfVarID(ctx, "offy");
    cache.ll = NativeScript_getSelfVarID(ctx, "ll");
    cache.tt = NativeScript_getSelfVarID(ctx, "tt");
    cache.ww = NativeScript_getSelfVarID(ctx, "ww");
    cache.hh = NativeScript_getSelfVarID(ctx, "hh");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sided_fire_Draw_0", sidedFireDraw);
}
