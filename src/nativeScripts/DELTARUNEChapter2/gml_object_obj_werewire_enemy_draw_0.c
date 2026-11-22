#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t state, stopAnim, sinerTimer, siner, animTimer, m, fsiner;
    int32_t idleSprite, flash, becomeflash, hangx, hangy, i;
    int32_t hangSprite, curveSprite, verticalSprite;
    NativeCallBinding hurtTired, drawHurt;
    GMLReal wireSin[32], wireCos[32];
} cache;

static GMLReal werewireReal(Instance* self, int32_t variable) {
    return RValue_toReal(Instance_getSelfVar(self, variable));
}

static void werewireSet(Instance* self, int32_t variable, GMLReal value) {
    Instance_setSelfVar(self, variable, RValue_makeReal(value));
}

static void werewireDrawSprite(Renderer* renderer, Instance* self, int32_t sprite,
    int32_t frame, float x, float y, bool flash, float flashAlpha) {
    Renderer_drawSpriteExt(renderer, sprite, frame, x, y, self->imageXscale,
        self->imageYscale, (float)self->imageAngle, self->imageBlend, self->imageAlpha);
    if (flash) {
#ifndef PLATFORM_3DS
        if (renderer->vtable->gpuSetFog != nullptr)
            renderer->vtable->gpuSetFog(renderer, true, self->imageBlend, 0, 1);
#endif
        Renderer_drawSpriteExt(renderer, sprite, frame, x, y, self->imageXscale,
            self->imageYscale, (float)self->imageAngle, self->imageBlend, flashAlpha);
#ifndef PLATFORM_3DS
        if (renderer->vtable->gpuSetFog != nullptr)
            renderer->vtable->gpuSetFog(renderer, false, 0, 0, 0);
#endif
    }
}

static void werewireDrawWire(Renderer* renderer, Instance* self, int32_t tpag,
    float originX, float originY, float x, float y, bool flash, float flashAlpha) {
    renderer->vtable->drawSprite(renderer, tpag, x, y, originX, originY,
        self->imageXscale, self->imageYscale, (float)self->imageAngle,
        self->imageBlend, self->imageAlpha);
    if (flash) {
#ifndef PLATFORM_3DS
        if (renderer->vtable->gpuSetFog != nullptr)
            renderer->vtable->gpuSetFog(renderer, true, self->imageBlend, 0, 1);
#endif
        renderer->vtable->drawSprite(renderer, tpag, x, y, originX, originY,
            self->imageXscale, self->imageYscale, (float)self->imageAngle,
            self->imageBlend, flashAlpha);
#ifndef PLATFORM_3DS
        if (renderer->vtable->gpuSetFog != nullptr)
            renderer->vtable->gpuSetFog(renderer, false, 0, 0, 0);
#endif
    }
}

static bool werewireDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr) return false;
    Renderer* renderer = ctx->runner->renderer;
    if (renderer == nullptr) return false;

    if (werewireReal(self, cache.state) == 3) {
        RValue arg = RValue_makeReal(0.5);
        RValue result = NativeScript_call(ctx, &cache.hurtTired,
            "gml_Script_scr_enemyhurt_tired_after_damage", &arg, 1);
        RValue_free(&result);
        result = NativeScript_call(ctx, &cache.drawHurt,
            "gml_Script_scr_enemy_drawhurt_generic", nullptr, 0);
        RValue_free(&result);
    }

    GMLReal siner = werewireReal(self, cache.siner);
    GMLReal animTimer = werewireReal(self, cache.animTimer);
    bool stopped = werewireReal(self, cache.stopAnim) != 0;
    if (!stopped) {
        GMLReal timer = werewireReal(self, cache.sinerTimer) + self->imageSpeed;
        if (timer >= 1) {
            timer -= 1;
            siner += 8;
            werewireSet(self, cache.siner, siner);
        }
        werewireSet(self, cache.sinerTimer, timer);
        GMLReal m = werewireReal(self, cache.m);
        animTimer += GMLReal_sin(siner / (34 / m)) / (4 / m);
        werewireSet(self, cache.animTimer, animTimer);
    }

    if (werewireReal(self, cache.state) == 0) {
        GMLReal fsiner = werewireReal(self, cache.fsiner) + 1;
        werewireSet(self, cache.fsiner, fsiner);
        bool flash = werewireReal(self, cache.flash) == 1;
        float flashAlpha = flash ? (float)(-GMLReal_cos(fsiner / 5) * 0.4 + 0.6) : 0;
        float x = self->x, y = self->y;
        GMLReal phase = siner / 16;
        GMLReal sinPhase = GMLReal_sin(phase);
        GMLReal cosPhase = GMLReal_cos(phase);
        const Sprite* wire = &renderer->dataWin->sprt.sprites[cache.verticalSprite];
        int32_t wireTpag = Renderer_resolveTPAGIndex(renderer->dataWin, cache.verticalSprite, 0);
        bool nineSlice = wire->nineSliceEnabled &&
            (self->imageXscale != 1.0f || self->imageYscale != 1.0f);

        if (self->spriteIndex != cache.hangSprite) {
            if (!stopped) {
                int32_t idleSprite = RValue_toInt32(Instance_getSelfVar(self, cache.idleSprite));
                werewireDrawSprite(renderer, self, idleSprite, (int32_t)animTimer, x, y, flash, flashAlpha);
            } else {
                Renderer_drawSelf(renderer, self);
            }
            werewireDrawSprite(renderer, self, cache.curveSprite, 0, x - 16, y - 12, flash, flashAlpha);
            for (int32_t i = 0; i < 32; ++i) {
                GMLReal wave = cache.wireSin[i] * cosPhase + cache.wireCos[i] * sinPhase;
                float segmentX = (float)((x - 16) + (wave * i) / 4 + i * 2);
                float segmentY = y - 20 - i * 8;
                if (nineSlice || wireTpag < 0) {
                    werewireDrawSprite(renderer, self, cache.verticalSprite, 0,
                        segmentX, segmentY, flash, flashAlpha);
                } else {
                    werewireDrawWire(renderer, self, wireTpag, (float)wire->originX,
                        (float)wire->originY, segmentX, segmentY, flash, flashAlpha);
                }
            }
            werewireSet(self, cache.i, 32);
        } else {
            float hangx = (float)werewireReal(self, cache.hangx);
            float hangy = (float)werewireReal(self, cache.hangy);
            Renderer_drawSpriteExt(renderer, cache.hangSprite, (int32_t)phase,
                x + hangx, y + hangy, self->imageXscale, self->imageYscale,
                (float)self->imageAngle, self->imageBlend, self->imageAlpha);
            for (int32_t i = 0; i < 32; ++i) {
                GMLReal wave = cache.wireSin[i] * cosPhase + cache.wireCos[i] * sinPhase;
                float segmentX = (float)(x + (wave * i) / 8 + hangx);
                float segmentY = y + 14 - i * 8 + hangy;
                if (nineSlice || wireTpag < 0) {
                    Renderer_drawSpriteExt(renderer, cache.verticalSprite, 0, segmentX,
                        segmentY, self->imageXscale, self->imageYscale,
                        (float)self->imageAngle, self->imageBlend, self->imageAlpha);
                } else {
                    renderer->vtable->drawSprite(renderer, wireTpag, segmentX, segmentY,
                        (float)wire->originX, (float)wire->originY, self->imageXscale,
                        self->imageYscale, (float)self->imageAngle, self->imageBlend,
                        self->imageAlpha);
                }
            }
            werewireSet(self, cache.i, 32);
        }
    }

    if (werewireReal(self, cache.becomeflash) == 0)
        werewireSet(self, cache.flash, 0);
    werewireSet(self, cache.becomeflash, 0);
    renderer->drawColor = 0x0000FF;
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2WerewireDraw_register(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 2") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.hangSprite = -1;
    cache.curveSprite = -1;
    cache.verticalSprite = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (!sprite->present) continue;
        if (strcmp(sprite->name, "spr_werewire_hang") == 0) cache.hangSprite = (int32_t)i;
        else if (strcmp(sprite->name, "spr_werewire_wire_curve") == 0) cache.curveSprite = (int32_t)i;
        else if (strcmp(sprite->name, "spr_werewire_wire_vertical") == 0) cache.verticalSprite = (int32_t)i;
    }

    for (int32_t i = 0; i < 32; ++i) {
        GMLReal angle = (GMLReal)i / 4;
        cache.wireSin[i] = GMLReal_sin(angle);
        cache.wireCos[i] = GMLReal_cos(angle);
    }

    cache.hurtTired = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.drawHurt = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.hangSprite < 0 || cache.curveSprite < 0 || cache.verticalSprite < 0 ||
        !NativeScript_canCall(ctx, &cache.hurtTired, "gml_Script_scr_enemyhurt_tired_after_damage") ||
        !NativeScript_canCall(ctx, &cache.drawHurt, "gml_Script_scr_enemy_drawhurt_generic") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_werewire_enemy_Draw_0", werewireDraw)) return;

    cache.state = NativeScript_getSelfVarID(ctx, "state");
    cache.stopAnim = NativeScript_getSelfVarID(ctx, "stop_anim");
    cache.sinerTimer = NativeScript_getSelfVarID(ctx, "siner_timer");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.animTimer = NativeScript_getSelfVarID(ctx, "anim_timer_2");
    cache.m = NativeScript_getSelfVarID(ctx, "m");
    cache.fsiner = NativeScript_getSelfVarID(ctx, "fsiner");
    cache.idleSprite = NativeScript_getSelfVarID(ctx, "idlesprite");
    cache.flash = NativeScript_getSelfVarID(ctx, "flash");
    cache.becomeflash = NativeScript_getSelfVarID(ctx, "becomeflash");
    cache.hangx = NativeScript_getSelfVarID(ctx, "hangx");
    cache.hangy = NativeScript_getSelfVarID(ctx, "hangy");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
}
