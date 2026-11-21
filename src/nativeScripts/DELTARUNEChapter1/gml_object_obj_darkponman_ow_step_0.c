#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "math_compat.h"
#include "runner.h"
#include "stb_ds.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t rot, rotSpeed, xx, yy, eyeRadius, offx, offy;
    int32_t boss, battleMode, xrange, yrange, con, made, bmax;
    int32_t i, xa, ya, fb, siner, active, target, damage;
    int32_t mainchara, ponman, bulletObject, bulletSprite;
} cache;

static GMLReal ponmanLengthdir(GMLReal length, GMLReal direction, bool yAxis) {
    GMLReal radians = direction * (M_PI / 180.0);
    GMLReal result = length * (yAxis ? -GMLReal_sin(radians) : GMLReal_cos(radians));
    return GMLReal_fabs(result) < (GMLReal)1e-4 ? 0 : result;
}

static bool ponmanEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static bool darkPonmanStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal rot = RValue_toReal(Instance_getSelfVar(self, cache.rot));
    GMLReal rotSpeed = RValue_toReal(Instance_getSelfVar(self, cache.rotSpeed));
    GMLReal eyeRadius = RValue_toReal(Instance_getSelfVar(self, cache.eyeRadius));
    GMLReal offx = RValue_toReal(Instance_getSelfVar(self, cache.offx));
    GMLReal offy = RValue_toReal(Instance_getSelfVar(self, cache.offy));
    GMLReal con = RValue_toReal(Instance_getSelfVar(self, cache.con));
    RValue made = Instance_getSelfVar(self, cache.made);
    GMLReal bmax = RValue_toReal(Instance_getSelfVar(self, cache.bmax));

    if (ponmanEqual(made, 1, epsilon) && Instance_getSelfVar(self, cache.fb).type != RVALUE_ARRAY)
        return false;

    rot += rotSpeed;
    Instance_setSelfVar(self, cache.rot, RValue_makeReal(rot));
    Instance_setSelfVar(self, cache.xx, RValue_makeReal(ponmanLengthdir(eyeRadius, rot, false) + offx));
    Instance_setSelfVar(self, cache.yy, RValue_makeReal(ponmanLengthdir(eyeRadius, rot, true) + offy));

    if (ponmanEqual(Instance_getSelfVar(self, cache.boss), 1, epsilon)) {
        Instance* mainchara = VM_findInstanceByTarget(ctx, cache.mainchara);
        if (mainchara != nullptr) {
            Instance_setSelfVar(mainchara, cache.battleMode, RValue_makeReal(0));
            Instance** ponmen = runner->instancesByObject[cache.ponman];
            for (size_t i = 0; i < arrlenu(ponmen); ++i) {
                Instance* ponman = ponmen[i];
                if (!ponman->active) continue;
                GMLReal xrange = RValue_toReal(Instance_getSelfVar(ponman, cache.xrange));
                GMLReal yrange = RValue_toReal(Instance_getSelfVar(ponman, cache.yrange));
                if (mainchara->x > ponman->x - xrange && mainchara->x < ponman->x + xrange &&
                    mainchara->y > ponman->y - yrange && mainchara->y < ponman->y + yrange) {
                    Instance_setSelfVar(mainchara, cache.battleMode, RValue_makeReal(1));
                }
            }
        }
    }

    if (NativeScript_compareReals(con, 0, epsilon) == 0) {
        if (ponmanEqual(made, 0, epsilon)) {
            int32_t i = 0;
            for (; NativeScript_compareReals(i, bmax, epsilon) < 0; ++i) {
                Instance_setSelfVar(self, cache.i, RValue_makeReal(i));
                GMLReal length = eyeRadius * (2 + i * 2);
                GMLReal xa = ponmanLengthdir(length, rot - i * 2, false) + offx;
                GMLReal ya = ponmanLengthdir(length, rot - i * 2, true) + offy;
                Instance_setSelfVar(self, cache.xa, RValue_makeReal(xa));
                Instance_setSelfVar(self, cache.ya, RValue_makeReal(ya));

                Instance* bullet = Runner_createInstance(runner, (GMLReal)self->x + xa,
                    (GMLReal)self->y + ya, cache.bulletObject);
                if (bullet != nullptr && ctx->creatorVarID >= 0) {
                    Instance_setSelfVar(bullet, ctx->creatorVarID, RValue_makeReal(self->instanceId));
                }
                RValue* slot = IntRValueHashMap_getOrInsertUndefined(&self->selfVars, cache.fb);
                VM_setArrayElement(ctx, slot, i,
                    RValue_makeReal(bullet != nullptr ? (GMLReal)bullet->instanceId : INSTANCE_NOONE));
                if (bullet == nullptr) continue;

                VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_SPRITE_INDEX, "sprite_index",
                    RValue_makeReal(cache.bulletSprite), -1);
                Instance_setSelfVar(bullet, cache.active, RValue_makeReal(1));
                Instance_setSelfVar(bullet, cache.target, RValue_makeReal(3));
                Instance_setSelfVar(bullet, cache.damage, RValue_makeReal(16));
                VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
                    RValue_makeReal(rot - i), -1);
                VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal(200), -1);
                VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(-10), 0);
                bullet->imageAlpha = 0;
                VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
                    RValue_makeReal(0.7), -1);
            }
            Instance_setSelfVar(self, cache.i, RValue_makeReal(i));
            Instance_setSelfVar(self, cache.made, RValue_makeReal(1));
            made = RValue_makeReal(1);
        }

        if (NativeScript_compareReals(eyeRadius, 10, epsilon) <= 0) {
            eyeRadius += 1;
            Instance_setSelfVar(self, cache.eyeRadius, RValue_makeReal(eyeRadius));
        } else {
            con = 1;
            Instance_setSelfVar(self, cache.con, RValue_makeReal(con));
            self->imageSpeed = 0.1f;
        }
    }

    if (ponmanEqual(made, 1, epsilon)) {
        GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
        if (NativeScript_compareReals(rotSpeed, 0, epsilon) > 0) siner += 1;
        if (NativeScript_compareReals(rotSpeed, 0, epsilon) < 0) siner -= 1;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
        RValue fb = Instance_getSelfVar(self, cache.fb);
        GMLArray* bullets = fb.type == RVALUE_ARRAY ? fb.array : nullptr;
        GMLReal wave = GMLReal_sin(siner / 10);
        int32_t i = 0;
        for (; NativeScript_compareReals(i, bmax, epsilon) < 0; ++i) {
            Instance_setSelfVar(self, cache.i, RValue_makeReal(i));
            Instance* bullet = VM_findInstanceByTarget(ctx, RValue_toInt32(GMLArray_get(bullets, i)));
            if (bullet == nullptr || !bullet->active) continue;

            GMLReal length = eyeRadius * (2 + i * 2);
            GMLReal xa = ponmanLengthdir(length, rot - wave * (i * 2), false) + offx;
            GMLReal ya = ponmanLengthdir(length, rot - wave * (i * 2), true) + offy;
            Instance_setSelfVar(self, cache.xa, RValue_makeReal(xa));
            Instance_setSelfVar(self, cache.ya, RValue_makeReal(ya));
            VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_X, "x", RValue_makeReal((GMLReal)self->x + xa), -1);
            VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_Y, "y", RValue_makeReal((GMLReal)self->y + ya), -1);
            GMLReal alpha = 0.8 + GMLReal_sin(((i * 4) + siner) / 5) * 0.2;
            if (NativeScript_compareReals(con, 1, epsilon) < 0) alpha = eyeRadius / 10;
            bullet->imageAlpha = (float)alpha;
            VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
                RValue_makeReal(rot - wave * (i * 4)), -1);
        }
        Instance_setSelfVar(self, cache.i, RValue_makeReal(i));
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeDarkPonmanStep_register(VMContext* ctx) {
    cache.mainchara = -1;
    cache.ponman = -1;
    cache.bulletObject = -1;
    cache.bulletSprite = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_mainchara") == 0) cache.mainchara = (int32_t)i;
        if (strcmp(object->name, "obj_darkponman_ow") == 0) cache.ponman = (int32_t)i;
        if (strcmp(object->name, "obj_overworldbulletparent") == 0) cache.bulletObject = (int32_t)i;
    }
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "spr_diamondbullet") == 0) {
            cache.bulletSprite = (int32_t)i;
            break;
        }
    }
    if (cache.mainchara < 0 || cache.ponman < 0 || cache.bulletObject < 0 || cache.bulletSprite < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_darkponman_ow_Step_0", darkPonmanStep)) return;

#define RESOLVE(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    RESOLVE(rot)
    cache.rotSpeed = NativeScript_getSelfVarID(ctx, "rotspeed");
    RESOLVE(xx)
    RESOLVE(yy)
    cache.eyeRadius = NativeScript_getSelfVarID(ctx, "eyer");
    RESOLVE(offx)
    RESOLVE(offy)
    RESOLVE(boss)
    cache.battleMode = NativeScript_getSelfVarID(ctx, "battlemode");
    RESOLVE(xrange)
    RESOLVE(yrange)
    RESOLVE(con)
    RESOLVE(made)
    RESOLVE(bmax)
    RESOLVE(i)
    RESOLVE(xa)
    RESOLVE(ya)
    RESOLVE(fb)
    RESOLVE(siner)
    RESOLVE(active)
    RESOLVE(target)
    RESOLVE(damage)
#undef RESOLVE
}
