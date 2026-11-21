#include "nativeScripts/native_runtime.h"

#include "event_table.h"
#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t maxer, siner, xprev4, yprev4, xprev3, yprev3, xprev2, yprev2, pb, bounces;
} cache;
static NativeAssetBinding shakerBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding impactBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding soundBinding = NATIVE_CALL_BINDING_INIT;

static bool mouthballStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &shakerBinding, "obj_vsflowey_shaker") ||
        !NativeScript_canResolveAsset(ctx, &impactBinding, "snd_impact") ||
        !NativeScript_canCall(ctx, &soundBinding, "gml_Script_snd_play")) return false;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;

    GMLReal maxer = RValue_toReal(Instance_getSelfVar(self, cache.maxer));
    if (NativeScript_compareReals(maxer, 1.1, epsilon) < 0) {
        maxer += 0.1;
        Instance_setSelfVar(self, cache.maxer, RValue_makeReal(maxer));
    }
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    GMLReal scale = maxer + GMLReal_sin(siner / 6.0) * 0.1;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(scale), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(scale), -1);
    Instance_setSelfVar(self, cache.xprev4, Instance_getSelfVar(self, cache.xprev3));
    Instance_setSelfVar(self, cache.yprev4, Instance_getSelfVar(self, cache.yprev3));
    Instance_setSelfVar(self, cache.xprev3, Instance_getSelfVar(self, cache.xprev2));
    Instance_setSelfVar(self, cache.yprev3, Instance_getSelfVar(self, cache.yprev2));
    Instance_setSelfVar(self, cache.xprev2, RValue_makeReal(self->xprevious));
    Instance_setSelfVar(self, cache.yprev2, RValue_makeReal(self->yprevious));
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner + 1.0));
    GMLReal pb = RValue_toReal(Instance_getSelfVar(self, cache.bounces));
    Instance_setSelfVar(self, cache.pb, RValue_makeReal(pb));
    GMLReal bounces = pb;
    GMLReal width = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
    if (NativeScript_compareReals(bounces, 7.0, epsilon) < 0 &&
        NativeScript_compareReals(self->x, width - 20.0, epsilon) > 0 &&
        NativeScript_compareReals(self->hspeed, 0.0, epsilon) > 0) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", RValue_makeReal(-(GMLReal)self->hspeed), -1);
        bounces += 1.0;
        Instance_setSelfVar(self, cache.bounces, RValue_makeReal(bounces));
    }
    if (NativeScript_compareReals(bounces, 7.0, epsilon) < 0 &&
        NativeScript_compareReals(self->x, 20.0, epsilon) < 0 &&
        NativeScript_compareReals(self->hspeed, 0.0, epsilon) < 0) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", RValue_makeReal(-(GMLReal)self->hspeed), -1);
        bounces += 1.0;
        Instance_setSelfVar(self, cache.bounces, RValue_makeReal(bounces));
    }
    if (NativeScript_compareReals(bounces, 7.0, epsilon) < 0 &&
        NativeScript_compareReals(self->vspeed, 0.0, epsilon) > 0 &&
        NativeScript_compareReals(self->y, height - 20.0, epsilon) > 0) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(-(GMLReal)self->vspeed), -1);
        bounces += 1.0;
        Instance_setSelfVar(self, cache.bounces, RValue_makeReal(bounces));
    }
    if (NativeScript_compareReals(bounces, 7.0, epsilon) < 0 &&
        NativeScript_compareReals(self->vspeed, 0.0, epsilon) < 0 &&
        NativeScript_compareReals(self->y, 20.0, epsilon) < 0) {
        bounces += 1.0;
        Instance_setSelfVar(self, cache.bounces, RValue_makeReal(bounces));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(-(GMLReal)self->vspeed), -1);
    }
    if (NativeScript_compareReals(bounces, pb, epsilon) > 0 &&
        VM_findInstanceByTarget(ctx, shakerBinding.assetIndex) == nullptr) {
        Instance* shaker = Runner_createInstance(runner, 0.0, 0.0, shakerBinding.assetIndex);
        if (shaker != nullptr && ctx->creatorVarID >= 0)
            Instance_setSelfVar(shaker, ctx->creatorVarID, RValue_makeReal(self->instanceId));
        RValue sound = RValue_makeReal(impactBinding.assetIndex);
        RValue result = NativeScript_call(ctx, &soundBinding, "gml_Script_snd_play", &sound, 1);
        RValue_free(&result);
    }
    width = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    if (NativeScript_compareReals(self->x, width + 300.0, epsilon) > 0)
        Runner_executeEventFromObject(runner, self, ctx->currentCallFrame->other,
            self->objectIndex, EVENT_OTHER, OTHER_USER0 + 7);
    if (NativeScript_compareReals(self->x, -300.0, epsilon) < 0)
        Runner_executeEventFromObject(runner, self, ctx->currentCallFrame->other,
            self->objectIndex, EVENT_OTHER, OTHER_USER0 + 7);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeMouthballStep_register(VMContext* ctx) {
    cache.maxer = NativeScript_getSelfVarID(ctx, "maxer");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.xprev4 = NativeScript_getSelfVarID(ctx, "xprev4");
    cache.yprev4 = NativeScript_getSelfVarID(ctx, "yprev4");
    cache.xprev3 = NativeScript_getSelfVarID(ctx, "xprev3");
    cache.yprev3 = NativeScript_getSelfVarID(ctx, "yprev3");
    cache.xprev2 = NativeScript_getSelfVarID(ctx, "xprev2");
    cache.yprev2 = NativeScript_getSelfVarID(ctx, "yprev2");
    cache.pb = NativeScript_getSelfVarID(ctx, "pb");
    cache.bounces = NativeScript_getSelfVarID(ctx, "bounces");
    shakerBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    impactBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    soundBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_mouthball_Step_0", mouthballStep);
}
