#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "vm_builtins.h"

static int32_t sinerVar;

static bool shoeMusicnoteStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, sinerVar)) + 1.0;
    Instance_setSelfVar(self, sinerVar, RValue_makeReal(siner));
    GMLReal phase = siner / 3.0;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
        RValue_makeReal(self->x + GMLReal_sin(phase)), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
        RValue_makeReal(self->y + GMLReal_cos(phase)), -1);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6ShoeMusicnoteStep_register(VMContext* ctx) {
    sinerVar = NativeScript_getSelfVarID(ctx, "siner");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6shoe_musicnote_Step_0", shoeMusicnoteStep);
}
