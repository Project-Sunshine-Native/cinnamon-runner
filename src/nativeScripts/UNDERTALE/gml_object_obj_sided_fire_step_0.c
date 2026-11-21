#include "nativeScripts/native_runtime.h"

#include "collision.h"
#include "event_table.h"
#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

static NativeVarBinding borderBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding heartBinding = NATIVE_ASSET_BINDING_INIT;

static bool collidesWithHeart(VMContext* ctx, Instance* self) {
    if (!NativeScript_canResolveAsset(ctx, &heartBinding, "obj_heart") ||
        (uint32_t)heartBinding.assetIndex >= ctx->dataWin->objt.count) return false;
    Runner* runner = ctx->runner;
    Instance** hearts = runner->instancesByObject[heartBinding.assetIndex];
    if (arrlen(hearts) == 0) return false;

    InstanceBBox bbox = Collision_computeBBox(runner, self);
    GMLReal left = bbox.valid ? bbox.left : self->x;
    GMLReal top = bbox.valid ? bbox.top : self->y;
    GMLReal right = bbox.valid ? bbox.right : self->x;
    GMLReal bottom = bbox.valid ? bbox.bottom : self->y;
    if (runner->collisionCompatibilityMode) {
        // bbox_right/bottom are inclusive in compatibility mode, unlike the cached box.
        if (bbox.valid) {
            right -= 1.0;
            bottom -= 1.0;
        } else {
            left = right = GMLReal_bankersRound(self->x);
            top = bottom = GMLReal_bankersRound(self->y);
        }
    }
    if (left > right) {
        GMLReal temp = left;
        left = right;
        right = temp;
    }
    if (top > bottom) {
        GMLReal temp = top;
        top = bottom;
        bottom = temp;
    }

    for (ptrdiff_t i = 0; i < arrlen(hearts); ++i) {
        Instance* heart = hearts[i];
        if (!heart->active || heart == self) continue;
        if (Collision_rectOverlapsInstance(runner, heart, left, top, right, bottom)) return true;
    }
    return false;
}

static bool sidedFireStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    Runner* runner = ctx->runner;

    RValue border = NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &borderBinding, "idealborder");
    GMLReal bottom = RValue_toReal(NativeScript_getIndex(border, RValue_makeReal(3.0)));
    if (self->y > bottom && self->vspeed > 0.0) Runner_destroyInstance(runner, self, true);

    border = NativeScript_getVariable(ctx, ctx->globalScopeInstance, &borderBinding, "idealborder");
    GMLReal top = RValue_toReal(NativeScript_getIndex(border, RValue_makeReal(2.0)));
    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
    if (self->y < top - height && self->vspeed < 0.0) Runner_destroyInstance(runner, self, true);

    if (collidesWithHeart(ctx, self)) {
        Runner_executeEventFromObject(runner, self, ctx->currentCallFrame->other,
            self->objectIndex, EVENT_OTHER, OTHER_USER0);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSidedFireStep_register(VMContext* ctx) {
    borderBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    heartBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sided_fire_Step_0", sidedFireStep);
}
