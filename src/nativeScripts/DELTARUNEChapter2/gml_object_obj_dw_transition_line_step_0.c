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
    int32_t obj;
    int32_t spacer;
} cache;

static bool dwTransitionLineStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    if (NativeScript_compareReals(self->y, 400, epsilon) >= 0) {
        Runner_destroyInstance(runner, self, true);
    }

    Instance_setSelfVar(self, cache.obj, RValue_makeReal(self->instanceId));
    Instance_setSelfVar(self, cache.spacer, RValue_makeReal(400));

    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    GMLReal viewX = camera != nullptr ? (GMLReal)camera->viewX : -1;
    GMLReal viewY = camera != nullptr ? (GMLReal)camera->viewY : -1;
    GMLReal width = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1));
    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));

    if (NativeScript_compareReals((GMLReal)self->x + width + 400, viewX, epsilon) < 0 ||
        NativeScript_compareReals((GMLReal)self->x - 400, viewX + 640, epsilon) > 0 ||
        NativeScript_compareReals((GMLReal)self->y + height + 400, viewY, epsilon) < 0 ||
        NativeScript_compareReals((GMLReal)self->y - 400, viewY + 480, epsilon) > 0) {
        Runner_destroyInstance(runner, self, true);
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2DwTransitionLineStep_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_dw_transition_line_Step_0", dwTransitionLineStep)) return;

    cache.obj = NativeScript_getSelfVarID(ctx, "obj");
    cache.spacer = NativeScript_getSelfVarID(ctx, "spacer");
}
