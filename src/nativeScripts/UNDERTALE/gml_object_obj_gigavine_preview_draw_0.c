#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "math_compat.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t active, onoff, xxl, yyl, nowtime, maxtime, gv, memorymode;
} cache;
static NativeAssetBinding vineBinding = NATIVE_ASSET_BINDING_INIT;

static void drawWarningLines(Runner* runner, Instance* self, GMLReal xx, GMLReal yy) {
    Renderer* renderer = runner->renderer;
    if (renderer == nullptr) return;
    float offset = runner->applyOffsetForPrimitives ? 1.0f : 0.0f;
    float y1 = self->y + offset;
    float y2 = (float)(self->y + yy) + offset;
    for (int side = -8; side <= 8; side += 16) {
        float x1 = (float)((GMLReal)self->x + side) + offset;
        float x2 = (float)(self->x + xx + side) + offset;
        renderer->vtable->drawLine(renderer, x1, y1, x2, y2, 2.0f,
            renderer->drawColor, renderer->drawAlpha);
    }
}

static bool gigavinePreviewDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (!NativeScript_canResolveAsset(ctx, &vineBinding, "obj_gigavine") ||
        (uint32_t)vineBinding.assetIndex >= ctx->dataWin->objt.count) return false;

    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.active)), 1.0, epsilon) == 0) {
        GMLReal onoff = RValue_toReal(Instance_getSelfVar(self, cache.onoff)) + 1.0;
        if (NativeScript_compareReals(onoff, 2.0, epsilon) > 0) onoff = 0.0;
        Instance_setSelfVar(self, cache.onoff, RValue_makeReal(onoff));
        if (runner->renderer != nullptr) {
            const uint32_t colors[] = {0x0000FFu, 0x40A0FFu, 0x00FFFFu};
            for (int i = 0; i < 3; ++i) {
                if (NativeScript_compareReals(onoff, i, epsilon) == 0)
                    runner->renderer->drawColor = colors[i];
            }
        }

        GMLReal radians = (GMLReal)self->direction * (M_PI / 180.0);
        GMLReal xx = 600.0 * GMLReal_cos(radians);
        GMLReal yy = -600.0 * GMLReal_sin(radians);
        if (GMLReal_fabs(xx) < (GMLReal)1e-4) xx = 0.0;
        if (GMLReal_fabs(yy) < (GMLReal)1e-4) yy = 0.0;
        Instance_setSelfVar(self, cache.xxl, RValue_makeReal(xx));
        Instance_setSelfVar(self, cache.yyl, RValue_makeReal(yy));
        drawWarningLines(runner, self, xx, yy);

        GMLReal nowtime = RValue_toReal(Instance_getSelfVar(self, cache.nowtime)) + 1.0;
        Instance_setSelfVar(self, cache.nowtime, RValue_makeReal(nowtime));
        GMLReal maxtime = RValue_toReal(Instance_getSelfVar(self, cache.maxtime));
        if (NativeScript_compareReals(nowtime, maxtime, epsilon) > 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(self->direction), -1);
            Instance* vine = Runner_createInstance(runner, self->x, self->y, vineBinding.assetIndex);
            if (vine != nullptr && ctx->creatorVarID >= 0)
                Instance_setSelfVar(vine, ctx->creatorVarID, RValue_makeReal(self->instanceId));
            Instance_setSelfVar(self, cache.gv,
                RValue_makeReal(vine != nullptr ? (GMLReal)vine->instanceId : INSTANCE_NOONE));
            RValue memorymode = Instance_getSelfVar(self, cache.memorymode);
            if (vine != nullptr) {
                Instance_setSelfVar(vine, cache.memorymode, memorymode);
                VMBuiltins_setVariable(ctx, vine, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", RValue_makeReal(self->imageAngle), -1);
            }
            if (NativeScript_compareReals(RValue_toReal(memorymode), 0.0, epsilon) == 0) {
                Runner_destroyInstance(runner, self, true);
            } else {
                Instance_setSelfVar(self, cache.active, RValue_makeReal(0.0));
                self->visible = false;
            }
        }
    }

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeGigavinePreviewDraw_register(VMContext* ctx) {
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.onoff = NativeScript_getSelfVarID(ctx, "onoff");
    cache.xxl = NativeScript_getSelfVarID(ctx, "xxl");
    cache.yyl = NativeScript_getSelfVarID(ctx, "yyl");
    cache.nowtime = NativeScript_getSelfVarID(ctx, "nowtime");
    cache.maxtime = NativeScript_getSelfVarID(ctx, "maxtime");
    cache.gv = NativeScript_getSelfVarID(ctx, "gv");
    cache.memorymode = NativeScript_getSelfVarID(ctx, "memorymode");
    vineBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_gigavine_preview_Draw_0", gigavinePreviewDraw);
}
