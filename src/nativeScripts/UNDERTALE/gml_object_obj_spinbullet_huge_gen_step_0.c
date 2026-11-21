#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "math_compat.h"
#include "runner.h"

static struct {
    int32_t counter, num, trip, maxnum, strike, radius, addDir, side;
    int32_t xx, yy, centerx, centery, spinbullet;
} cache;
static NativeAssetBinding bulletBinding = NATIVE_ASSET_BINDING_INIT;

static bool spinbulletGeneratorStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal counter = RValue_toReal(Instance_getSelfVar(self, cache.counter));
    GMLReal num = RValue_toReal(Instance_getSelfVar(self, cache.num));
    GMLReal trip = RValue_toReal(Instance_getSelfVar(self, cache.trip));
    GMLReal maxnum = RValue_toReal(Instance_getSelfVar(self, cache.maxnum));
    GMLReal strike = RValue_toReal(Instance_getSelfVar(self, cache.strike));
    GMLReal nextCounter = counter + 1.0;

    if (NativeScript_compareReals(nextCounter, (maxnum - 1.0) * trip, epsilon) > 0 ||
        (NativeScript_compareReals(nextCounter, -44.0, epsilon) == 0 &&
            NativeScript_compareReals(strike, 1.0, epsilon) == 0)) return false;

    if (NativeScript_compareReals(counter, num * trip, epsilon) == 0) {
        if (!NativeScript_canResolveAsset(ctx, &bulletBinding, "obj_spinbullet_huge") ||
            (uint32_t)bulletBinding.assetIndex >= ctx->dataWin->objt.count) return false;

        GMLReal radius = RValue_toReal(Instance_getSelfVar(self, cache.radius));
        GMLReal addDir = RValue_toReal(Instance_getSelfVar(self, cache.addDir));
        GMLReal side = RValue_toReal(Instance_getSelfVar(self, cache.side));
        GMLReal radians = (addDir + ((num / maxnum) * 360.0 * side)) * (M_PI / 180.0);
        GMLReal xx = radius * GMLReal_cos(radians);
        GMLReal yy = -radius * GMLReal_sin(radians);
        if (GMLReal_fabs(xx) < (GMLReal)1e-4) xx = 0.0;
        if (GMLReal_fabs(yy) < (GMLReal)1e-4) yy = 0.0;
        Instance_setSelfVar(self, cache.xx, RValue_makeReal(xx));
        Instance_setSelfVar(self, cache.yy, RValue_makeReal(yy));

        GMLReal centerx = RValue_toReal(Instance_getSelfVar(self, cache.centerx));
        GMLReal centery = RValue_toReal(Instance_getSelfVar(self, cache.centery));
        Instance* bullet = Runner_createInstance(runner, centerx + xx, centery + yy, bulletBinding.assetIndex);
        if (bullet != nullptr && ctx->creatorVarID >= 0)
            Instance_setSelfVar(bullet, ctx->creatorVarID, RValue_makeReal(self->instanceId));
        RValue* slot = IntRValueHashMap_getOrInsertUndefined(&self->selfVars, cache.spinbullet);
        VM_setArrayElement(ctx, slot, (int32_t)num,
            RValue_makeReal(bullet != nullptr ? (GMLReal)bullet->instanceId : INSTANCE_NOONE));
        Instance_setSelfVar(self, cache.num, RValue_makeReal(num + 1.0));
    }
    Instance_setSelfVar(self, cache.counter, RValue_makeReal(nextCounter));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSpinbulletGeneratorStep_register(VMContext* ctx) {
    cache.counter = NativeScript_getSelfVarID(ctx, "counter");
    cache.num = NativeScript_getSelfVarID(ctx, "num");
    cache.trip = NativeScript_getSelfVarID(ctx, "trip");
    cache.maxnum = NativeScript_getSelfVarID(ctx, "maxnum");
    cache.strike = NativeScript_getSelfVarID(ctx, "strike");
    cache.radius = NativeScript_getSelfVarID(ctx, "radius");
    cache.addDir = NativeScript_getSelfVarID(ctx, "add_dir");
    cache.side = NativeScript_getSelfVarID(ctx, "side");
    cache.xx = NativeScript_getSelfVarID(ctx, "xx");
    cache.yy = NativeScript_getSelfVarID(ctx, "yy");
    cache.centerx = NativeScript_getSelfVarID(ctx, "centerx");
    cache.centery = NativeScript_getSelfVarID(ctx, "centery");
    cache.spinbullet = NativeScript_getSelfVarID(ctx, "spinbullet");
    bulletBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_spinbullet_huge_gen_Step_0", spinbulletGeneratorStep);
}
