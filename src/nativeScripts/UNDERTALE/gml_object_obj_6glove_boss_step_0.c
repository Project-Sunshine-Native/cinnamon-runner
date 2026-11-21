#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "math_compat.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

#include <string.h>

static struct {
    int32_t angspeed, radius, siner, i, maxnum, glove, gloveang, ourang, slower, spec;
} cache;
static NativeAssetBinding bossBinding = NATIVE_ASSET_BINDING_INIT;
static struct {
    GMLReal radians, cosine, sine;
    bool valid;
} rotationCache[7];

static bool gloveBossStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &bossBinding, "obj_6glove_boss")) return false;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    GMLReal angspeed = 1.1 + GMLReal_sin(siner / 6.0) * 0.5;
    Instance_setSelfVar(self, cache.angspeed, RValue_makeReal(angspeed));
    GMLReal radius = RValue_toReal(Instance_getSelfVar(self, cache.radius)) + GMLReal_sin(siner / 12.0) * 3.0;
    Instance_setSelfVar(self, cache.radius, RValue_makeReal(radius));
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner + 1.0));

    GMLReal maxnum = RValue_toReal(Instance_getSelfVar(self, cache.maxnum));
    GMLReal ourang = RValue_toReal(Instance_getSelfVar(self, cache.ourang));
    int32_t i = 0;
    for (; NativeScript_compareReals(i, maxnum, epsilon) < 0; ++i) {
        Instance_setSelfVar(self, cache.i, RValue_makeReal(i));
        RValue index = RValue_makeReal(i);
        RValue gloves = Instance_getSelfVar(self, cache.glove);
        int32_t id = RValue_toInt32(NativeScript_getIndex(gloves, index));
        Instance* glove = VM_findInstanceByTarget(ctx, id);
        if (glove == nullptr || !glove->active) continue;
        RValue angles = Instance_getSelfVar(self, cache.gloveang);
        GMLReal angle = RValue_toReal(NativeScript_getIndex(angles, index)) + angspeed;
        RValue* slot = IntRValueHashMap_getOrInsertUndefined(&self->selfVars, cache.gloveang);
        VM_setArrayElement(ctx, slot, i, RValue_makeReal(angle));
        angle += ourang;
        GMLReal radians = angle * (M_PI / 180.0);
        GMLReal cosine, sine;
        if (i < 7 && rotationCache[i].valid &&
            memcmp(&rotationCache[i].radians, &radians, sizeof(radians)) == 0) {
            cosine = rotationCache[i].cosine;
            sine = rotationCache[i].sine;
        } else {
            cosine = GMLReal_cos(radians);
            sine = GMLReal_sin(radians);
            if (i < 7) {
                rotationCache[i].radians = radians;
                rotationCache[i].cosine = cosine;
                rotationCache[i].sine = sine;
                rotationCache[i].valid = true;
            }
        }
        GMLReal xx = radius * cosine;
        GMLReal yy = -radius * sine;
        if (GMLReal_fabs(xx) < 1e-4) xx = 0.0;
        if (GMLReal_fabs(yy) < 1e-4) yy = 0.0;
        VMBuiltins_setVariable(ctx, glove, BUILTIN_VAR_X, "x", RValue_makeReal(self->x + xx), -1);
        VMBuiltins_setVariable(ctx, glove, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + yy), -1);
        VMBuiltins_setVariable(ctx, glove, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(angle), -1);
    }
    Instance_setSelfVar(self, cache.i, RValue_makeReal(i));

    if (NativeScript_compareReals(self->x, 0.0, epsilon) < 0)
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", RValue_makeReal(-(GMLReal)self->hspeed), -1);
    GMLReal width = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    if (NativeScript_compareReals(self->x, width, epsilon) > 0)
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", RValue_makeReal(-(GMLReal)self->hspeed), -1);
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.slower)), 1.0, epsilon) == 0) {
        GMLReal speed = NativeScript_compareReals(self->speed, 0.0, epsilon) > 0
            ? (GMLReal)self->speed - 0.05 : 0.0;
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPEED, "speed", RValue_makeReal(speed), -1);
    }
    if (NativeScript_compareReals(self->y, 1000.0, epsilon) > 0 &&
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.spec)), 0.0, epsilon) == 0)
        Runner_destroyInstance(runner, self, true);

    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
    if (NativeScript_compareReals(self->y, height, epsilon) > 0 &&
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.spec)), 1.0, epsilon) == 0 &&
        NativeScript_compareReals(self->vspeed, 0.0, epsilon) > 0) {
        GMLReal speed = -GMLReal_fabs((GMLReal)self->vspeed);
        Instance** bosses = runner->instancesByObject[bossBinding.assetIndex];
        for (ptrdiff_t j = 0; j < arrlen(bosses); ++j) {
            if (bosses[j]->active)
                VMBuiltins_setVariable(ctx, bosses[j], BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(speed), -1);
        }
    }
    if (NativeScript_compareReals(self->y, 0.0, epsilon) < 0 &&
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.spec)), 1.0, epsilon) == 0 &&
        NativeScript_compareReals(self->vspeed, 0.0, epsilon) < 0) {
        GMLReal speed = GMLReal_fabs((GMLReal)self->vspeed);
        Instance** bosses = runner->instancesByObject[bossBinding.assetIndex];
        for (ptrdiff_t j = 0; j < arrlen(bosses); ++j) {
            if (bosses[j]->active)
                VMBuiltins_setVariable(ctx, bosses[j], BUILTIN_VAR_VSPEED, "vspeed", RValue_makeReal(speed), -1);
        }
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6GloveBossStep_register(VMContext* ctx) {
    cache.angspeed = NativeScript_getSelfVarID(ctx, "angspeed");
    cache.radius = NativeScript_getSelfVarID(ctx, "radius");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
    cache.maxnum = NativeScript_getSelfVarID(ctx, "maxnum");
    cache.glove = NativeScript_getSelfVarID(ctx, "glove");
    cache.gloveang = NativeScript_getSelfVarID(ctx, "gloveang");
    cache.ourang = NativeScript_getSelfVarID(ctx, "ourang");
    cache.slower = NativeScript_getSelfVarID(ctx, "slower");
    cache.spec = NativeScript_getSelfVarID(ctx, "spec");
    bossBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    memset(rotationCache, 0, sizeof(rotationCache));
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6glove_boss_Step_0", gloveBossStep);
}
