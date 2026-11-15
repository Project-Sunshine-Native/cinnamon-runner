#include "asgore_battle.h"

#include "collision.h"
#include "instance.h"
#include "runner.h"

#include "stb_ds.h"

static NativeAssetBinding asgoreBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding heartBinding = NATIVE_ASSET_BINDING_INIT;

void NativeAsgoreBattle_reset(void) {
    asgoreBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    heartBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
}

bool NativeAsgoreBattle_isActive(VMContext* ctx) {
    if (!NativeScript_canResolveAsset(ctx, &asgoreBinding, "obj_asgoreb") ||
        !NativeScript_canResolveAsset(ctx, &heartBinding, "obj_heart")) return false;
    if ((uint32_t)asgoreBinding.assetIndex >= ctx->dataWin->objt.count ||
        (uint32_t)heartBinding.assetIndex >= ctx->dataWin->objt.count) return false;
    Instance** instances = ctx->runner->instancesByObject[asgoreBinding.assetIndex];
    for (ptrdiff_t i = 0; i < arrlen(instances); ++i) {
        if (instances[i]->active) return true;
    }
    return false;
}

bool NativeAsgoreBattle_collidesWithHeart(VMContext* ctx, Instance* self) {
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
