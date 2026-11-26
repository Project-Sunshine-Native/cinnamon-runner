#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t parent, siner, timer, m, length, angle, speed, accel, angleChange;
    int32_t damage, grazePoints, timePoints, inv, target, grazed, grazeTimer, element;
    int32_t laser, laserVar, laserSprite, frontSprite, neoLaserSprite, neoFrontSprite, spamton;
    int32_t destroyOnHit, tri, triAngle, partCount;
    NativeCallBinding bulletInit;
} cache;

static bool werewireLaserCircleCreate(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    RValue result = NativeScript_call(ctx, &cache.bulletInit, "gml_Script_scr_bullet_init", nullptr, 0);
    RValue_free(&result);
    Instance_setSelfVar(self, cache.grazePoints, RValue_makeReal(3));
    Instance_setSelfVar(self, cache.destroyOnHit, RValue_makeReal(0));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(0), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(0), -1);
    Instance_setSelfVar(self, cache.timer, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.tri, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.triAngle, RValue_makeReal(20));
    Instance_setSelfVar(self, cache.angle, RValue_makeReal(180));
    Instance_setSelfVar(self, cache.speed, RValue_makeReal(4));
    Instance_setSelfVar(self, cache.accel, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.length, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.angleChange, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.partCount, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.m, RValue_makeReal(2));
    Instance_setSelfVar(self, cache.parent, RValue_makeReal(-1));
    *outResult = RValue_makeUndefined();
    return true;
}

static GMLReal circleReal(Instance* self, int32_t id) {
    return RValue_toReal(Instance_getSelfVar(self, id));
}

static void circleSet(Instance* self, int32_t id, GMLReal value) {
    Instance_setSelfVar(self, id, RValue_makeReal(value));
}

static void circleCopyBulletVar(Instance* source, Instance* bullet, int32_t id) {
    RValue value = Instance_getSelfVar(source, id);
    if (RValue_toReal(value) != -1)
        Instance_setSelfVar(bullet, id, value);
}

static void circleInheritBullet(Instance* source, Instance* bullet) {
    circleCopyBulletVar(source, bullet, cache.damage);
    circleCopyBulletVar(source, bullet, cache.grazePoints);
    circleCopyBulletVar(source, bullet, cache.timePoints);
    circleCopyBulletVar(source, bullet, cache.inv);
    circleCopyBulletVar(source, bullet, cache.target);
    if (circleReal(source, cache.grazed) != -1)
        circleSet(bullet, cache.grazed, 0);
    if (circleReal(source, cache.grazeTimer) != -1)
        circleSet(bullet, cache.grazeTimer, 0);
    Instance_setSelfVar(bullet, cache.element, Instance_getSelfVar(source, cache.element));
}

static bool werewireLaserCircleStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal parent = circleReal(self, cache.parent);
    if (NativeScript_compareReals(parent, -1, epsilon) != 0) {
        Instance* owner = VM_findInstanceByTarget(ctx, (int32_t)parent);
        if (owner != nullptr && owner->active) {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
                RValue_makeReal((GMLReal)owner->x - 32), -1);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
                RValue_makeReal(owner->y), -1);
        }
    }

    GMLReal siner = circleReal(self, cache.siner) + (GMLReal)0.5;
    GMLReal timer = circleReal(self, cache.timer) + 1;
    GMLReal m = circleReal(self, cache.m);
    circleSet(self, cache.siner, siner);
    circleSet(self, cache.timer, timer);

    if (NativeScript_compareReals(timer, 6 / m, epsilon) <= 0) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            RValue_makeReal((GMLReal)self->imageXscale + (0.4 * m)), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            RValue_makeReal((GMLReal)self->imageYscale + (0.4 * m)), -1);
    }

    GMLReal length = circleReal(self, cache.length);
    if (NativeScript_compareReals(timer, 6 / m, epsilon) > 0 &&
        NativeScript_compareReals(timer, length + 3, epsilon) <= 0) {
        Instance* laser = Runner_createInstance(runner, self->x, self->y, cache.laser);
        Instance_setSelfVar(self, cache.laserVar,
            RValue_makeReal(laser != nullptr ? (GMLReal)laser->instanceId : INSTANCE_NOONE));
        if (laser != nullptr) {
            if (ctx->creatorVarID >= 0)
                Instance_setSelfVar(laser, ctx->creatorVarID, RValue_makeReal(self->instanceId));
            circleInheritBullet(self, laser);
        }

        GMLReal scale = GMLReal_sin(siner * (GMLReal)1.3 * m) * (GMLReal)0.2;
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            RValue_makeReal((GMLReal)self->imageXscale - scale), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            RValue_makeReal((GMLReal)self->imageYscale - scale), -1);

        if (laser != nullptr) {
            GMLReal angle = circleReal(self, cache.angle);
            VMBuiltins_setVariable(ctx, laser, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
                RValue_makeReal(angle), -1);
            laser->speed = (float)circleReal(self, cache.speed);
            VMBuiltins_setVariable(ctx, laser, BUILTIN_VAR_DIRECTION, "direction",
                RValue_makeReal(angle), -1);
            laser->friction = (float)-circleReal(self, cache.accel);
            VMBuiltins_setVariable(ctx, laser, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
                RValue_makeReal(1), -1);
            circleSet(laser, cache.angleChange, circleReal(self, cache.angleChange));

            bool neo = VM_findInstanceByTarget(ctx, cache.spamton) != nullptr;
            int32_t sprite = neo ? cache.neoLaserSprite : cache.laserSprite;
            if (NativeScript_compareReals(timer, 3, epsilon) == 0 ||
                NativeScript_compareReals(timer, length + 3, epsilon) == 0) {
                sprite = neo ? cache.neoFrontSprite : cache.frontSprite;
            }
            VMBuiltins_setVariable(ctx, laser, BUILTIN_VAR_SPRITE_INDEX, "sprite_index",
                RValue_makeInt32(sprite), -1);
            if (NativeScript_compareReals(timer, length + 3, epsilon) == 0)
                laser->imageIndex = 1;
        }
    } else {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            RValue_makeReal((GMLReal)self->imageXscale - (0.1 * m)), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            RValue_makeReal((GMLReal)self->imageYscale - (0.1 * m)), -1);
        if (NativeScript_compareReals(self->imageXscale, 0, epsilon) <= 0 &&
            NativeScript_compareReals(self->imageYscale, 0, epsilon) <= 0)
            Runner_destroyInstance(runner, self, true);
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2WerewireLaserCircleStep_register(VMContext* ctx) {
    const Objt* objects = &ctx->dataWin->objt;
    cache.laser = -1;
    cache.spamton = -1;
    for (uint32_t i = 0; i < objects->count; ++i) {
        const GameObject* object = &objects->objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_werewire_bullet_lasersquare") == 0)
            cache.laser = (int32_t)i;
        else if (strcmp(object->name, "obj_spamton_neo_enemy") == 0)
            cache.spamton = (int32_t)i;
    }

    cache.laserSprite = -1;
    cache.frontSprite = -1;
    cache.neoLaserSprite = -1;
    cache.neoFrontSprite = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (!sprite->present) continue;
        if (strcmp(sprite->name, "spr_bullet_laser_square") == 0)
            cache.laserSprite = (int32_t)i;
        else if (strcmp(sprite->name, "spr_bullet_laser_front") == 0)
            cache.frontSprite = (int32_t)i;
        else if (strcmp(sprite->name, "spr_bullet_laser_square_sneo") == 0)
            cache.neoLaserSprite = (int32_t)i;
        else if (strcmp(sprite->name, "spr_bullet_laser_front_sneo") == 0)
            cache.neoFrontSprite = (int32_t)i;
    }

    if (cache.laser < 0 || cache.spamton < 0 || cache.laserSprite < 0 ||
        cache.frontSprite < 0 || cache.neoLaserSprite < 0 || cache.neoFrontSprite < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_werewire_bullet_lasercircle_Step_0",
            werewireLaserCircleStep)) return;

    cache.parent = NativeScript_getSelfVarID(ctx, "parent");
    cache.laserVar = NativeScript_getSelfVarID(ctx, "laser");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.m = NativeScript_getSelfVarID(ctx, "m");
    cache.length = NativeScript_getSelfVarID(ctx, "my_length");
    cache.angle = NativeScript_getSelfVarID(ctx, "my_angle");
    cache.speed = NativeScript_getSelfVarID(ctx, "my_speed");
    cache.accel = NativeScript_getSelfVarID(ctx, "my_accel");
    cache.angleChange = NativeScript_getSelfVarID(ctx, "my_angle_change");
    cache.damage = NativeScript_getSelfVarID(ctx, "damage");
    cache.grazePoints = NativeScript_getSelfVarID(ctx, "grazepoints");
    cache.timePoints = NativeScript_getSelfVarID(ctx, "timepoints");
    cache.inv = NativeScript_getSelfVarID(ctx, "inv");
    cache.target = NativeScript_getSelfVarID(ctx, "target");
    cache.grazed = NativeScript_getSelfVarID(ctx, "grazed");
    cache.grazeTimer = NativeScript_getSelfVarID(ctx, "grazetimer");
    cache.element = NativeScript_getSelfVarID(ctx, "element");
    cache.destroyOnHit = NativeScript_getSelfVarID(ctx, "destroyonhit");
    cache.tri = NativeScript_getSelfVarID(ctx, "tri");
    cache.triAngle = NativeScript_getSelfVarID(ctx, "tri_angle");
    cache.partCount = NativeScript_getSelfVarID(ctx, "part_count");
    cache.bulletInit = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (NativeScript_canCall(ctx, &cache.bulletInit, "gml_Script_scr_bullet_init"))
        VM_registerNativeScript(ctx, "gml_Object_obj_werewire_bullet_lasercircle_Create_0", werewireLaserCircleCreate);
}
