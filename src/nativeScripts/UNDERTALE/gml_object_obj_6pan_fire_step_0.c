#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static bool panFireStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(self->vspeed, 0.0, epsilon) > 0)
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal(-13.0), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VISIBLE, "visible", RValue_makeReal(1.0), -1);
    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
    if (NativeScript_compareReals(self->y, height, epsilon) > 0)
        Runner_destroyInstance(ctx->runner, self, true);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6PanFireStep_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6pan_fire_Step_0", panFireStep);
}
