// taken from https://github.com/Project-Sunshine-Native/cinnamon/tree/UNDERTALE-3DS because i was lazy
#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

static NativeVarBinding timeBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding aaBinding = NATIVE_VAR_BINDING_INIT;

static bool steamplume2Step(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal t = RValue_toReal(NativeScript_getVariable(ctx, self, &timeBinding, "t"));
    GMLReal aa = RValue_toReal(NativeScript_getVariable(ctx, self, &aaBinding, "aa"));

    self->imageXscale += 0.1f;
    self->imageYscale += 0.1f;
    t += 1.0;

    if (t > 7.0) self->imageAlpha -= 0.08f;
    if (self->imageAlpha <= 0.02f) Runner_destroyInstance(ctx->runner, self, true);

    self->imageAngle += (float)aa;
    NativeScript_setVariable(ctx, self, &timeBinding, "t", RValue_makeReal(t));
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeSteamplume2Step_register(VMContext* ctx) {
    timeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    aaBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_steamplume2_Step_0", steamplume2Step);
}
