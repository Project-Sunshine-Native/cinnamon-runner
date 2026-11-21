#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static int32_t sizeVar;

static bool eyeshotStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        RValue_makeReal(self->imageAngle + 10.0), -1);
    GMLReal size = RValue_toReal(Instance_getSelfVar(self, sizeVar));
    if (NativeScript_compareReals(size, 1.0, ctx->runner->mathEpsilon) < 0) {
        size += 0.1;
        Instance_setSelfVar(self, sizeVar, RValue_makeReal(size));
    }
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(size), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(size), -1);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeEyeshotStep_register(VMContext* ctx) {
    sizeVar = NativeScript_getSelfVarID(ctx, "size");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_eyeshot_a_Step_0", eyeshotStep);
}
