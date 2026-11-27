#ifndef _CINNAMON_CHAPTER3_NATIVE_COMMON_H_
#define _CINNAMON_CHAPTER3_NATIVE_COMMON_H_

#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

#include <string.h>

#define R(value) ((GMLReal)(value))

typedef struct {
    BuiltinFunc func;
    const char* name;
} Chapter3Builtin;

static inline bool Chapter3_bindBuiltin(VMContext* ctx, Chapter3Builtin* builtin, const char* name) {
    builtin->func = VM_findBuiltin(ctx, name);
    builtin->name = name;
    return builtin->func != nullptr;
}

static inline RValue Chapter3_call(VMContext* ctx, const Chapter3Builtin* builtin, RValue* args, int32_t argCount) {
    return builtin->func(ctx, args, argCount);
}

static inline void Chapter3_callVoid(VMContext* ctx, const Chapter3Builtin* builtin, RValue* args, int32_t argCount) {
    RValue result = builtin->func(ctx, args, argCount);
    RValue_free(&result);
}

static inline GMLReal Chapter3_callReal(VMContext* ctx, const Chapter3Builtin* builtin, RValue* args, int32_t argCount) {
    RValue result = builtin->func(ctx, args, argCount);
    GMLReal value = RValue_toReal(result);
    RValue_free(&result);
    return value;
}

static inline void Chapter3_callVoidAs(VMContext* ctx, Instance* self, const Chapter3Builtin* builtin, RValue* args, int32_t argCount) {
    CallFrame* frame = ctx->currentCallFrame;
    Instance* previousSelf = frame->self;
    Instance* previousOther = frame->other;
    frame->other = previousSelf;
    frame->self = self;
    Chapter3_callVoid(ctx, builtin, args, argCount);
    frame->self = previousSelf;
    frame->other = previousOther;
}

typedef struct {
    NativeCallBinding binding;
    const char* name;
} Chapter3InstanceCreator;

static inline bool Chapter3_bindInstanceCreate(VMContext* ctx, Chapter3InstanceCreator* creator) {
    creator->binding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    creator->name = VM_findBuiltin(ctx, "instance_create") != nullptr ? "instance_create" : "gml_Script_instance_create";
    return NativeScript_canCall(ctx, &creator->binding, creator->name);
}

static inline RValue Chapter3_instanceCreate(VMContext* ctx, Chapter3InstanceCreator* creator, RValue x, RValue y, RValue object) {
    RValue args[] = { x, y, object };
    return NativeScript_call(ctx, &creator->binding, creator->name, args, 3);
}

typedef void (*Chapter3WithBody)(VMContext* ctx, Instance* self, Instance* inst);

static inline void Chapter3_with(VMContext* ctx, Instance* self, int32_t objectIndex, Chapter3WithBody body) {
    Runner* runner = ctx->runner;
    if (0 > objectIndex || runner->dataWin->objt.count <= (uint32_t)objectIndex) return;
    Instance** source = runner->instancesByObject[objectIndex];
    int32_t sourceCount = (int32_t)arrlen(source);
    Instance** list = nullptr;
    for (int32_t i = 0; sourceCount > i; i++) {
        if (source[i]->active) arrput(list, source[i]);
    }
    int32_t count = (int32_t)arrlen(list);
    if (count >= 3) {
        for (int32_t i = 0, j = count - 1; j > i; i++, j--) {
            Instance* swap = list[i];
            list[i] = list[j];
            list[j] = swap;
        }
    }
    for (int32_t i = 0; count > i; i++) {
        if (i > 0 && !list[i]->active) continue;
        body(ctx, self, list[i]);
    }
    arrfree(list);
}

static inline int32_t Chapter3_findObject(VMContext* ctx, const char* name) {
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && object->name != nullptr && strcmp(object->name, name) == 0) return (int32_t)i;
    }
    return -1;
}

static inline int32_t Chapter3_findSprite(VMContext* ctx, const char* name) {
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const char* spriteName = ctx->dataWin->sprt.sprites[i].name;
        if (spriteName != nullptr && strcmp(spriteName, name) == 0) return (int32_t)i;
    }
    return -1;
}

static inline int32_t Chapter3_findSound(VMContext* ctx, const char* name) {
    for (uint32_t i = 0; i < ctx->dataWin->sond.count; ++i) {
        const char* soundName = ctx->dataWin->sond.sounds[i].name;
        if (soundName != nullptr && strcmp(soundName, name) == 0) return (int32_t)i;
    }
    return -1;
}

static inline int Chapter3_compare(VMContext* ctx, GMLReal a, GMLReal b) {
    return NativeScript_compareReals(a, b, ctx->runner->mathEpsilon);
}

static inline bool Chapter3_isString(RValue value, const char* text) {
    return value.type == RVALUE_STRING && strcmp(value.string != nullptr ? value.string : "", text) == 0;
}

static inline Instance* Chapter3_resolveInstance(Runner* runner, RValue value) {
    if (value.type == RVALUE_UNDEFINED || value.type == RVALUE_STRING || value.type == RVALUE_ARRAY) return nullptr;
    int32_t id = RValue_toInt32(value);
    if (id >= 0 && runner->dataWin->objt.count > (uint32_t)id) {
        Instance* found = nullptr;
        int32_t base = Runner_pushInstancesOfObject(runner, id);
        int32_t end = (int32_t)arrlen(runner->instanceSnapshots);
        for (int32_t i = base; end > i; i++) {
            if (runner->instanceSnapshots[i]->active) {
                found = runner->instanceSnapshots[i];
                break;
            }
        }
        Runner_popInstanceSnapshot(runner, base);
        return found;
    }
    Instance* inst = hmget(runner->instancesById, id);
    return inst != nullptr && inst->active ? inst : nullptr;
}

static inline bool Chapter3_instanceExists(VMContext* ctx, RValue value) {
    if (value.type == RVALUE_UNDEFINED || value.type == RVALUE_STRING || value.type == RVALUE_ARRAY) return false;
    if (Chapter3_compare(ctx, RValue_toReal(value), 0) <= 0) return false;
    return Chapter3_resolveInstance(ctx->runner, value) != nullptr;
}

static inline GMLReal Chapter3_getBuiltin(VMContext* ctx, Instance* inst, int16_t builtinVarId, const char* name) {
    RValue value = VMBuiltins_getVariable(ctx, inst, builtinVarId, name, -1);
    GMLReal result = RValue_toReal(value);
    RValue_free(&value);
    return result;
}

static inline void Chapter3_setBuiltin(VMContext* ctx, Instance* inst, int16_t builtinVarId, const char* name, GMLReal value) {
    VMBuiltins_setVariable(ctx, inst, builtinVarId, name, RValue_makeReal(value), -1);
}

static inline GMLReal Chapter3_getReal(Instance* inst, int32_t varId) {
    return RValue_toReal(Instance_getSelfVar(inst, varId));
}

static inline void Chapter3_setReal(Instance* inst, int32_t varId, GMLReal value) {
    Instance_setSelfVar(inst, varId, RValue_makeReal(value));
}

static inline RValue Chapter3_increment(RValue value) {
    if (value.type == RVALUE_INT32) return RValue_makeInt32(value.int32 + 1);
    return RValue_makeReal(RValue_toReal(value) + 1);
}

static inline GMLReal Chapter3_cameraX(Runner* runner) {
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    return camera != nullptr ? (GMLReal)camera->viewX : -1;
}

static inline GMLReal Chapter3_cameraY(Runner* runner) {
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    return camera != nullptr ? (GMLReal)camera->viewY : -1;
}

static inline GMLReal Chapter3_cameraWidth(Runner* runner) {
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    return camera != nullptr ? (GMLReal)camera->viewWidth : -1;
}

static inline GMLReal Chapter3_cameraHeight(Runner* runner) {
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    return camera != nullptr ? (GMLReal)camera->viewHeight : -1;
}

#endif /* _CINNAMON_CHAPTER3_NATIVE_COMMON_H_ */
