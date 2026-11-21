#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "math_compat.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding ckBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding cognoBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding offxBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding offyBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding mediumCogBinding = NATIVE_ASSET_BINDING_INIT;

static bool cogsmallDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    bool large = self->imageXscale >= 2.0f;
    if (large && !NativeScript_canResolveAsset(ctx, &mediumCogBinding, "spr_cogmed_e")) return false;

    GMLReal ck = RValue_toReal(NativeScript_getVariable(ctx, self, &ckBinding, "ck"));
    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner"));
    if (ck == 0.0) siner += 6.0;
    if (ck == 1.0) siner -= 6.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));

    renderer->circlePrecision = large ? 8 : 4;
    GMLReal cogno = RValue_toReal(NativeScript_getVariable(ctx, self, &cognoBinding, "cogno"));
    GMLReal xscale = self->imageXscale;
    GMLReal yscale = self->imageYscale;
    GMLReal centerOffset = large ? 0.4 : 0.3;
    GMLReal primitiveOffset = ctx->runner->applyOffsetForPrimitives ? 1.0 : 0.0;

    if (cogno > 0.0) renderer->drawColor = 0x000080u;
    int32_t i;
    for (i = 0; (GMLReal)i < cogno; ++i) {
        GMLReal angle = (((GMLReal)i / cogno) * 360.0 + siner) * (M_PI / 180.0);
        GMLReal dx = 8.0 * GMLReal_cos(angle);
        GMLReal dy = -8.0 * GMLReal_sin(angle);
        if (GMLReal_fabs(dx) < 1e-4) dx = 0.0;
        if (GMLReal_fabs(dy) < 1e-4) dy = 0.0;

        GMLReal x = self->x - centerOffset * xscale + dx * xscale + primitiveOffset;
        GMLReal y = self->y - centerOffset * yscale + dy * yscale + primitiveOffset;
        Renderer_drawCircle(renderer, (float)x, (float)y, (float)(2.0 * xscale), false);
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));

    GMLReal offx = RValue_toReal(NativeScript_getVariable(ctx, self, &offxBinding, "offx"));
    GMLReal offy = RValue_toReal(NativeScript_getVariable(ctx, self, &offyBinding, "offy"));
    int32_t sprite = large ? mediumCogBinding.assetIndex : self->spriteIndex;
    float spriteScale = large ? 0.5f : 1.0f;
    Renderer_drawSpriteExt(renderer, sprite, 0, (float)(self->x + offx), (float)(self->y + offy),
        (float)(xscale * spriteScale), (float)(yscale * spriteScale), 0.0f, 0xFFFFFFu, 1.0f);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeCogsmallDraw_register(VMContext* ctx) {
    ckBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    cognoBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    offxBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    offyBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    mediumCogBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_cogsmall_Draw_0", cogsmallDraw);
}
