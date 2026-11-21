#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t shakeboy, shake;
} cache;
static NativeCallBinding randomBinding = NATIVE_CALL_BINDING_INIT;

static bool glovePartStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.shakeboy)),
        1.0, ctx->runner->mathEpsilon) == 0) {
        GMLReal shake = RValue_toReal(Instance_getSelfVar(self, cache.shake)) + 0.2;
        Instance_setSelfVar(self, cache.shake, RValue_makeReal(shake));
        RValue arg = RValue_makeReal(shake);
        GMLReal x1 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        GMLReal x2 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(self->x + (x1 - x2)), -1);
        GMLReal y1 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        GMLReal y2 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + (y1 - y2)), -1);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6GlovePartStep_register(VMContext* ctx) {
    cache.shakeboy = NativeScript_getSelfVarID(ctx, "shakeboy");
    cache.shake = NativeScript_getSelfVarID(ctx, "shake");
    randomBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6glove_part_Step_0", glovePartStep);
}
