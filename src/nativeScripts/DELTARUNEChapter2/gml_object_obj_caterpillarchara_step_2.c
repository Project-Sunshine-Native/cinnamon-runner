// ch1 script with added logic for ch2
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
    SUSIE_DARK,
    SUSIE_DW,
    SUSIE_LW,
    NOELLE_DW,
    RALSEI_OLD,
    RALSEI_DW,
    FOLLOWER_ASSET_COUNT,
    FOLLOWER_HISTORY = 75,
};

#define FOLLOWER_VARS(X) \
    X(init) X(ignoredepth) X(name) X(nowx) X(nowy) X(moved) X(walk) X(runmove) \
    X(slided) X(follow) X(blushtimer) X(i) X(remx) X(remy) X(facing) X(sliding) \
    X(target) X(dir) X(fun) X(walkbuffer) X(walktimer) X(slidesprite) \
    X(distfrommcx) X(distfrommcy)

static struct {
#define DECLARE_ID(name) int32_t name;
    FOLLOWER_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t sprites[4], blushSprites[4];
    int32_t assets[FOLLOWER_ASSET_COUNT];
    int32_t leader, globalFacing, interact, darkzone;
} cache;

#define GET(name) Instance_getSelfVar(self, cache.name)
#define REAL(name) RValue_toReal(GET(name))
#define SET(name, value) Instance_setSelfVar(self, cache.name, RValue_makeReal(value))

static bool followerEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static GMLArray* followerArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static void followerShift(GMLArray* array, RValue first) {
    RValue value = RValue_makeIndependent(first);
    GMLArray_growTo(array, FOLLOWER_HISTORY + 1);
    RValue* slots = GMLArray_slot(array, 0);
    RValue_free(&slots[FOLLOWER_HISTORY]);
    memmove(slots + 1, slots, FOLLOWER_HISTORY * sizeof(*slots));
    slots[0] = value;
    array->owner = &cache;
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

static void followerBlush(VMContext* ctx, Instance* self, Instance* leader,
    RValue downSprite, bool animate, GMLReal epsilon) {
    if (!animate ||
        (!followerEqual(downSprite, cache.assets[RALSEI_OLD], epsilon) &&
            !followerEqual(downSprite, cache.assets[RALSEI_DW], epsilon)) ||
        !followerEqual(Instance_getSelfVar(ctx->globalScopeInstance, cache.interact), 0, epsilon)) {
        SET(blushtimer, 0);
        return;
    }

    GMLReal dx = (GMLReal)leader->x - ((GMLReal)self->x + 2);
    GMLReal dy = (GMLReal)leader->y - ((GMLReal)self->y + 12);
    SET(distfrommcx, dx);
    SET(distfrommcy, dy);
    GMLReal timer = REAL(blushtimer);
    if (NativeScript_compareReals(GMLReal_fabs(dy), 14, epsilon) <= 0 &&
        NativeScript_compareReals(GMLReal_fabs(dx), 20, epsilon) <= 0) {
        RValue facing = Instance_getSelfVar(ctx->globalScopeInstance, cache.globalFacing);
        if (NativeScript_compareReals(dx, 0, epsilon) <= 0 && followerEqual(facing, 1, epsilon))
            SET(blushtimer, ++timer);
        if (NativeScript_compareReals(dx, 0, epsilon) >= 0 && followerEqual(facing, 3, epsilon))
            SET(blushtimer, ++timer);
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

    GMLArray* xs = followerArray(GET(remx));
    GMLArray* ys = followerArray(GET(remy));
    GMLArray* facings = followerArray(GET(facing));
    GMLArray* sliding = followerArray(GET(sliding));
    int32_t target = RValue_toInt32(GET(target));
    RValue nameValue = GET(name);
    if (target < 0 || target >= FOLLOWER_HISTORY || nameValue.type != RVALUE_STRING ||
        GMLArray_length1D(facings) <= target || GMLArray_length1D(sliding) <= target)
        return false;

    GMLReal epsilon = ctx->runner->mathEpsilon;
    bool moved = !followerEqual(GMLArray_get(xs, 0), leader->x, epsilon) ||
        !followerEqual(GMLArray_get(ys, 0), leader->y, epsilon) ||
        followerEqual(GMLArray_get(sliding, target), 1, epsilon);
    bool follow = moved && followerEqual(GET(follow), 1, epsilon);
    if (follow && (GMLArray_length1D(xs) < FOLLOWER_HISTORY ||
        GMLArray_length1D(ys) < FOLLOWER_HISTORY ||
        GMLArray_length1D(facings) < FOLLOWER_HISTORY ||
        GMLArray_length1D(sliding) < FOLLOWER_HISTORY ||
        xs->refCount != 1 || ys->refCount != 1 ||
        facings->refCount != 1 || sliding->refCount != 1))
        return false;

    const char* name = nameValue.string != nullptr ? nameValue.string : "";
    bool ralsei = strcmp(name, "ralsei") == 0;
    if (!RValue_toBool(GET(init))) {
        SET(ignoredepth, 0);
        SET(init, 1);
    }
    if (!RValue_toBool(GET(ignoredepth))) {
        GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self,
            BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
        GMLReal depth = 100000 - (((GMLReal)self->y * 10) + (height * 10));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal(depth), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth",
            RValue_makeReal((GMLReal)self->depth + 5), -1);
        if (ralsei)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth",
                RValue_makeReal((GMLReal)self->depth - 80), -1);
        if (strcmp(name, "susie") == 0 &&
            followerEqual(Instance_getSelfVar(ctx->globalScopeInstance, cache.darkzone), 1, epsilon))
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth",
                RValue_makeReal((GMLReal)self->depth - 60), -1);
        if (strcmp(name, "noelle") == 0)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth",
                RValue_makeReal((GMLReal)self->depth - 5), -1);
    }

    GMLReal oldX = self->x, oldY = self->y;
    SET(nowx, oldX);
    SET(nowy, oldY);
    SET(moved, moved);
    bool run = false;
    bool slided = false;
    if (follow) {
        SET(blushtimer, 0);
        followerShift(xs, RValue_makeReal(leader->x));
        followerShift(ys, RValue_makeReal(leader->y));
        followerShift(facings, Instance_getSelfVar(ctx->globalScopeInstance, cache.globalFacing));
        followerShift(sliding, Instance_getSelfVar(leader, cache.sliding));
        ctx->currentArrayOwner = &cache;
        SET(i, 0);

        RValue upSprite = Instance_getSelfVar(self, cache.sprites[2]);
        GMLReal offsetX = 2, offsetY = 12;
        if (followerEqual(upSprite, cache.assets[SUSIE_DARK], epsilon) ||
            followerEqual(upSprite, cache.assets[SUSIE_DW], epsilon)) {
            offsetX = 6;
            offsetY = 16;
        } else if (followerEqual(upSprite, cache.assets[SUSIE_LW], epsilon)) {
            offsetX = 3;
            offsetY = 6;
        } else if (followerEqual(upSprite, cache.assets[NOELLE_DW], epsilon)) {
            offsetX = 4;
            offsetY = 20;
        }
        GMLReal x = RValue_toReal(GMLArray_get(xs, target));
        GMLReal y = RValue_toReal(GMLArray_get(ys, target));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(x - offsetX), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(y - offsetY), -1);
        slided = followerEqual(GMLArray_get(sliding, target), 1, epsilon);
        if (slided) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(x), -1);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(y), -1);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", GET(slidesprite), -1);
        }
        GMLReal dx = RValue_toReal(GMLArray_get(xs, target + 1)) - x;
        GMLReal dy = RValue_toReal(GMLArray_get(ys, target + 1)) - y;
        run = NativeScript_compareReals(GMLReal_fabs(dx), 4, epsilon) > 0 ||
            NativeScript_compareReals(GMLReal_fabs(dy), 4, epsilon) > 0;
        Instance_setSelfVar(self, cache.dir, GMLArray_get(facings, target));
    }
    SET(runmove, run);
    SET(slided, slided);

    bool walk = NativeScript_compareReals(self->x, oldX, epsilon) != 0 ||
        NativeScript_compareReals(self->y, oldY, epsilon) != 0;
    SET(walk, walk);
    GMLReal buffer = walk ? 6 : REAL(walkbuffer);
    bool animate = followerEqual(GET(fun), 0, epsilon);
    if (animate) followerAnimate(self, buffer, run, epsilon);
    SET(walkbuffer, buffer - 0.75);
    RValue downSprite = Instance_getSelfVar(self, cache.sprites[0]);
    if (animate && !slided) {
        RValue facing = GMLArray_get(facings, target);
        for (int32_t i = 0; i < 4; ++i) {
            if (followerEqual(facing, i, epsilon)) {
                RValue sprite = Instance_getSelfVar(self, cache.sprites[i]);
                VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", sprite, -1);
            }
        }
    }
    followerBlush(ctx, self, leader, downSprite, animate, epsilon);
    if (ralsei && followerEqual(downSprite, self->spriteIndex, epsilon))
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth",
            RValue_makeReal((GMLReal)self->depth + 20), -1);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2CaterpillarEndStep_register(VMContext* ctx) {
    if (!DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;
    cache.leader = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && strcmp(object->name, "obj_mainchara") == 0) {
            cache.leader = (int32_t)i;
            break;
        }
    }
    static const char* const assets[] = {
        "spr_susieu_dark", "spr_susie_walk_up_dw", "spr_susie_walk_up_lw",
        "spr_noelle_walk_up_dw", "spr_ralseid", "spr_ralsei_walk_down",
    };
    for (int32_t i = 0; i < FOLLOWER_ASSET_COUNT; ++i) {
        cache.assets[i] = -1;
        for (uint32_t j = 0; j < ctx->dataWin->sprt.count; ++j) {
            const Sprite* sprite = &ctx->dataWin->sprt.sprites[j];
            if (sprite->present && strcmp(sprite->name, assets[i]) == 0) {
                cache.assets[i] = (int32_t)j;
                break;
            }
        }
        if (cache.assets[i] < 0) return;
    }
    if (cache.leader < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_caterpillarchara_Step_2", caterpillarEndStep))
        return;

    cache.globalFacing = VM_getOrAllocateVarID(ctx, "facing");
    cache.interact = VM_getOrAllocateVarID(ctx, "interact");
    cache.darkzone = VM_getOrAllocateVarID(ctx, "darkzone");
#define RESOLVE(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    FOLLOWER_VARS(RESOLVE)
#undef RESOLVE
    static const char* const sprites[] = { "dsprite", "rsprite", "usprite", "lsprite" };
    static const char* const blushSprites[] = {
        "dsprite_blush", "rsprite_blush", "usprite_blush", "lsprite_blush",
    };
    for (int32_t i = 0; i < 4; ++i) {
        cache.sprites[i] = NativeScript_getSelfVarID(ctx, sprites[i]);
        cache.blushSprites[i] = NativeScript_getSelfVarID(ctx, blushSprites[i]);
    }
}
