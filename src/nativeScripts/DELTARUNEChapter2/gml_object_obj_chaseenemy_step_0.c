#include "nativeScripts/native_runtime.h"

#include "collision.h"
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

#define CHASE_STEP_VARS(X) \
    X(encounterflag) X(init) X(targetx) X(targety) X(frozen) X(offscreen_frozen) X(ignorefreeze) \
    X(hadfrozen) X(remspeed) X(pacecon) X(pacetimer) X(pacetype) X(pacecon2) X(cancelwalk) \
    X(walk_index) X(yy) X(xx) X(pacespeed) X(moveradius) X(pathSpeed) X(facing) X(t) \
    X(increment) X(shift) X(amplitude) X(rotation) X(distance) X(spd) X(paceinit) \
    X(siner) X(pacesinspeed) X(viewblocked) X(viewpointx) X(viewpointy) \
    X(detectpointxoffset) X(detectpointyoffset) X(canAlertThroughWalls) X(mcx) X(mcy) \
    X(alerted) X(alerttype) X(radius) X(blind) X(alerttimer) X(alertcon) X(alertskip) \
    X(noAlertSound) X(targalarm) X(chasetype) X(excl) X(xnext) X(ynext) \
    X(startchasespeed) X(topchasespeed) X(chaseaccel) X(minspeed) X(maxspeed) \
    X(mymidx) X(mymidy) X(facetimer) X(test_release_bullet) X(customhitbox) X(hitx1) X(hitx2) X(hity1) X(hity2)

static struct {
#define DECLARE_ID(name) int32_t name;
    CHASE_STEP_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t floatAmount, interact, inputHeld;
    int32_t mainchara, solid, interactable, solidEnemy, exclamation, alertSound;
    int32_t werewire, virovirokun, mazeRoom;
    BuiltinFunc move, line, pathEnd, pointDirection, lengthX, lengthY;
    NativeCallBinding depth, outside, sound;
} cache;

#define CHASE_GET(name) Instance_getSelfVar(self, cache.name)
#define CHASE_REAL(name) RValue_toReal(CHASE_GET(name))
#define CHASE_SET(name, value) Instance_setSelfVar(self, cache.name, RValue_makeReal((GMLReal)(value)))

static bool chaseEqual(GMLReal a, GMLReal b, GMLReal epsilon) {
    return NativeScript_compareReals(a, b, epsilon) == 0;
}

static void chaseBuiltin(VMContext* ctx, Instance* self, int16_t id, const char* name, GMLReal value) {
    VMBuiltins_setVariable(ctx, self, id, name, RValue_makeReal(value), -1);
}

static void chaseMove(VMContext* ctx, GMLReal x, GMLReal y, GMLReal speed) {
    RValue args[] = { RValue_makeReal(x), RValue_makeReal(y), RValue_makeReal(speed) };
    RValue result = cache.move(ctx, args, 3);
    RValue_free(&result);
}

static void chaseFace(Instance* self, GMLReal epsilon) {
    CHASE_SET(facing, NativeScript_compareReals(self->x, self->xprevious, epsilon) > 0 ? 1 : 0);
}

static GMLReal chaseDirection(VMContext* ctx, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2) {
    RValue args[] = { RValue_makeReal(x1), RValue_makeReal(y1), RValue_makeReal(x2), RValue_makeReal(y2) };
    RValue result = cache.pointDirection(ctx, args, 4);
    GMLReal direction = RValue_toReal(result);
    RValue_free(&result);
    return direction;
}

static void chasePace(VMContext* ctx, Instance* self, Instance* mainchara, GMLReal epsilon) {
    GMLReal timer = CHASE_REAL(pacetimer) + 1;
    GMLReal pace = CHASE_REAL(pacetype);
    GMLReal floating = RValue_toReal(Instance_getSelfVar(self, cache.floatAmount));
    CHASE_SET(pacetimer, timer);
    if (chaseEqual(pace, 1, epsilon)) {
        if (chaseEqual(timer, 10, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", 2);
        if (chaseEqual(timer, 34, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", 0);
        if (chaseEqual(timer, 50, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", -2);
        if (chaseEqual(timer, 74, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", 0);
        if (chaseEqual(timer, 80, epsilon)) CHASE_SET(pacetimer, 0);
    }
    if (chaseEqual(pace, 2, epsilon)) {
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", GMLReal_sin(timer / 24) * 4);
        chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", GMLReal_cos(timer / 24) * 4);
    }
    if (chaseEqual(pace, 5, epsilon)) {
        if (chaseEqual(timer, 1, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", 6);
        if (chaseEqual(timer, 25, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", -6);
        if (NativeScript_compareReals(self->y, self->ystart, epsilon) < 0) CHASE_SET(pacetimer, 0);
    }
    if (chaseEqual(pace, 6, epsilon)) {
        CHASE_SET(cancelwalk, 1);
        CHASE_SET(walk_index, CHASE_REAL(walk_index) + (GMLReal)0.25);
    }
    if (chaseEqual(pace, 7, epsilon) || chaseEqual(pace, 7.1, epsilon))
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", -GMLReal_sin(timer / 30) * 10);
    if (chaseEqual(pace, 7.5, epsilon))
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", GMLReal_sin(timer / 30) * 10);
    if (chaseEqual(pace, 8, epsilon))
        chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", -GMLReal_sin(timer / 25) * (GMLReal)12.5);
    if (chaseEqual(pace, 9, epsilon)) {
        if (!chaseEqual(floating, 0, epsilon))
            chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) - GMLReal_sin(timer / 5) * floating);
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed",
            GMLReal_sin(timer / (CHASE_REAL(pacespeed) * 30)) * (CHASE_REAL(moveradius) / 20));
        CHASE_SET(pacespeed, 0.8);
    }
    if (chaseEqual(pace, 9.5, epsilon)) {
        if (!chaseEqual(floating, 0, epsilon))
            chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) - GMLReal_sin(timer / floating));
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed",
            2 * (GMLReal_sin(timer / 24) * (CHASE_REAL(moveradius) / 20)));
    }
    if (chaseEqual(pace, 10, epsilon)) {
        if (chaseEqual(CHASE_REAL(pacecon2), 0, epsilon)) {
            if (NativeScript_compareReals(self->vspeed, 0, epsilon) > 0)
                chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", (GMLReal)self->vspeed * (GMLReal)0.9);
            if (NativeScript_compareReals(self->vspeed, 0.5, epsilon) <= 0) CHASE_SET(pacecon2, 1);
        }
        if (chaseEqual(CHASE_REAL(pacecon2), 1, epsilon)) chaseMove(ctx, mainchara->x, mainchara->y, 4);
    }
    if (chaseEqual(pace, 11, epsilon)) {
        CHASE_SET(cancelwalk, 1);
        self->imageSpeed = 0.25f;
        if (chaseEqual(CHASE_REAL(pathSpeed), 0, epsilon)) CHASE_SET(pathSpeed, 6);
        if (NativeScript_compareReals(self->direction, 136, epsilon) >= 0 &&
            NativeScript_compareReals(self->direction, 225, epsilon) <= 0) CHASE_SET(facing, 0);
        if (NativeScript_compareReals(self->direction, 306, epsilon) >= 0 ||
            NativeScript_compareReals(self->direction, 45, epsilon) <= 0) CHASE_SET(facing, 1);
    }
    if (chaseEqual(pace, 12, epsilon)) {
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", -GMLReal_sin(timer / 30) * 10);
        chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", (-GMLReal_sin(timer / 12) * (GMLReal)12.5) / 10);
    }
    if (chaseEqual(pace, 13, epsilon) || chaseEqual(pace, 15, epsilon)) {
        if (chaseEqual(pace, 15, epsilon) && !chaseEqual(floating, 0, epsilon))
            chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) - GMLReal_sin(timer / floating));
        GMLReal t = GMLReal_fmod(CHASE_REAL(t) + CHASE_REAL(increment), 360);
        GMLReal shift = CHASE_REAL(amplitude) * GMLReal_sin(t * M_PI / 180.0);
        CHASE_SET(t, t);
        CHASE_SET(shift, shift);
        if (chaseEqual(pace, 13, epsilon)) {
            chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) + shift);
        } else {
            chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", CHASE_REAL(xx) + shift);
            chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", (-GMLReal_sin(timer / 12) * (GMLReal)12.5) / 10);
        }
    }
    if (chaseEqual(pace, 14, epsilon)) {
        GMLReal rotation = CHASE_REAL(rotation);
        GMLReal cosine = GMLReal_cos(rotation * M_PI / 180.0);
        GMLReal sine = GMLReal_sin(rotation * M_PI / 180.0);
        GMLReal distance = CHASE_REAL(distance);
        chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", CHASE_REAL(xx) + cosine * distance + sine * distance);
        chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) - sine * distance + cosine * distance);
        rotation += CHASE_REAL(spd);
        if (NativeScript_compareReals(rotation, 359, epsilon) > 0) rotation -= 360;
        CHASE_SET(rotation, rotation);
        if (ctx->runner->currentRoomIndex == cache.mazeRoom)
            CHASE_SET(facing, NativeScript_compareReals(rotation, 180, epsilon) > 0 ? 1 : 0);
    }
    if (chaseEqual(pace, 16, epsilon)) {
        GMLReal phases[] = {0, 0.1, 1, 1.1};
        for (int32_t i = 0; i < 4; ++i) {
            if (!chaseEqual(CHASE_REAL(pacecon2), phases[i], epsilon)) continue;
            if ((i & 1) == 0) {
                chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", (GMLReal)self->hspeed * (GMLReal)1.2);
                chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", GMLReal_fmin(GMLReal_fmax(self->hspeed, -12), 12));
                if ((i == 0 && NativeScript_compareReals(self->x, 240, epsilon) <= 0) ||
                    (i == 2 && NativeScript_compareReals(self->x, 860, epsilon) >= 0))
                    CHASE_SET(pacecon2, phases[i + 1]);
            } else {
                chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", (GMLReal)self->hspeed * (GMLReal)0.7);
                if (NativeScript_compareReals(GMLReal_fabs(self->hspeed), 0.5, epsilon) < 0) {
                    chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", i == 1 ? 1 : -1);
                    CHASE_SET(pacecon2, i == 1 ? 1 : 0);
                }
            }
        }
    }
    if (chaseEqual(pace, 17, epsilon) || chaseEqual(pace, 18, epsilon) || chaseEqual(pace, 23, epsilon))
        chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) - GMLReal_sin(timer / 5));
    if (chaseEqual(pace, 17, epsilon) || chaseEqual(pace, 19, epsilon) || chaseEqual(pace, 23, epsilon)) {
        GMLReal radius = CHASE_REAL(moveradius);
        if (chaseEqual(pace, 17, epsilon)) radius = 10;
        else if (chaseEqual(pace, 19, epsilon)) radius = 20;
        chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", CHASE_REAL(xx) - GMLReal_sin(timer / 20) * radius);
        chaseFace(self, epsilon);
    }
    if (chaseEqual(pace, 20, epsilon)) {
        if (!chaseEqual(floating, 0, epsilon))
            chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) - GMLReal_sin(timer / floating));
        self->imageSpeed = 0.25f;
        CHASE_SET(cancelwalk, 1);
        if (chaseEqual(CHASE_REAL(pacecon2), 0, epsilon)) {
            if (NativeScript_compareReals(self->x, self->xstart + CHASE_REAL(moveradius), epsilon) < 0) {
                chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", self->x + CHASE_REAL(pacespeed));
            } else {
                chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", self->xstart + CHASE_REAL(moveradius));
                CHASE_SET(pacecon2, 1);
            }
        }
        if (chaseEqual(CHASE_REAL(pacecon2), 1, epsilon)) {
            if (NativeScript_compareReals(self->x, self->xstart - CHASE_REAL(moveradius), epsilon) > 0) {
                chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", self->x - CHASE_REAL(pacespeed));
            } else {
                chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", self->xstart - CHASE_REAL(moveradius));
                CHASE_SET(pacecon2, 0);
            }
        }
        chaseFace(self, epsilon);
    }
    if (chaseEqual(pace, 21, epsilon) || chaseEqual(pace, 22, epsilon)) {
        GMLReal siner = CHASE_REAL(siner) + 1;
        CHASE_SET(siner, siner);
        GMLReal shift = GMLReal_sin(siner / CHASE_REAL(pacesinspeed)) * CHASE_REAL(moveradius);
        chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", CHASE_REAL(xx) + (chaseEqual(pace, 21, epsilon) ? -shift : shift));
        chaseFace(self, epsilon);
    }
    if (chaseEqual(pace, 24, epsilon)) {
        CHASE_SET(pacespeed, 25);
        chaseBuiltin(ctx, self, BUILTIN_VAR_Y, "y", CHASE_REAL(yy) + GMLReal_sin(timer / 20) * 10);
        chaseBuiltin(ctx, self, BUILTIN_VAR_X, "x", CHASE_REAL(xx) - GMLReal_sin(timer / CHASE_REAL(pacespeed)) * CHASE_REAL(moveradius));
        chaseFace(self, epsilon);
    }
}

static void chasePursue(VMContext* ctx, Instance* self, Instance* mainchara, GMLReal epsilon) {
    GMLReal type = CHASE_REAL(chasetype);
    GMLReal x = CHASE_REAL(targetx), y = CHASE_REAL(targety);
    if (chaseEqual(type, -1, epsilon)) {
        CHASE_SET(alerttimer, 0);
        CHASE_SET(alertcon, 0);
        CHASE_SET(pacecon, 0);
    }
    if (chaseEqual(type, 1, epsilon) || chaseEqual(type, 2, epsilon)) {
        if (chaseEqual(CHASE_REAL(alerttimer), 0, epsilon)) {
            if (chaseEqual(type, 2, epsilon)) {
                RValue value = Instance_getSelfVar(ctx->globalScopeInstance, cache.inputHeld);
                GMLArray* held = value.type == RVALUE_ARRAY ? value.array : nullptr;
                GMLReal dx = 0, dy = 0;
                if (RValue_toBool(GMLArray_get(held, 1))) dx = 132;
                if (RValue_toBool(GMLArray_get(held, 3))) dx = -132;
                if (RValue_toBool(GMLArray_get(held, 0))) dy = 132;
                if (RValue_toBool(GMLArray_get(held, 2))) dy = -132;
                CHASE_SET(xnext, dx);
                CHASE_SET(ynext, dy);
                x += dx;
                y += dy;
            }
            chaseMove(ctx, x, y, 10);
        }
        GMLReal timer = CHASE_REAL(alerttimer) + 1;
        CHASE_SET(alerttimer, timer);
        if (NativeScript_compareReals(timer, 20, epsilon) >= 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", (GMLReal)self->speed * (GMLReal)0.75);
        if (NativeScript_compareReals(timer, 25, epsilon) >= 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 0);
        if (NativeScript_compareReals(timer, 27, epsilon) >= 0) CHASE_SET(alerttimer, 0);
    }
    bool steering = chaseEqual(type, 5, epsilon) || chaseEqual(type, 5.5, epsilon) || chaseEqual(type, 9, epsilon);
    if (steering) {
        RValue args[] = {
            RValue_makeReal(0.5), RValue_makeReal(chaseDirection(ctx, self->x, self->y, x, y)),
        };
        RValue dx = cache.lengthX(ctx, args, 2);
        chaseBuiltin(ctx, self, BUILTIN_VAR_HSPEED, "hspeed", self->hspeed + RValue_toReal(dx));
        RValue_free(&dx);
        RValue dy = cache.lengthY(ctx, args, 2);
        chaseBuiltin(ctx, self, BUILTIN_VAR_VSPEED, "vspeed", self->vspeed + RValue_toReal(dy));
        RValue_free(&dy);
    }
    if (chaseEqual(type, 0, epsilon) || chaseEqual(type, 3, epsilon) ||
        chaseEqual(type, 4, epsilon) || chaseEqual(type, 5, epsilon)) {
        GMLReal minimum = 4, maximum = 7;
        if (chaseEqual(type, 3, epsilon)) {
            minimum = 2;
            maximum = 4;
        } else if (chaseEqual(type, 4, epsilon)) {
            minimum = 6;
            maximum = 14;
        }
        if (NativeScript_compareReals(self->speed, minimum, epsilon) < 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", minimum);
        if (NativeScript_compareReals(self->speed, maximum, epsilon) < 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", (GMLReal)self->speed + (GMLReal)0.5);
        if (chaseEqual(type, 5, epsilon) && NativeScript_compareReals(self->speed, 7, epsilon) >= 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 7);
        chaseMove(ctx, x, y, self->speed);
    }
    if (chaseEqual(type, 5.5, epsilon)) {
        if (NativeScript_compareReals(self->speed, CHASE_REAL(startchasespeed), epsilon) < 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", CHASE_REAL(startchasespeed));
        if (!chaseEqual(self->speed, CHASE_REAL(topchasespeed), epsilon))
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed",
                self->speed + (CHASE_REAL(topchasespeed) - self->speed) * CHASE_REAL(chaseaccel));
        chaseMove(ctx, x, y, self->speed);
    }
    if (chaseEqual(type, 6, epsilon)) {
        chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 8);
        chaseMove(ctx, x, y, self->speed);
    }
    if (chaseEqual(type, 7, epsilon)) {
        if (chaseEqual(self->speed, 0, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 6);
        if (NativeScript_compareReals(self->speed, 10, epsilon) < 0)
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", (GMLReal)self->speed * (GMLReal)1.1);
        self->imageSpeed = 0.25f;
        chaseMove(ctx, x, y, self->speed);
    }
    if (chaseEqual(type, 8, epsilon)) {
        RValue width = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1);
        InstanceBBox bbox = Collision_computeBBox(ctx->runner, self);
        GMLReal midx = self->x + RValue_toReal(width) / 2;
        GMLReal midy = self->y + ((bbox.valid ? bbox.bottom : self->y) - self->y) / 2;
        CHASE_SET(mymidx, midx);
        CHASE_SET(mymidy, midy);
        GMLReal dx = mainchara->x + 20 - midx, dy = mainchara->y + 48 - midy;
        if (NativeScript_compareReals(GMLReal_sqrt(dx * dx + dy * dy), 50, epsilon) > 0) {
            GMLReal angle = chaseDirection(ctx, midx, midy, mainchara->x + 20, mainchara->y + 48);
            chaseBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction", angle + 180);
        }
        chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 6);
    }
    if (chaseEqual(type, 9, epsilon)) {
        chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed",
            GMLReal_fmin(GMLReal_fmax(self->speed, CHASE_REAL(minspeed)), CHASE_REAL(maxspeed)));
        chaseMove(ctx, x, y, self->speed);
    }
}

static bool chaseLineBlocked(VMContext* ctx, GMLReal x1, GMLReal y1, GMLReal x2, GMLReal y2,
    int32_t object, bool precise, bool notme) {
    RValue args[] = {
        RValue_makeReal(x1), RValue_makeReal(y1), RValue_makeReal(x2), RValue_makeReal(y2),
        RValue_makeInt32(object), RValue_makeBool(precise), RValue_makeBool(notme),
    };
    RValue result = cache.line(ctx, args, 7);
    bool blocked = NativeScript_compareReals(RValue_toReal(result), 0, ctx->runner->mathEpsilon) > 0;
    RValue_free(&result);
    return blocked;
}

static bool chaseenemyStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    if ((NativeScript_compareReals(CHASE_REAL(encounterflag), 0, epsilon) > 0 &&
        chaseEqual(CHASE_REAL(init), 0, epsilon)) ||
        chaseEqual(CHASE_REAL(test_release_bullet), 1, epsilon)) return false;
    GMLReal pace = CHASE_REAL(pacetype);
    if ((chaseEqual(pace, 21, epsilon) || chaseEqual(pace, 22, epsilon)) &&
        chaseEqual(CHASE_REAL(paceinit), 0, epsilon)) return false;
    if (chaseEqual(CHASE_REAL(alerttype), 1, epsilon)) return false;
    Instance* mainchara = VM_findInstanceByTarget(ctx, cache.mainchara);
    if (mainchara == nullptr) return false;
    RValue result = NativeScript_call(ctx, &cache.depth, "gml_Script_scr_depth_alt", nullptr, 0);
    RValue_free(&result);
    RValue widthValue = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1);
    RValue heightValue = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1);
    GMLReal width = RValue_toReal(widthValue), height = RValue_toReal(heightValue);
    RValue charWidth = VMBuiltins_getVariable(ctx, mainchara, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1);
    InstanceBBox charBox = Collision_computeBBox(runner, mainchara);
    GMLReal charBottom = charBox.valid ? charBox.bottom : mainchara->y;
    GMLReal origin = 0;
    if (self->spriteIndex >= 0 && (uint32_t)self->spriteIndex < ctx->dataWin->sprt.count)
        origin = ctx->dataWin->sprt.sprites[self->spriteIndex].originY;
    CHASE_SET(targetx, mainchara->x + RValue_toReal(charWidth) / 2 - width / 2);
    CHASE_SET(targety, charBottom - height + origin * self->imageYscale);
    RValue tolerance = RValue_makeReal(200);
    result = NativeScript_call(ctx, &cache.outside, "gml_Script_scr_outside_camera", &tolerance, 1);
    bool outside = RValue_toBool(result);
    RValue_free(&result);
    RValue interactValue = Instance_getSelfVar(ctx->globalScopeInstance, cache.interact);
    GMLReal interact = RValue_toReal(interactValue);
    bool frozen = (outside && chaseEqual(CHASE_REAL(offscreen_frozen), 1, epsilon)) ||
        (!chaseEqual(interact, 0, epsilon) && !chaseEqual(interact, 4, epsilon));
    CHASE_SET(frozen, frozen);
    if (frozen && chaseEqual(CHASE_REAL(ignorefreeze), 0, epsilon) && chaseEqual(CHASE_REAL(hadfrozen), 0, epsilon)) {
        CHASE_SET(remspeed, self->speed);
        chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 0);
        CHASE_SET(hadfrozen, 1);
    }
    if (!frozen) {
        if (chaseEqual(CHASE_REAL(hadfrozen), 1, epsilon)) {
            if (chaseEqual(self->speed, 0, epsilon)) chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", CHASE_REAL(remspeed));
            CHASE_SET(hadfrozen, 0);
        }
        if (chaseEqual(CHASE_REAL(pacecon), 0, epsilon)) chasePace(ctx, self, mainchara, epsilon);
        InstanceBBox bbox = Collision_computeBBox(runner, self);
        GMLReal left = bbox.valid ? bbox.left : self->x, right = bbox.valid ? bbox.right : self->x;
        GMLReal bottom = bbox.valid ? bbox.bottom : self->y;
        GMLReal viewpointX = (left + right) / 2 + CHASE_REAL(detectpointxoffset);
        GMLReal viewpointY = bottom + CHASE_REAL(detectpointyoffset);
        CHASE_SET(viewblocked, 0);
        CHASE_SET(viewpointx, viewpointX);
        CHASE_SET(viewpointy, viewpointY);
        if (chaseEqual(CHASE_REAL(canAlertThroughWalls), 0, epsilon)) {
            GMLReal mcx = ((charBox.valid ? charBox.left : mainchara->x) + (charBox.valid ? charBox.right : mainchara->x)) / 2;
            CHASE_SET(mcx, mcx);
            CHASE_SET(mcy, charBottom);
            bool blocked = chaseLineBlocked(ctx, viewpointX, viewpointY, mcx, charBottom, cache.solid, true, true);
            blocked |= chaseLineBlocked(ctx, viewpointX, viewpointY, mcx, charBottom, cache.interactable, true, false);
            blocked |= chaseLineBlocked(ctx, viewpointX, viewpointY, mcx, charBottom, cache.solidEnemy, true, false);
            if (blocked) CHASE_SET(viewblocked, 1);
        }
        if (chaseEqual(CHASE_REAL(alerted), 0, epsilon) && chaseEqual(CHASE_REAL(alerttype), 0, epsilon)) {
            GMLReal dx = mainchara->x + 20 - viewpointX, dy = mainchara->y + 48 - viewpointY;
            if (NativeScript_compareReals(GMLReal_sqrt(dx * dx + dy * dy), CHASE_REAL(radius), epsilon) <= 0 &&
                chaseEqual(CHASE_REAL(blind), 0, epsilon)) CHASE_SET(alerted, 1);
        }
        if (chaseEqual(CHASE_REAL(viewblocked), 1, epsilon)) CHASE_SET(alerted, 0);
        if (chaseEqual(CHASE_REAL(alerted), 1, epsilon)) {
            chaseBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 0);
            CHASE_SET(pacecon, 1);
            CHASE_SET(alerttimer, 0);
            CHASE_SET(alertcon, 1);
            if (chaseEqual(pace, 11, epsilon)) {
                result = cache.pathEnd(ctx, nullptr, 0);
                RValue_free(&result);
            }
            if (chaseEqual(CHASE_REAL(alertskip), 0, epsilon)) {
                if (chaseEqual(CHASE_REAL(noAlertSound), 0, epsilon)) {
                    RValue sound = RValue_makeInt32(cache.alertSound);
                    result = NativeScript_call(ctx, &cache.sound, "gml_Script_snd_play", &sound, 1);
                    RValue_free(&result);
                }
                GMLReal alarm = chaseEqual(CHASE_REAL(chasetype), 8, epsilon) ? 8 : 20;
                CHASE_SET(targalarm, alarm);
                Instance* exclamation = Runner_createInstance(runner, (float)(self->x + width / 2), self->y - 20, cache.exclamation);
                CHASE_SET(excl, exclamation != nullptr ? (GMLReal)exclamation->instanceId : INSTANCE_NOONE);
                if (exclamation != nullptr) {
                    if (ctx->creatorVarID >= 0)
                        Instance_setSelfVar(exclamation, ctx->creatorVarID, RValue_makeReal(self->instanceId));
                    VMBuiltins_setVariable(ctx, exclamation, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(alarm), 0);
                }
                VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(alarm), 4);
            } else {
                CHASE_SET(alertcon, 2);
            }
            CHASE_SET(alerted, 2);
        }
        if (chaseEqual(CHASE_REAL(alertcon), 2, epsilon)) chasePursue(ctx, self, mainchara, epsilon);
        GMLReal facetimer = CHASE_REAL(facetimer) + 1;
        CHASE_SET(facetimer, facetimer);
        if (NativeScript_compareReals(facetimer, 10, epsilon) >= 0) {
            if (NativeScript_compareReals(self->hspeed, -0.2, epsilon) <= 0 && chaseEqual(CHASE_REAL(facing), 1, epsilon)) {
                CHASE_SET(facing, 0);
                CHASE_SET(facetimer, 0);
            }
            if (NativeScript_compareReals(self->hspeed, 0.2, epsilon) >= 0 && chaseEqual(CHASE_REAL(facing), 0, epsilon)) {
                CHASE_SET(facing, 1);
                CHASE_SET(facetimer, 0);
            }
        }
        if (chaseEqual(CHASE_REAL(cancelwalk), 0, epsilon)) {
            CHASE_SET(walk_index, CHASE_REAL(walk_index) + (GMLReal)self->speed / 20);
            if (chaseEqual(self->speed, 0, epsilon)) CHASE_SET(walk_index, 0);
        } else {
            CHASE_SET(walk_index, self->imageIndex);
        }
    }
    if (self->spriteIndex == cache.werewire) {
        CHASE_SET(detectpointyoffset, -88);
        CHASE_SET(customhitbox, 1);
        CHASE_SET(hitx1, -2);
        CHASE_SET(hitx2, 66);
        CHASE_SET(hity1, 42);
        CHASE_SET(hity2, 130);
    }
    if (self->spriteIndex == cache.virovirokun) CHASE_SET(detectpointyoffset, -50);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2ChaseenemyStep_register(VMContext* ctx) {
    static const char* const objects[] = {
        "obj_mainchara", "obj_solidblock", "obj_interactablesolid", "obj_solidenemy", "obj_excblcon",
    };
    int32_t* ids[] = { &cache.mainchara, &cache.solid, &cache.interactable, &cache.solidEnemy, &cache.exclamation };
    for (int32_t j = 0; j < 5; ++j) {
        *ids[j] = -1;
        for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
            const GameObject* object = &ctx->dataWin->objt.objects[i];
            if (object->present && strcmp(object->name, objects[j]) == 0) {
                *ids[j] = (int32_t)i;
                break;
            }
        }
        if (*ids[j] < 0) return;
    }
    cache.werewire = -1;
    cache.virovirokun = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (!sprite->present) continue;
        if (strcmp(sprite->name, "spr_werewire_hang_overworld") == 0) cache.werewire = (int32_t)i;
        else if (strcmp(sprite->name, "spr_virovirokun_idle") == 0) cache.virovirokun = (int32_t)i;
    }
    cache.alertSound = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sond.count; ++i) {
        if (strcmp(ctx->dataWin->sond.sounds[i].name, "snd_b") == 0) cache.alertSound = (int32_t)i;
    }
    cache.mazeRoom = -1;
    for (uint32_t i = 0; i < ctx->dataWin->room.count; ++i) {
        if (strcmp(ctx->dataWin->room.rooms[i].name, "room_dw_cyber_maze_virokun") == 0)
            cache.mazeRoom = (int32_t)i;
    }
    cache.move = VM_findBuiltin(ctx, "move_towards_point");
    cache.line = VM_findBuiltin(ctx, "collision_line");
    cache.pathEnd = VM_findBuiltin(ctx, "path_end");
    cache.pointDirection = VM_findBuiltin(ctx, "point_direction");
    cache.lengthX = VM_findBuiltin(ctx, "lengthdir_x");
    cache.lengthY = VM_findBuiltin(ctx, "lengthdir_y");
    cache.depth = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.outside = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.sound = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.werewire < 0 || cache.virovirokun < 0 || cache.alertSound < 0 ||
        cache.move == nullptr || cache.line == nullptr || cache.pathEnd == nullptr ||
        cache.pointDirection == nullptr || cache.lengthX == nullptr || cache.lengthY == nullptr ||
        !NativeScript_canCall(ctx, &cache.depth, "gml_Script_scr_depth_alt") ||
        !NativeScript_canCall(ctx, &cache.outside, "gml_Script_scr_outside_camera") ||
        !NativeScript_canCall(ctx, &cache.sound, "gml_Script_snd_play") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_chaseenemy_Step_0", chaseenemyStep)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    CHASE_STEP_VARS(RESOLVE_SELF)
#undef RESOLVE_SELF
    cache.floatAmount = NativeScript_getSelfVarID(ctx, "float");
    cache.interact = NativeScript_getGlobalVarID(ctx, "interact");
    cache.inputHeld = NativeScript_getGlobalVarID(ctx, "input_held");
}
