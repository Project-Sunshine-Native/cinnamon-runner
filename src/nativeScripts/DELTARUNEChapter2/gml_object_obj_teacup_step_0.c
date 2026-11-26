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

#define CUP_STEP_VARS(X) \
    X(state) X(reverse) X(camTargetHeight) X(camTargetHeightReverse) X(camTargetHeightNormal) \
    X(riseSpeedBoost) X(riseSpeedBoostMax) X(riseSpeed) X(riseSpeedReverse) X(tutorialRide) \
    X(uniqueRideExperience) X(held) X(heldThisRide) X(holdHeight) X(endY) X(mode) X(spinSpeed) \
    X(spin) X(camFollow) X(camFollowAfter) X(shaketimer) X(riseSpeedBoostFriction) X(crusher) \
    X(cupDistanceFromCenter) X(depthSeparation) X(cupx) X(cupy) X(teaFilled) X(partySize) \
    X(cup_char) X(cupCharXnudge) X(cupCharYnudge) X(fadeOutOthers) X(scoreboardExists) \
    X(scoreboard) X(theScore) X(scoreboardGoal) X(scoreCol) X(idletimer) X(drawtutorialarrows) \
    X(wooshnoise) X(bouncenoise) X(swallownoise)

enum {
    CUP_FLAG_OWNER = 0x28f60,
};

static struct {
#define DECLARE_ID(name) int32_t name;
    CUP_STEP_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t party, inputHeld, flag, mazeRoom, swallow, jump;
    int32_t teaSprites[4], fullTeaSprites[4];
    NativeCallBinding playPitch, volume, isPlaying, play;
} cache;

#define CUP_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define CUP_REAL(inst, name) RValue_toReal(CUP_GET(inst, name))
#define CUP_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static GMLArray* teacupArray(RValue value) {
    return value.type == RVALUE_ARRAY ? value.array : nullptr;
}

static GMLReal teacupApproach(GMLReal value, GMLReal target, GMLReal amount, GMLReal epsilon) {
    if (NativeScript_compareReals(value, target, epsilon) < 0) {
        value += amount;
        return NativeScript_compareReals(value, target, epsilon) > 0 ? target : value;
    }
    value -= amount;
    return NativeScript_compareReals(value, target, epsilon) < 0 ? target : value;
}

static GMLReal teacupWrap(GMLReal value, GMLReal epsilon) {
    while (NativeScript_compareReals(value, 0, epsilon) < 0) value += 360;
    while (NativeScript_compareReals(value, 360, epsilon) > 0) value -= 360;
    return value;
}

static void teacupSounds(VMContext* ctx, Instance* self) {
    if (NativeScript_compareReals(CUP_REAL(self, wooshnoise), 1, ctx->runner->mathEpsilon) == 0) {
        RValue args[] = { RValue_makeInt32(cache.swallow), RValue_makeReal(0.6) };
        RValue sound = NativeScript_call(ctx, &cache.playPitch, "gml_Script_snd_play_pitch", args, 2);
        RValue volume[] = { sound, RValue_makeReal(0.1), RValue_makeReal(0) };
        RValue result = NativeScript_call(ctx, &cache.volume, "gml_Script_snd_volume", volume, 3);
        RValue_free(&result);
        volume[1] = RValue_makeReal(1);
        volume[2] = RValue_makeReal(1);
        result = NativeScript_call(ctx, &cache.volume, "gml_Script_snd_volume", volume, 3);
        RValue_free(&result);
        RValue_free(&sound);
        CUP_SET(self, wooshnoise, 0);
    }
    if (RValue_toBool(CUP_GET(self, bouncenoise))) {
        RValue sound = RValue_makeInt32(cache.jump);
        RValue playing = NativeScript_call(ctx, &cache.isPlaying, "gml_Script_snd_is_playing", &sound, 1);
        bool active = RValue_toBool(playing);
        RValue_free(&playing);
        if (!active) {
            RValue args[] = { sound, RValue_makeReal(1.5) };
            RValue result = NativeScript_call(ctx, &cache.playPitch, "gml_Script_snd_play_pitch", args, 2);
            RValue_free(&result);
        }
        CUP_SET(self, bouncenoise, 0);
    }
    if (RValue_toBool(CUP_GET(self, swallownoise))) {
        RValue sound = RValue_makeInt32(cache.swallow);
        RValue result = NativeScript_call(ctx, &cache.play, "gml_Script_snd_play", &sound, 1);
        RValue_free(&result);
        CUP_SET(self, swallownoise, 0);
    }
}

static bool teacupStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    GMLReal state = CUP_REAL(self, state);
    bool riding = NativeScript_compareReals(state, 5, epsilon) == 0;
    if (!riding && NativeScript_compareReals(state, 1, epsilon) != 0 &&
        NativeScript_compareReals(state, 12, epsilon) != 0 &&
        NativeScript_compareReals(state, 999, epsilon) != 0) return false;
    if (NativeScript_compareReals(CUP_REAL(self, crusher), INSTANCE_NOONE, epsilon) != 0) return false;
    bool reverse = RValue_toBool(CUP_GET(self, reverse));
    if (riding && (RValue_toBool(CUP_GET(self, held)) ||
        NativeScript_compareReals(CUP_REAL(self, uniqueRideExperience), 0, epsilon) != 0)) return false;

    GMLReal boost = GMLReal_fmin(CUP_REAL(self, riseSpeedBoost), CUP_REAL(self, riseSpeedBoostMax));
    GMLReal speed = CUP_REAL(self, riseSpeed);
    RValue reverseSpeed = CUP_GET(self, riseSpeedReverse);
    if (reverse && reverseSpeed.type != RVALUE_UNDEFINED) speed = RValue_toReal(reverseSpeed);
    GMLReal finalSpeed = RValue_toBool(CUP_GET(self, tutorialRide)) ? boost : speed + boost;
    GMLReal destination = reverse ? (GMLReal)self->ystart : CUP_REAL(self, endY);
    float nextY = self->y;
    GMLReal nextSpin = CUP_REAL(self, spin);
    if (riding) {
        GMLReal moveDirection = reverse ? 1 : -1;
        if (!RValue_toBool(CUP_GET(self, heldThisRide)) && !reverse &&
            NativeScript_compareReals(CUP_REAL(self, holdHeight), -1, epsilon) > 0 &&
            NativeScript_compareReals(self->y + finalSpeed * moveDirection, CUP_REAL(self, holdHeight), epsilon) < 0)
            return false;
        GMLArray* held = teacupArray(Instance_getSelfVar(ctx->globalScopeInstance, cache.inputHeld));
        int32_t spinDirection = (RValue_toBool(GMLArray_get(held, 0)) || RValue_toBool(GMLArray_get(held, 3))) -
            (RValue_toBool(GMLArray_get(held, 2)) || RValue_toBool(GMLArray_get(held, 1)));
        GMLReal mode = CUP_REAL(self, mode), spinSpeed = CUP_REAL(self, spinSpeed);
        if (NativeScript_compareReals(mode, 0, epsilon) == 0) {
            nextSpin -= spinSpeed;
            nextY = (float)(self->y + spinDirection * finalSpeed);
        }
        if (NativeScript_compareReals(mode, 1, epsilon) == 0) {
            nextSpin += spinSpeed * spinDirection;
            nextY = (float)teacupApproach(self->y, destination, finalSpeed, epsilon);
        }
        if (NativeScript_compareReals(mode, 2, epsilon) == 0) {
            nextSpin += spinSpeed * moveDirection;
            nextY = (float)teacupApproach(self->y, destination, finalSpeed, epsilon);
        }
        if (NativeScript_compareReals(nextY, destination, epsilon) == 0) return false;
        nextSpin = teacupWrap(nextSpin, epsilon);
    }
    GMLArray* cupArray = teacupArray(CUP_GET(self, cup_char));
    GMLArray* party = teacupArray(Instance_getSelfVar(ctx->globalScopeInstance, cache.party));
    Instance* cups[3];
    int32_t characters[3];
    for (int32_t i = 0; i < 3; ++i) {
        cups[i] = VM_findInstanceByTarget(ctx, RValue_toInt32(GMLArray_get(cupArray, i)));
        characters[i] = RValue_toInt32(GMLArray_get(party, i));
        if (cups[i] == nullptr || characters[i] < 0 || characters[i] > 3) return false;
    }
    Instance* scoreboard = nullptr;
    if (RValue_toBool(CUP_GET(self, scoreboardExists))) {
        scoreboard = VM_findInstanceByTarget(ctx, RValue_toInt32(CUP_GET(self, scoreboard)));
        if (scoreboard == nullptr) return false;
    }

    GMLReal camTarget = reverse ? CUP_REAL(self, camTargetHeightReverse) : CUP_REAL(self, camTargetHeightNormal);
    CUP_SET(self, camTargetHeight, camTarget);
    CUP_SET(self, riseSpeedBoost, boost);
    if (riding) {
        if (runner->currentRoomIndex == cache.mazeRoom) {
            RValue* flags = IntRValueHashMap_getOrInsertUndefined(&ctx->globalScopeInstance->selfVars, cache.flag);
            if (NativeScript_compareReals(RValue_toReal(GMLArray_get(teacupArray(*flags), 442)), 0, epsilon) == 0) {
                if (NativeScript_compareReals(boost, 0, epsilon) == 0) {
                    CUP_SET(self, idletimer, CUP_REAL(self, idletimer) + 1);
                } else {
                    ctx->currentArrayOwner = (void*)(intptr_t)CUP_FLAG_OWNER;
                    VM_setArrayElement(ctx, flags, 442, RValue_makeReal(1));
                    CUP_SET(self, idletimer, 0);
                }
            }
        }
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(nextY), -1);
        CUP_SET(self, spin, nextSpin);
        GMLReal moved = GMLReal_fabs(reverse ? CUP_REAL(self, endY) - self->y : (GMLReal)self->ystart - self->y);
        if (RValue_toBool(CUP_GET(self, camFollow)) &&
            NativeScript_compareReals(moved, CUP_REAL(self, camFollowAfter), epsilon) > 0) {
            GMLCamera* camera = Runner_getCameraForView(runner, 0);
            if (camera != nullptr) {
                camera->viewY = (float)(camera->viewY + (self->y - camTarget - camera->viewY) * (GMLReal)0.2);
                camera->viewY = (float)GMLReal_fmin(GMLReal_fmax(camera->viewY, 0), (GMLReal)runner->currentRoom->height - 480);
                if (NativeScript_compareReals(moved, CUP_REAL(self, camFollowAfter) * 2, epsilon) > 0)
                    camera->viewY = (float)GMLReal_fmin(camera->viewY, self->y - camTarget + 140);
                if (NativeScript_compareReals(CUP_REAL(self, shaketimer), 0, epsilon) > 0) {
                    GMLReal shake = CUP_REAL(self, shaketimer) - 1;
                    CUP_SET(self, shaketimer, shake);
                    if (NativeScript_compareReals(GMLReal_fmod(shake, 2), 0, epsilon) == 0) shake *= -1;
                    camera->viewX = (float)(GMLReal_fmin(GMLReal_fmax(self->x - camera->viewWidth / 2, 0),
                        (GMLReal)runner->currentRoom->width - camera->viewWidth) + shake);
                }
                Runner_updateCameraViewSimple(camera);
            }
        }
        if (NativeScript_compareReals(boost, 0, epsilon) != 0)
            CUP_SET(self, riseSpeedBoost, teacupApproach(boost, 0, CUP_REAL(self, riseSpeedBoostFriction), epsilon));
    }
    for (int32_t i = 0; i < 3; ++i) {
        Instance* cup = cups[i];
        GMLReal spin = (-90 + 120 * i) - CUP_REAL(self, spin);
        GMLReal radians = spin * (M_PI / 180.0);
        GMLReal cupx = CUP_REAL(self, cupDistanceFromCenter) * GMLReal_cos(radians);
        GMLReal cupy = -(CUP_REAL(self, cupDistanceFromCenter) * (GMLReal)0.5) * GMLReal_sin(radians);
        if (GMLReal_fabs(cupx) < (GMLReal)1e-4) cupx = 0;
        if (GMLReal_fabs(cupy) < (GMLReal)1e-4) cupy = 0;
        CUP_SET(self, cupx, cupx);
        CUP_SET(self, cupy, cupy);
        if (NativeScript_compareReals(CUP_REAL(self, teaFilled), 0.1, epsilon) > 0 && characters[i] != 2) {
            int32_t* sprites = NativeScript_compareReals(CUP_REAL(self, teaFilled), 1, epsilon) >= 0
                ? cache.fullTeaSprites : cache.teaSprites;
            int32_t character = riding && NativeScript_compareReals(i, CUP_REAL(self, partySize), epsilon) < 0 ? characters[i] : 0;
            VMBuiltins_setVariable(ctx, cup, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", RValue_makeInt32(sprites[character]), -1);
        }
        CUP_SET(cup, spin, spin);
        VMBuiltins_setVariable(ctx, cup, BUILTIN_VAR_X, "x", RValue_makeReal(self->x + cupx), -1);
        VMBuiltins_setVariable(ctx, cup, BUILTIN_VAR_Y, "y", RValue_makeReal((self->y + cupy) - 16), -1);
        VMBuiltins_setVariable(ctx, cup, BUILTIN_VAR_X, "x", RValue_makeReal(cup->x + CUP_REAL(self, cupCharXnudge)), -1);
        VMBuiltins_setVariable(ctx, cup, BUILTIN_VAR_Y, "y", RValue_makeReal(cup->y + CUP_REAL(self, cupCharYnudge)), -1);
        cup->imageIndex = (float)(int32_t)(teacupWrap(spin, epsilon) / 30);
        if (RValue_toBool(CUP_GET(self, fadeOutOthers)) && !reverse && i != 0) {
            GMLReal alpha;
            if (riding) {
                bool close = NativeScript_compareReals(GMLReal_fabs(destination - self->y), 120, epsilon) <= 0;
                alpha = teacupApproach(cup->imageAlpha, close ? 1 : (GMLReal)0.15,
                    close ? (GMLReal)0.02 + CUP_REAL(self, riseSpeed) / 120 : (GMLReal)0.05, epsilon);
            } else {
                alpha = teacupApproach(cup->imageAlpha, 1, (GMLReal)0.05, epsilon);
            }
            cup->imageAlpha = (float)alpha;
        }
        VMBuiltins_setVariable(ctx, cup, BUILTIN_VAR_DEPTH, "depth",
            RValue_makeReal(self->depth - CUP_REAL(self, depthSeparation) - cupy - 1), -1);
    }
    if (scoreboard != nullptr) {
        GMLReal score = CUP_REAL(self, theScore), goal = CUP_REAL(self, scoreboardGoal);
        VMBuiltins_setVariable(ctx, scoreboard, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + 92), -1);
        CUP_SET(scoreboard, theScore, score);
        RValue number = VMBuiltins_getVariable(ctx, scoreboard, BUILTIN_VAR_IMAGE_NUMBER, "image_number", -1);
        GMLReal frames = RValue_toReal(number);
        RValue_free(&number);
        scoreboard->imageIndex = (float)GMLReal_floor((score / goal) * (frames - 1));
        if (NativeScript_compareReals(score, goal, epsilon) >= 0) {
            scoreboard->imageIndex = (float)(frames - 1);
            CUP_SET(scoreboard, scoreCol, 65280);
        }
    }
    if (runner->currentRoomIndex == cache.mazeRoom) {
        RValue flags = Instance_getSelfVar(ctx->globalScopeInstance, cache.flag);
        bool show = riding && NativeScript_compareReals(CUP_REAL(self, idletimer), 30, epsilon) > 0 &&
            NativeScript_compareReals(RValue_toReal(GMLArray_get(teacupArray(flags), 442)), 0, epsilon) == 0;
        CUP_SET(self, drawtutorialarrows, show);
    }
    teacupSounds(ctx, self);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2TeacupStep_register(VMContext* ctx) {
    static const char* const names[] = {
        "spr_teacup_tea", "spr_teacup_kris_tea", "spr_teacup_kris_tea", "spr_teacup_ralsei_tea",
        "spr_teacup_tea2", "spr_teacup_kris_tea2", "spr_teacup_kris_tea", "spr_teacup_ralsei_tea2",
    };
    for (int32_t j = 0; j < 8; ++j) {
        int32_t id = -1;
        for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
            const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
            if (sprite->present && strcmp(sprite->name, names[j]) == 0) {
                id = (int32_t)i;
                break;
            }
        }
        if (id < 0) return;
        if (j < 4) cache.teaSprites[j] = id;
        else cache.fullTeaSprites[j - 4] = id;
    }
    cache.mazeRoom = -1;
    for (uint32_t i = 0; i < ctx->dataWin->room.count; ++i) {
        if (strcmp(ctx->dataWin->room.rooms[i].name, "room_dw_cyber_battle_maze_1") == 0)
            cache.mazeRoom = (int32_t)i;
    }
    cache.swallow = -1;
    cache.jump = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sond.count; ++i) {
        const char* name = ctx->dataWin->sond.sounds[i].name;
        if (strcmp(name, "snd_swallow") == 0) cache.swallow = (int32_t)i;
        else if (strcmp(name, "snd_jump") == 0) cache.jump = (int32_t)i;
    }
    cache.playPitch = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.volume = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.isPlaying = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.play = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.swallow < 0 || cache.jump < 0 ||
        !NativeScript_canCall(ctx, &cache.playPitch, "gml_Script_snd_play_pitch") ||
        !NativeScript_canCall(ctx, &cache.volume, "gml_Script_snd_volume") ||
        !NativeScript_canCall(ctx, &cache.isPlaying, "gml_Script_snd_is_playing") ||
        !NativeScript_canCall(ctx, &cache.play, "gml_Script_snd_play") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_teacup_Step_0", teacupStep)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    CUP_STEP_VARS(RESOLVE_SELF)
#undef RESOLVE_SELF
    cache.party = NativeScript_getGlobalVarID(ctx, "char");
    cache.inputHeld = NativeScript_getGlobalVarID(ctx, "input_held");
    cache.flag = NativeScript_getGlobalVarID(ctx, "flag");
}
