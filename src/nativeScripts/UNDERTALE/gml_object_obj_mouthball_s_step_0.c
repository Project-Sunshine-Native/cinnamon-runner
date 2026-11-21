#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t nowsize, size, siner, dont, parent;
    int32_t xprev2, yprev2, xprev3, yprev3, xprev4, yprev4;
} cache;

static bool mouthballTrailStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal dont = RValue_toReal(Instance_getSelfVar(self, cache.dont));
    Instance* parent = nullptr;
    if (NativeScript_compareReals(dont, 0.0, epsilon) == 0) {
        int32_t id = RValue_toInt32(Instance_getSelfVar(self, cache.parent));
        parent = VM_findInstanceByTarget(ctx, id);
        if (parent == nullptr) return false;
    }

    GMLReal size = RValue_toReal(Instance_getSelfVar(self, cache.size));
    GMLReal nowsize = RValue_toReal(Instance_getSelfVar(self, cache.nowsize));
    if (NativeScript_compareReals(nowsize, size, epsilon) < 0) {
        nowsize += 0.1;
        Instance_setSelfVar(self, cache.nowsize, RValue_makeReal(nowsize));
    }
    if (NativeScript_compareReals(nowsize, size, epsilon) > 0) {
        nowsize = size;
        Instance_setSelfVar(self, cache.nowsize, RValue_makeReal(nowsize));
    }
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    RValue scale = RValue_makeReal(nowsize + GMLReal_sin(siner / 6.0) * 0.1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", scale, -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", scale, -1);

    if (parent != nullptr) {
        Instance_setSelfVar(self, cache.xprev4, Instance_getSelfVar(self, cache.xprev3));
        Instance_setSelfVar(self, cache.yprev4, Instance_getSelfVar(self, cache.yprev3));
        Instance_setSelfVar(self, cache.xprev3, Instance_getSelfVar(self, cache.xprev2));
        Instance_setSelfVar(self, cache.yprev3, Instance_getSelfVar(self, cache.yprev2));
        Instance_setSelfVar(self, cache.xprev2, RValue_makeReal(self->xprevious));
        Instance_setSelfVar(self, cache.yprev2, RValue_makeReal(self->yprevious));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", Instance_getSelfVar(parent, cache.xprev4), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", Instance_getSelfVar(parent, cache.yprev4), -1);
    }
    if (NativeScript_compareReals(dont, 1.0, epsilon) == 0) {
        Runner_destroyInstance(runner, self, true);
        siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    }
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner + 1.0));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeMouthballTrailStep_register(VMContext* ctx) {
    cache.nowsize = NativeScript_getSelfVarID(ctx, "nowsize");
    cache.size = NativeScript_getSelfVarID(ctx, "size");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.dont = NativeScript_getSelfVarID(ctx, "dont");
    cache.parent = NativeScript_getSelfVarID(ctx, "parent");
    cache.xprev2 = NativeScript_getSelfVarID(ctx, "xprev2");
    cache.yprev2 = NativeScript_getSelfVarID(ctx, "yprev2");
    cache.xprev3 = NativeScript_getSelfVarID(ctx, "xprev3");
    cache.yprev3 = NativeScript_getSelfVarID(ctx, "yprev3");
    cache.xprev4 = NativeScript_getSelfVarID(ctx, "xprev4");
    cache.yprev4 = NativeScript_getSelfVarID(ctx, "yprev4");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_mouthball_s_Step_0", mouthballTrailStep);
}
