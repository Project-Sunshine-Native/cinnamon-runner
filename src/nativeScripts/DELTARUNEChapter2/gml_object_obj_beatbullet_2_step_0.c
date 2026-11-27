#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "stb_ds.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define BEAT_VARS(X) \
    X(destroyoffscreen) X(tolerance) X(fadeInSpeed) X(active) X(target) X(damage) \
    X(beats) X(beatsPrev) X(lanesEndY) X(follow) X(shouldActivate) X(readyToGo) \
    X(forgiveLoop) X(inSync) X(spb) X(bpf) X(targetBeat) X(beatMax) X(framesAway) \
    X(frames) X(horizontal) X(endY) X(targetX) X(looping) X(battlemode)

static struct {
#define DECLARE_ID(name) int32_t name;
    BEAT_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t currentSong, flag, mainchara, flashObject;
    BuiltinFunc isPlaying, trackPosition, createDepth;
    NativeCallBinding debug;
} cache;

#define BEAT_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define BEAT_REAL(inst, name) RValue_toReal(BEAT_GET(inst, name))
#define BEAT_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static GMLReal beatApproach(GMLReal value, GMLReal target, GMLReal amount, GMLReal epsilon) {
    if (NativeScript_compareReals(value, target, epsilon) < 0) {
        value += amount;
        if (NativeScript_compareReals(value, target, epsilon) > 0) return target;
    } else {
        value -= amount;
        if (NativeScript_compareReals(value, target, epsilon) < 0) return target;
    }
    return value;
}

static bool beatbullet2Create(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    BEAT_SET(self, destroyoffscreen, 0);
    BEAT_SET(self, tolerance, 0);
    BEAT_SET(self, fadeInSpeed, 8);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(2), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(2), -1);
    BEAT_SET(self, active, 0);
    BEAT_SET(self, target, 3);
    BEAT_SET(self, damage, 20);
    BEAT_SET(self, beats, 0);
    BEAT_SET(self, beatsPrev, 0);
    GMLCamera* camera = Runner_getCameraForView(ctx->runner, 0);
    GMLReal viewY = camera != nullptr ? camera->viewY : -1;
    BEAT_SET(self, lanesEndY, viewY + 150);
    BEAT_SET(self, follow, INSTANCE_NOONE);
    BEAT_SET(self, shouldActivate, 0);
    BEAT_SET(self, readyToGo, 0);
    BEAT_SET(self, forgiveLoop, 0);
    *outResult = RValue_makeUndefined();
    return true;
}

static bool beatbullet2Step(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    Instance* follow = nullptr;
    if (NativeScript_compareReals(self->imageAlpha, 1, epsilon) < 0 &&
        NativeScript_compareReals(BEAT_REAL(self, follow), INSTANCE_NOONE, epsilon) != 0) {
        follow = VM_findInstanceByTarget(ctx, RValue_toInt32(BEAT_GET(self, follow)));
        if (follow == nullptr) return false;
    }

    Instance** characters = runner->instancesByObject[cache.mainchara];
    Instance* first = nullptr;
    Instance* last = nullptr;
    int32_t count = 0;
    for (size_t i = 0; i < arrlenu(characters); ++i) {
        if (!characters[i]->active) continue;
        if (first == nullptr) first = characters[i];
        last = characters[i];
        count++;
    }
    Instance* character = count > 2 ? first : last;
    bool activate = character != nullptr &&
        NativeScript_compareReals(BEAT_REAL(character, battlemode), 1, epsilon) == 0;
    BEAT_SET(self, shouldActivate, activate);
    if (activate && NativeScript_compareReals(BEAT_REAL(self, framesAway), BEAT_REAL(self, frames), epsilon) > 0)
        BEAT_SET(self, readyToGo, 1);

    RValue songs = Instance_getSelfVar(ctx->globalScopeInstance, cache.currentSong);
    GMLArray* songArray = songs.type == RVALUE_ARRAY ? songs.array : nullptr;
    RValue song = GMLArray_get(songArray, 1);
    RValue playing = cache.isPlaying(ctx, &song, 1);
    bool synced = RValue_toBool(playing) &&
        NativeScript_compareReals(BEAT_REAL(self, inSync), 1, epsilon) == 0;
    RValue_free(&playing);

    GMLReal beats = BEAT_REAL(self, beats);
    GMLReal bpf = BEAT_REAL(self, bpf);
    if (synced) {
        GMLReal previous = beats;
        RValue position = cache.trackPosition(ctx, &song, 1);
        beats = RValue_toReal(position) / BEAT_REAL(self, spb);
        RValue_free(&position);
        BEAT_SET(self, beats, beats);
        if (NativeScript_compareReals(beats, previous, epsilon) < 0) {
            BEAT_SET(self, targetBeat, BEAT_REAL(self, targetBeat) - BEAT_REAL(self, beatMax));
            BEAT_SET(self, forgiveLoop, 1);
        }
    } else {
        BEAT_SET(self, inSync, 0);
        beats += bpf;
        BEAT_SET(self, beats, beats);
    }
    if (NativeScript_compareReals(GMLReal_fabs(beats - BEAT_REAL(self, beatsPrev)), 3, epsilon) > 0) {
        if (!RValue_toBool(BEAT_GET(self, forgiveLoop))) {
            BEAT_SET(self, inSync, 0);
        } else {
            BEAT_SET(self, forgiveLoop, 0);
        }
    }
    BEAT_SET(self, beatsPrev, beats);

    GMLReal framesAway = (BEAT_REAL(self, targetBeat) - beats) / bpf;
    GMLReal frames = BEAT_REAL(self, frames);
    GMLReal percent = GMLReal_fmax(0, framesAway / frames);
    GMLReal endY = BEAT_REAL(self, endY);
    bool horizontal = RValue_toBool(BEAT_GET(self, horizontal));
    GMLReal start = horizontal ? self->xstart : self->ystart;
    GMLReal distance = GMLReal_fabs(start - endY);
    GMLReal position = (-distance * (GMLReal_pow(percent - 1, 3) + 1)) + endY;
    BEAT_SET(self, framesAway, framesAway);
    if (!horizontal) {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(position), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y",
            RValue_makeReal(GMLReal_fmax(self->y, self->ystart)), -1);
    } else {
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(position), -1);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
            RValue_makeReal(GMLReal_fmax(self->x, self->xstart)), -1);
    }

    if (NativeScript_compareReals(self->imageAlpha, 1, epsilon) >= 0) {
        BEAT_SET(self, active, 1);
    } else {
        if (follow != nullptr)
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x",
                RValue_makeReal(follow->x + BEAT_REAL(self, targetX)), -1);
        BEAT_SET(self, active, 0);
    }

    GMLReal fade = BEAT_REAL(self, fadeInSpeed);
    bool ready = RValue_toBool(BEAT_GET(self, readyToGo));
    if (NativeScript_compareReals(framesAway, frames + fade, epsilon) < 0 && ready)
        self->imageAlpha = (float)beatApproach(self->imageAlpha, 1, 1 / fade, epsilon);
    if (!ready)
        self->imageAlpha = (float)beatApproach(self->imageAlpha, 0, 1 / fade, epsilon);

    if (NativeScript_compareReals(framesAway, frames, epsilon) < 0 &&
        NativeScript_compareReals(percent, 0, epsilon) <= 0) {
        if (RValue_toBool(BEAT_GET(self, active))) {
            RValue flags = Instance_getSelfVar(ctx->globalScopeInstance, cache.flag);
            GMLArray* flagArray = flags.type == RVALUE_ARRAY ? flags.array : nullptr;
            if (NativeScript_compareReals(RValue_toReal(GMLArray_get(flagArray, 8)), 0, epsilon) == 0) {
                RValue args[] = {
                    RValue_makeReal(self->x), RValue_makeReal(self->y),
                    RValue_makeReal(self->depth), RValue_makeInt32(cache.flashObject),
                };
                RValue created = cache.createDepth(ctx, args, 4);
                Instance* flash = VM_findInstanceByTarget(ctx, RValue_toInt32(created));
                RValue_free(&created);
                if (flash != nullptr) {
                    RValue width = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1);
                    VMBuiltins_setVariable(ctx, flash, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", width, -1);
                    RValue_free(&width);
                    RValue height = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1);
                    VMBuiltins_setVariable(ctx, flash, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
                        RValue_makeReal(((GMLReal)1 / RValue_toReal(height)) * 300), -1);
                    RValue_free(&height);
                    VMBuiltins_setVariable(ctx, flash, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
                        RValue_makeReal(self->imageAngle), -1);
                }
            }
        }
        self->imageAlpha = 0;
        BEAT_SET(self, readyToGo, 0);
        if (RValue_toBool(BEAT_GET(self, shouldActivate))) BEAT_SET(self, readyToGo, 1);
        GMLReal looping = BEAT_REAL(self, looping);
        if (NativeScript_compareReals(looping, 0, epsilon) != 0)
            BEAT_SET(self, targetBeat, BEAT_REAL(self, targetBeat) + looping);
        BEAT_SET(self, active, 0);
    }
    *outResult = RValue_makeUndefined();
    return true;
}

static bool beatbullet2Draw(VMContext* ctx, RValue* outResult) {
    Renderer* renderer = ctx->runner->renderer;
    if (renderer == nullptr) return false;
    Renderer_drawSelf(renderer, ctx->currentCallFrame->self);
    renderer->drawColor = 0xFFFFFF;
    *outResult = RValue_makeUndefined();
    return true;
}

static bool beatbullet2KeyPress(VMContext* ctx, RValue* outResult) {
    RValue debug = NativeScript_call(ctx, &cache.debug, "gml_Script_scr_debug", nullptr, 0);
    if (RValue_toBool(debug)) BEAT_SET(ctx->currentCallFrame->self, inSync, 0);
    RValue_free(&debug);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2Beatbullet2_register(VMContext* ctx) {
    cache.mainchara = -1;
    cache.flashObject = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_mainchara") == 0) cache.mainchara = (int32_t)i;
        else if (strcmp(object->name, "obj_beatflash") == 0) cache.flashObject = (int32_t)i;
    }
    cache.isPlaying = VM_findBuiltin(ctx, "audio_is_playing");
    cache.trackPosition = VM_findBuiltin(ctx, "audio_sound_get_track_position");
    cache.createDepth = VM_findBuiltin(ctx, "instance_create_depth");
    cache.debug = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.mainchara < 0 || cache.flashObject < 0 || cache.isPlaying == nullptr ||
        cache.trackPosition == nullptr || cache.createDepth == nullptr ||
        !NativeScript_canCall(ctx, &cache.debug, "gml_Script_scr_debug")) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    BEAT_VARS(RESOLVE_SELF)
#undef RESOLVE_SELF
    cache.currentSong = NativeScript_getGlobalVarID(ctx, "currentsong");
    cache.flag = NativeScript_getGlobalVarID(ctx, "flag");
    VM_registerNativeScript(ctx, "gml_Object_obj_beatbullet_2_Create_0", beatbullet2Create);
    VM_registerNativeScript(ctx, "gml_Object_obj_beatbullet_2_Step_0", beatbullet2Step);
    VM_registerNativeScript(ctx, "gml_Object_obj_beatbullet_2_Draw_0", beatbullet2Draw);
    VM_registerNativeScript(ctx, "gml_Object_obj_beatbullet_2_KeyPress_32", beatbullet2KeyPress);
}
