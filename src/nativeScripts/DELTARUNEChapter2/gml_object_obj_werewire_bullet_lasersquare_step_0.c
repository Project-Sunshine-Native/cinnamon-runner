#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "spatial_grid.h"
#include "vm_builtins.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t stretch, m, myAngleChange;
    int32_t destroyOnHit, grazePoints, initCon, initTimer, circleSize;
    NativeCallBinding bulletInit;
} cache;

static bool werewireLaserSquareCreate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    RValue result = NativeScript_call(ctx, &cache.bulletInit, "gml_Script_scr_bullet_init", nullptr, 0);
    RValue_free(&result);
    Instance_setSelfVar(self, cache.destroyOnHit, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.grazePoints, RValue_makeReal(2));
    Instance_setSelfVar(self, cache.initCon, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.initTimer, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.circleSize, RValue_makeReal(1));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(2), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(3), -1);
    self->imageSpeed = 0;
    Instance_setSelfVar(self, cache.myAngleChange, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.m, RValue_makeReal(2));
    Instance_setSelfVar(self, cache.stretch, RValue_makeReal(1));
    *outResult = RValue_makeUndefined();
    return true;
}

static bool werewireLaserSquareStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    GMLReal epsilon = ctx->runner->mathEpsilon;

    GMLReal stretch = RValue_toReal(Instance_getSelfVar(self, cache.stretch));
    if (NativeScript_compareReals(stretch, 1, epsilon) == 0) {
        GMLReal m = RValue_toReal(Instance_getSelfVar(self, cache.m));
        float scale = (float)(2 + (self->speed / (10 * m)));
        if (self->imageXscale != scale) {
            self->imageXscale = scale;
            SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, self);
        }
    }

    if (NativeScript_compareReals(self->x, -100, epsilon) <= 0)
        Runner_destroyInstance(ctx->runner, self, true);

    GMLReal angleChange = RValue_toReal(Instance_getSelfVar(self, cache.myAngleChange));
    GMLReal direction = self->direction + angleChange;
    if (!(direction >= 0 && direction < 360)) {
        direction = GMLReal_fmod(direction, 360);
        if (direction < 0) direction += 360;
    }
    self->direction = (float)direction;
    Instance_computeComponentsFromSpeed(self);
    if (self->imageAngle != self->direction) {
        self->imageAngle = self->direction;
        SpatialGrid_markInstanceAsDirty(ctx->runner->spatialGrid, self);
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2WerewireLaserSquareStep_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_werewire_bullet_lasersquare_Step_0",
        werewireLaserSquareStep)) return;

    cache.stretch = NativeScript_getSelfVarID(ctx, "stretch");
    cache.m = NativeScript_getSelfVarID(ctx, "m");
    cache.myAngleChange = NativeScript_getSelfVarID(ctx, "my_angle_change");
    cache.destroyOnHit = NativeScript_getSelfVarID(ctx, "destroyonhit");
    cache.grazePoints = NativeScript_getSelfVarID(ctx, "grazepoints");
    cache.initCon = NativeScript_getSelfVarID(ctx, "init_con");
    cache.initTimer = NativeScript_getSelfVarID(ctx, "init_timer");
    cache.circleSize = NativeScript_getSelfVarID(ctx, "circle_size");
    cache.bulletInit = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (NativeScript_canCall(ctx, &cache.bulletInit, "gml_Script_scr_bullet_init"))
        VM_registerNativeScript(ctx, "gml_Object_obj_werewire_bullet_lasersquare_Create_0", werewireLaserSquareCreate);
}
