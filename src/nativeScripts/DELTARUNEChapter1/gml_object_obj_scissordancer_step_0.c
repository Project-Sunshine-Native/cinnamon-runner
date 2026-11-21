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
    int32_t danceTimer;
    int32_t specTurn;
    int32_t myInteract;
    int32_t soundCount;
    int32_t soundTimer;
    int32_t pitch;
    int32_t channel;
    int32_t bullet;
    int32_t bellSound;
    NativeCallBinding random;
    NativeCallBinding soundPlay;
    NativeCallBinding soundPitch;
} cache;

static void scissordancerTurn(VMContext* ctx, Instance* self) {
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
        RValue_makeReal(self->y - self->vspeed), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
        RValue_makeReal(self->x - self->hspeed), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DIRECTION, "direction",
        RValue_makeReal(self->direction - 90), -1);
}

static void scissordancerRingBell(VMContext* ctx, Instance* self) {
    RValue range = RValue_makeReal(0.7);
    RValue random = NativeScript_call(ctx, &cache.random, "random", &range, 1);
    RValue pitch = RValue_makeReal(0.5 + RValue_toReal(random));
    RValue_free(&random);
    Instance_setSelfVar(self, cache.pitch, pitch);

    RValue bell = RValue_makeReal(cache.bellSound);
    RValue channel = NativeScript_call(ctx, &cache.soundPlay, "gml_Script_snd_play", &bell, 1);
    Instance_setSelfVar(self, cache.channel, channel);

    RValue pitchArgs[] = { channel, pitch };
    RValue result = NativeScript_call(ctx, &cache.soundPitch, "gml_Script_snd_pitch", pitchArgs, 2);
    RValue_free(&result);
    RValue_free(&channel);
}

static bool scissordancerStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;

    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal danceTimer = RValue_toReal(Instance_getSelfVar(self, cache.danceTimer)) + 1;

    if (NativeScript_compareReals(danceTimer, 30, epsilon) >= 0 &&
        NativeScript_compareReals(danceTimer, 75, epsilon) <= 0) {
        self->imageIndex += 0.25f;
    }

    if (NativeScript_compareReals(danceTimer, 112, epsilon) >= 0 &&
        NativeScript_compareReals(danceTimer, 157, epsilon) <= 0) {
        self->imageIndex -= 0.25f;
    }

    if (NativeScript_compareReals(danceTimer, 166, epsilon) >= 0) danceTimer = 0;
    Instance_setSelfVar(self, cache.danceTimer, RValue_makeReal(danceTimer));

    GMLReal specTurn = RValue_toReal(Instance_getSelfVar(self, cache.specTurn));
    if (NativeScript_compareReals(specTurn, 1, epsilon) == 0) {
        if (self->x >= self->xstart + 170 && self->y <= self->ystart + 10) {
            scissordancerTurn(ctx, self);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
                RValue_makeReal(self->xstart + 169), -1);
        }

        if (self->y >= self->ystart + 160) {
            scissordancerTurn(ctx, self);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
                RValue_makeReal(self->ystart + 159), -1);
        }

        if (self->x <= self->xstart) {
            scissordancerTurn(ctx, self);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
                RValue_makeReal(self->xstart + 1), -1);
        }

        if (self->y <= self->ystart) {
            scissordancerTurn(ctx, self);
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
                RValue_makeReal(self->ystart + 1), -1);
        }
    }

    GMLReal soundCount = RValue_toReal(Instance_getSelfVar(self, cache.soundCount));
    GMLReal myInteract = RValue_toReal(Instance_getSelfVar(self, cache.myInteract));

    if (NativeScript_compareReals(myInteract, 1, epsilon) == 0 &&
        NativeScript_compareReals(soundCount, 0, epsilon) == 0) {
        soundCount = 1;
        Instance_setSelfVar(self, cache.soundCount, RValue_makeReal(soundCount));
        Instance_setSelfVar(self, cache.myInteract, RValue_makeReal(0));
    } else {
        Instance_setSelfVar(self, cache.myInteract, RValue_makeReal(2));
    }

    if (NativeScript_compareReals(soundCount, 1, epsilon) >= 0 &&
        NativeScript_compareReals(soundCount, 5, epsilon) <= 0) {
        GMLReal soundTimer = RValue_toReal(Instance_getSelfVar(self, cache.soundTimer)) - 1;
        Instance_setSelfVar(self, cache.soundTimer, RValue_makeReal(soundTimer));

        if (NativeScript_compareReals(soundTimer, 0, epsilon) <= 0) {
            scissordancerRingBell(ctx, self);
            soundCount += 1;
            Instance_setSelfVar(self, cache.soundCount, RValue_makeReal(soundCount));
            Instance_setSelfVar(self, cache.soundTimer, RValue_makeReal(3));
        }
    }

    if (NativeScript_compareReals(soundCount, 6, epsilon) == 0) {
        GMLReal soundTimer = RValue_toReal(Instance_getSelfVar(self, cache.soundTimer)) + 1;
        Instance_setSelfVar(self, cache.soundTimer, RValue_makeReal(soundTimer));

        if (NativeScript_compareReals(soundTimer, 15, epsilon) >= 0) {
            Instance_setSelfVar(self, cache.myInteract, RValue_makeReal(0));
            Instance_setSelfVar(self, cache.soundCount, RValue_makeReal(0));
            Instance_setSelfVar(self, cache.soundTimer, RValue_makeReal(0));
        }
    }

    GMLReal height = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1));
    GMLReal depth = 100000 - (((GMLReal)self->y * 10) + (height * 10));
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal(depth), -1);

    RValue bullet = Instance_getSelfVar(self, cache.bullet);
    Instance* bulletInstance = VM_findInstanceByTarget(ctx, RValue_toInt32(bullet));
    if (bulletInstance != nullptr && bulletInstance->active) {
        VMBuiltins_setVariable(ctx, bulletInstance, BUILTIN_VAR_X, "x",
            RValue_makeReal(self->x + self->hspeed), -1);
        VMBuiltins_setVariable(ctx, bulletInstance, BUILTIN_VAR_Y, "y",
            RValue_makeReal(self->y + self->vspeed), -1);
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeScissordancerStep_register(VMContext* ctx) {
    cache.bellSound = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sond.count; ++i) {
        const Sound* sound = &ctx->dataWin->sond.sounds[i];
        if (sound->name != nullptr && strcmp(sound->name, "snd_scissorbell") == 0) {
            cache.bellSound = (int32_t)i;
            break;
        }
    }

    cache.random = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundPlay = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.soundPitch = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;

    if (cache.bellSound < 0 ||
        !NativeScript_canCall(ctx, &cache.random, "random") ||
        !NativeScript_canCall(ctx, &cache.soundPlay, "gml_Script_snd_play") ||
        !NativeScript_canCall(ctx, &cache.soundPitch, "gml_Script_snd_pitch") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_scissordancer_Step_0", scissordancerStep)) {
        return;
    }

    cache.danceTimer = NativeScript_getSelfVarID(ctx, "dancetimer");
    cache.specTurn = NativeScript_getSelfVarID(ctx, "specturn");
    cache.myInteract = NativeScript_getSelfVarID(ctx, "myinteract");
    cache.soundCount = NativeScript_getSelfVarID(ctx, "sndcon");
    cache.soundTimer = NativeScript_getSelfVarID(ctx, "sndtimer");
    cache.pitch = NativeScript_getSelfVarID(ctx, "p");
    cache.channel = NativeScript_getSelfVarID(ctx, "b");
    cache.bullet = NativeScript_getSelfVarID(ctx, "bullet");
}
