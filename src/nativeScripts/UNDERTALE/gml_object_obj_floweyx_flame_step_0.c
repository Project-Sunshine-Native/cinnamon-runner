#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "utils.h"
#include "vm_builtins.h"

static struct {
    int32_t size, rp;
} cache;
static NativeAssetBinding heartBinding = NATIVE_ASSET_BINDING_INIT;

static bool floweyFlameStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &heartBinding, "obj_vsflowey_heart")) return false;
    Instance* heart = VM_findInstanceByTarget(ctx, heartBinding.assetIndex);
    if (heart == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;

    float hspeed = self->hspeed;
    float vspeed = self->vspeed;
    bool velocityAssigned = false;
    if (NativeScript_compareReals(hspeed, 0.0, epsilon) > 0) {
        hspeed = (float)((GMLReal)hspeed - 0.1);
        velocityAssigned = true;
    }
    if (NativeScript_compareReals(hspeed, 0.0, epsilon) < 0) {
        hspeed = (float)((GMLReal)hspeed + 0.1);
        velocityAssigned = true;
    }
    int heartX = NativeScript_compareReals(heart->x + 8.0, self->x, epsilon);
    int heartY = NativeScript_compareReals(heart->y + 8.0, self->y, epsilon);
    if (heartX > 0) {
        hspeed = (float)((GMLReal)hspeed + 0.1);
        velocityAssigned = true;
    }
    if (heartY > 0) {
        vspeed = (float)((GMLReal)vspeed + 0.1);
        velocityAssigned = true;
    }
    if (heartY < 0) {
        vspeed = (float)((GMLReal)vspeed - 0.12);
        velocityAssigned = true;
    }
    if (heartX < 0) {
        hspeed = (float)((GMLReal)hspeed - 0.1);
        velocityAssigned = true;
    }
    if (NativeScript_compareReals(self->y, 380.0, epsilon) > 0) {
        hspeed = (float)((GMLReal)hspeed * 1.12);
        velocityAssigned = true;
    }
    if (velocityAssigned && hspeed == 0.0f && vspeed == 0.0f) return false;

    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(self->direction), -1);
    self->friction = 0.3f;
    if (velocityAssigned) {
        self->hspeed = hspeed;
        self->vspeed = vspeed;
        Instance_computeSpeedFromComponents(self);
    }

    RValue size = Instance_getSelfVar(self, cache.size);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", size, -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", size, -1);
    GMLReal rp = RValue_toReal(Instance_getSelfVar(self, cache.rp));
    if (NativeScript_compareReals(self->imageIndex, 9.0, epsilon) > 0 && NativeScript_compareReals(rp, 2.0, epsilon) < 0) {
        self->imageIndex = 5.0f;
        Instance_setSelfVar(self, cache.rp, RValue_makeReal(rp + 1.0));
    }
    if (NativeScript_compareReals(self->imageIndex, 18.0, epsilon) > 0) {
        if (NativeScript_compareReals(self->imageAlpha, 0.8, epsilon) > 0) self->imageAlpha = 0.8f;
        self->imageBlend = (uint32_t)Color_lerp((int32_t)self->imageBlend, 0xFFFF00, 0.1f);
        self->imageAlpha = (float)((GMLReal)self->imageAlpha - 0.05);
        if (NativeScript_compareReals(self->imageAlpha, 0.1, epsilon) <= 0)
            Runner_destroyInstance(ctx->runner, self, true);
    }
    if (NativeScript_compareReals(self->imageIndex, 40.0, epsilon) >= 0)
        Runner_destroyInstance(ctx->runner, self, true);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyFlameStep_register(VMContext* ctx) {
    cache.size = NativeScript_getSelfVarID(ctx, "size");
    cache.rp = NativeScript_getSelfVarID(ctx, "rp");
    heartBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_flame_Step_0", floweyFlameStep);
}
