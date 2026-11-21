#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static int32_t sizeVar;

static bool handforkStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal size = RValue_toReal(Instance_getSelfVar(self, sizeVar));
    if (NativeScript_compareReals(size, 1.0, epsilon) < 0) {
        size += 0.025;
        Instance_setSelfVar(self, sizeVar, RValue_makeReal(size));
    }
    if (NativeScript_compareReals(self->hspeed, 0.0, epsilon) > 0)
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(size), -1);
    if (NativeScript_compareReals(self->hspeed, 0.0, epsilon) < 0)
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(-size), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(size), -1);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeHandforkStep_register(VMContext* ctx) {
    sizeVar = NativeScript_getSelfVarID(ctx, "size");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_f_handfork_Step_0", handforkStep);
}
