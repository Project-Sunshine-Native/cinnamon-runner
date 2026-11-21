#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

static struct {
    const VMContext* owner;
    int32_t mys, mysadd, mysf, mysv, selfspeed, selfspeed2;
    int32_t count, hl, hl2, s, sf, sv;
} cache;
static NativeAssetBinding fireBinding = NATIVE_ASSET_BINDING_INIT;

static void resolveCache(VMContext* ctx) {
    if (cache.owner == ctx) return;
    cache.owner = ctx;
    cache.mys = NativeScript_getSelfVarID(ctx, "mys");
    cache.mysadd = NativeScript_getSelfVarID(ctx, "mysadd");
    cache.mysf = NativeScript_getSelfVarID(ctx, "mysf");
    cache.mysv = NativeScript_getSelfVarID(ctx, "mysv");
    cache.selfspeed = NativeScript_getSelfVarID(ctx, "selfspeed");
    cache.selfspeed2 = NativeScript_getSelfVarID(ctx, "selfspeed2");
    cache.count = NativeScript_getSelfVarID(ctx, "count");
    cache.hl = NativeScript_getSelfVarID(ctx, "hl");
    cache.hl2 = NativeScript_getSelfVarID(ctx, "hl2");
    cache.s = NativeScript_getSelfVarID(ctx, "s");
    cache.sf = NativeScript_getSelfVarID(ctx, "sf");
    cache.sv = NativeScript_getSelfVarID(ctx, "sv");
}

static GMLReal getNumber(Instance* instance, int32_t id) {
    return RValue_toReal(Instance_getSelfVar(instance, id));
}

static void setNumber(Instance* instance, int32_t id, GMLReal value) {
    Instance_setSelfVar(instance, id, RValue_makeReal(value));
}

static Instance* spawnFire(VMContext* ctx, Instance* self, GMLReal mys,
    GMLReal mysf, GMLReal mysv, GMLReal speed) {
    Instance* fire = Runner_createInstance(ctx->runner, self->x, self->y, fireBinding.assetIndex);
    if (fire == nullptr) return nullptr;

    if (ctx->creatorVarID >= 0)
        setNumber(fire, ctx->creatorVarID, (GMLReal)self->instanceId);

    setNumber(fire, cache.s, mys);
    setNumber(fire, cache.sf, mysf);
    setNumber(fire, cache.sv, mysv);
    fire->vspeed = (float)speed;
    Instance_computeSpeedFromComponents(fire);
    return fire;
}

static bool helixFiregenAlarm(VMContext* ctx, RValue* outResult) {
    if (!NativeScript_canResolveAsset(ctx, &fireBinding, "obj_sinefire_asghelix")) return false;

    Instance* self = ctx->currentCallFrame->self;
    resolveCache(ctx);

    GMLReal mys = getNumber(self, cache.mys) + getNumber(self, cache.mysadd);
    GMLReal mysf = getNumber(self, cache.mysf);
    GMLReal mysv = getNumber(self, cache.mysv);
    setNumber(self, cache.mys, mys);

    Instance* hl = spawnFire(ctx, self, mys, mysf, mysv,
        5.5 + getNumber(self, cache.selfspeed));
    setNumber(self, cache.hl, hl != nullptr ? (GMLReal)hl->instanceId : INSTANCE_NOONE);

    Instance* hl2 = spawnFire(ctx, self, mys, mysf, -mysv,
        5.5 + getNumber(self, cache.selfspeed2));
    setNumber(self, cache.hl2, hl2 != nullptr ? (GMLReal)hl2->instanceId : INSTANCE_NOONE);

    self->alarm[0] = 2;
    self->activeAlarmMask |= 1u;
    GMLReal count = getNumber(self, cache.count) + 1.0;
    setNumber(self, cache.count, count);
    if (count > 15.0) Runner_destroyInstance(ctx->runner, self, true);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeHelixFiregenAlarm_register(VMContext* ctx) {
    cache.owner = nullptr;
    fireBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_helixfiregen_Alarm_0", helixFiregenAlarm);
}
