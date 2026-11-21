#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

static bool flameCollision(VMContext* ctx, RValue* outResult) {
    GMLReal frame = ctx->currentCallFrame->self->imageIndex;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(frame, 3.0, epsilon) >= 0 &&
        NativeScript_compareReals(frame, 18.0, epsilon) <= 0) return false;

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyFlameCollision_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_flame_Collision_1587", flameCollision);
}
