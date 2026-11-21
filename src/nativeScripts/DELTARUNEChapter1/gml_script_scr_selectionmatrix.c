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

static struct {
    int32_t slmxx, slmyy, s_siner, charcolor, i, myxx;
} cache;

static bool selectionMatrix(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    RValue argX = NativeScript_getArgument(ctx, 0);
    RValue argY = NativeScript_getArgument(ctx, 1);
    Instance_setSelfVar(self, cache.slmxx, argX);
    Instance_setSelfVar(self, cache.slmyy, argY);
    GMLReal x = RValue_toReal(argX);
    GMLReal y = RValue_toReal(argY);
    GMLReal right = x + 210;
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.s_siner)) + 2;
    Instance_setSelfVar(self, cache.s_siner, RValue_makeReal(siner));

    uint32_t color = RValue_toColour(Instance_getSelfVar(self, cache.charcolor));
    renderer->drawColor = color;
    renderer->vtable->drawRectangle(renderer, (float)x, (float)y, (float)right, (float)(y + 3),
        color, renderer->drawAlpha, false);

    float pixelOffset = runner->applyOffsetForPrimitives ? 1.0f : 0.0f;
    float leftLine = (float)x + pixelOffset;
    float rightLine = (float)right + pixelOffset;
    float top = (float)y + pixelOffset;
    float bottom = (float)(y + 33) + pixelOffset;
    bool skipInvisibleLines = false;
#ifdef PLATFORM_3DS
    if (renderer->d3d == nullptr && renderer->vtable->gpuGetBlendEnable(renderer) &&
        renderer->vtable->gpuGetBlendMode(renderer) == bm_normal) {
        BlendFactors blend = renderer->vtable->gpuGetBlendFactors(renderer);
        skipInvisibleLines = blend.dstAlpha == bm_inv_src_alpha;
    }
#endif
    GMLReal phase = siner;
    for (int32_t i = 0; i < 12; ++i) {
        phase = siner + (GMLReal)i * (GMLReal)31.41592653589793;
        GMLReal angle = phase / (GMLReal)60;
        GMLReal sine = GMLReal_sin(angle);
        float alpha = (float)sine;
        if (skipInvisibleLines && alpha <= 0.0f) continue;
        renderer->drawAlpha = alpha;
        renderer->vtable->drawLine(renderer, leftLine, top, leftLine, bottom, 2.0f, color, alpha);
        renderer->vtable->drawLine(renderer, rightLine, top, rightLine, bottom, 2.0f, color, alpha);
        if (NativeScript_compareReals(GMLReal_cos(angle), 0, runner->mathEpsilon) < 0) {
            GMLReal offset = sine * 30;
            float innerLeft = (float)((x - offset) + 30) + pixelOffset;
            float innerRight = (float)((right + offset) - 30) + pixelOffset;
            renderer->vtable->drawLine(renderer, innerLeft, top, innerLeft, bottom, 2.0f, color, alpha);
            renderer->vtable->drawLine(renderer, innerRight, top, innerRight, bottom, 2.0f, color, alpha);
        }
    }

    Instance_setSelfVar(self, cache.myxx, RValue_makeReal(phase));
    Instance_setSelfVar(self, cache.i, RValue_makeReal(12.0));
    renderer->drawAlpha = 1.0f;
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeSelectionMatrix_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0) ||
        !VM_registerNativeScript(ctx, "gml_Script_scr_selectionmatrix", selectionMatrix)) return;

    cache.slmxx = NativeScript_getSelfVarID(ctx, "slmxx");
    cache.slmyy = NativeScript_getSelfVarID(ctx, "slmyy");
    cache.s_siner = NativeScript_getSelfVarID(ctx, "s_siner");
    cache.charcolor = NativeScript_getSelfVarID(ctx, "charcolor");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
    cache.myxx = NativeScript_getSelfVarID(ctx, "myxx");
}
