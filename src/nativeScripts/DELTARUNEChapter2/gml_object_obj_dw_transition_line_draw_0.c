#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static int32_t darkzoneId;

static bool dwTransitionLineDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    RValue darkzone = Instance_getSelfVar(ctx->globalScopeInstance, darkzoneId);
    *outResult = RValue_makeUndefined();
    if (renderer == nullptr || darkzone.type == RVALUE_UNDEFINED) return true;

    GMLReal zone = RValue_toReal(darkzone);
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(zone, 0, epsilon) == 0) {
        Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
            self->x, self->y, self->imageXscale, self->imageYscale, 0, 0xFFFFFF, self->imageAlpha);
    }
    if (NativeScript_compareReals(zone, 1, epsilon) == 0) {
        Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
            self->x * 2, self->y * 2, self->imageXscale * 2, self->imageYscale * 2,
            0, 0xFFFFFF, self->imageAlpha);
    }
    return true;
}

void NativeChapter2DwTransitionLineDraw_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_dw_transition_line_Draw_0", dwTransitionLineDraw)) return;

    darkzoneId = VM_getOrAllocateVarID(ctx, "darkzone");
}
