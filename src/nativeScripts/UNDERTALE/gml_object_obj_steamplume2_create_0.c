// taken from https://github.com/Project-Sunshine-Native/cinnamon/tree/UNDERTALE-3DS because i was lazy
#include "nativeScripts/native_runtime.h"

#include "instance.h"

#include <stdlib.h>

static NativeVarBinding aaBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding timeBinding = NATIVE_VAR_BINDING_INIT;

static GMLReal randomReal(GMLReal maxValue) {
    if (maxValue <= 0.0) return 0.0;
    return ((GMLReal)rand() / ((GMLReal)RAND_MAX + 1.0)) * maxValue;
}

static bool steamplume2Create(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    self->friction = 0.1f;
    self->vspeed = -6.0f;
    self->imageXscale = 0.3f;
    self->imageYscale = 0.3f;
    self->imageAngle = (float)randomReal(360.0);
    self->hspeed = (float)(0.2 - randomReal(0.4));
    Instance_computeSpeedFromComponents(self);

    NativeScript_setVariable(ctx, self, &aaBinding, "aa", RValue_makeReal(2.0 - randomReal(4.0)));
    NativeScript_setVariable(ctx, self, &timeBinding, "t", RValue_makeReal(0.0));
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeSteamplume2Create_register(VMContext* ctx) {
    aaBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    timeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_steamplume2_Create_0", steamplume2Create);
}
