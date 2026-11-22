#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "stb_ds.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t chapter, darkzone, plot, cutscene, bg, wd, ht, initwd, initht;
    int32_t shake, background, advanced, queen;
} cache;

static bool maincharaEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static bool maincharaObjectExists(Runner* runner, int32_t object) {
    Instance** instances = runner->instancesByObject[object];
    for (size_t i = 0; i < arrlenu(instances); ++i)
        if (instances[i]->active) return true;
    return false;
}

static void maincharaUserEvent(Runner* runner, Instance* self, int32_t object, bool camera) {
    int32_t base = Runner_pushInstancesOfObject(runner, object);
    int32_t end = (int32_t)arrlen(runner->instanceSnapshots);
    int32_t count = 0;
    for (int32_t i = base; i < end; ++i) {
        Instance* instance = runner->instanceSnapshots[i];
        if (instance->active) runner->instanceSnapshots[base + count++] = instance;
    }
    if (camera) {
        RValue cutscene = Instance_getSelfVar(self, cache.cutscene);
        for (int32_t i = 0; i < count; ++i)
            Instance_setSelfVar(runner->instanceSnapshots[base + i], cache.cutscene, cutscene);
    }
    for (int32_t i = 0; i < count; ++i) {
        Instance* instance = runner->instanceSnapshots[base + (count > 2 ? count - 1 - i : i)];
        Runner_executeEventFromObject(runner, instance, self, instance->objectIndex, EVENT_OTHER, OTHER_USER0);
    }
    Runner_popInstanceSnapshot(runner, base);
}

static float maincharaClampView(GMLReal position, GMLReal limit, GMLReal epsilon) {
    float stored = (float)position;
    if (NativeScript_compareReals(stored, 0, epsilon) < 0) stored = 0;
    if (NativeScript_compareReals(stored, limit, epsilon) > 0) stored = (float)limit;
    return stored;
}

static void maincharaMoveCamera(Runner* runner, Instance* self, bool dark) {
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    GMLReal width = camera != nullptr ? (GMLReal)camera->viewWidth : -1;
    GMLReal height = camera != nullptr ? (GMLReal)camera->viewHeight : -1;
    GMLReal x, y;
    if (dark) {
        x = self->x - GMLReal_floor(width / 2 -
            RValue_toReal(Instance_getSelfVar(self, cache.initwd)) / 2);
        y = self->y - GMLReal_floor(height / 2 -
            RValue_toReal(Instance_getSelfVar(self, cache.initht)) / 2);
    } else {
        if (camera != nullptr) camera->objectId = INSTANCE_NOONE;
        x = (self->x - GMLReal_floor(width / 2)) + 11;
        y = (self->y - GMLReal_floor(height / 2)) + 17;
    }
    Instance_setSelfVar(self, cache.wd, RValue_makeReal(x));
    Instance_setSelfVar(self, cache.ht, RValue_makeReal(y));
    if (camera != nullptr) {
        camera->viewX = maincharaClampView(x, (GMLReal)runner->currentRoom->width - width, runner->mathEpsilon);
        camera->viewY = maincharaClampView(y, (GMLReal)runner->currentRoom->height - height, runner->mathEpsilon);
        Runner_updateCameraViewSimple(camera);
    }
}

static bool maincharaEndStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    Runner* runner = ctx->runner;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = runner->mathEpsilon;
    RValue chapter = Instance_getSelfVar(global, cache.chapter);
    *outResult = RValue_makeUndefined();
    if (maincharaEqual(chapter, 2, epsilon) && maincharaObjectExists(runner, cache.queen))
        return true;
    RValue zone = Instance_getSelfVar(global, cache.darkzone);
    bool dark = maincharaEqual(zone, 1, epsilon);
    bool follow = dark;
    if (maincharaEqual(zone, 0, epsilon)) {
        GMLReal plot = RValue_toReal(Instance_getSelfVar(global, cache.plot));
        follow = NativeScript_compareReals(plot, 245, epsilon) >= 0 ||
            NativeScript_compareReals(RValue_toReal(chapter), 2, epsilon) >= 0;
    }
    if (follow) {
        if (maincharaObjectExists(runner, cache.advanced)) {
            maincharaUserEvent(runner, self, cache.advanced, true);
        } else if (maincharaEqual(Instance_getSelfVar(self, cache.cutscene), 0, epsilon) &&
            !maincharaObjectExists(runner, cache.shake)) {
            maincharaMoveCamera(runner, self, dark);
        }
    }
    if (maincharaEqual(Instance_getSelfVar(self, cache.bg), 1, epsilon))
        maincharaUserEvent(runner, self, cache.background, false);
    return true;
}

void NativeChapter2MaincharaEndStep_register(VMContext* ctx) {
    if (!DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;
    cache.shake = cache.background = cache.advanced = cache.queen = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_shake") == 0) cache.shake = (int32_t)i;
        if (strcmp(object->name, "obj_backgrounderparent") == 0) cache.background = (int32_t)i;
        if (strcmp(object->name, "obj_camera_advanced") == 0) cache.advanced = (int32_t)i;
        if (strcmp(object->name, "obj_gigaqueen_enemy") == 0) cache.queen = (int32_t)i;
    }
    if (cache.background < 0 || cache.shake < 0 || cache.advanced < 0 || cache.queen < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_mainchara_Step_2", maincharaEndStep)) return;
    cache.chapter = VM_getOrAllocateVarID(ctx, "chapter");
    cache.darkzone = VM_getOrAllocateVarID(ctx, "darkzone");
    cache.plot = VM_getOrAllocateVarID(ctx, "plot");
    cache.cutscene = NativeScript_getSelfVarID(ctx, "cutscene");
    cache.bg = NativeScript_getSelfVarID(ctx, "bg");
    cache.wd = NativeScript_getSelfVarID(ctx, "wd");
    cache.ht = NativeScript_getSelfVarID(ctx, "ht");
    cache.initwd = NativeScript_getSelfVarID(ctx, "initwd");
    cache.initht = NativeScript_getSelfVarID(ctx, "initht");
}
