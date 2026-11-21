#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "vm_builtins.h"

static int32_t sizeVar, rpVar;
static NativeAssetBinding soundBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding stopBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding playBinding = NATIVE_CALL_BINDING_INIT;

static bool flameCreate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (!NativeScript_canResolveAsset(ctx, &soundBinding, "snd_flameloop") ||
        !NativeScript_canCall(ctx, &stopBinding, "gml_Script_snd_stop") ||
        !NativeScript_canCall(ctx, &playBinding, "gml_Script_snd_play")) return false;

    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(self->direction), -1);
    self->imageSpeed = 1.0f;
    Instance_setSelfVar(self, sizeVar, RValue_makeReal(1.0));
    Instance_setSelfVar(self, rpVar, RValue_makeReal(0.0));
    RValue sound = RValue_makeReal(soundBinding.assetIndex);
    RValue result = NativeScript_call(ctx, &stopBinding, "gml_Script_snd_stop", &sound, 1);
    RValue_free(&result);
    result = NativeScript_call(ctx, &playBinding, "gml_Script_snd_play", &sound, 1);
    RValue_free(&result);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyFlameCreate_register(VMContext* ctx) {
    sizeVar = NativeScript_getSelfVarID(ctx, "size");
    rpVar = NativeScript_getSelfVarID(ctx, "rp");
    soundBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    stopBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    playBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_flame_Create_0", flameCreate);
}
