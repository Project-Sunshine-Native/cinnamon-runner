#include "native_runtime.h"
#include "asgore_battle.h"

#include "event_table.h"
#include "instance.h"
#include "runner.h"
#include "spatial_grid.h"

#include <string.h>

static struct {
    const VMContext* owner;
    int32_t s, sv, sf;
} cache;
static struct {
    GMLReal phase;
    GMLReal value;
    bool valid;
} sineCache[2];
static uint32_t nextSine;

static GMLReal helixSine(GMLReal phase) {
    // Mirrored bullet pairs repeat the same two phase inputs across emitters.
    for (int i = 0; i < 2; ++i) {
        if (sineCache[i].valid && memcmp(&phase, &sineCache[i].phase, sizeof(phase)) == 0)
            return sineCache[i].value;
    }
    GMLReal value = GMLReal_sin(phase);
    sineCache[nextSine].phase = phase;
    sineCache[nextSine].value = value;
    sineCache[nextSine].valid = true;
    nextSine ^= 1u;
    return value;
}

static bool sinefireAsghelixStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (!NativeAsgoreBattle_isActive(ctx)) return false;
    Runner* runner = ctx->runner;
    if (cache.owner != ctx) {
        cache.owner = ctx;
        cache.s = NativeScript_getSelfVarID(ctx, "s");
        cache.sv = NativeScript_getSelfVarID(ctx, "sv");
        cache.sf = NativeScript_getSelfVarID(ctx, "sf");
    }

    GMLReal s = RValue_toReal(Instance_getSelfVar(self, cache.s)) + 1.0;
    Instance_setSelfVar(self, cache.s, RValue_makeReal(s));
    GMLReal sv = RValue_toReal(Instance_getSelfVar(self, cache.sv));
    GMLReal sf = RValue_toReal(Instance_getSelfVar(self, cache.sf));
    float x = (float)(self->x + helixSine(s / sv) * sf);
    if (x != self->x) {
        self->x = x;
        SpatialGrid_markInstanceAsDirty(runner->spatialGrid, self);
    }

    GMLReal roomHeight = runner->currentRoom != nullptr ? (GMLReal)runner->currentRoom->height : -1.0;
    if (self->y > roomHeight + 100.0) Runner_destroyInstance(runner, self, true);

    if (NativeAsgoreBattle_collidesWithHeart(ctx, self)) {
        Runner_executeEventFromObject(runner, self, ctx->currentCallFrame->other,
            self->objectIndex, EVENT_OTHER, OTHER_USER0);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSinefireAsghelixStep_register(VMContext* ctx) {
    cache.owner = nullptr;
    sineCache[0].valid = false;
    sineCache[1].valid = false;
    nextSine = 0u;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sinefire_asghelix_Step_0", sinefireAsghelixStep);
}
