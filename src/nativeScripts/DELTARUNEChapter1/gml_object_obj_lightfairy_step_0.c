#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "spatial_grid.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t siner;
    int32_t sinx;
    int32_t siny;
    int32_t period;
} cache;

static bool lightfairyStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    GMLReal sinx = RValue_toReal(Instance_getSelfVar(self, cache.sinx));
    GMLReal siny = RValue_toReal(Instance_getSelfVar(self, cache.siny));
    GMLReal period = RValue_toReal(Instance_getSelfVar(self, cache.period));

    float x = (float)(self->x + GMLReal_sin(siner / sinx) * period);
    float y = (float)(self->y + GMLReal_sin(siner / siny) * period);
    bool movedX = x != self->x;
    bool movedY = y != self->y;
    if (movedX) self->x = x;
    if (movedY) self->y = y;
    if (movedX || movedY) {
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, self);
    }
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner + 1));

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeLightfairyStep_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_lightfairy_Step_0", lightfairyStep)) return;

    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.sinx = NativeScript_getSelfVarID(ctx, "sinx");
    cache.siny = NativeScript_getSelfVarID(ctx, "siny");
    cache.period = NativeScript_getSelfVarID(ctx, "period");
}
