#include "nativeScripts/native_runtime.h"

#include "instance.h"

static struct {
    int32_t grazed, grazeTimer, destroyOnHit, target, inv, damage, element;
    int32_t grazePoints, timePoints, active, updateImageAngle;
} cache;

static bool bulletInit(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Instance_setSelfVar(self, cache.grazed, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.grazeTimer, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.destroyOnHit, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.target, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.inv, RValue_makeReal(60));
    Instance_setSelfVar(self, cache.damage, RValue_makeReal(10));
    Instance_setSelfVar(self, cache.element, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.grazePoints, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.timePoints, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.active, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.updateImageAngle, RValue_makeReal(0));
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2BulletInit_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Script_scr_bullet_init", bulletInit)) return;

    cache.grazed = NativeScript_getSelfVarID(ctx, "grazed");
    cache.grazeTimer = NativeScript_getSelfVarID(ctx, "grazetimer");
    cache.destroyOnHit = NativeScript_getSelfVarID(ctx, "destroyonhit");
    cache.target = NativeScript_getSelfVarID(ctx, "target");
    cache.inv = NativeScript_getSelfVarID(ctx, "inv");
    cache.damage = NativeScript_getSelfVarID(ctx, "damage");
    cache.element = NativeScript_getSelfVarID(ctx, "element");
    cache.grazePoints = NativeScript_getSelfVarID(ctx, "grazepoints");
    cache.timePoints = NativeScript_getSelfVarID(ctx, "timepoints");
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.updateImageAngle = NativeScript_getSelfVarID(ctx, "updateimageangle");
}
