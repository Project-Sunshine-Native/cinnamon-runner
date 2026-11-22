#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t stretch, m, myAngleChange;
} cache;

static bool werewireLaserSquareStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    GMLReal epsilon = ctx->runner->mathEpsilon;

    GMLReal stretch = RValue_toReal(Instance_getSelfVar(self, cache.stretch));
    if (NativeScript_compareReals(stretch, 1, epsilon) == 0) {
        GMLReal m = RValue_toReal(Instance_getSelfVar(self, cache.m));
        GMLReal scale = 2 + (self->speed / (10 * m));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            RValue_makeReal(scale), -1);
    }

    if (NativeScript_compareReals(self->x, -100, epsilon) <= 0)
        Runner_destroyInstance(ctx->runner, self, true);

    GMLReal angleChange = RValue_toReal(Instance_getSelfVar(self, cache.myAngleChange));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DIRECTION, "direction",
        RValue_makeReal(self->direction + angleChange), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        RValue_makeReal(self->direction), -1);

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2WerewireLaserSquareStep_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_werewire_bullet_lasersquare_Step_0",
        werewireLaserSquareStep)) return;

    cache.stretch = NativeScript_getSelfVarID(ctx, "stretch");
    cache.m = NativeScript_getSelfVarID(ctx, "m");
    cache.myAngleChange = NativeScript_getSelfVarID(ctx, "my_angle_change");
}
