#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "runner.h"
#include "runner_gamepad.h"
#include "runner_keyboard.h"
#include "stb_ds.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define TIME_SELF_VARS(X) \
    X(paused) X(quit_timer) X(fullscreen_toggle) X(gamepad_check_timer) X(quicksaved) \
    X(gamepad_active) X(gamepad_id) X(axis_value) X(__returnvalue)

#define TIME_GLOBAL_VARS(X) \
    X(time) X(is_console) X(input_k) X(input_g) X(input_held) X(input_pressed) X(input_released)

enum {
    TIME_INPUT_COUNT = 10,
    TIME_HELD_OWNER = 0x28f50,
    TIME_PRESSED_OWNER,
    TIME_RELEASED_OWNER,
};

static struct {
#define DECLARE_ID(name) int32_t name;
    TIME_SELF_VARS(DECLARE_ID)
    TIME_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t controllerObject;
    BuiltinFunc osPaused;
} cache;

#define TIME_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define TIME_REAL(inst, name) RValue_toReal(TIME_GET(inst, name))
#define TIME_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static GMLArray* timeArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static GMLArray* timeWritableInputs(RValue value) {
    GMLArray* array = timeArray(value);
    if (array == nullptr || array->refCount != 1 || GMLArray_slot(array, TIME_INPUT_COUNT - 1) == nullptr)
        return nullptr;
    return array;
}

static bool timeEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static Instance* timeGamepadController(const Runner* runner) {
    Instance** controllers = runner->instancesByObject[cache.controllerObject];
    for (size_t i = 0; i < arrlenu(controllers); ++i) {
        if (controllers[i]->active) return controllers[i];
    }
    return nullptr;
}

static void timeUpdateInput(VMContext* ctx, Instance* self, Instance* controller,
    RValue* held, RValue* pressed, RValue* released) {
    Runner* runner = ctx->runner;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = runner->mathEpsilon;
    GMLArray* keys = timeArray(TIME_GET(global, input_k));
    bool gamepad = timeEqual(TIME_GET(controller, gamepad_active), 1, epsilon) &&
        !timeEqual(TIME_GET(self, quicksaved), 2, epsilon);
    int32_t device = gamepad ? RValue_toInt32(TIME_GET(controller, gamepad_id)) : -1;
    GMLArray* buttons = gamepad ? timeArray(TIME_GET(global, input_g)) : nullptr;
    GMLReal axes[2] = {0, 0};
    if (gamepad) {
        axes[0] = RunnerGamepad_axisValue(runner->gamepads, device, GP_AXIS_LV);
        axes[1] = RunnerGamepad_axisValue(runner->gamepads, device, GP_AXIS_LH);
    }
    GMLReal threshold = TIME_REAL(self, axis_value);
    for (int32_t i = 0; i < TIME_INPUT_COUNT; ++i) {
        bool down = RunnerKeyboard_check(runner->keyboard, RValue_toInt32(GMLArray_get(keys, i)));
        if (!down && gamepad) {
            down = RunnerGamepad_buttonCheck(runner->gamepads, device, RValue_toInt32(GMLArray_get(buttons, i)));
            if (!down && i < 4) {
                down = i < 2
                    ? NativeScript_compareReals(axes[i & 1], threshold, epsilon) >= 0
                    : NativeScript_compareReals(axes[i & 1], -threshold, epsilon) <= 0;
                TIME_SET(self, __returnvalue, down);
            }
        }
        RValue previous = held[i];
        RValue_copyIntoSlot(&pressed[i], RValue_makeReal(down && timeEqual(previous, 0, epsilon)));
        RValue_copyIntoSlot(&released[i], RValue_makeReal(!down && timeEqual(previous, 1, epsilon)));
        RValue_copyIntoSlot(&held[i], RValue_makeReal(down));
    }
    ctx->currentArrayOwner = (void*)(intptr_t)TIME_HELD_OWNER;
}

static bool timeBeginStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (RValue_toBool(TIME_GET(self, paused))) {
        *outResult = RValue_makeUndefined();
        return true;
    }
    Runner* runner = ctx->runner;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = runner->mathEpsilon;
    if (RValue_toBool(TIME_GET(global, is_console))) {
        RValue paused = cache.osPaused(ctx, nullptr, 0);
        bool needPause = RValue_toBool(paused);
        RValue_free(&paused);
        if (needPause) return false;
    }
    GMLReal quit = TIME_REAL(self, quit_timer);
    if (RunnerKeyboard_check(runner->keyboard, VK_ESCAPE)) {
        quit = NativeScript_compareReals(quit, 0, epsilon) < 0 ? 1 : quit + 1;
        if (NativeScript_compareReals(quit, 30, epsilon) >= 0) return false;
    } else {
        quit -= 2;
    }
    Instance* controller = timeGamepadController(runner);
    GMLArray* held = timeWritableInputs(TIME_GET(global, input_held));
    GMLArray* pressed = timeWritableInputs(TIME_GET(global, input_pressed));
    GMLArray* released = timeWritableInputs(TIME_GET(global, input_released));
    if (controller == nullptr || held == nullptr || pressed == nullptr || released == nullptr ||
        held == pressed || held == released || pressed == released) return false;

    TIME_SET(global, time, TIME_REAL(global, time) + 1);
    TIME_SET(self, quit_timer, quit);
    if (RunnerKeyboard_checkPressed(runner->keyboard, VK_F4) ||
        timeEqual(TIME_GET(self, fullscreen_toggle), 1, epsilon)) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(1), 1);
    }
    GMLReal timer = TIME_REAL(self, gamepad_check_timer) + 1;
    if (NativeScript_compareReals(timer, 90, epsilon) >= 0) {
        int32_t device = RValue_toInt32(TIME_GET(controller, gamepad_id));
        if (!RunnerGamepad_isConnected(runner->gamepads, device)) {
            bool connected = false;
            int32_t count = RunnerGamepad_getDeviceCount(runner->gamepads);
            for (int32_t i = 0; i < count; ++i) {
                if (RunnerGamepad_isConnected(runner->gamepads, i)) {
                    TIME_SET(controller, gamepad_active, 1);
                    TIME_SET(controller, gamepad_id, i);
                    connected = true;
                    break;
                }
            }
            if (!connected) TIME_SET(controller, gamepad_active, 0);
        }
        timer = 0;
    }
    TIME_SET(self, gamepad_check_timer, timer);
    held->owner = (void*)(intptr_t)TIME_HELD_OWNER;
    pressed->owner = (void*)(intptr_t)TIME_PRESSED_OWNER;
    released->owner = (void*)(intptr_t)TIME_RELEASED_OWNER;
    timeUpdateInput(ctx, self, controller, GMLArray_slot(held, 0), GMLArray_slot(pressed, 0), GMLArray_slot(released, 0));
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2TimeBeginStep_register(VMContext* ctx) {
    cache.controllerObject = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && strcmp(object->name, "obj_gamecontroller") == 0) {
            cache.controllerObject = (int32_t)i;
            break;
        }
    }
    cache.osPaused = VM_findBuiltin(ctx, "os_is_paused");
    if (cache.controllerObject < 0 || cache.osPaused == nullptr ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_time_Step_1", timeBeginStep)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = NativeScript_getGlobalVarID(ctx, #name);
    TIME_SELF_VARS(RESOLVE_SELF)
    TIME_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF
}
