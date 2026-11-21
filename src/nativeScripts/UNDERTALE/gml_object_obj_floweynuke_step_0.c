#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

static int32_t faderVar;

static bool floweynukeStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, faderVar)), 1.0, epsilon) == 0) {
        self->imageAlpha = (float)((GMLReal)self->imageAlpha - 0.2);
        if (NativeScript_compareReals(self->imageAlpha, 0.4, epsilon) < 0)
            Runner_destroyInstance(ctx->runner, self, true);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweynukeStep_register(VMContext* ctx) {
    faderVar = NativeScript_getSelfVarID(ctx, "fader");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweynuke_Step_0", floweynukeStep);
}
