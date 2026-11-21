#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t frozen, siner, flash, blend2, op, i, offx, offy, offs, offx2, offy2, offs2;
} cache;
static NativeVarBinding debugBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding rescueBinding = NATIVE_VAR_BINDING_INIT;
static NativeCallBinding ellipseBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding colorBinding = NATIVE_CALL_BINDING_INIT;
static GMLReal curveX[16], curveY[16];

static bool floweypipetest3Draw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (RValue_toReal(NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &debugBinding, "debug")) == 1.0) return false;

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    if (RValue_toReal(Instance_getSelfVar(self, cache.frozen)) == 0.0) {
        siner += 1.0;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
    GMLReal flash = RValue_toReal(Instance_getSelfVar(self, cache.flash));
    GMLReal op = RValue_toReal(Instance_getSelfVar(self, cache.op));
    if (flash == 0.0) {
        self->imageBlend = 0xFFFFFFu;
        Instance_setSelfVar(self, cache.blend2, RValue_makeReal(16777215.0));
    }
    if (flash == 1.0 || flash == 2.0) {
        GMLReal sine = GMLReal_sin(siner / 3.0);
        renderer->drawAlpha = (float)(sine / 2.0);
        RValue ellipseArgs[7] = {
            RValue_makeReal(flash == 1.0 ? self->x - 45.0 : (self->x - 45.0) + op),
            RValue_makeReal(self->y + 40.0),
            RValue_makeReal(flash == 1.0 ? self->x + 45.0 : self->x + 45.0 + op),
            RValue_makeReal(self->y - 40.0),
            RValue_makeReal(flash == 1.0 ? 32768.0 : 65535.0),
            RValue_makeReal(0.0), RValue_makeReal(0.0),
        };
        RValue result = NativeScript_call(ctx, &ellipseBinding, "draw_ellipse_color", ellipseArgs, 7);
        RValue_free(&result);
        renderer->drawAlpha = 1.0f;
        RValue colorArgs[3] = {
            RValue_makeReal(flash == 1.0 ? 100.0 - sine * 100.0 : 230.0),
            RValue_makeReal(flash == 1.0 ? 255.0 : 230.0),
            RValue_makeReal(flash == 1.0 ? 100.0 - sine * 100.0 : 130.0 - sine * 120.0),
        };
        result = NativeScript_call(ctx, &colorBinding, "make_color_rgb", colorArgs, 3);
        if (flash == 1.0) self->imageBlend = RValue_toColour(result);
        else Instance_setSelfVar(self, cache.blend2, result);
        RValue_free(&result);
    }

    GMLReal rescue = RValue_toReal(NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &rescueBinding, "soul_rescue"));
    uint32_t blend2 = RValue_toColour(Instance_getSelfVar(self, cache.blend2));
    GMLReal wave[16], scale[16];
    int32_t frame = (int32_t)self->imageIndex;
    GMLReal offx, offy;
    for (int32_t i = 0; i < 16; ++i) {
        wave[i] = GMLReal_cos(((GMLReal)i + siner) / 6.0) * 8.0;
        GMLReal offs = GMLReal_sin((siner + (GMLReal)i * 4.0) / 6.0) * 2.0;
        scale[i] = offs > 1.0 ? (offs - 1.0) / 3.0 : 0.0;
        offx = self->x + curveX[i];
        offy = self->y + curveY[i] + wave[i];
        Renderer_drawSpriteExt(renderer, self->spriteIndex, frame, (float)offx, (float)offy,
            rescue < 5.0 ? (float)(1.0 + scale[i]) : 1.0f,
            rescue < 5.0 ? (float)(1.0 + scale[i]) : 1.0f,
            (float)(i * 20 + 50), rescue < 5.0 ? self->imageBlend : 0x808080u, 1.0f);
    }
    Instance_setSelfVar(self, cache.offx, RValue_makeReal(offx));
    Instance_setSelfVar(self, cache.offy, RValue_makeReal(offy));
    Instance_setSelfVar(self, cache.offs, RValue_makeReal(scale[15]));
    for (int32_t i = 0; i < 16; ++i) {
        offx = (self->x - curveX[i]) + op;
        offy = self->y + curveY[i] + wave[i];
        Renderer_drawSpriteExt(renderer, self->spriteIndex, frame, (float)offx, (float)offy,
            rescue < 6.0 ? (float)(1.0 + scale[i]) : 1.0f,
            rescue < 6.0 ? (float)(1.0 + scale[i]) : 1.0f,
            (float)(i * -20 - 50), rescue < 6.0 ? blend2 : 0x808080u, 1.0f);
    }
    Instance_setSelfVar(self, cache.offx2, RValue_makeReal(offx));
    Instance_setSelfVar(self, cache.offy2, RValue_makeReal(offy));
    Instance_setSelfVar(self, cache.offs2, RValue_makeReal(scale[15]));
    Instance_setSelfVar(self, cache.i, RValue_makeReal(16.0));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweypipetest3Draw_register(VMContext* ctx) {
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.flash = NativeScript_getSelfVarID(ctx, "flash");
    cache.blend2 = NativeScript_getSelfVarID(ctx, "blend2");
    cache.op = NativeScript_getSelfVarID(ctx, "op");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
    cache.offx = NativeScript_getSelfVarID(ctx, "offx");
    cache.offy = NativeScript_getSelfVarID(ctx, "offy");
    cache.offs = NativeScript_getSelfVarID(ctx, "offs");
    cache.offx2 = NativeScript_getSelfVarID(ctx, "offx2");
    cache.offy2 = NativeScript_getSelfVarID(ctx, "offy2");
    cache.offs2 = NativeScript_getSelfVarID(ctx, "offs2");
    debugBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rescueBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    ellipseBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    colorBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    for (int32_t i = 0; i < 16; ++i) {
        curveX[i] = GMLReal_sin(((GMLReal)i - 34.0) / 3.0) * 45.0;
        curveY[i] = GMLReal_cos(((GMLReal)i - 34.0) / 3.0) * 40.0;
    }
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweypipetest_3_Draw_0", floweypipetest3Draw);
}
