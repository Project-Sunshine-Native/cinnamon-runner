#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

static NativeCallBinding colorcycleBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding rectangleBinding = NATIVE_CALL_BINDING_INIT;
static NativeVarBinding colorBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding mBinding = NATIVE_VAR_BINDING_INIT;

static bool finalbarrierDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (!NativeScript_canCall(ctx, &colorcycleBinding, "gml_Script_scr_colorcycle") ||
        !NativeScript_canCall(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle")) return false;

    RValue result = NativeScript_call(ctx, &colorcycleBinding, "gml_Script_scr_colorcycle", nullptr, 0);
    RValue_free(&result);

    RValue color = NativeScript_getVariable(ctx, self, &colorBinding, "color");
    Renderer* renderer = ctx->runner->renderer;
    if (renderer != nullptr) renderer->drawColor = RValue_toColour(color);

    GMLReal roomWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    GMLReal roomHeight = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
    GMLReal m = RValue_toReal(NativeScript_getVariable(ctx, self, &mBinding, "m"));
    RValue args[4] = {
        RValue_makeReal(self->x),
        RValue_makeReal(self->y),
        RValue_makeReal(roomWidth / 2.0 + roomWidth / m),
        RValue_makeReal(roomHeight / 2.0 + roomHeight / m),
    };
    result = NativeScript_call(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle", args, 4);
    RValue_free(&result);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFinalbarrierDraw_register(VMContext* ctx) {
    colorcycleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    rectangleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    colorBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    mBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_finalbarrier_Draw_0", finalbarrierDraw);
}
