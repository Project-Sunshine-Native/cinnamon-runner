#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static NativeVarBinding sizeBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding goalSizeBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding ggBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding rrBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding vvBinding = NATIVE_VAR_BINDING_INIT;

static bool orangeparticleStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal size = RValue_toReal(NativeScript_getVariable(ctx, self, &sizeBinding, "size"));
    GMLReal goalsize = RValue_toReal(NativeScript_getVariable(ctx, self, &goalSizeBinding, "goalsize"));
    if (goalsize > size + 0.1) size += 0.03;
    if (goalsize < size - 0.1) size -= 0.03;
    NativeScript_setVariable(ctx, self, &sizeBinding, "size", RValue_makeReal(size));

    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(size), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(size), -1);
    self->imageAlpha -= 0.01;
    if (self->imageAlpha < 0.01) Runner_destroyInstance(ctx->runner, self, true);

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));
    GMLReal gg = RValue_toReal(NativeScript_getVariable(ctx, self, &ggBinding, "gg"));
    GMLReal rr = RValue_toReal(NativeScript_getVariable(ctx, self, &rrBinding, "rr"));
    GMLReal vv = RValue_toReal(NativeScript_getVariable(ctx, self, &vvBinding, "vv"));
    GMLReal phase = siner / gg;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
        RValue_makeReal(self->x + GMLReal_sin(phase) * rr), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
        RValue_makeReal(self->y + GMLReal_cos(phase) * vv), -1);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeOrangeparticleStep_register(VMContext* ctx) {
    sizeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    goalSizeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    ggBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rrBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    vvBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_orangeparticle_Step_0", orangeparticleStep);
}
