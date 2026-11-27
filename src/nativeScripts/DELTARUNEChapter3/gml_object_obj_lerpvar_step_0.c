#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t variable, varname, pointa, pointb, time, maxtime, target, init, easetype, easeinout;
    int32_t respectGlobalInteract, lerpvarRef, lerpvarRefOuter, globalInteract;
    int32_t lerpvarObject;
    BuiltinFunc variableInstanceGet, variableInstanceSet, lerp;
    Chapter3InstanceCreator instanceCreate;
    NativeCallBinding easeOut, easeIn, easeInOut;
} cache;

static bool lerpvarCreate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Instance_setSelfVar(self, cache.variable, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.varname, RValue_makeString("variable"));
    Instance_setSelfVar(self, cache.pointa, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.pointb, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.time, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.maxtime, RValue_makeReal(30));
    Instance_setSelfVar(self, cache.target, RValue_makeReal(-1));
    Instance_setSelfVar(self, cache.init, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.easetype, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.easeinout, RValue_makeString("out"));
    Instance_setSelfVar(self, cache.respectGlobalInteract, RValue_makeInt32(0));
    *outResult = RValue_makeUndefined();
    return true;
}

static void applyValue(VMContext* ctx, RValue target, RValue varname, RValue value) {
    RValue args[] = { target, varname, value };
    RValue result = cache.variableInstanceSet(ctx, args, 3);
    RValue_free(&result);
}

static bool lerpvarStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    *outResult = RValue_makeUndefined();

    RValue target = Instance_getSelfVar(self, cache.target);
    if (!Chapter3_instanceExists(ctx, target)) {
        Runner_destroyInstance(runner, self, true);
        return true;
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.init), 0) == 0) {
        if (Instance_getSelfVar(self, cache.pointa).type == RVALUE_STRING) {
            RValue args[] = { target, Instance_getSelfVar(self, cache.varname) };
            RValue value = cache.variableInstanceGet(ctx, args, 2);
            Instance_setSelfVar(self, cache.pointa, value);
            RValue_free(&value);
        }
        Instance_setSelfVar(self, cache.init, RValue_makeReal(1));
    }

    bool cont = true;
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.respectGlobalInteract), 1) == 0) {
        if (Chapter3_compare(ctx, Chapter3_getReal(ctx->globalScopeInstance, cache.globalInteract), 0) != 0)
            cont = false;
    }
    if (cont) Instance_setSelfVar(self, cache.time, Chapter3_increment(Instance_getSelfVar(self, cache.time)));

    GMLReal t = Chapter3_getReal(self, cache.time) / Chapter3_getReal(self, cache.maxtime);
    target = Instance_getSelfVar(self, cache.target);
    RValue varname = Instance_getSelfVar(self, cache.varname);
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.easetype), 0) == 0) {
        RValue args[] = { Instance_getSelfVar(self, cache.pointa), Instance_getSelfVar(self, cache.pointb), RValue_makeReal(t) };
        RValue value = cache.lerp(ctx, args, 3);
        applyValue(ctx, target, varname, value);
        RValue_free(&value);
    } else {
        static const char* const modes[] = { "out", "in", "inout" };
        NativeCallBinding* bindings[] = { &cache.easeOut, &cache.easeIn, &cache.easeInOut };
        static const char* const scripts[] = { "gml_Script_lerp_ease_out", "gml_Script_lerp_ease_in", "gml_Script_lerp_ease_inout" };
        for (int32_t i = 0; 3 > i; i++) {
            if (!Chapter3_isString(Instance_getSelfVar(self, cache.easeinout), modes[i])) continue;
            RValue args[] = { Instance_getSelfVar(self, cache.pointa), Instance_getSelfVar(self, cache.pointb),
                RValue_makeReal(t), Instance_getSelfVar(self, cache.easetype) };
            RValue value = NativeScript_call(ctx, bindings[i], scripts[i], args, 4);
            applyValue(ctx, Instance_getSelfVar(self, cache.target), Instance_getSelfVar(self, cache.varname), value);
            RValue_free(&value);
        }
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.time), Chapter3_getReal(self, cache.maxtime)) >= 0)
        Runner_destroyInstance(runner, self, true);
    return true;
}

static RValue createLerpvar(VMContext* ctx, RValue target, int32_t argOffset, int32_t argCount, bool withEase) {
    Instance* self = ctx->currentCallFrame->self;
    RValue id = Chapter3_instanceCreate(ctx, &cache.instanceCreate, RValue_makeReal(0), RValue_makeReal(0), RValue_makeReal((GMLReal)cache.lerpvarObject));
    Instance_setSelfVar(self, cache.lerpvarRef, id);
    Instance* lerpvar = Chapter3_resolveInstance(ctx->runner, id);
    if (lerpvar != nullptr) {
        Instance_setSelfVar(lerpvar, cache.target, target);
        Instance_setSelfVar(lerpvar, cache.varname, NativeScript_getArgument(ctx, argOffset));
        Instance_setSelfVar(lerpvar, cache.pointa, NativeScript_getArgument(ctx, argOffset + 1));
        Instance_setSelfVar(lerpvar, cache.pointb, NativeScript_getArgument(ctx, argOffset + 2));
        Instance_setSelfVar(lerpvar, cache.maxtime, NativeScript_getArgument(ctx, argOffset + 3));
        if (withEase && argCount >= argOffset + 5)
            Instance_setSelfVar(lerpvar, cache.easetype, NativeScript_getArgument(ctx, argOffset + 4));
        if (withEase && argCount >= argOffset + 6)
            Instance_setSelfVar(lerpvar, cache.easeinout, NativeScript_getArgument(ctx, argOffset + 5));
    }
    return id;
}

static bool scrLerpvar(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    int32_t argCount = ctx->currentCallFrame->scriptArgsCount;
    RValue selfId = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_ID, "id", -1);
    RValue id = createLerpvar(ctx, selfId, 0, argCount, argCount >= 6);
    RValue_free(&selfId);
    Instance_setSelfVar(self, cache.lerpvarRefOuter, id);
    *outResult = id;
    return true;
}

static bool scrLerpvarInstance(VMContext* ctx, RValue* outResult) {
    int32_t argCount = ctx->currentCallFrame->scriptArgsCount;
    *outResult = createLerpvar(ctx, NativeScript_getArgument(ctx, 0), 1, argCount, true);
    return true;
}

void NativeChapter3Lerpvar_register(VMContext* ctx) {
    cache.lerpvarObject = Chapter3_findObject(ctx, "obj_lerpvar");
    if (cache.lerpvarObject < 0) return;
    cache.variableInstanceGet = VM_findBuiltin(ctx, "variable_instance_get");
    cache.variableInstanceSet = VM_findBuiltin(ctx, "variable_instance_set");
    cache.lerp = VM_findBuiltin(ctx, "lerp");
    if (!Chapter3_bindInstanceCreate(ctx, &cache.instanceCreate) || cache.variableInstanceGet == nullptr ||
        cache.variableInstanceSet == nullptr || cache.lerp == nullptr) return;

    cache.variable = NativeScript_getSelfVarID(ctx, "variable");
    cache.varname = NativeScript_getSelfVarID(ctx, "varname");
    cache.pointa = NativeScript_getSelfVarID(ctx, "pointa");
    cache.pointb = NativeScript_getSelfVarID(ctx, "pointb");
    cache.time = NativeScript_getSelfVarID(ctx, "time");
    cache.maxtime = NativeScript_getSelfVarID(ctx, "maxtime");
    cache.target = NativeScript_getSelfVarID(ctx, "target");
    cache.init = NativeScript_getSelfVarID(ctx, "init");
    cache.easetype = NativeScript_getSelfVarID(ctx, "easetype");
    cache.easeinout = NativeScript_getSelfVarID(ctx, "easeinout");
    cache.respectGlobalInteract = NativeScript_getSelfVarID(ctx, "respectglobalinteract");
    cache.lerpvarRef = NativeScript_getSelfVarID(ctx, "__lerpvar");
    cache.lerpvarRefOuter = NativeScript_getSelfVarID(ctx, "___lerpvar");
    cache.globalInteract = NativeScript_getGlobalVarID(ctx, "interact");
    cache.easeOut = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.easeIn = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.easeInOut = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_lerpvar_Create_0", lerpvarCreate);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_lerpvar_Step_0", lerpvarStep);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_lerpvar", scrLerpvar);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_lerpvar_instance", scrLerpvarInstance);
}
