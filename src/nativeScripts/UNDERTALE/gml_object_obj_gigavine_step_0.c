#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t active, con, memorymode;
} cache;
static NativeCallBinding colorBinding = NATIVE_CALL_BINDING_INIT;

static bool gigavineStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.active)), 1.0, epsilon) == 0) {
        GMLReal con = RValue_toReal(Instance_getSelfVar(self, cache.con));
        if (NativeScript_compareReals(self->imageXscale, 1.0, epsilon) < 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
                RValue_makeReal((GMLReal)self->imageXscale + 0.08), -1);
            if (NativeScript_compareReals(self->imageXscale, 0.2, epsilon) > 0)
                VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VISIBLE, "visible", RValue_makeReal(1.0), -1);
        } else if (NativeScript_compareReals(con, 0.0, epsilon) == 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(1.0), -1);
            con = 2.0;
            Instance_setSelfVar(self, cache.con, RValue_makeReal(con));
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(10.0), 4);
        }
        if (NativeScript_compareReals(con, 3.0, epsilon) == 0) {
            con = 4.0;
            Instance_setSelfVar(self, cache.con, RValue_makeReal(con));
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(3.0), 4);
        }
        if (NativeScript_compareReals(con, 4.0, epsilon) == 0) {
            GMLReal component = 160.0 + (GMLReal)self->alarm[4] * (70.0 / 3.0);
            RValue args[3] = {RValue_makeReal(255.0), RValue_makeReal(component), RValue_makeReal(component)};
            RValue result = NativeScript_call(ctx, &colorBinding, "make_color_rgb", args, 3);
            self->imageBlend = RValue_toColour(result);
            RValue_free(&result);
        }
        if (NativeScript_compareReals(con, 5.0, epsilon) == 0) {
            self->imageAlpha = (float)((GMLReal)self->imageAlpha - 0.2);
            if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.memorymode)), 0.0, epsilon) == 0 &&
                NativeScript_compareReals(self->imageAlpha, 0.1, epsilon) < 0)
                Runner_destroyInstance(ctx->runner, self, true);
            if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.memorymode)), 1.0, epsilon) == 0 &&
                NativeScript_compareReals(self->imageAlpha, 0.1, epsilon) < 0) {
                VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_VISIBLE, "visible", RValue_makeReal(0.0), -1);
                Instance_setSelfVar(self, cache.active, RValue_makeReal(0.0));
            }
        }
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeGigavineStep_register(VMContext* ctx) {
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.memorymode = NativeScript_getSelfVarID(ctx, "memorymode");
    colorBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_gigavine_Step_0", gigavineStep);
}
