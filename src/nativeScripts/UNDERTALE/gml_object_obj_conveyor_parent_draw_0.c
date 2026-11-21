#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static bool conveyorParentDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    for (int32_t i = 0; (GMLReal)i < self->imageYscale; ++i) {
        for (int32_t j = 0; (GMLReal)j < self->imageXscale; ++j) {
            Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
                self->x + j * 20.0f, self->y + i * 20.0f,
                1.0f, 1.0f, 0.0f, 0xFFFFFFu, self->imageAlpha);
        }
    }

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeConveyorParentDraw_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_conveyor_parent_Draw_0", conveyorParentDraw);
}
