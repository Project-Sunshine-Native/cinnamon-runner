#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static NativeVarBinding sizeBinding = NATIVE_VAR_BINDING_INIT;

static bool bouncersteamStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal size = RValue_toReal(NativeScript_getVariable(ctx, self, &sizeBinding, "size")) + 0.08;
    NativeScript_setVariable(ctx, self, &sizeBinding, "size", RValue_makeReal(size));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(size), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(size), -1);

    self->imageAlpha = (float)((GMLReal)self->imageAlpha - 0.07);
    if (self->imageAlpha < 0.1f) Runner_destroyInstance(ctx->runner, self, true);

    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        RValue_makeReal(self->imageAngle + 6.0), -1);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeBouncersteamStep_register(VMContext* ctx) {
    sizeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_bouncersteam_Step_0", bouncersteamStep);
}
