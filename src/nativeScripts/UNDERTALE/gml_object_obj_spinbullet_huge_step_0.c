#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "vm_builtins.h"

static bool spinbulletHugeStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        RValue_makeReal(self->imageAngle + 30.0), -1);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSpinbulletHugeStep_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_spinbullet_huge_Step_0", spinbulletHugeStep);
}
