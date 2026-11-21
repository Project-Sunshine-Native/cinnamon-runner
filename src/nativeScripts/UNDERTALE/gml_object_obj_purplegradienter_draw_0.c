#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding acBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding amtBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding fadeBinding = NATIVE_VAR_BINDING_INIT;
static NativeCallBinding rectangleBinding = NATIVE_CALL_BINDING_INIT;

static bool purplegradienterDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canCall(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle")) return false;

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner"));
    GMLReal ac = 1.5 + GMLReal_sin(siner / 20.0);
    NativeScript_setVariable(ctx, self, &acBinding, "ac", RValue_makeReal(ac));
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner + 1.0));
    renderer->drawAlpha = 1.0f;

    GMLReal roomWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    GMLReal roomHeight = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
    GMLReal amt = RValue_toReal(NativeScript_getVariable(ctx, self, &amtBinding, "amt"));
    for (int32_t i = 0; i < 10; ++i) {
        NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(i));
        renderer->drawAlpha = (float)((0.8 - (GMLReal)i / 16.0) * amt);
        renderer->drawColor = 0x800080u;
        RValue args[4] = {
            RValue_makeReal(-10.0),
            RValue_makeReal(roomHeight - i * i * ac),
            RValue_makeReal(roomWidth + 10.0),
            RValue_makeReal(roomHeight - (i + 1) * (i + 1) * ac),
        };
        RValue result = NativeScript_call(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle", args, 4);
        RValue_free(&result);
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(10.0));
    renderer->drawAlpha = 1.0f;

    if (RValue_toReal(NativeScript_getVariable(ctx, self, &fadeBinding, "fade")) == 1.0) {
        amt -= 0.03;
        NativeScript_setVariable(ctx, self, &amtBinding, "amt", RValue_makeReal(amt));
        if (amt < 0.05) Runner_destroyInstance(ctx->runner, self, true);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativePurplegradienterDraw_register(VMContext* ctx) {
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    acBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    amtBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    fadeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rectangleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_purplegradienter_Draw_0", purplegradienterDraw);
}
