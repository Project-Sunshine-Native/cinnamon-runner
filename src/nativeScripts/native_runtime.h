#ifndef _CINNAMON_NATIVE_RUNTIME_H_
#define _CINNAMON_NATIVE_RUNTIME_H_

#include "rvalue.h"
#include "vm.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const VMContext* owner;
    int32_t varId;
    int16_t builtinVarId;
} NativeVarBinding;

typedef struct {
    const VMContext* owner;
    BuiltinFunc builtin;
    int32_t codeIndex;
} NativeCallBinding;

typedef struct {
    const VMContext* owner;
    int32_t assetIndex;
} NativeAssetBinding;

#define NATIVE_VAR_BINDING_INIT { nullptr, -1, -1 }
#define NATIVE_CALL_BINDING_INIT { nullptr, nullptr, -1 }
#define NATIVE_ASSET_BINDING_INIT { nullptr, -1 }

typedef struct {
    VMContext* ctx;
    RValue* temporaries;
    int32_t temporaryCount;
    int32_t activeTemporaryCount;
    RValue* locals;
    int32_t localCount;
} NativeScriptScope;

typedef enum {
    NATIVE_OP_ADD,
    NATIVE_OP_SUBTRACT,
    NATIVE_OP_MULTIPLY,
    NATIVE_OP_DIVIDE,
    NATIVE_OP_INT_DIVIDE,
    NATIVE_OP_MODULO,
    NATIVE_OP_EQUAL,
    NATIVE_OP_NOT_EQUAL,
    NATIVE_OP_LESS,
    NATIVE_OP_LESS_EQUAL,
    NATIVE_OP_GREATER,
    NATIVE_OP_GREATER_EQUAL,
    NATIVE_OP_BIT_AND,
    NATIVE_OP_BIT_OR,
    NATIVE_OP_BIT_XOR,
    NATIVE_OP_SHIFT_LEFT,
    NATIVE_OP_SHIFT_RIGHT
} NativeBinaryOp;

void NativeScriptScope_init(NativeScriptScope* scope, VMContext* ctx,
    RValue* temporaries, int32_t temporaryCount, RValue* locals, int32_t localCount);
void NativeScriptScope_destroy(NativeScriptScope* scope);
void NativeScriptScope_clearTemps(NativeScriptScope* scope);
RValue NativeScriptScope_storeTemp(NativeScriptScope* scope, int32_t index, RValue value);
RValue NativeScriptScope_getTemp(const NativeScriptScope* scope, int32_t index);
void NativeScriptScope_finish(NativeScriptScope* scope, RValue* outResult, RValue value);

RValue NativeScript_getVariable(VMContext* ctx, struct Instance* instance,
    NativeVarBinding* binding, const char* name);
void NativeScript_setVariable(VMContext* ctx, struct Instance* instance,
    NativeVarBinding* binding, const char* name, RValue value);
void NativeScript_setVariableIndex(VMContext* ctx, struct Instance* instance,
    NativeVarBinding* binding, const char* name, RValue index, RValue value);
RValue NativeScript_getArgument(VMContext* ctx, int32_t index);
RValue NativeScript_getLocal(const NativeScriptScope* scope, int32_t index);
void NativeScript_setLocal(NativeScriptScope* scope, int32_t index, RValue value);
void NativeScript_setLocalIndex(NativeScriptScope* scope, int32_t localIndex,
    RValue arrayIndex, RValue value);
RValue NativeScript_getIndex(RValue value, RValue index);

bool NativeScript_canCall(VMContext* ctx, NativeCallBinding* binding, const char* name);
RValue NativeScript_call(VMContext* ctx, NativeCallBinding* binding, const char* name,
    RValue* args, int32_t argCount);
bool NativeScript_canResolveAsset(VMContext* ctx, NativeAssetBinding* binding, const char* name);
RValue NativeScript_getAsset(VMContext* ctx, NativeAssetBinding* binding, const char* name);

RValue NativeScript_binary(VMContext* ctx, NativeBinaryOp op, RValue left, RValue right);
RValue NativeScript_negate(RValue value);
RValue NativeScript_not(RValue value);
RValue NativeScript_bitNot(RValue value);

#endif /* _CINNAMON_NATIVE_RUNTIME_H_ */
