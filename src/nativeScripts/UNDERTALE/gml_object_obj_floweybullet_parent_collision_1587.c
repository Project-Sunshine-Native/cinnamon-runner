#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"

static int32_t invulnerabilityVar;

static bool floweyBulletDamage(VMContext* ctx, RValue* outResult) {
    RValue invulnerability = Instance_getSelfVar(ctx->globalScopeInstance, invulnerabilityVar);
    if (invulnerability.type != RVALUE_REAL && invulnerability.type != RVALUE_INT32)
        return false;
    if (NativeScript_compareReals(RValue_toReal(invulnerability), 0.0, ctx->runner->mathEpsilon) < 0)
        return false;

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyBulletDamage_register(VMContext* ctx) {
    invulnerabilityVar = -1;
    for (uint32_t i = 0; i < ctx->dataWin->vari.variableCount; ++i) {
        Variable* variable = &ctx->dataWin->vari.variables[i];
        if (variable->instanceType == INSTANCE_GLOBAL && strcmp(variable->name, "my_inv") == 0) {
            invulnerabilityVar = variable->varID;
            break;
        }
    }
    if (invulnerabilityVar < 0) return;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweybullet_parent_Collision_1587", floweyBulletDamage);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweybullet_parent_Other_22", floweyBulletDamage);
}
