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

enum {
    FOLLOWER_HISTORY = 25,
    FOLLOWER_X_OWNER = 0x287e3,
    FOLLOWER_Y_OWNER = 0x287e4,
    FOLLOWER_FACING_OWNER = 0x2870b,
};

#define FOLLOWER_VARS(X) \
    X(nowx) X(nowy) X(moved) X(walk) X(runmove) X(blushtimer) X(i) X(remx) X(remy) \
    X(facing) X(target) X(dir) X(fun) X(walkbuffer) X(walktimer) X(distfrommcx) X(distfrommcy)

static struct {
#define DECLARE_ID(name) int32_t name;
    FOLLOWER_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t sprites[4], blushSprites[4];
    int32_t globalFacing, interact, leader, susieUp, ralseiDown;
} cache;

#define GET(name) Instance_getSelfVar(self, cache.name)
#define REAL(name) RValue_toReal(GET(name))
#define SET(name, value) Instance_setSelfVar(self, cache.name, RValue_makeReal(value))

static GMLArray* followerArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static bool followerEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static void followerShift(GMLArray* array, RValue first, intptr_t owner) {
    RValue value = RValue_makeIndependent(first);
    GMLArray_growTo(array, FOLLOWER_HISTORY + 1);
    RValue* slots = GMLArray_slot(array, 0);
    RValue_free(&slots[FOLLOWER_HISTORY]);
    memmove(slots + 1, slots, FOLLOWER_HISTORY * sizeof(*slots));
    slots[0] = value;
    array->owner = (void*)owner;
}

static void followerAnimate(Instance* self, GMLReal buffer, bool run, GMLReal epsilon) {
    GMLReal timer = REAL(walktimer);
    if (NativeScript_compareReals(buffer, 3, epsilon) > 0) {
        timer += 1.5;
        if (run) timer += 1.5;
        if (NativeScript_compareReals(timer, 40, epsilon) >= 0) timer -= 40;
        SET(walktimer, timer);
        if (NativeScript_compareReals(timer, 30, epsilon) >= 0) self->imageIndex = 3;
        else if (NativeScript_compareReals(timer, 20, epsilon) >= 0) self->imageIndex = 2;
        else if (NativeScript_compareReals(timer, 10, epsilon) >= 0) self->imageIndex = 1;
        else self->imageIndex = 0;
    }
    if (NativeScript_compareReals(buffer, 0, epsilon) <= 0) {
        if (NativeScript_compareReals(timer, 10, epsilon) < 0) SET(walktimer, 9.5);
        else if (NativeScript_compareReals(timer, 20, epsilon) < 0) SET(walktimer, 19.5);
        else if (NativeScript_compareReals(timer, 30, epsilon) < 0) SET(walktimer, 29.5);
        else SET(walktimer, 39.5);
        self->imageIndex = 0;
    }
}

static void followerBlush(VMContext* ctx, Instance* self, Instance* leader, RValue facing,
    RValue downSprite, bool animate, GMLReal epsilon) {
    if (!animate || !followerEqual(downSprite, cache.ralseiDown, epsilon) ||
        !followerEqual(Instance_getSelfVar(ctx->globalScopeInstance, cache.interact), 0, epsilon)) {
        SET(blushtimer, 0);
        return;
    }

    GMLReal dx = (GMLReal)leader->x - ((GMLReal)self->x + 4);
    GMLReal dy = (GMLReal)leader->y - ((GMLReal)self->y + 12);
    SET(distfrommcx, dx);
    SET(distfrommcy, dy);
    GMLReal timer = REAL(blushtimer);
    if (NativeScript_compareReals(GMLReal_fabs(dy), 10, epsilon) <= 0 &&
        NativeScript_compareReals(GMLReal_fabs(dx), 16, epsilon) <= 0) {
        if (NativeScript_compareReals(dx, 0, epsilon) <= 0 && followerEqual(facing, 1, epsilon)) {
            SET(blushtimer, ++timer);
        }
        if (NativeScript_compareReals(dx, 0, epsilon) >= 0 && followerEqual(facing, 3, epsilon)) {
            SET(blushtimer, ++timer);
        }
    } else {
        timer = 0;
        SET(blushtimer, timer);
    }

    if (NativeScript_compareReals(timer, 300, epsilon) >= 0) {
        for (int32_t i = 0; i < 4; ++i) {
            if (followerEqual(Instance_getSelfVar(self, cache.sprites[i]), self->spriteIndex, epsilon)) {
                RValue blush = Instance_getSelfVar(self, cache.blushSprites[i]);
                VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", blush, -1);
            }
        }
    }
}

static bool caterpillarEndStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Instance* leader = VM_findInstanceByTarget(ctx, cache.leader);
    if (self == nullptr || leader == nullptr) return false;

    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLArray* xs = followerArray(GET(remx));
    GMLArray* ys = followerArray(GET(remy));
    GMLArray* facings = followerArray(GET(facing));
    int32_t target = RValue_toInt32(GET(target));
    if (target < 0 || target >= FOLLOWER_HISTORY) return false;

    bool moved = !followerEqual(GMLArray_get(xs, 0), leader->x, epsilon) ||
        !followerEqual(GMLArray_get(ys, 0), leader->y, epsilon);
    if (moved && (GMLArray_length1D(xs) < FOLLOWER_HISTORY ||
        GMLArray_length1D(ys) < FOLLOWER_HISTORY || GMLArray_length1D(facings) < FOLLOWER_HISTORY ||
        xs->refCount != 1 || ys->refCount != 1 || facings->refCount != 1)) return false;

    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
    GMLReal depth = 100000 - (((GMLReal)self->y * 10) + (height * 10));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal(depth), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal((GMLReal)self->depth + 5), -1);

    GMLReal oldX = self->x, oldY = self->y;
    SET(nowx, oldX);
    SET(nowy, oldY);
    SET(moved, moved);
    RValue globalFacing = Instance_getSelfVar(ctx->globalScopeInstance, cache.globalFacing);
    bool run = false;
    if (moved) {
        SET(blushtimer, 0);
        followerShift(xs, RValue_makeReal(leader->x), FOLLOWER_X_OWNER);
        followerShift(ys, RValue_makeReal(leader->y), FOLLOWER_Y_OWNER);
        followerShift(facings, globalFacing, FOLLOWER_FACING_OWNER);
        ctx->currentArrayOwner = (void*)(intptr_t)FOLLOWER_FACING_OWNER;
        SET(i, 0);

        bool susie = followerEqual(Instance_getSelfVar(self, cache.sprites[2]), cache.susieUp, epsilon);
        GMLReal x = RValue_toReal(GMLArray_get(xs, target));
        GMLReal y = RValue_toReal(GMLArray_get(ys, target));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(x - (susie ? 6 : 4)), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(y - (susie ? 16 : 12)), -1);

        GMLReal dx = RValue_toReal(GMLArray_get(xs, target + 1)) - x;
        GMLReal dy = RValue_toReal(GMLArray_get(ys, target + 1)) - y;
        run = NativeScript_compareReals(GMLReal_fabs(dx), 4, epsilon) > 0 ||
            NativeScript_compareReals(GMLReal_fabs(dy), 4, epsilon) > 0;
        Instance_setSelfVar(self, cache.dir, GMLArray_get(facings, target));
    }
    SET(runmove, run);

    bool walk = NativeScript_compareReals(self->x, oldX, epsilon) != 0 ||
        NativeScript_compareReals(self->y, oldY, epsilon) != 0;
    SET(walk, walk);
    GMLReal buffer = walk ? 6 : REAL(walkbuffer);
    bool animate = followerEqual(GET(fun), 0, epsilon);
    if (animate) followerAnimate(self, buffer, run, epsilon);
    SET(walkbuffer, buffer - 0.75);

    RValue downSprite = Instance_getSelfVar(self, cache.sprites[0]);
    if (animate) {
        RValue facing = GMLArray_get(facings, target);
        for (int32_t i = 0; i < 4; ++i) {
            if (followerEqual(facing, i, epsilon)) {
                RValue sprite = Instance_getSelfVar(self, cache.sprites[i]);
                VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", sprite, -1);
            }
        }
    }
    followerBlush(ctx, self, leader, globalFacing, downSprite, animate, epsilon);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeCaterpillarEndStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.leader = -1;
    cache.susieUp = -1;
    cache.ralseiDown = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && strcmp(object->name, "obj_mainchara") == 0) {
            cache.leader = (int32_t)i;
            break;
        }
    }
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (!sprite->present) continue;
        if (strcmp(sprite->name, "spr_susieu_dark") == 0) cache.susieUp = (int32_t)i;
        if (strcmp(sprite->name, "spr_ralseid") == 0) cache.ralseiDown = (int32_t)i;
    }
    if (cache.leader < 0 || cache.susieUp < 0 || cache.ralseiDown < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_caterpillarchara_Step_2", caterpillarEndStep)) return;

    cache.globalFacing = VM_getOrAllocateVarID(ctx, "facing");
    cache.interact = VM_getOrAllocateVarID(ctx, "interact");
#define RESOLVE(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    FOLLOWER_VARS(RESOLVE)
#undef RESOLVE
    static const char* const sprites[] = { "dsprite", "rsprite", "usprite", "lsprite" };
    static const char* const blushSprites[] = { "dsprite_blush", "rsprite_blush", "usprite_blush", "lsprite_blush" };
    for (int32_t i = 0; i < 4; ++i) {
        cache.sprites[i] = NativeScript_getSelfVarID(ctx, sprites[i]);
        cache.blushSprites[i] = NativeScript_getSelfVarID(ctx, blushSprites[i]);
    }
}
