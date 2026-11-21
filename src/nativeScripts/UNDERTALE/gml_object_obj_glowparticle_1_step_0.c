#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "random.h"
#include "runner.h"

static NativeVarBinding apBinding = NATIVE_VAR_BINDING_INIT;

static bool glowparticle1Step(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal ap = RValue_toReal(NativeScript_getVariable(ctx, self, &apBinding, "ap"));
    if (NativeScript_compareReals(ap, 0.0, ctx->runner->mathEpsilon) == 0)
        self->imageAlpha += 0.25f;

    if (NativeScript_compareReals(self->imageAlpha, 0.6, ctx->runner->mathEpsilon) > 0)
        NativeScript_setVariable(ctx, self, &apBinding, "ap", RValue_makeReal(1.0));

    GMLReal randomDirection = (GMLReal)Random_nextUInt32(&ctx->runner->random) / (GMLReal)BS_RAND_MAX;
    GMLReal direction = GMLReal_fmod((GMLReal)self->direction + (randomDirection * 6.0 - 3.0), 360.0);
    if (direction < 0.0) direction += 360.0;
    self->direction = (float)direction;

    GMLReal randomSpeed = (GMLReal)Random_nextUInt32(&ctx->runner->random) / (GMLReal)BS_RAND_MAX;
    self->speed = (float)((GMLReal)self->speed + (randomSpeed * 0.04 - 0.02));
    Instance_computeComponentsFromSpeed(self);

    self->imageAlpha = (float)((GMLReal)self->imageAlpha - 0.01);
    if (NativeScript_compareReals(self->imageAlpha, 0.02, ctx->runner->mathEpsilon) < 0)
        Runner_destroyInstance(ctx->runner, self, true);

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeGlowparticle1Step_register(VMContext* ctx) {
    apBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_glowparticle_1_Step_0", glowparticle1Step);
}
