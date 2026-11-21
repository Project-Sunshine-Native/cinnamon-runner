#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

static NativeVarBinding turntimerBinding = NATIVE_VAR_BINDING_INIT;

static bool asgorebulparentStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal turntimer = RValue_toReal(NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &turntimerBinding, "turntimer"));
    if (turntimer < 0.0) Runner_destroyInstance(ctx->runner, self, true);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeAsgorebulparentStep_register(VMContext* ctx) {
    turntimerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_asgorebulparent_Step_2", asgorebulparentStep);
}
