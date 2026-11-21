#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define HEART_SELF_VARS(X) \
    X(wallcheck) X(press_l) X(press_r) X(press_d) X(press_u) X(bkx) X(bky) X(bkxy) X(jelly) \
    X(px) X(py) X(wspeed) X(disableslow) X(xmeet) X(ymeet) X(xymeet) X(g) X(mvd) X(i) X(j) \
    X(boundaryup) X(dmgnoise)

#define HEART_GLOBAL_VARS(X) X(input_held) X(inv) X(heartx) X(hearty)

static struct {
#define DECLARE_ID(name) int32_t name;
    HEART_SELF_VARS(DECLARE_ID)
    HEART_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t wallObject, hurtSound;
    NativeCallBinding soundStop, soundPlay;
} cache;

typedef struct {
    VMContext* ctx;
    Instance* self;
    GMLReal speed, epsilon, px, py;
    bool left, right, up, down;
} HeartMove;

#define HEART_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define HEART_REAL(inst, name) RValue_toReal(HEART_GET(inst, name))
#define HEART_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static int heartCompare(const HeartMove* move, GMLReal a, GMLReal b) {
    return NativeScript_compareReals(a, b, move->epsilon);
}

static bool heartMeets(const HeartMove* move, GMLReal x, GMLReal y) {
    return VMBuiltins_placeMeeting(move->ctx->runner, move->self, x, y, cache.wallObject);
}

static void heartPosition(HeartMove* move, int16_t variable, const char* name, GMLReal position) {
    VMBuiltins_setVariable(move->ctx, move->self, variable, name, RValue_makeReal(position), -1);
}

static void heartSlide(HeartMove* move, bool horizontal) {
    Instance* self = move->self;
    bool towardNegative = horizontal ? !move->down : !move->right;
    bool towardPositive = horizontal ? !move->up : !move->left;
    GMLReal x = horizontal ? self->x + move->px : self->x;
    GMLReal y = horizontal ? self->y : self->y + move->py;
    GMLReal g = move->speed;
    if (heartCompare(move, g, 0) > 0) HEART_SET(self, mvd, 0);
    for (; heartCompare(move, g, 0) > 0; g -= 1) {
        GMLReal delta = 0;
        if (towardNegative && !heartMeets(move, horizontal ? x : x - g, horizontal ? y - g : y)) {
            delta = -g;
        } else if (towardPositive && !heartMeets(move, horizontal ? x : x + g, horizontal ? y + g : y)) {
            delta = g;
        }
        if (delta != 0) {
            if (horizontal) {
                heartPosition(move, BUILTIN_VAR_Y, "y", y + delta);
                move->py = 0;
            } else {
                heartPosition(move, BUILTIN_VAR_X, "x", x + delta);
                move->px = 0;
            }
            break;
        }
    }
    HEART_SET(self, g, g);
}

static GMLReal heartBackOff(HeartMove* move, bool horizontal, bool* cleared) {
    Instance* self = move->self;
    GMLReal i = horizontal ? move->px : move->py;
    int direction = heartCompare(move, i, 0);
    if (direction == 0) return 0;

    while (direction * heartCompare(move, i, 0) >= 0) {
        GMLReal x = horizontal ? self->x + i : self->x;
        GMLReal y = horizontal ? self->y : self->y + i;
        if (!heartMeets(move, x, y)) {
            *cleared = true;
            break;
        }
        i -= direction;
    }
    HEART_SET(self, i, i);
    return *cleared ? i : 0;
}

static GMLReal heartTowardZero(HeartMove* move, GMLReal value) {
    if (heartCompare(move, GMLReal_fabs(value), 1) < 0) return 0;
    if (heartCompare(move, value, 0) > 0) value -= 1;
    if (heartCompare(move, value, 0) < 0) value += 1;
    return value;
}

static void heartResolveWalls(HeartMove* move) {
    Instance* self = move->self;
    bool diagonal = heartMeets(move, self->x + move->px, self->y + move->py);
    bool xHit = move->py == 0 ? diagonal : heartMeets(move, self->x + move->px, self->y);
    bool clearedX = false, clearedY = false, clearedXY = false;
    if (xHit) {
        heartSlide(move, true);
        move->px = heartBackOff(move, true, &clearedX);
    }

    bool yHit = !xHit && move->px == 0 ? diagonal : heartMeets(move, self->x, self->y + move->py);
    if (yHit) {
        heartSlide(move, false);
        move->py = heartBackOff(move, false, &clearedY);
    }

    bool finalHit = xHit || yHit ? heartMeets(move, self->x + move->px, self->y + move->py) : diagonal;
    if (finalHit) {
        GMLReal i = move->px;
        GMLReal j = move->py;
        while (heartCompare(move, j, 0) != 0 || heartCompare(move, i, 0) != 0) {
            if (!heartMeets(move, self->x + i, self->y + j)) {
                clearedXY = true;
                break;
            }
            j = heartTowardZero(move, j);
            i = heartTowardZero(move, i);
        }
        HEART_SET(self, i, i);
        HEART_SET(self, j, j);
        move->px = clearedXY ? i : 0;
        move->py = clearedXY ? j : 0;
    }
    HEART_SET(self, xmeet, xHit);
    HEART_SET(self, ymeet, yHit);
    HEART_SET(self, xymeet, diagonal || finalHit);
    HEART_SET(self, bkx, clearedX);
    HEART_SET(self, bky, clearedY);
    HEART_SET(self, bkxy, clearedXY);
}

static bool heartStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    Instance* global = ctx->globalScopeInstance;
    Runner* runner = ctx->runner;
    RValue held = HEART_GET(global, input_held);
    GMLArray* input = held.type == RVALUE_ARRAY ? held.array : nullptr;
    HeartMove move = {
        .ctx = ctx, .self = self, .speed = HEART_REAL(self, wspeed), .epsilon = runner->mathEpsilon,
        .left = RValue_toBool(GMLArray_get(input, 3)), .right = RValue_toBool(GMLArray_get(input, 1)),
        .up = RValue_toBool(GMLArray_get(input, 2)), .down = RValue_toBool(GMLArray_get(input, 0)),
    };

    HEART_SET(self, wallcheck, 0);
    HEART_SET(self, jelly, 2);
    HEART_SET(self, press_l, move.left);
    HEART_SET(self, press_r, move.right);
    HEART_SET(self, press_u, move.up);
    HEART_SET(self, press_d, move.down);
    move.px = move.left ? -move.speed : move.right ? move.speed : 0;
    move.py = move.up ? -move.speed : move.down ? move.speed : 0;
    if (RValue_toBool(GMLArray_get(input, 5)) || RValue_toBool(GMLArray_get(input, 8))) {
        RValue disable = HEART_GET(self, disableslow);
        if (disable.type != RVALUE_UNDEFINED && heartCompare(&move, RValue_toReal(disable), 0) == 0) {
            move.px = GMLReal_ceil(move.px * (GMLReal)0.5);
            move.py = GMLReal_ceil(move.py * (GMLReal)0.5);
        }
    } else {
        HEART_SET(self, disableslow, 0);
    }
    heartResolveWalls(&move);

    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    GMLReal viewX = camera != nullptr ? (GMLReal)camera->viewX : -1;
    GMLReal viewY = camera != nullptr ? (GMLReal)camera->viewY : -1;
    GMLReal spriteWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1));
    GMLReal spriteHeight = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
    GMLReal right = (viewX + 640) - spriteWidth;
    GMLReal bottom = (viewY + 320) - spriteHeight;
    GMLReal boundary = HEART_REAL(self, boundaryup);
    if (heartCompare(&move, self->x + move.px, right) >= 0) move.px = right - self->x;
    if (heartCompare(&move, self->x + move.px, 0) <= 0) move.px = -self->x;
    if (heartCompare(&move, self->y + move.py, 0) <= 0) move.py = -self->y;
    if (heartCompare(&move, self->y + move.py, bottom + boundary) >= 0) move.py = (bottom - self->y) + boundary;
    HEART_SET(self, px, move.px);
    HEART_SET(self, py, move.py);
    heartPosition(&move, BUILTIN_VAR_X, "x", self->x + move.px);
    heartPosition(&move, BUILTIN_VAR_Y, "y", self->y + move.py);

    RValue noise = HEART_GET(self, dmgnoise);
    if (noise.type != RVALUE_UNDEFINED && heartCompare(&move, RValue_toReal(noise), 1) == 0) {
        HEART_SET(self, dmgnoise, 0);
        RValue sound = RValue_makeInt32(cache.hurtSound);
        RValue result = NativeScript_call(ctx, &cache.soundStop, "gml_Script_snd_stop", &sound, 1);
        RValue_free(&result);
        result = NativeScript_call(ctx, &cache.soundPlay, "gml_Script_snd_play", &sound, 1);
        RValue_free(&result);
    }

    GMLReal inv = HEART_REAL(global, inv) - 1;
    HEART_SET(global, inv, inv);
    if (heartCompare(&move, inv, 0) > 0) {
        self->imageSpeed = 0.25f;
    } else {
        self->imageSpeed = 0;
        self->imageIndex = 0;
    }
    HEART_SET(global, heartx, (self->x + (GMLReal)2) - viewX);
    HEART_SET(global, hearty, (self->y + (GMLReal)2) - viewY);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeHeartStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.wallObject = -1;
    cache.hurtSound = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && strcmp(object->name, "obj_battlesolid") == 0) {
            cache.wallObject = (int32_t)i;
            break;
        }
    }
    for (uint32_t i = 0; i < ctx->dataWin->sond.count; ++i) {
        const Sound* sound = &ctx->dataWin->sond.sounds[i];
        if (sound->name != nullptr && strcmp(sound->name, "snd_hurt1") == 0) {
            cache.hurtSound = (int32_t)i;
            break;
        }
    }

    cache.soundStop = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundPlay = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.wallObject < 0 || cache.hurtSound < 0 ||
        !NativeScript_canCall(ctx, &cache.soundStop, "gml_Script_snd_stop") ||
        !NativeScript_canCall(ctx, &cache.soundPlay, "gml_Script_snd_play") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_heart_Step_0", heartStep)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = VM_getOrAllocateVarID(ctx, #name);
    HEART_SELF_VARS(RESOLVE_SELF)
    HEART_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF
}
