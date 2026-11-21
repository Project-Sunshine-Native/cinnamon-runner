#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeCallBinding rectangleBinding = NATIVE_CALL_BINDING_INIT;

static bool trueAntiwaverDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canCall(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle")) return false;

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));

    GMLReal waveAlpha = GMLReal_sin(siner / 12.0) * 0.5 + 0.5;
    GMLReal right = self->x + 20.0 * self->imageXscale;
    renderer->drawColor = 0x000000u;

    for (int32_t i = 0; i < 8; ++i) {
        NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));
        renderer->drawAlpha = (float)(waveAlpha * (1.0 - (GMLReal)i / 8.0));

        GMLReal top = self->y + 16.0 - i * 5.0;
        GMLReal bottom = self->y + 20.0 - i * 5.0;
        RValue args[4] = {
            RValue_makeReal(self->x),
            RValue_makeReal(top),
            RValue_makeReal(right),
            RValue_makeReal(bottom),
        };
        RValue result = NativeScript_call(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle", args, 4);
        RValue_free(&result);
    }
    renderer->drawAlpha = 1.0f;
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(8.0));

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeTrueAntiwaverDraw_register(VMContext* ctx) {
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rectangleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_true_antiwaver_Draw_0", trueAntiwaverDraw);
}
