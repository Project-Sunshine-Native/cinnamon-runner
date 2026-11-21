#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t siner, type, counter;
} cache;

static bool shoePartStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner)) + 1.0;
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    GMLReal type = RValue_toReal(Instance_getSelfVar(self, cache.type));
    GMLReal counter = RValue_toReal(Instance_getSelfVar(self, cache.counter));
    if (NativeScript_compareReals(type, 0.0, epsilon) == 0) {
        if (NativeScript_compareReals(counter, 0.0, epsilon) > 0 && NativeScript_compareReals(counter, 14.0, epsilon) < 0)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(-6.0), -1);
        if (NativeScript_compareReals(counter, 14.0, epsilon) == 0)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(0.0), -1);
        if (NativeScript_compareReals(counter, 20.0, epsilon) > 0 && NativeScript_compareReals(counter, 30.0, epsilon) < 0)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(8.0), -1);
        if (NativeScript_compareReals(counter, 30.0, epsilon) == 0)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(0.0), -1);
        if (NativeScript_compareReals(counter, 40.0, epsilon) > 0) counter = 1.0;
    }
    if (NativeScript_compareReals(type, 1.0, epsilon) == 0) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(0.0), -1);
        GMLReal speed = NativeScript_compareReals(self->speed, 0.0, epsilon) > 0
            ? (GMLReal)self->speed - 0.2 : 0.0;
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPEED, "speed", RValue_makeReal(speed), -1);
    }
    Instance_setSelfVar(self, cache.counter, RValue_makeReal(counter + 1.0));
    if (NativeScript_compareReals(self->x, -100.0, epsilon) < 0)
        Runner_destroyInstance(ctx->runner, self, true);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6ShoePartStep_register(VMContext* ctx) {
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.type = NativeScript_getSelfVarID(ctx, "type");
    cache.counter = NativeScript_getSelfVarID(ctx, "counter");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6shoe_part_Step_0", shoePartStep);
}
