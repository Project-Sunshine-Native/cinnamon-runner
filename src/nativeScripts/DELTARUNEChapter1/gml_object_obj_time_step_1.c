#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "runner.h"
#include "runner_gamepad.h"
#include "runner_keyboard.h"
#include "stb_ds.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define TIME_SELF_VARS(X) \
    X(paused) X(quit_timer) X(fullscreen_toggle) X(window_center_toggle) X(i) \
    X(gamepad_active) X(gamepad_id) X(axis_value) X(__returnvalue) \
    X(border_alpha) X(border_fade_in) X(border_fade_out) X(border_fade_value)

#define TIME_GLOBAL_VARS(X) \
    X(time) X(is_console) X(debug) X(input_k) X(input_g) X(input_held) X(input_pressed) X(input_released)

enum {
    TIME_INPUT_COUNT = 10,
    TIME_HELD_OWNER = 0x38914,
    TIME_PRESSED_OWNER = 0x38911,
    TIME_RELEASED_OWNER = 0x3894c,
    TIME_DEBUG_SPEED_KEY = 192,
    TIME_DEBUG_SLOW_KEY = 99,
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
    if (array == nullptr || array->refCount != 1 || GMLArray_slot(array, TIME_INPUT_COUNT - 1) == nullptr) {
        return nullptr;
    }
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

static void timeUpdateInput(VMContext* ctx, Instance* self, RValue* held, RValue* pressed, RValue* released) {
    Runner* runner = ctx->runner;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = runner->mathEpsilon;
    GMLArray* keyBindings = timeArray(TIME_GET(global, input_k));
    Instance* controller = timeGamepadController(runner);
    bool gamepad = controller != nullptr && timeEqual(TIME_GET(controller, gamepad_active), 1, epsilon);
    int32_t device = gamepad ? RValue_toInt32(TIME_GET(controller, gamepad_id)) : -1;
    GMLArray* padBindings = gamepad ? timeArray(TIME_GET(global, input_g)) : nullptr;
    GMLReal axes[2] = {0, 0};
    if (gamepad && runner->gamepads != nullptr) {
        axes[0] = RunnerGamepad_axisValue(runner->gamepads, device, GP_AXIS_LV);
        axes[1] = RunnerGamepad_axisValue(runner->gamepads, device, GP_AXIS_LH);
    }
    RValue deadzone = TIME_GET(self, axis_value);
    int32_t lastAxisResult = -1;
    for (int32_t i = 0; i < TIME_INPUT_COUNT; ++i) {
        int32_t key = RValue_toInt32(GMLArray_get(keyBindings, i));
        bool down = RunnerKeyboard_check(runner->keyboard, key);
        if (!down && gamepad) {
            int32_t button = RValue_toInt32(GMLArray_get(padBindings, i));
            down = runner->gamepads != nullptr && RunnerGamepad_buttonCheck(runner->gamepads, device, button);
            if (!down && i < 4) {
                GMLReal axis = axes[i & 1];
                GMLReal threshold = RValue_toReal(deadzone);
                if (i < 2) {
                    down = deadzone.type != RVALUE_UNDEFINED &&
                        NativeScript_compareReals(axis, threshold, epsilon) >= 0;
                } else {
                    down = NativeScript_compareReals(axis, -threshold, epsilon) <= 0;
                }
                lastAxisResult = down;
            }
        }

        RValue previous = held[i];
        RValue_copyIntoSlot(&pressed[i], RValue_makeReal(down && timeEqual(previous, 0, epsilon)));
        RValue_copyIntoSlot(&released[i], RValue_makeReal(!down && timeEqual(previous, 1, epsilon)));
        RValue_copyIntoSlot(&held[i], RValue_makeReal(down));
    }
    if (lastAxisResult >= 0) TIME_SET(self, __returnvalue, lastAxisResult);
    TIME_SET(self, i, TIME_INPUT_COUNT);
    ctx->currentArrayOwner = (void*)(intptr_t)TIME_HELD_OWNER;
}

static void timeUpdateBorder(Instance* self, GMLReal epsilon) {
    RValue value = TIME_GET(self, border_alpha);
    if (value.type == RVALUE_UNDEFINED) return;
    GMLReal alpha = RValue_toReal(value);
    if (RValue_toBool(TIME_GET(self, border_fade_out))) {
        if (NativeScript_compareReals(alpha, 0, epsilon) > 0) {
            alpha -= TIME_REAL(self, border_fade_value);
            TIME_SET(self, border_alpha, NativeScript_compareReals(alpha, 0, epsilon) <= 0 ? 0 : alpha);
        }
    } else if (RValue_toBool(TIME_GET(self, border_fade_in)) &&
        NativeScript_compareReals(alpha, 1, epsilon) <= 0) {
        alpha += TIME_REAL(self, border_fade_value);
        TIME_SET(self, border_alpha, NativeScript_compareReals(alpha, 1, epsilon) >= 0 ? 1 : alpha);
    }
}

static bool timeBeginStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    if (RValue_toBool(TIME_GET(self, paused))) {
        *outResult = RValue_makeUndefined();
        return true;
    }

    Runner* runner = ctx->runner;
    Instance* global = ctx->globalScopeInstance;
    RunnerKeyboardState* keyboard = runner->keyboard;
    GMLReal epsilon = runner->mathEpsilon;
    RValue center = TIME_GET(self, window_center_toggle);
    if (timeEqual(TIME_GET(self, fullscreen_toggle), 1, epsilon) ||
        RunnerKeyboard_checkPressed(keyboard, VK_F4) || timeEqual(center, 2, epsilon)) return false;
    if (RValue_toBool(TIME_GET(global, debug)) &&
        (RunnerKeyboard_checkPressed(keyboard, TIME_DEBUG_SPEED_KEY) ||
            RunnerKeyboard_checkPressed(keyboard, TIME_DEBUG_SLOW_KEY))) return false;
    if (RValue_toBool(TIME_GET(global, is_console))) {
        RValue paused = cache.osPaused(ctx, nullptr, 0);
        bool needPause = RValue_toBool(paused);
        RValue_free(&paused);
        if (needPause) return false;
    }

    GMLReal quit = TIME_REAL(self, quit_timer);
    if (RunnerKeyboard_check(keyboard, VK_ESCAPE)) {
        quit = NativeScript_compareReals(quit, 0, epsilon) < 0 ? 1 : quit + 1;
        if (NativeScript_compareReals(quit, 30, epsilon) >= 0) return false;
    } else {
        quit -= 2;
    }

    GMLArray* held = timeWritableInputs(TIME_GET(global, input_held));
    GMLArray* pressed = timeWritableInputs(TIME_GET(global, input_pressed));
    GMLArray* released = timeWritableInputs(TIME_GET(global, input_released));
    if (held == nullptr || pressed == nullptr || released == nullptr ||
        held == pressed || held == released || pressed == released) return false;

    TIME_SET(global, time, TIME_REAL(global, time) + 1);
    TIME_SET(self, quit_timer, quit);
    if (timeEqual(center, 1, epsilon)) TIME_SET(self, window_center_toggle, 2);
    held->owner = (void*)(intptr_t)TIME_HELD_OWNER;
    pressed->owner = (void*)(intptr_t)TIME_PRESSED_OWNER;
    released->owner = (void*)(intptr_t)TIME_RELEASED_OWNER;
    timeUpdateInput(ctx, self, GMLArray_slot(held, 0), GMLArray_slot(pressed, 0), GMLArray_slot(released, 0));
    timeUpdateBorder(self, epsilon);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeTimeBeginStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.controllerObject = -1;
    const Objt* objt = &ctx->dataWin->objt;
    for (uint32_t i = 0; i < objt->count; ++i) {
        const GameObject* object = &objt->objects[i];
        if (object->present && strcmp(object->name, "obj_gamecontroller") == 0) {
            cache.controllerObject = (int32_t)i;
            break;
        }
    }
    cache.osPaused = VM_findBuiltin(ctx, "os_is_paused");
    if (cache.controllerObject < 0 || cache.osPaused == nullptr ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_time_Step_1", timeBeginStep)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = VM_getOrAllocateVarID(ctx, #name);
    TIME_SELF_VARS(RESOLVE_SELF)
    TIME_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF
}
