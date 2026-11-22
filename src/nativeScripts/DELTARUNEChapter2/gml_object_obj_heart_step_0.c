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
    X(boundaryup) X(dmgnoise) X(canmove) X(color) X(remove_slow_z_buffer) \
    X(chargeshot_delay) X(z_hold) X(z_hold_f) X(z_charge) X(chargeshot_sound) \
    X(chargeshotcount) X(bigshot) X(part) X(f)

#define HEART_GLOBAL_VARS(X) X(input_held) X(input_released) X(flag) X(inv) X(heartx) X(hearty) X(chapter)

static struct {
#define DECLARE_ID(name) int32_t name;
    HEART_SELF_VARS(DECLARE_ID)
    HEART_GLOBAL_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t wallObject, hurtSound, shotObject, enemyObject, superchargeEnd, particleObject;
    int32_t bigshotSprite, shotSound, chargeSound, chargeFireSound, starSound;
    int32_t enemyBigshotCount, enemyBigshotUsed;
    int32_t big, trail;
    NativeCallBinding soundStop, soundPlay, soundLoop, soundPitch, soundVolume;
    NativeCallBinding buttonPressed, random;
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

static bool heartInput(GMLArray* input, int32_t first, int32_t second) {
    return RValue_toBool(GMLArray_get(input, first)) || RValue_toBool(GMLArray_get(input, second));
}

static GMLReal heartRandom(VMContext* ctx, GMLReal maximum) {
    RValue arg = RValue_makeReal(maximum);
    RValue result = NativeScript_call(ctx, &cache.random, "random", &arg, 1);
    GMLReal value = RValue_toReal(result);
    RValue_free(&result);
    return value;
}

static void heartSound(VMContext* ctx, NativeCallBinding* binding, const char* script,
    RValue* args, int32_t count) {
    RValue result = NativeScript_call(ctx, binding, script, args, count);
    RValue_free(&result);
}

static Instance* heartFirstEnemy(Runner* runner) {
    Instance** instances = runner->instancesByObject[cache.enemyObject];
    for (size_t i = 0; i < arrlenu(instances); ++i) {
        if (instances[i]->active) return instances[i];
    }
    return nullptr;
}

static Instance* heartCreateInstance(VMContext* ctx, Instance* creator,
    int32_t objectIndex, GMLReal x, GMLReal y) {
    Instance* instance = Runner_createInstance(ctx->runner, x, y, objectIndex);
    if (instance != nullptr && ctx->creatorVarID >= 0)
        Instance_setSelfVar(instance, ctx->creatorVarID, RValue_makeReal(creator->instanceId));
    return instance;
}

static void heartCreateParticles(VMContext* ctx, Instance* self, GMLReal speed, bool large) {
    GMLReal angle = heartRandom(ctx, 45);
    for (int32_t i = 0; i < 8; ++i) {
        Instance* particle = heartCreateInstance(ctx, self, cache.particleObject, self->x + 10, self->y + 10);
        if (particle != nullptr) {
            HEART_SET(self, part, particle->instanceId);
            GMLReal direction = angle + heartRandom(ctx, 15);
            VMBuiltins_setVariable(ctx, particle, BUILTIN_VAR_DIRECTION, "direction",
                RValue_makeReal(direction), -1);
            if (large) {
                VMBuiltins_setVariable(ctx, particle, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
                    RValue_makeReal(2 + heartRandom(ctx, 2)), -1);
                VMBuiltins_setVariable(ctx, particle, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
                    RValue_makeReal(2 + heartRandom(ctx, 2)), -1);
            }
            VMBuiltins_setVariable(ctx, particle, BUILTIN_VAR_SPEED, "speed", RValue_makeReal(speed), -1);
            particle->friction = 0.8f;
        }
        angle += 45;
    }
}

static void heartFireChargedShot(VMContext* ctx, Instance* self) {
    Runner* runner = ctx->runner;
    RValue chargeSound = HEART_GET(self, chargeshot_sound);
    heartSound(ctx, &cache.soundStop, "gml_Script_snd_stop", &chargeSound, 1);
    RValue fireSound = RValue_makeInt32(cache.chargeFireSound);
    heartSound(ctx, &cache.soundPlay, "gml_Script_snd_play", &fireSound, 1);

    Instance* shot = heartCreateInstance(ctx, self, cache.shotObject, self->x + 10, self->y + 10);
    if (shot != nullptr) {
        HEART_SET(self, bigshot, shot->instanceId);
        HEART_SET(shot, big, 1);
        VMBuiltins_setVariable(ctx, shot, BUILTIN_VAR_SPRITE_INDEX, "sprite_index",
            RValue_makeInt32(cache.bigshotSprite), -1);
        GMLReal velocity = 4 * HEART_REAL(self, f);
        VMBuiltins_setVariable(ctx, shot, BUILTIN_VAR_HSPEED, "hspeed", RValue_makeReal(velocity), -1);
        shot->friction = (float)(-0.2 * HEART_REAL(self, f));
        shot->imageAlpha = 0.5f;
        VMBuiltins_setVariable(ctx, shot, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(0.1), -1);
        VMBuiltins_setVariable(ctx, shot, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(2), -1);
    }

    HEART_SET(self, z_hold, 0);
    HEART_SET(self, chargeshot_delay, 5);
    self->imageIndex = 0;
    GMLReal count = HEART_REAL(self, chargeshotcount) + 1;
    HEART_SET(self, chargeshotcount, count);
    if (count > 1) {
        int32_t base = Runner_pushInstancesOfObject(runner, cache.enemyObject);
        int32_t end = (int32_t)arrlen(runner->instanceSnapshots);
        for (int32_t i = base; i < end; ++i) {
            Instance* enemy = runner->instanceSnapshots[i];
            if (enemy->active)
                Runner_executeEventFromObject(runner, enemy, self, enemy->objectIndex, EVENT_OTHER, OTHER_USER0 + 4);
        }
        Runner_popInstanceSnapshot(runner, base);
    }

    if (HEART_REAL(ctx->globalScopeInstance, chapter) != 2) return;
    Instance* enemy = heartFirstEnemy(runner);
    if (enemy == nullptr) return;
    Instance_setSelfVar(enemy, cache.enemyBigshotUsed, RValue_makeReal(1));
    GMLReal remaining = RValue_toReal(Instance_getSelfVar(enemy, cache.enemyBigshotCount));
    if (remaining <= 0 || shot == nullptr) return;

    VMBuiltins_setVariable(ctx, shot, BUILTIN_VAR_HSPEED, "hspeed",
        RValue_makeReal(10 * HEART_REAL(self, f)), -1);
    HEART_SET(shot, trail, 1);
    remaining -= 1;
    Instance_setSelfVar(enemy, cache.enemyBigshotCount, RValue_makeReal(remaining));
    if (remaining == 0) {
        heartCreateInstance(ctx, self, cache.superchargeEnd, self->x, self->y);
        RValue sound = RValue_makeInt32(cache.starSound);
        heartSound(ctx, &cache.soundPlay, "gml_Script_snd_play", &sound, 1);
        heartCreateParticles(ctx, self, 16, true);
    } else {
        heartCreateParticles(ctx, self, 8, false);
    }
}

static void heartYellowSoul(VMContext* ctx, Instance* self, GMLArray* held, GMLArray* released) {
    Instance* global = ctx->globalScopeInstance;
    HEART_SET(self, wspeed, 5);
    GMLReal delay = HEART_REAL(self, chargeshot_delay);
    if (delay > 0) HEART_SET(self, chargeshot_delay, delay - 1);

    bool release = heartInput(released, 4, 7);
    GMLReal hold = HEART_REAL(self, z_hold);
    RValue pressed = NativeScript_call(ctx, &cache.buttonPressed, "gml_Script_button1_p", nullptr, 0);
    bool fire = RValue_toBool(pressed);
    RValue_free(&pressed);
    if (fire || (hold >= 10 && hold <= 39 && release)) {
        Instance** shots = ctx->runner->instancesByObject[cache.shotObject];
        int32_t activeShots = 0;
        for (size_t i = 0; i < arrlenu(shots); ++i) {
            if (shots[i]->active) ++activeShots;
        }
        if (activeShots < 3 && HEART_REAL(self, chargeshot_delay) == 0) {
            heartCreateInstance(ctx, self, cache.shotObject, self->x + 10, self->y + 10);
            RValue sound = RValue_makeInt32(cache.shotSound);
            heartSound(ctx, &cache.soundPlay, "gml_Script_snd_play", &sound, 1);
        }
    }

    RValue chargeSound = HEART_GET(self, chargeshot_sound);
    if (hold == 20) {
        RValue sound = RValue_makeInt32(cache.chargeSound);
        RValue result = NativeScript_call(ctx, &cache.soundLoop, "gml_Script_snd_loop", &sound, 1);
        Instance_setSelfVar(self, cache.chargeshot_sound, result);
        RValue_free(&result);
        chargeSound = HEART_GET(self, chargeshot_sound);
        RValue pitch[2] = {chargeSound, RValue_makeReal(0.1)};
        heartSound(ctx, &cache.soundPitch, "gml_Script_snd_pitch", pitch, 2);
        RValue volume[3] = {chargeSound, RValue_makeReal(0), RValue_makeReal(0)};
        heartSound(ctx, &cache.soundVolume, "gml_Script_snd_volume", volume, 3);
        volume[1] = RValue_makeReal(0.3);
        volume[2] = RValue_makeReal(20);
        heartSound(ctx, &cache.soundVolume, "gml_Script_snd_volume", volume, 3);
    }
    if (hold >= 20 && hold < 40) {
        RValue pitch[2] = {chargeSound, RValue_makeReal(0.1 + (hold - 20) / 20)};
        heartSound(ctx, &cache.soundPitch, "gml_Script_snd_pitch", pitch, 2);
    }
    if (hold >= 40) self->imageIndex = 2;
    if (HEART_REAL(self, z_charge) < 15) HEART_SET(self, chargeshotcount, 0);
    if (hold >= 40 && release) heartFireChargedShot(ctx, self);

    GMLReal holdFrames = HEART_REAL(self, z_hold_f);
    if (heartInput(held, 4, 7)) {
        Instance* enemy = nullptr;
        if (HEART_REAL(global, chapter) == 2)
            enemy = heartFirstEnemy(ctx->runner);
        if (enemy != nullptr && RValue_toReal(Instance_getSelfVar(enemy, cache.enemyBigshotCount)) > 0)
            holdFrames += 1;
        holdFrames += 1;
    } else {
        holdFrames = 0;
        RValue sound = HEART_GET(self, chargeshot_sound);
        heartSound(ctx, &cache.soundStop, "gml_Script_snd_stop", &sound, 1);
    }
    HEART_SET(self, z_hold_f, holdFrames);
    HEART_SET(self, z_hold, holdFrames * 2);
}

static bool heartStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    Instance* global = ctx->globalScopeInstance;
    Runner* runner = ctx->runner;
    RValue inputHeld = HEART_GET(global, input_held);
    GMLArray* held = inputHeld.type == RVALUE_ARRAY ? inputHeld.array : nullptr;
    HeartMove move = {
        .ctx = ctx, .self = self, .speed = HEART_REAL(self, wspeed), .epsilon = runner->mathEpsilon,
        .left = RValue_toBool(GMLArray_get(held, 3)), .right = RValue_toBool(GMLArray_get(held, 1)),
        .up = RValue_toBool(GMLArray_get(held, 2)), .down = RValue_toBool(GMLArray_get(held, 0)),
    };

    HEART_SET(self, wallcheck, 0);
    HEART_SET(self, jelly, 2);
    HEART_SET(self, press_l, move.left);
    HEART_SET(self, press_r, move.right);
    HEART_SET(self, press_u, move.up);
    HEART_SET(self, press_d, move.down);
    HEART_SET(self, bkx, 0);
    HEART_SET(self, bky, 0);
    HEART_SET(self, bkxy, 0);

    move.px = 0;
    move.py = 0;
    if (HEART_REAL(self, canmove) != 0) {
        move.px = move.left ? -move.speed : move.right ? move.speed : 0;
        move.py = move.up ? -move.speed : move.down ? move.speed : 0;
        RValue flags = HEART_GET(global, flag);
        GMLArray* flag = flags.type == RVALUE_ARRAY ? flags.array : nullptr;
        bool slowdown = heartInput(held, 5, 8) && RValue_toReal(GMLArray_get(flag, 22)) == 0;
        if (slowdown) {
            if (HEART_REAL(self, disableslow) == 0) {
                move.px = GMLReal_ceil(move.px * (GMLReal)0.5);
                move.py = GMLReal_ceil(move.py * (GMLReal)0.5);
            }
        } else {
            HEART_SET(self, disableslow, 0);
        }
    }
    if (HEART_REAL(self, color) == 0)
        HEART_SET(self, remove_slow_z_buffer, HEART_REAL(self, remove_slow_z_buffer) + (GMLReal)0.5);

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
        heartSound(ctx, &cache.soundStop, "gml_Script_snd_stop", &sound, 1);
        heartSound(ctx, &cache.soundPlay, "gml_Script_snd_play", &sound, 1);
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

    if (HEART_REAL(self, color) == 1) {
        RValue released = HEART_GET(global, input_released);
        heartYellowSoul(ctx, self, held, released.type == RVALUE_ARRAY ? released.array : nullptr);
    }
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2HeartStep_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 2") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.wallObject = -1;
    cache.hurtSound = -1;
    cache.shotObject = -1;
    cache.enemyObject = -1;
    cache.superchargeEnd = -1;
    cache.particleObject = -1;
    cache.bigshotSprite = -1;
    cache.shotSound = -1;
    cache.chargeSound = -1;
    cache.chargeFireSound = -1;
    cache.starSound = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_battlesolid") == 0) cache.wallObject = (int32_t)i;
        else if (strcmp(object->name, "obj_yheart_shot") == 0) cache.shotObject = (int32_t)i;
        else if (strcmp(object->name, "obj_spamton_neo_enemy") == 0) cache.enemyObject = (int32_t)i;
        else if (strcmp(object->name, "obj_supercharge_end") == 0) cache.superchargeEnd = (int32_t)i;
        else if (strcmp(object->name, "obj_yheart_sneo_particle") == 0) cache.particleObject = (int32_t)i;
    }
    for (uint32_t i = 0; i < ctx->dataWin->sond.count; ++i) {
        const Sound* sound = &ctx->dataWin->sond.sounds[i];
        if (sound->name == nullptr) continue;
        if (strcmp(sound->name, "snd_hurt1") == 0) cache.hurtSound = (int32_t)i;
        else if (strcmp(sound->name, "snd_heartshot_dr_b") == 0) cache.shotSound = (int32_t)i;
        else if (strcmp(sound->name, "snd_chargeshot_charge") == 0) cache.chargeSound = (int32_t)i;
        else if (strcmp(sound->name, "snd_chargeshot_fire") == 0) cache.chargeFireSound = (int32_t)i;
        else if (strcmp(sound->name, "snd_stardrop") == 0) cache.starSound = (int32_t)i;
    }

    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "spr_yheart_bigshot") == 0) {
            cache.bigshotSprite = (int32_t)i;
            break;
        }
    }
    cache.soundStop = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundPlay = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundLoop = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundPitch = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundVolume = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.buttonPressed = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.random = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.wallObject < 0 || cache.hurtSound < 0 || cache.shotObject < 0 ||
        cache.enemyObject < 0 || cache.superchargeEnd < 0 || cache.particleObject < 0 ||
        cache.bigshotSprite < 0 || cache.shotSound < 0 || cache.chargeSound < 0 ||
        cache.chargeFireSound < 0 || cache.starSound < 0 ||
        !NativeScript_canCall(ctx, &cache.soundStop, "gml_Script_snd_stop") ||
        !NativeScript_canCall(ctx, &cache.soundPlay, "gml_Script_snd_play") ||
        !NativeScript_canCall(ctx, &cache.soundLoop, "gml_Script_snd_loop") ||
        !NativeScript_canCall(ctx, &cache.soundPitch, "gml_Script_snd_pitch") ||
        !NativeScript_canCall(ctx, &cache.soundVolume, "gml_Script_snd_volume") ||
        !NativeScript_canCall(ctx, &cache.buttonPressed, "gml_Script_button1_p") ||
        !NativeScript_canCall(ctx, &cache.random, "random") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_heart_Step_0", heartStep)) return;

    cache.big = NativeScript_getSelfVarID(ctx, "big");
    cache.trail = NativeScript_getSelfVarID(ctx, "trail");
    cache.enemyBigshotCount = NativeScript_getSelfVarID(ctx, "bigshotcount");
    cache.enemyBigshotUsed = NativeScript_getSelfVarID(ctx, "bigshotused");

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
#define RESOLVE_GLOBAL(name) cache.name = VM_getOrAllocateVarID(ctx, #name);
    HEART_SELF_VARS(RESOLVE_SELF)
    HEART_GLOBAL_VARS(RESOLVE_GLOBAL)
#undef RESOLVE_GLOBAL
#undef RESOLVE_SELF
}
