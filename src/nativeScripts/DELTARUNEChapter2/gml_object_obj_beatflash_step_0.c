#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static bool beatflashStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
        RValue_makeReal(GMLReal_fmax(0, (GMLReal)self->imageXscale * (GMLReal)0.8)), -1);
    self->imageAlpha = (float)GMLReal_fmax(0, (GMLReal)self->imageAlpha - (GMLReal)0.016666666666666666);
    if (NativeScript_compareReals(self->imageXscale, 0, ctx->runner->mathEpsilon) <= 0)
        Runner_destroyInstance(ctx->runner, self, true);
    *outResult = RValue_makeUndefined();
    return true;
}

static bool beatflashDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (renderer == nullptr) return false;
    *outResult = RValue_makeUndefined();
    if (self->imageAlpha <= 0) return true;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal angle = self->imageAngle;
    bool down = NativeScript_compareReals(angle, 0, epsilon) == 0;
    bool left = NativeScript_compareReals(angle, 270, epsilon) == 0;
    bool right = NativeScript_compareReals(angle, 90, epsilon) == 0;
    if (down || left || right) {
        RValue widthValue = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1);
        GMLReal halfWidth = RValue_toReal(widthValue) * (GMLReal)0.5;
        RValue_free(&widthValue);
        GMLReal x = down ? (GMLReal)self->x - halfWidth : self->x;
        GMLReal y = self->y;
        if (left) y -= halfWidth;
        if (right) y += halfWidth;
        Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
            (float)x, (float)y, self->imageXscale, self->imageYscale, (float)angle,
            self->imageBlend, self->imageAlpha);
    }
    return true;
}

static bool beatFlashOWStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(self->imageAlpha, 0, epsilon) > 0)
        self->imageAlpha = (float)((GMLReal)self->imageAlpha * (GMLReal)0.9);
    if (NativeScript_compareReals(self->imageAlpha, 0.01, epsilon) < 0)
        self->imageAlpha = 0;
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2Beatflash_register(VMContext* ctx) {
    VM_registerNativeScript(ctx, "gml_Object_obj_beatflash_Step_0", beatflashStep);
    VM_registerNativeScript(ctx, "gml_Object_obj_beatflash_Draw_0", beatflashDraw);
    VM_registerNativeScript(ctx, "gml_Object_obj_beatFlashOW_Step_0", beatFlashOWStep);
}
