// manual native script of scr_colorcycle
#include "nativeScripts/native_runtime.h"

#include "instance.h"

static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding cBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding uBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding colorBinding = NATIVE_VAR_BINDING_INIT;
static NativeCallBinding makeColorBinding = NATIVE_CALL_BINDING_INIT;

static bool colorcycle(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    for (int32_t i = 0; i < 3; ++i) {
        RValue index = RValue_makeReal(i);
        NativeScript_setVariable(ctx, self, &iBinding, "i", index);
        RValue c = NativeScript_getVariable(ctx, self, &cBinding, "c");
        GMLReal channel = RValue_toReal(NativeScript_getIndex(c, index));

        if (channel < 10.0) {
            NativeScript_setVariableIndex(ctx, self, &uBinding, "u", index, RValue_makeReal(1.0));
        }
        if (channel > 250.0) {
            NativeScript_setVariableIndex(ctx, self, &uBinding, "u", index, RValue_makeReal(0.0));
        }

        RValue u = NativeScript_getVariable(ctx, self, &uBinding, "u");
        GMLReal direction = RValue_toReal(NativeScript_getIndex(u, index));
        channel = RValue_toReal(NativeScript_getIndex(c, index));
        if (direction == 1.0)
            channel += 2.0;
        else
            channel -= 3.0;
        NativeScript_setVariableIndex(ctx, self, &cBinding, "c", index, RValue_makeReal(channel));
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(3.0));

    RValue c = NativeScript_getVariable(ctx, self, &cBinding, "c");
    RValue channels[3] = {
        NativeScript_getIndex(c, RValue_makeReal(0.0)),
        NativeScript_getIndex(c, RValue_makeReal(1.0)),
        NativeScript_getIndex(c, RValue_makeReal(2.0)),
    };
    RValue color = NativeScript_call(ctx, &makeColorBinding, "make_color_rgb", channels, 3);
    NativeScript_setVariable(ctx, self, &colorBinding, "color", color);
    RValue_free(&color);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeColorcycle_register(VMContext* ctx) {
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    cBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    uBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    colorBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    makeColorBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_colorcycle", colorcycle);
}
