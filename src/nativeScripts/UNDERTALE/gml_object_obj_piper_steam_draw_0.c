// taken from https://github.com/Project-Sunshine-Native/cinnamon/tree/UNDERTALE-3DS because i was lazy
#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding timerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding liftBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding directionBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding bottomBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding topBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding plumeBinding = NATIVE_ASSET_BINDING_INIT;

static bool piperSteamDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr || runner->renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &bottomBinding, "spr_steamer_bottom") ||
        !NativeScript_canResolveAsset(ctx, &topBinding, "spr_steamer_top") ||
        !NativeScript_canResolveAsset(ctx, &plumeBinding, "obj_steamplume2")) return false;

    GMLReal timer = RValue_toReal(NativeScript_getVariable(ctx, self, &timerBinding, "timer"));
    GMLReal f_l = RValue_toReal(NativeScript_getVariable(ctx, self, &liftBinding, "f_l"));

    timer += 1.0;
    Renderer_drawSprite(runner->renderer, bottomBinding.assetIndex, 0, self->x, self->y);
    Renderer_drawSprite(runner->renderer, topBinding.assetIndex, 0, self->x, self->y + (float)(f_l * 3.0));

    if (timer == 30.0) {
        f_l = 0.0;
        NativeScript_setVariable(ctx, self, &directionBinding, "f_d", RValue_makeReal(1.0));
    }

    if (timer > 30.0 && 50.0 > timer) {
        Instance* plume = Runner_createInstance(runner, self->x + 7.0, self->y + 6.0 + f_l * 3.0,
            plumeBinding.assetIndex);
        if (plume != nullptr && ctx->creatorVarID >= 0) {
            Instance_setSelfVar(plume, ctx->creatorVarID, RValue_makeReal((GMLReal)self->instanceId));
        }

        f_l += 0.3;
        if (f_l >= 3.0) timer = 50.0;
    }

    if (timer >= 50.0 && 90.0 > timer) {
        f_l -= 0.1;
        if (f_l <= 0.0) {
            f_l = 0.0;
            timer = 25.0;
        }
    }

    NativeScript_setVariable(ctx, self, &timerBinding, "timer", RValue_makeReal(timer));
    NativeScript_setVariable(ctx, self, &liftBinding, "f_l", RValue_makeReal(f_l));
    *outResult = RValue_makeUndefined();
    return true;
}

void NativePiperSteamDraw_register(VMContext* ctx) {
    timerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    liftBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    directionBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    bottomBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    topBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    plumeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_piper_steam_Draw_0", piperSteamDraw);
}
