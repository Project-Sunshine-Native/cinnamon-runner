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
    int32_t autoX, autoY, ss, ssy;
} cache;

static bool parallaxUpdate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLCamera* camera = Runner_getCameraForView(ctx->runner, 0);
    GMLReal viewX = camera != nullptr ? camera->viewX : -1;
    GMLReal viewY = camera != nullptr ? camera->viewY : -1;
    GMLReal x = ((viewX + RValue_toReal(Instance_getSelfVar(self, cache.autoX))) *
        RValue_toReal(Instance_getSelfVar(self, cache.ss))) + self->xstart;
    GMLReal y = ((viewY + RValue_toReal(Instance_getSelfVar(self, cache.autoY))) *
        RValue_toReal(Instance_getSelfVar(self, cache.ssy))) + self->ystart;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(x), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(y), -1);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2ParallaxUpdate_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_parallaxer_layer_cyber_Other_10", parallaxUpdate))
        return;
    cache.autoX = NativeScript_getSelfVarID(ctx, "auto");
    cache.autoY = NativeScript_getSelfVarID(ctx, "autoy");
    cache.ss = NativeScript_getSelfVarID(ctx, "ss");
    cache.ssy = NativeScript_getSelfVarID(ctx, "ssy");
}
