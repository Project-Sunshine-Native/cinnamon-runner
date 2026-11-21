#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t con, shakeboy, cr, no, dgoal, curdir, idealdir;
    int32_t facingMinusTarget, angleDiff, leastAccurateAim, dirspeed, angleDiff2;
} cache;
static NativeCallBinding directionBinding = NATIVE_CALL_BINDING_INIT;

static bool gunAimStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.con)), 7.0, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.shakeboy)), 1.0, epsilon) == 0) return false;
    RValue crosshairs = Instance_getSelfVar(self, cache.cr);
    RValue index = Instance_getSelfVar(self, cache.no);
    int32_t target = RValue_toInt32(NativeScript_getIndex(crosshairs, index));
    Instance* crosshair = VM_findInstanceByTarget(ctx, target);
    if (crosshair == nullptr) return false;

    RValue args[4] = {
        RValue_makeReal(self->x), RValue_makeReal(self->y),
        RValue_makeReal(crosshair->x), RValue_makeReal(crosshair->y),
    };
    GMLReal dgoal = RValue_toReal(NativeScript_call(ctx, &directionBinding, "point_direction", args, 4));
    Instance_setSelfVar(self, cache.dgoal, RValue_makeReal(dgoal));
    GMLReal curdir = self->imageAngle;
    Instance_setSelfVar(self, cache.curdir, RValue_makeReal(curdir));
    Instance_setSelfVar(self, cache.idealdir, RValue_makeReal(dgoal));
    GMLReal facingMinusTarget = curdir - dgoal;
    Instance_setSelfVar(self, cache.facingMinusTarget, RValue_makeReal(facingMinusTarget));
    GMLReal angleDiff = facingMinusTarget;
    if (NativeScript_compareReals(GMLReal_fabs(facingMinusTarget), 180.0, epsilon) > 0) {
        if (NativeScript_compareReals(curdir, dgoal, epsilon) > 0) angleDiff = -1.0 * ((360.0 - curdir) + dgoal);
        else angleDiff = (360.0 - dgoal) + curdir;
    }
    Instance_setSelfVar(self, cache.angleDiff, RValue_makeReal(angleDiff));
    Instance_setSelfVar(self, cache.leastAccurateAim, RValue_makeReal(5.0));
    if (NativeScript_compareReals(GMLReal_fabs(angleDiff), 5.0, epsilon) > 0) {
        GMLReal dirspeed = 1.0;
        GMLReal angleDiff2 = GMLReal_fabs(angleDiff);
        Instance_setSelfVar(self, cache.angleDiff2, RValue_makeReal(angleDiff2));
        const GMLReal speeds[] = {5.0, 8.0, 12.0, 15.0, 18.0, 24.0, 30.0, 36.0, 42.0, 50.0};
        for (int i = 0; i < 10; ++i) {
            if (NativeScript_compareReals(angleDiff2, (GMLReal)(i + 1) * 10.0, epsilon) > 0)
                dirspeed = speeds[i];
        }
        if (NativeScript_compareReals(angleDiff, 0.0, epsilon) < 0) dirspeed = -dirspeed;
        Instance_setSelfVar(self, cache.dirspeed, RValue_makeReal(dirspeed));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(self->imageAngle - dirspeed), -1);
    } else {
        Instance_setSelfVar(self, cache.con, RValue_makeReal(8.0));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(dgoal), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(1.0), 4);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6GunStep_register(VMContext* ctx) {
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.shakeboy = NativeScript_getSelfVarID(ctx, "shakeboy");
    cache.cr = NativeScript_getSelfVarID(ctx, "cr");
    cache.no = NativeScript_getSelfVarID(ctx, "no");
    cache.dgoal = NativeScript_getSelfVarID(ctx, "dgoal");
    cache.curdir = NativeScript_getSelfVarID(ctx, "curdir");
    cache.idealdir = NativeScript_getSelfVarID(ctx, "idealdir");
    cache.facingMinusTarget = NativeScript_getSelfVarID(ctx, "facingMinusTarget");
    cache.angleDiff = NativeScript_getSelfVarID(ctx, "angleDiff");
    cache.leastAccurateAim = NativeScript_getSelfVarID(ctx, "leastAccurateAim");
    cache.dirspeed = NativeScript_getSelfVarID(ctx, "dirspeed");
    cache.angleDiff2 = NativeScript_getSelfVarID(ctx, "angleDiff2");
    directionBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6gun_Step_0", gunAimStep);
}
