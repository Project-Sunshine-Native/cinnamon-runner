#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static bool healStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(self->imageAlpha, 1.0, epsilon) < 0)
        self->imageAlpha = (float)((GMLReal)self->imageAlpha + 0.1);
    if (NativeScript_compareReals(self->y, 540.0, epsilon) > 0)
        Runner_destroyInstance(ctx->runner, self, true);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        RValue_makeReal(self->imageAngle + 1.0), -1);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6HealStep_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6heal_Step_0", healStep);
}
