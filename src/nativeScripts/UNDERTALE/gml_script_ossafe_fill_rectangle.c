#include "nativeScripts/native_runtime.h"

#include "renderer.h"
#include "runner.h"

static bool ossafeFillRectangle(VMContext* ctx, RValue* outResult) {
    GMLReal x1 = RValue_toReal(NativeScript_getArgument(ctx, 0));
    GMLReal y1 = RValue_toReal(NativeScript_getArgument(ctx, 1));
    GMLReal x2 = RValue_toReal(NativeScript_getArgument(ctx, 2));
    GMLReal y2 = RValue_toReal(NativeScript_getArgument(ctx, 3));

    if (x1 > x2) {
        GMLReal temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if (y1 > y2) {
        GMLReal temp = y1;
        y1 = y2;
        y2 = temp;
    }

    Runner* runner = ctx->runner;
    if (runner->osType == OS_PS4 || runner->osType == OS_PSVITA) {
        x2++;
        y2++;
    }

    Renderer* renderer = runner->renderer;
    if (renderer != nullptr) {
        renderer->vtable->drawRectangle(renderer, (float)x1, (float)y1, (float)x2, (float)y2,
            renderer->drawColor, renderer->drawAlpha, false);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeOssafeFillRectangle_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Script_ossafe_fill_rectangle", ossafeFillRectangle);
}
