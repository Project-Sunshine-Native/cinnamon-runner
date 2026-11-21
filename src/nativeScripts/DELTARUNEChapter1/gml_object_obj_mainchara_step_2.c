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
    int32_t darkzone, plot, cutscene, bg, wd, ht, initwd, initht;
    int32_t shakeObject, backgroundObject;
} cache;

static bool maincharaEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static bool maincharaCameraShaking(const Runner* runner) {
    Instance** shakes = runner->instancesByObject[cache.shakeObject];
    for (size_t i = 0; i < arrlenu(shakes); ++i) {
        if (shakes[i]->active) return true;
    }
    return false;
}

static float maincharaClampView(GMLReal position, GMLReal limit, GMLReal epsilon) {
    float stored = (float)position;
    if (NativeScript_compareReals(stored, 0, epsilon) < 0) stored = 0;
    if (NativeScript_compareReals(stored, limit, epsilon) > 0) stored = (float)limit;
    return stored;
}

static void maincharaMoveCamera(Runner* runner, Instance* self, bool dark, GMLReal epsilon) {
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    GMLReal width = camera != nullptr ? (GMLReal)camera->viewWidth : -1;
    GMLReal height = camera != nullptr ? (GMLReal)camera->viewHeight : -1;
    GMLReal x, y;
    if (dark) {
        GMLReal initialWidth = RValue_toReal(Instance_getSelfVar(self, cache.initwd));
        GMLReal initialHeight = RValue_toReal(Instance_getSelfVar(self, cache.initht));
        x = self->x - GMLReal_floor(width * (GMLReal)0.5 - initialWidth * (GMLReal)0.5);
        y = self->y - GMLReal_floor(height * (GMLReal)0.5 - initialHeight * (GMLReal)0.5);
    } else {
        if (camera != nullptr) camera->objectId = INSTANCE_NOONE;
        x = (self->x - GMLReal_floor(width * (GMLReal)0.5)) + 11;
        y = (self->y - GMLReal_floor(height * (GMLReal)0.5)) + 17;
    }
    Instance_setSelfVar(self, cache.wd, RValue_makeReal(x));
    Instance_setSelfVar(self, cache.ht, RValue_makeReal(y));

    if (camera != nullptr) {
        Room* room = runner->currentRoom;
        camera->viewX = maincharaClampView(x, (GMLReal)room->width - width, epsilon);
        camera->viewY = maincharaClampView(y, (GMLReal)room->height - height, epsilon);
        Runner_updateCameraViewSimple(camera);
    }
}

static void maincharaUpdateBackgrounds(Runner* runner, Instance* self) {
    if (arrlen(runner->instancesByObject[cache.backgroundObject]) == 0) return;

    int32_t base = Runner_pushInstancesOfObject(runner, cache.backgroundObject);
    int32_t end = (int32_t)arrlen(runner->instanceSnapshots);
    int32_t count = 0;
    for (int32_t i = base; i < end; ++i) {
        Instance* instance = runner->instanceSnapshots[i];
        if (instance->active) runner->instanceSnapshots[base + count++] = instance;
    }
    for (int32_t i = 0; i < count; ++i) {
        Instance* background = runner->instanceSnapshots[base + (count > 2 ? count - 1 - i : i)];
        Runner_executeEventFromObject(runner, background, self, background->objectIndex, EVENT_OTHER, OTHER_USER0);
    }
    Runner_popInstanceSnapshot(runner, base);
}

static bool maincharaEndStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    Runner* runner = ctx->runner;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = runner->mathEpsilon;
    RValue zone = Instance_getSelfVar(global, cache.darkzone);
    bool dark = maincharaEqual(zone, 1, epsilon);
    bool follow = dark;
    if (!dark && maincharaEqual(zone, 0, epsilon)) {
        RValue plot = Instance_getSelfVar(global, cache.plot);
        follow = plot.type != RVALUE_UNDEFINED &&
            NativeScript_compareReals(RValue_toReal(plot), 245, epsilon) >= 0;
    }
    if (follow && maincharaEqual(Instance_getSelfVar(self, cache.cutscene), 0, epsilon) &&
        !maincharaCameraShaking(runner)) {
        maincharaMoveCamera(runner, self, dark, epsilon);
    }

    if (maincharaEqual(Instance_getSelfVar(self, cache.bg), 1, epsilon)) {
        maincharaUpdateBackgrounds(runner, self);
    }
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeMaincharaEndStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    const Objt* objt = &ctx->dataWin->objt;
    cache.shakeObject = -1;
    cache.backgroundObject = -1;
    for (uint32_t i = 0; i < objt->count; ++i) {
        const GameObject* object = &objt->objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_shake") == 0) cache.shakeObject = (int32_t)i;
        if (strcmp(object->name, "obj_backgrounderparent") == 0) cache.backgroundObject = (int32_t)i;
    }
    if (cache.shakeObject < 0 || cache.backgroundObject < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_mainchara_Step_2", maincharaEndStep)) return;

    cache.darkzone = VM_getOrAllocateVarID(ctx, "darkzone");
    cache.plot = VM_getOrAllocateVarID(ctx, "plot");
    cache.cutscene = NativeScript_getSelfVarID(ctx, "cutscene");
    cache.bg = NativeScript_getSelfVarID(ctx, "bg");
    cache.wd = NativeScript_getSelfVarID(ctx, "wd");
    cache.ht = NativeScript_getSelfVarID(ctx, "ht");
    cache.initwd = NativeScript_getSelfVarID(ctx, "initwd");
    cache.initht = NativeScript_getSelfVarID(ctx, "initht");
}
