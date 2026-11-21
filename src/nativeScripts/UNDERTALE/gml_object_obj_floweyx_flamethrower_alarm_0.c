#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "math_compat.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t num, side, on, xx, yy, fl;
} cache;
static NativeAssetBinding flameBinding = NATIVE_ASSET_BINDING_INIT;

static bool flamethrowerAlarm(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (!NativeScript_canResolveAsset(ctx, &flameBinding, "obj_floweyx_flame") ||
        (uint32_t)flameBinding.assetIndex >= ctx->dataWin->objt.count) return false;

    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal num = RValue_toReal(Instance_getSelfVar(self, cache.num));
    GMLReal on = RValue_toReal(Instance_getSelfVar(self, cache.on));
    bool off = NativeScript_compareReals(on, 0.0, epsilon) == 0;
    if (NativeScript_compareReals(num, 7.0, epsilon) < 0) {
        GMLReal side = RValue_toReal(Instance_getSelfVar(self, cache.side));
        bool left = NativeScript_compareReals(side, 0.0, epsilon) == 0;
        bool right = NativeScript_compareReals(side, 1.0, epsilon) == 0;
        bool reverse = NativeScript_compareReals(on, 1.0, epsilon) == 0;
        if ((left || right) && (off || reverse)) {
            GMLReal angle = self->imageAngle + ((left == off) ? 8.0 : -8.0);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(angle), -1);
        }
        GMLReal radians = self->imageAngle * (M_PI / 180.0);
        GMLReal xx = 165.0 * GMLReal_cos(radians);
        GMLReal yy = -165.0 * GMLReal_sin(radians);
        if (GMLReal_fabs(xx) < (GMLReal)1e-4) xx = 0.0;
        if (GMLReal_fabs(yy) < (GMLReal)1e-4) yy = 0.0;
        Instance_setSelfVar(self, cache.xx, RValue_makeReal(xx));
        Instance_setSelfVar(self, cache.yy, RValue_makeReal(yy));

        Instance* flame = Runner_createInstance(runner, self->x + xx, self->y + yy, flameBinding.assetIndex);
        if (flame != nullptr && ctx->creatorVarID >= 0)
            Instance_setSelfVar(flame, ctx->creatorVarID, RValue_makeReal(self->instanceId));
        Instance_setSelfVar(self, cache.fl,
            RValue_makeReal(flame != nullptr ? (GMLReal)flame->instanceId : (GMLReal)INSTANCE_NOONE));
        if (flame != nullptr) {
            VMBuiltins_setVariable(ctx, flame, BUILTIN_VAR_DIRECTION, "direction", RValue_makeReal(self->imageAngle), -1);
            VMBuiltins_setVariable(ctx, flame, BUILTIN_VAR_SPEED, "speed", RValue_makeReal(7.0), -1);
        }
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(2.0), 0);
        num += 1.0;
    } else {
        Instance_setSelfVar(self, cache.on, RValue_makeReal(off ? 1.0 : 0.0));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(30.0), 0);
        num = 0.0;
    }
    Instance_setSelfVar(self, cache.num, RValue_makeReal(num));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyFlamethrowerAlarm_register(VMContext* ctx) {
    cache.num = NativeScript_getSelfVarID(ctx, "num");
    cache.side = NativeScript_getSelfVarID(ctx, "side");
    cache.on = NativeScript_getSelfVarID(ctx, "on");
    cache.xx = NativeScript_getSelfVarID(ctx, "xx");
    cache.yy = NativeScript_getSelfVarID(ctx, "yy");
    cache.fl = NativeScript_getSelfVarID(ctx, "fl");
    flameBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_flamethrower_Alarm_0", flamethrowerAlarm);
}
