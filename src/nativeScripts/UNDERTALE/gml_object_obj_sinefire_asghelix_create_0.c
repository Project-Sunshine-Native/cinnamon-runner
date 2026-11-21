#include "nativeScripts/native_runtime.h"

#include "instance.h"

static struct {
    const VMContext* owner;
    int32_t s, sf, sv;
} cache;

static bool sinefireAsghelixCreate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (cache.owner != ctx) {
        cache.owner = ctx;
        cache.s = NativeScript_getSelfVarID(ctx, "s");
        cache.sf = NativeScript_getSelfVarID(ctx, "sf");
        cache.sv = NativeScript_getSelfVarID(ctx, "sv");
    }

    Instance_setSelfVar(self, cache.s, RValue_makeReal(0.0));
    Instance_setSelfVar(self, cache.sf, RValue_makeReal(10.0));
    Instance_setSelfVar(self, cache.sv, RValue_makeReal(5.0));
    self->vspeed = 2.0f;
    Instance_computeSpeedFromComponents(self);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSinefireAsghelixCreate_register(VMContext* ctx) {
    cache.owner = nullptr;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sinefire_asghelix_Create_0", sinefireAsghelixCreate);
}
