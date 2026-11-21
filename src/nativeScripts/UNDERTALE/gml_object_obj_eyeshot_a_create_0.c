#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "vm_builtins.h"

static struct {
    int32_t size, saved, active, memorymode;
} cache;

static bool eyeshotCreate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(0.0), -1);
    Instance_setSelfVar(self, cache.size, RValue_makeReal(0.1));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(0.1), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(0.1), -1);
    Instance_setSelfVar(self, cache.saved, RValue_makeReal(0.0));
    Instance_setSelfVar(self, cache.active, RValue_makeReal(1.0));
    Instance_setSelfVar(self, cache.memorymode, RValue_makeReal(0.0));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeEyeshotCreate_register(VMContext* ctx) {
    cache.size = NativeScript_getSelfVarID(ctx, "size");
    cache.saved = NativeScript_getSelfVarID(ctx, "saved");
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.memorymode = NativeScript_getSelfVarID(ctx, "memorymode");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_eyeshot_a_Create_0", eyeshotCreate);
}
