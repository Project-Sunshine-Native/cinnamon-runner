#include "native_runtime.h"
#include "asgore_battle.h"

#include "event_table.h"
#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static NativeVarBinding borderBinding = NATIVE_VAR_BINDING_INIT;

static bool sidedFireStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr || !NativeAsgoreBattle_isActive(ctx)) return false;
    Runner* runner = ctx->runner;

    RValue border = NativeScript_getVariable(ctx, ctx->globalScopeInstance,
        &borderBinding, "idealborder");
    GMLReal bottom = RValue_toReal(NativeScript_getIndex(border, RValue_makeReal(3.0)));
    if (self->y > bottom && self->vspeed > 0.0) Runner_destroyInstance(runner, self, true);

    border = NativeScript_getVariable(ctx, ctx->globalScopeInstance, &borderBinding, "idealborder");
    GMLReal top = RValue_toReal(NativeScript_getIndex(border, RValue_makeReal(2.0)));
    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
    if (self->y < top - height && self->vspeed < 0.0) Runner_destroyInstance(runner, self, true);

    if (NativeAsgoreBattle_collidesWithHeart(ctx, self)) {
        Runner_executeEventFromObject(runner, self, ctx->currentCallFrame->other,
            self->objectIndex, EVENT_OTHER, OTHER_USER0);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSidedFireStep_register(VMContext* ctx) {
    borderBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sided_fire_Step_0", sidedFireStep);
}
