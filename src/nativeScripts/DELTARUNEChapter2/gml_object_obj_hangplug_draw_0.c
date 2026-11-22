#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static int32_t extensionSprite;

static bool hangplugDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    Renderer* renderer = ctx->runner->renderer;
    *outResult = RValue_makeUndefined();
    if (renderer == nullptr) return true;

    Renderer_drawSelf(renderer, self);
    int32_t frame = (int32_t)self->imageIndex;
    for (int32_t i = 0; i < 20; ++i) {
        GMLReal y = (GMLReal)self->y - (40 * i);
        Renderer_drawSpriteExt(renderer, extensionSprite, frame, self->x, y,
            2, 2, 0, self->imageBlend, self->imageAlpha);
    }
    return true;
}

void NativeChapter2HangplugDraw_register(VMContext* ctx) {
    extensionSprite = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "spr_ch2_gimmick_hangplug_extension") == 0) {
            extensionSprite = (int32_t)i;
            break;
        }
    }
    if (extensionSprite >= 0)
        (void)VM_registerNativeScript(ctx, "gml_Object_obj_hangplug_Draw_0", hangplugDraw);
}
