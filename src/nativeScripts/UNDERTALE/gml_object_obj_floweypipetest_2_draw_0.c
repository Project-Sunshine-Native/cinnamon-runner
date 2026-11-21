#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t frozen, siner, flash, blend2, op, i, offx, offy, offs, offv;
    int32_t offx2, offy2, offs2, offv2, magicfactor, magicfactor2, magicfactor3;
} cache;
static NativeVarBinding debugBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding rescueBinding = NATIVE_VAR_BINDING_INIT;
static NativeCallBinding ellipseBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding colorBinding = NATIVE_CALL_BINDING_INIT;
static GMLReal curveX[13], curveY[13], squash[13];

static bool floweypipetest2Draw(VMContext* ctx, RValue* outResult) {
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
            RValue_makeReal(flash == 1.0 ? self->x - 85.0 : (self->x - 105.0) + op),
            RValue_makeReal(self->y + 40.0),
            RValue_makeReal(flash == 1.0 ? self->x + 85.0 : self->x + 85.0 + op),
            RValue_makeReal(self->y - 40.0),
            RValue_makeReal(flash == 1.0 ? 16711680.0 : 16711935.0),
            RValue_makeReal(0.0), RValue_makeReal(0.0),
        };
        RValue result = NativeScript_call(ctx, &ellipseBinding, "draw_ellipse_color", ellipseArgs, 7);
        RValue_free(&result);
        renderer->drawAlpha = 1.0f;
        RValue colorArgs[3] = {
            RValue_makeReal(flash == 1.0 ? 120.0 - (sine / 2.0) * 100.0 : 230.0),
            RValue_makeReal(flash == 1.0 ? 120.0 - (sine / 2.0) * 100.0 : 100.0 - sine * 100.0),
            RValue_makeReal(flash == 1.0 ? 255.0 : 200.0),
        };
        result = NativeScript_call(ctx, &colorBinding, "make_color_rgb", colorArgs, 3);
        if (flash == 1.0) self->imageBlend = RValue_toColour(result);
        else Instance_setSelfVar(self, cache.blend2, result);
        RValue_free(&result);
    }

    GMLReal rescue = RValue_toReal(NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &rescueBinding, "soul_rescue"));
    uint32_t blend2 = RValue_toColour(Instance_getSelfVar(self, cache.blend2));
    GMLReal magicfactor = RValue_toReal(Instance_getSelfVar(self, cache.magicfactor));
    GMLReal magicfactor2 = RValue_toReal(Instance_getSelfVar(self, cache.magicfactor2));
    GMLReal magicfactor3 = RValue_toReal(Instance_getSelfVar(self, cache.magicfactor3));
    GMLReal wave[13], scale[13];
    int32_t frame = (int32_t)self->imageIndex;
    GMLReal offx, offy, offv;
    for (int32_t i = 0; i < 13; ++i) {
        wave[i] = GMLReal_cos(((GMLReal)i + siner) / 6.0) * 8.0;
        GMLReal offs = GMLReal_sin((siner + (GMLReal)i * 4.0) / 6.0) * 2.0;
        scale[i] = offs > 1.0 ? (offs - 1.0) / 3.0 : 0.0;
        offx = self->x + curveX[i];
        offy = self->y + curveY[i] + wave[i];
        offv = (GMLReal)i * -12.0 - 330.0;
        if (i > 5) offv = (GMLReal)i * -20.0 - 310.0;
        if (i > 7) offv = (GMLReal)i * -22.0 - 310.0;
        Renderer_drawSpriteExt(renderer, self->spriteIndex, frame, (float)offx, (float)offy,
            rescue < 3.0 ? (float)(1.0 + scale[i]) : 1.0f,
            rescue < 3.0 ? (float)((1.0 + scale[i]) - squash[i]) : 1.0f,
            (float)offv, rescue < 3.0 ? self->imageBlend : 0x808080u, 1.0f);
    }
    Instance_setSelfVar(self, cache.offx, RValue_makeReal(offx));
    Instance_setSelfVar(self, cache.offy, RValue_makeReal(offy));
    Instance_setSelfVar(self, cache.offs, RValue_makeReal(scale[12]));
    Instance_setSelfVar(self, cache.offv, RValue_makeReal(offv));
    for (int32_t i = 0; i < 13; ++i) {
        offx = (self->x - curveX[i]) + op;
        offy = self->y + curveY[i] + wave[i];
        offv = (GMLReal)i * 12.0 + magicfactor;
        if (i > 5) offv = (GMLReal)i * 30.0 + magicfactor2;
        if (i > 7) offv = (GMLReal)i * 20.0 + magicfactor3;
        Renderer_drawSpriteExt(renderer, self->spriteIndex, frame, (float)offx, (float)offy,
            rescue < 4.0 ? (float)(-1.0 - scale[i]) : -1.0f,
            rescue < 4.0 ? (float)((1.0 + scale[i]) - squash[i]) : 1.0f,
            (float)offv, rescue < 4.0 ? blend2 : 0x808080u, 1.0f);
    }
    Instance_setSelfVar(self, cache.offx2, RValue_makeReal(offx));
    Instance_setSelfVar(self, cache.offy2, RValue_makeReal(offy));
    Instance_setSelfVar(self, cache.offs2, RValue_makeReal(scale[12]));
    Instance_setSelfVar(self, cache.offv2, RValue_makeReal(offv));
    Instance_setSelfVar(self, cache.i, RValue_makeReal(13.0));
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweypipetest2Draw_register(VMContext* ctx) {
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.flash = NativeScript_getSelfVarID(ctx, "flash");
    cache.blend2 = NativeScript_getSelfVarID(ctx, "blend2");
    cache.op = NativeScript_getSelfVarID(ctx, "op");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
    cache.offx = NativeScript_getSelfVarID(ctx, "offx");
    cache.offy = NativeScript_getSelfVarID(ctx, "offy");
    cache.offs = NativeScript_getSelfVarID(ctx, "offs");
    cache.offv = NativeScript_getSelfVarID(ctx, "offv");
    cache.offx2 = NativeScript_getSelfVarID(ctx, "offx2");
    cache.offy2 = NativeScript_getSelfVarID(ctx, "offy2");
    cache.offs2 = NativeScript_getSelfVarID(ctx, "offs2");
    cache.offv2 = NativeScript_getSelfVarID(ctx, "offv2");
    cache.magicfactor = NativeScript_getSelfVarID(ctx, "magicfactor");
    cache.magicfactor2 = NativeScript_getSelfVarID(ctx, "magicfactor2");
    cache.magicfactor3 = NativeScript_getSelfVarID(ctx, "magicfactor3");
    debugBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rescueBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    ellipseBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    colorBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    for (int32_t i = 0; i < 13; ++i) {
        curveX[i] = GMLReal_sin(((GMLReal)i - 49.0) / 3.0) * 85.0;
        curveY[i] = GMLReal_cos(((GMLReal)i - 3.0) / 2.2) * 40.0;
        squash[i] = GMLReal_sin((GMLReal)i / 5.0) * 0.3;
    }
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweypipetest_2_Draw_0", floweypipetest2Draw);
}
