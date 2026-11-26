#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t wallDestroy, updateImageAngle, spin, spinSpeed, bottomFade;
} cache;

static bool regularBulletStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    RValue wallDestroy = Instance_getSelfVar(self, cache.wallDestroy);
    if (wallDestroy.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(wallDestroy), 1, epsilon) == 0) {
        GMLCamera* camera = Runner_getCameraForView(runner, 0);
        GMLReal x = camera != nullptr ? (GMLReal)camera->viewX : -1;
        GMLReal y = camera != nullptr ? (GMLReal)camera->viewY : -1;
        if (NativeScript_compareReals(self->x, x - 80, epsilon) < 0 ||
            NativeScript_compareReals(self->x, x + 760, epsilon) > 0 ||
            NativeScript_compareReals(self->y, y - 80, epsilon) < 0 ||
            NativeScript_compareReals(self->y, y + 580, epsilon) > 0) {
            Runner_destroyInstance(runner, self, true);
        }
    }

    RValue updateImageAngle = Instance_getSelfVar(self, cache.updateImageAngle);
    if (updateImageAngle.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(updateImageAngle), 1, epsilon) == 0) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
            RValue_makeReal(self->direction), -1);
    }

    RValue spin = Instance_getSelfVar(self, cache.spin);
    if (spin.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(spin), 1, epsilon) == 0) {
        GMLReal spinSpeed = RValue_toReal(Instance_getSelfVar(self, cache.spinSpeed));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
            RValue_makeReal(self->imageAngle + spinSpeed), -1);
    }

    GMLReal bottomFade = RValue_toReal(Instance_getSelfVar(self, cache.bottomFade));
    if (NativeScript_compareReals(bottomFade, 0, epsilon) != 0) {
        GMLCamera* camera = Runner_getCameraForView(runner, 0);
        GMLReal y = camera != nullptr ? (GMLReal)camera->viewY : -1;
        if (NativeScript_compareReals(self->y, y + bottomFade, epsilon) > 0)
            self->imageAlpha = (float)((GMLReal)self->imageAlpha * (GMLReal)0.8);
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2RegularBulletStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 2") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0) ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_regularbullet_Step_0", regularBulletStep)) return;

    cache.wallDestroy = NativeScript_getSelfVarID(ctx, "wall_destroy");
    cache.updateImageAngle = NativeScript_getSelfVarID(ctx, "updateimageangle");
    cache.spin = NativeScript_getSelfVarID(ctx, "spin");
    cache.spinSpeed = NativeScript_getSelfVarID(ctx, "spinspeed");
    cache.bottomFade = NativeScript_getSelfVarID(ctx, "bottomfade");
}
