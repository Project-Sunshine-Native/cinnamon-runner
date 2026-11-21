#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

#include "3ds/3ds_renderer.h"

static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding boffBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding coffBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding aBinding = NATIVE_VAR_BINDING_INIT;
static NativeCallBinding rectangleBinding = NATIVE_CALL_BINDING_INIT;

static bool trueLavawaverDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canCall(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle")) return false;

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));

    GMLReal b = RValue_toReal(NativeScript_getVariable(ctx, self, &boffBinding, "boff"));
    GMLReal c = RValue_toReal(NativeScript_getVariable(ctx, self, &coffBinding, "coff"));
    GMLReal a = RValue_toReal(NativeScript_getVariable(ctx, self, &aBinding, "a")) + 3.0;
    GMLReal spriteWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1));

    int32_t tpagIndex = Renderer_resolveTPAGIndex(renderer->dataWin,
        self->spriteIndex, (int32_t)self->imageIndex);
    TexturePageItem* tpag = tpagIndex >= 0 ? &renderer->dataWin->tpag.items[tpagIndex] : nullptr;
    float left = 0.0f;
    float width = (float)spriteWidth;
    float xOffset = 0.0f;

    if (tpag != nullptr) {
        if (tpag->targetX > left) {
            xOffset = tpag->targetX - left;
            width -= xOffset;
        } else {
            left -= tpag->targetX;
        }
        if (width > tpag->sourceWidth - left) width = tpag->sourceWidth - left;
    }

    GMLReal waveX[20];
    for (int32_t i = 0; i < 20; ++i) {
        a += 1.0;
        waveX[i] = self->x + GMLReal_sin(a / b) * c;
    }

    bool drawn = C3D_drawTrueLavaWave(renderer, tpagIndex, left, width, xOffset,
        self->y, waveX, renderer->drawAlpha);
    if (!drawn && tpag != nullptr && width > 0.0f) {
        for (int32_t i = 0; i < 20; ++i) {
            float top = (float)(i * 2);
            float height = 2.0f;
            float yOffset = 0.0f;
            if (tpag->targetY > top) {
                yOffset = tpag->targetY - top;
                height -= yOffset;
                top = 0.0f;
            } else {
                top -= tpag->targetY;
            }
            if (height > tpag->sourceHeight - top) height = tpag->sourceHeight - top;
            if (height <= 0.0f) continue;

            GMLReal xx = waveX[i];
            for (int32_t g = 0; g < 4; ++g) {
                GMLReal yy = self->y + i * 2;
                for (int32_t f = 0; f < 8; ++f) {
                    renderer->vtable->drawSpritePart(renderer, tpagIndex,
                        left, top, width, height, (float)xx + xOffset, (float)yy + yOffset,
                        1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0xFFFFFFu, renderer->drawAlpha);
                    yy += 40.0;
                }
                xx += 100.0;
            }
        }
    }
    NativeScript_setVariable(ctx, self, &aBinding, "a", RValue_makeReal(a));

    renderer->drawAlpha = (float)(GMLReal_sin(siner / 12.0) * 0.3 + 0.5);
    renderer->drawColor = 0x000000u;

    GMLReal viewX = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_VIEW_XVIEW, "view_xview", 0));
    GMLReal viewY = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_VIEW_YVIEW, "view_yview", 0));
    RValue rectangleArgs[4] = {
        RValue_makeReal(viewX - 10.0),
        RValue_makeReal(viewY - 10.0),
        RValue_makeReal(viewX + 330.0),
        RValue_makeReal(viewY + 250.0),
    };
    RValue result = NativeScript_call(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle", rectangleArgs, 4);
    RValue_free(&result);
    renderer->drawAlpha = 1.0f;

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeTrueLavawaverDraw_register(VMContext* ctx) {
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    boffBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    coffBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    aBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rectangleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_true_lavawaver_Draw_0", trueLavawaverDraw);
}
