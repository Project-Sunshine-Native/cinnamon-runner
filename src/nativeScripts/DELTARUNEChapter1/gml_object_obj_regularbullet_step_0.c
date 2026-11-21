#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

#include <string.h>

static int32_t wallDestroyId;

static bool regularBulletStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    Runner* runner = ctx->runner;
    RValue wallDestroy = Instance_getSelfVar(self, wallDestroyId);
    GMLReal epsilon = runner->mathEpsilon;
    if (wallDestroy.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(wallDestroy), 1, epsilon) == 0) {
        GMLCamera* camera = Runner_getCameraForView(runner, 0);
        GMLReal x = camera != nullptr ? (GMLReal)camera->viewX : -1;
        GMLReal y = camera != nullptr ? (GMLReal)camera->viewY : -1;
        if (NativeScript_compareReals(self->x, x - 40, epsilon) < 0 ||
            NativeScript_compareReals(self->x, x + 680, epsilon) > 0 ||
            NativeScript_compareReals(self->y, y - 40, epsilon) < 0 ||
            NativeScript_compareReals(self->y, y + 520, epsilon) > 0) {
            Runner_destroyInstance(runner, self, true);
        }
    }
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeRegularBulletStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0) ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_regularbullet_Step_0", regularBulletStep)) return;

    wallDestroyId = NativeScript_getSelfVarID(ctx, "wall_destroy");
}
