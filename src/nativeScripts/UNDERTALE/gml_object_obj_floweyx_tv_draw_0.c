#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t tvmode, overnoiser, shudder, size, anim, animspeed, animchoice, animtimer, siner;
} cache;
static NativeAssetBinding screenBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding noiseBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding randomBinding = NATIVE_CALL_BINDING_INIT;

static bool floweyTvDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.tvmode)), 0.0, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.overnoiser)), 0.0, epsilon) > 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.shudder)), 0.0, epsilon) > 0) return false;
    if (!NativeScript_canResolveAsset(ctx, &screenBinding, "spr_tvinside_old") ||
        !NativeScript_canResolveAsset(ctx, &noiseBinding, "spr_noise")) return false;

    GMLReal size = RValue_toReal(Instance_getSelfVar(self, cache.size));
    GMLReal anim = RValue_toReal(Instance_getSelfVar(self, cache.anim));
    RValue small = RValue_makeReal(0.1);
    RValue jitter = RValue_makeReal(4.0);
    GMLReal yscale = size + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &small, 1)) + 0.4;
    GMLReal xscale = size + 0.9 + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &small, 1));
    GMLReal y = self->y + 50.0 + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &jitter, 1));
    GMLReal x = self->x + 20.0 + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &jitter, 1));
    int32_t frame = (int32_t)anim;
    if (frame < 0) frame = (int32_t)self->imageIndex;
    Renderer_drawSpriteExt(renderer, screenBinding.assetIndex, frame, (float)x, (float)y,
        (float)xscale, (float)yscale, 0.0f, 0xFFFFFFu, 1.0f);

    GMLReal animspeed = RValue_toReal(Instance_getSelfVar(self, cache.animspeed));
    anim += animspeed;
    Instance_setSelfVar(self, cache.anim, RValue_makeReal(anim));
    GMLReal animchoice = RValue_toReal(Instance_getSelfVar(self, cache.animchoice));
    if (NativeScript_compareReals(anim, animchoice + 1.0, epsilon) > 0) {
        animspeed = -animspeed;
        Instance_setSelfVar(self, cache.animspeed, RValue_makeReal(animspeed));
    }
    if (NativeScript_compareReals(anim, animchoice - 1.0, epsilon) < 0) {
        animspeed = -animspeed;
        Instance_setSelfVar(self, cache.animspeed, RValue_makeReal(animspeed));
    }
    GMLReal animtimer = RValue_toReal(Instance_getSelfVar(self, cache.animtimer)) + 1.0;
    Instance_setSelfVar(self, cache.animtimer, RValue_makeReal(animtimer));
    if (NativeScript_compareReals(animtimer, 100.0, epsilon) > 0) {
        yscale = size + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &small, 1)) + 0.4;
        xscale = size + 0.8 + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &small, 1));
        y = self->y + 50.0 + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &jitter, 1));
        x = self->x + 20.0 + RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &jitter, 1));
        RValue noise = RValue_makeReal(3.0);
        frame = RValue_toInt32(NativeScript_call(ctx, &randomBinding, "random", &noise, 1));
        Renderer_drawSpriteExt(renderer, noiseBinding.assetIndex, frame, (float)x, (float)y,
            (float)xscale, (float)yscale, 0.0f, 0xFFFFFFu, 1.0f);
        RValue choice = RValue_makeReal(100.0);
        animchoice = GMLReal_bankersRound(RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &choice, 1)));
        Instance_setSelfVar(self, cache.animchoice, RValue_makeReal(animchoice));
        Instance_setSelfVar(self, cache.anim, RValue_makeReal(animchoice));
        if (NativeScript_compareReals(animtimer, 106.0, epsilon) > 0)
            Instance_setSelfVar(self, cache.animtimer, RValue_makeReal(0.0));
    }
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    Renderer_drawSprite(renderer, self->spriteIndex, (int32_t)self->imageIndex,
        self->x, (float)(self->y + GMLReal_sin(siner / 3.0)));
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner + 1.0));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyTvDraw_register(VMContext* ctx) {
    cache.tvmode = NativeScript_getSelfVarID(ctx, "tvmode");
    cache.overnoiser = NativeScript_getSelfVarID(ctx, "overnoiser");
    cache.shudder = NativeScript_getSelfVarID(ctx, "shudder");
    cache.size = NativeScript_getSelfVarID(ctx, "size");
    cache.anim = NativeScript_getSelfVarID(ctx, "anim");
    cache.animspeed = NativeScript_getSelfVarID(ctx, "animspeed");
    cache.animchoice = NativeScript_getSelfVarID(ctx, "animchoice");
    cache.animtimer = NativeScript_getSelfVarID(ctx, "animtimer");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    screenBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    noiseBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    randomBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_tv_Draw_0", floweyTvDraw);
}
