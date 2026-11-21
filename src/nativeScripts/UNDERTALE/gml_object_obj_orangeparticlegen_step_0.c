#include "nativeScripts/native_runtime.h"

#include "vm_builtins.h"

static NativeAssetBinding particleBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding randomBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding createBinding = NATIVE_CALL_BINDING_INIT;

static bool orangeparticlegenStep(VMContext* ctx, RValue* outResult) {
    if (!NativeScript_canResolveAsset(ctx, &particleBinding, "obj_orangeparticle")) return false;

    Instance* self = ctx->currentCallFrame->self;
    GMLReal roomWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    GMLReal roomHeight = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
    RValue range = RValue_makeReal(roomWidth + 200.0);
    RValue random = NativeScript_call(ctx, &randomBinding, "random", &range, 1);
    RValue args[3] = {
        RValue_makeReal(-100.0 + RValue_toReal(random)),
        RValue_makeReal(roomHeight + 10.0),
        RValue_makeReal(particleBinding.assetIndex),
    };
    RValue result = NativeScript_call(ctx, &createBinding, "instance_create", args, 3);
    RValue_free(&random);
    RValue_free(&result);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeOrangeparticlegenStep_register(VMContext* ctx) {
    particleBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    randomBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    createBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_orangeparticlegen_Step_0", orangeparticlegenStep);
}
