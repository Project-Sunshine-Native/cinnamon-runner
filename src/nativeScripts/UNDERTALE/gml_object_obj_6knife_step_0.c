#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t type, relx, rely, inita, shake, dd;
} cache;
static NativeAssetBinding actBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding heartBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding randomBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding distanceBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding moveBinding = NATIVE_CALL_BINDING_INIT;

static bool knifeStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal type = RValue_toReal(Instance_getSelfVar(self, cache.type));
    if (!NativeScript_canResolveAsset(ctx, &actBinding, "obj_6knife_act") ||
        !NativeScript_canResolveAsset(ctx, &heartBinding, "obj_vsflowey_heart")) return false;
    Instance* heart = nullptr;
    if (NativeScript_compareReals(type, 1.0, epsilon) == 0) {
        heart = VM_findInstanceByTarget(ctx, heartBinding.assetIndex);
        if (heart == nullptr) return false;
    }

    if (NativeScript_compareReals(type, 0.0, epsilon) == 0 ||
        NativeScript_compareReals(type, 2.0, epsilon) == 0) {
        Instance* act = VM_findInstanceByTarget(ctx, actBinding.assetIndex);
        if (act != nullptr) {
            GMLReal relx = RValue_toReal(Instance_getSelfVar(self, cache.relx));
            GMLReal rely = RValue_toReal(Instance_getSelfVar(self, cache.rely));
            GMLReal inita = RValue_toReal(Instance_getSelfVar(self, cache.inita));
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(relx + act->x), -1);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(rely + act->y), -1);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
                RValue_makeReal(act->imageAngle + inita), -1);
        }
        GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_HEIGHT, "room_height", -1));
        GMLReal width = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
        GMLReal spriteWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1));
        if (NativeScript_compareReals(self->y, height + 60.0, epsilon) > 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y - spriteWidth * 6.0), -1);
            GMLReal rely = RValue_toReal(Instance_getSelfVar(self, cache.rely)) - spriteWidth * 6.0;
            Instance_setSelfVar(self, cache.rely, RValue_makeReal(rely));
        }
        if (NativeScript_compareReals(self->y, -60.0, epsilon) < 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + spriteWidth * 6.0), -1);
            GMLReal rely = RValue_toReal(Instance_getSelfVar(self, cache.rely)) + spriteWidth * 6.0;
            Instance_setSelfVar(self, cache.rely, RValue_makeReal(rely));
        }
        if (NativeScript_compareReals(self->x, width + 60.0, epsilon) > 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(self->x - spriteWidth * 7.0), -1);
            GMLReal relx = RValue_toReal(Instance_getSelfVar(self, cache.relx)) - spriteWidth * 7.0;
            Instance_setSelfVar(self, cache.relx, RValue_makeReal(relx));
        }
        if (NativeScript_compareReals(self->x, -60.0, epsilon) < 0) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(self->x + spriteWidth * 7.0), -1);
            GMLReal relx = RValue_toReal(Instance_getSelfVar(self, cache.relx)) + spriteWidth * 7.0;
            Instance_setSelfVar(self, cache.relx, RValue_makeReal(relx));
        }
        if (NativeScript_compareReals(type, 2.0, epsilon) == 0) {
            GMLReal shake = RValue_toReal(Instance_getSelfVar(self, cache.shake)) + 0.2;
            Instance_setSelfVar(self, cache.shake, RValue_makeReal(shake));
            RValue arg = RValue_makeReal(shake);
            GMLReal x1 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
            GMLReal x2 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(self->x + (x1 - x2)), -1);
            GMLReal y1 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
            GMLReal y2 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + (y1 - y2)), -1);
        }
    }
    if (NativeScript_compareReals(type, 1.0, epsilon) == 0) {
        RValue target = RValue_makeReal(heartBinding.assetIndex);
        GMLReal dd = RValue_toReal(NativeScript_call(ctx, &distanceBinding, "distance_to_object", &target, 1));
        Instance_setSelfVar(self, cache.dd, RValue_makeReal(dd));
        const GMLReal distances[] = {240.0, 180.0, 120.0, 60.0, 30.0};
        int32_t speed = 1;
        for (int i = 0; i < 5; ++i) {
            if (NativeScript_compareReals(dd, distances[i], epsilon) < 0) speed = i + 2;
        }
        RValue moveArgs[3] = {
            RValue_makeReal(heart->x + 8.0), RValue_makeReal(heart->y + 8.0), RValue_makeReal(speed),
        };
        RValue result = NativeScript_call(ctx, &moveBinding, "move_towards_point", moveArgs, 3);
        RValue_free(&result);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
            RValue_makeReal(self->imageAngle + self->speed), -1);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6KnifeStep_register(VMContext* ctx) {
    cache.type = NativeScript_getSelfVarID(ctx, "type");
    cache.relx = NativeScript_getSelfVarID(ctx, "relx");
    cache.rely = NativeScript_getSelfVarID(ctx, "rely");
    cache.inita = NativeScript_getSelfVarID(ctx, "inita");
    cache.shake = NativeScript_getSelfVarID(ctx, "shake");
    cache.dd = NativeScript_getSelfVarID(ctx, "dd");
    actBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    heartBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    randomBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    distanceBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    moveBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6knife_Step_0", knifeStep);
}
