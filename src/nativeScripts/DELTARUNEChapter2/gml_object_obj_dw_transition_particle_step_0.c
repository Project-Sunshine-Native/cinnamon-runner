#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static bool dwTransitionParticleStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    self->imageAlpha = (float)((GMLReal)self->imageAlpha - 0.05);
    if (NativeScript_compareReals(self->imageAlpha, 0, ctx->runner->mathEpsilon) <= 0) {
        Runner_destroyInstance(ctx->runner, self, true);
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2DwTransitionParticleStep_register(VMContext* ctx) {
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_dw_transition_particle_Step_0",
        dwTransitionParticleStep);
}
