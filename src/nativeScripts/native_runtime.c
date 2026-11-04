#include "native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

void NativeScriptScope_init(NativeScriptScope* scope, VMContext* ctx,
    RValue* temporaries, int32_t temporaryCount, RValue* locals, int32_t localCount) {
    scope->ctx = ctx;
    scope->temporaries = temporaries;
    scope->temporaryCount = temporaryCount;
    scope->activeTemporaryCount = 0;
    scope->locals = locals;
    scope->localCount = localCount;
    for (int32_t i = 0; temporaryCount > i; ++i) temporaries[i] = RValue_makeUndefined();
    for (int32_t i = 0; localCount > i; ++i) locals[i] = RValue_makeUndefined();
}

void NativeScriptScope_destroy(NativeScriptScope* scope) {
    NativeScriptScope_clearTemps(scope);
    for (int32_t i = 0; scope->localCount > i; ++i) RValue_free(&scope->locals[i]);
}

void NativeScriptScope_clearTemps(NativeScriptScope* scope) {
    for (int32_t i = 0; scope->activeTemporaryCount > i; ++i) {
        RValue_free(&scope->temporaries[i]);
        scope->temporaries[i] = RValue_makeUndefined();
    }
    scope->activeTemporaryCount = 0;
}

RValue NativeScriptScope_storeTemp(NativeScriptScope* scope, int32_t index, RValue value) {
    if (0 > index || index >= scope->temporaryCount) return RValue_makeUndefined();
    RValue_free(&scope->temporaries[index]);
    scope->temporaries[index] = RValue_stealOwnershipOrCopy(value);
    if (scope->activeTemporaryCount <= index) scope->activeTemporaryCount = index + 1;
    RValue result = scope->temporaries[index];
    result.ownsReference = false;
    return result;
}

RValue NativeScriptScope_getTemp(const NativeScriptScope* scope, int32_t index) {
    if (0 > index || index >= scope->temporaryCount) return RValue_makeUndefined();
    RValue result = scope->temporaries[index];
    result.ownsReference = false;
    return result;
}

void NativeScriptScope_finish(NativeScriptScope* scope, RValue* outResult, RValue value) {
    *outResult = RValue_makeIndependent(value);
    NativeScriptScope_destroy(scope);
}

static void resolveVariableBinding(VMContext* ctx, NativeVarBinding* binding, const char* name) {
    if (binding->owner == ctx) return;
    binding->owner = ctx;
    binding->builtinVarId = VMBuiltins_resolveBuiltinVarId(name);
    binding->varId = binding->builtinVarId == BUILTIN_VAR_UNKNOWN
        ? VM_getOrAllocateVarID(ctx, name) : VARIABLE_BUILTIN;
}

RValue NativeScript_getVariable(VMContext* ctx, Instance* instance,
    NativeVarBinding* binding, const char* name) {
    if (instance == nullptr) return RValue_makeUndefined();
    resolveVariableBinding(ctx, binding, name);
    if (binding->builtinVarId != BUILTIN_VAR_UNKNOWN) {
        return VMBuiltins_getVariable(ctx, instance, binding->builtinVarId, name, -1);
    }
    return Instance_getSelfVar(instance, binding->varId);
}

void NativeScript_setVariable(VMContext* ctx, Instance* instance,
    NativeVarBinding* binding, const char* name, RValue value) {
    if (instance == nullptr) return;
    resolveVariableBinding(ctx, binding, name);
    if (binding->builtinVarId != BUILTIN_VAR_UNKNOWN) {
        VMBuiltins_setVariable(ctx, instance, binding->builtinVarId, name, value, -1);
        return;
    }
    Instance_setSelfVar(instance, binding->varId, value);
}

void NativeScript_setVariableIndex(VMContext* ctx, Instance* instance,
    NativeVarBinding* binding, const char* name, RValue index, RValue value) {
    if (instance == nullptr) return;
    resolveVariableBinding(ctx, binding, name);
    int32_t arrayIndex = RValue_toInt32(index);
    if (binding->builtinVarId != BUILTIN_VAR_UNKNOWN) {
        VMBuiltins_setVariable(ctx, instance, binding->builtinVarId, name, value, arrayIndex);
        return;
    }
    RValue* slot = IntRValueHashMap_getOrInsertUndefined(&instance->selfVars, binding->varId);
    VM_setArrayElement(ctx, slot, arrayIndex, value);
}

RValue NativeScript_getArgument(VMContext* ctx, int32_t index) {
    CallFrame* frame = ctx->currentCallFrame;
    if (frame == nullptr || 0 > index || index >= frame->scriptArgsCount) {
        return RValue_makeUndefined();
    }
    RValue result = frame->scriptArgs[index];
    result.ownsReference = false;
    return result;
}

RValue NativeScript_getLocal(const NativeScriptScope* scope, int32_t index) {
    if (0 > index || index >= scope->localCount) return RValue_makeUndefined();
    RValue result = scope->locals[index];
    result.ownsReference = false;
    return result;
}

void NativeScript_setLocal(NativeScriptScope* scope, int32_t index, RValue value) {
    if (0 > index || index >= scope->localCount) return;
    RValue_copyIntoSlot(&scope->locals[index], value);
}

void NativeScript_setLocalIndex(NativeScriptScope* scope, int32_t localIndex,
    RValue arrayIndex, RValue value) {
    if (0 > localIndex || localIndex >= scope->localCount) return;
    VM_setArrayElement(scope->ctx, &scope->locals[localIndex], RValue_toInt32(arrayIndex), value);
}

RValue NativeScript_getIndex(RValue value, RValue index) {
    if (value.type != RVALUE_ARRAY || value.array == nullptr) return RValue_makeUndefined();
    return GMLArray_get(value.array, RValue_toInt32(index));
}

static bool resolveCallBinding(VMContext* ctx, NativeCallBinding* binding, const char* name) {
    if (binding->owner == ctx) return binding->builtin != nullptr || binding->codeIndex >= 0;
    binding->owner = ctx;
    binding->builtin = VM_findBuiltin(ctx, name);
    binding->codeIndex = -1;
    if (binding->builtin == nullptr) {
        ptrdiff_t entry = shgeti(ctx->codeIndexByName, (char*)name);
        if (entry >= 0) binding->codeIndex = ctx->codeIndexByName[entry].value;
    }
    return binding->builtin != nullptr || binding->codeIndex >= 0;
}

bool NativeScript_canCall(VMContext* ctx, NativeCallBinding* binding, const char* name) {
    return resolveCallBinding(ctx, binding, name);
}

RValue NativeScript_call(VMContext* ctx, NativeCallBinding* binding, const char* name,
    RValue* args, int32_t argCount) {
    if (!resolveCallBinding(ctx, binding, name)) return RValue_makeUndefined();
    if (binding->builtin != nullptr) return binding->builtin(ctx, args, argCount);

    CallFrame* caller = ctx->currentCallFrame;
    return VM_executeCode(ctx, binding->codeIndex, caller->self, caller->other,
        caller->eventObjectIndex, caller->eventType, caller->eventSubtype, args, argCount);
}

bool NativeScript_canResolveAsset(VMContext* ctx, NativeAssetBinding* binding, const char* name) {
    if (binding->owner != ctx) {
        binding->owner = ctx;
        binding->assetIndex = -1;
        ptrdiff_t entry = shgeti(ctx->runner->assetsByName, (char*)name);
        if (entry >= 0) binding->assetIndex = ctx->runner->assetsByName[entry].value;
    }
    return binding->assetIndex >= 0;
}

RValue NativeScript_getAsset(VMContext* ctx, NativeAssetBinding* binding, const char* name) {
    if (!NativeScript_canResolveAsset(ctx, binding, name)) return RValue_makeReal(-1.0);
    return RValue_makeReal((GMLReal)binding->assetIndex);
}

static bool valuesEqual(RValue left, RValue right) {
    if (left.type == RVALUE_STRING && right.type == RVALUE_STRING) {
        const char* a = left.string != nullptr ? left.string : "";
        const char* b = right.string != nullptr ? right.string : "";
        return strcmp(a, b) == 0;
    }
    return RValue_toReal(left) == RValue_toReal(right);
}

RValue NativeScript_binary(VMContext* ctx, NativeBinaryOp op, RValue left, RValue right) {
    if (op == NATIVE_OP_ADD && (left.type == RVALUE_STRING || right.type == RVALUE_STRING)) {
        char* a = RValue_toString(left, ctx->dataWin);
        char* b = RValue_toString(right, ctx->dataWin);
        size_t aLength = strlen(a);
        size_t bLength = strlen(b);
        char* joined = (char*)safeMalloc(aLength + bLength + 1);
        memcpy(joined, a, aLength);
        memcpy(joined + aLength, b, bLength + 1);
        free(a);
        free(b);
        return RValue_makeOwnedString(joined);
    }

    GMLReal a = RValue_toReal(left);
    GMLReal b = RValue_toReal(right);
    switch (op) {
        case NATIVE_OP_ADD: return RValue_makeReal(a + b);
        case NATIVE_OP_SUBTRACT: return RValue_makeReal(a - b);
        case NATIVE_OP_MULTIPLY: return RValue_makeReal(a * b);
        case NATIVE_OP_DIVIDE: return RValue_makeReal(a / b);
        case NATIVE_OP_INT_DIVIDE: return RValue_makeInt64(RValue_toInt64(left) / RValue_toInt64(right));
        case NATIVE_OP_MODULO: return RValue_makeReal(GMLReal_fmod(a, b));
        case NATIVE_OP_EQUAL: return RValue_makeBool(valuesEqual(left, right));
        case NATIVE_OP_NOT_EQUAL: return RValue_makeBool(!valuesEqual(left, right));
        case NATIVE_OP_LESS: return RValue_makeBool(a < b);
        case NATIVE_OP_LESS_EQUAL: return RValue_makeBool(a <= b);
        case NATIVE_OP_GREATER: return RValue_makeBool(a > b);
        case NATIVE_OP_GREATER_EQUAL: return RValue_makeBool(a >= b);
        case NATIVE_OP_BIT_AND: return RValue_makeInt64(RValue_toInt64(left) & RValue_toInt64(right));
        case NATIVE_OP_BIT_OR: return RValue_makeInt64(RValue_toInt64(left) | RValue_toInt64(right));
        case NATIVE_OP_BIT_XOR: return RValue_makeInt64(RValue_toInt64(left) ^ RValue_toInt64(right));
        case NATIVE_OP_SHIFT_LEFT: return RValue_makeInt64(RValue_toInt64(left) << RValue_toInt32(right));
        case NATIVE_OP_SHIFT_RIGHT: return RValue_makeInt64(RValue_toInt64(left) >> RValue_toInt32(right));
    }
    return RValue_makeUndefined();
}

RValue NativeScript_negate(RValue value) {
    return RValue_makeReal(-RValue_toReal(value));
}

RValue NativeScript_not(RValue value) {
    return RValue_makeBool(!RValue_toBool(value));
}

RValue NativeScript_bitNot(RValue value) {
    return RValue_makeInt64(~RValue_toInt64(value));
}
