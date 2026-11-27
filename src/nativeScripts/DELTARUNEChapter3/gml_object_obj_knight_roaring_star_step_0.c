#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t outbound, con, split, timer, finalx, playSound, growstart, splitease, splitmax;
    int32_t loopIndex, child, deceleration, fadeSpeed;
    int32_t starchildObject, sprTop, sprBottom, sndFirework;
    Chapter3Builtin lengthdirX, lengthdirY, clamp, power, spriteGetWidth, spriteGetHeight;
    Chapter3Builtin drawSpriteExt, mergeColor, gpuSetBlendmode;
    NativeCallBinding sndPlay, childBullet, afterimage, drawBeamColor;
} cache;

static GMLReal clamp01(VMContext* ctx, GMLReal value) {
    RValue args[] = { RValue_makeReal(value), RValue_makeReal(0), RValue_makeReal(1) };
    return Chapter3_callReal(ctx, &cache.clamp, args, 3);
}

static GMLReal lengthdir(VMContext* ctx, const Chapter3Builtin* func, GMLReal length, GMLReal direction) {
    RValue args[] = { RValue_makeReal(length), RValue_makeReal(direction) };
    return Chapter3_callReal(ctx, func, args, 2);
}

static GMLReal spriteSize(VMContext* ctx, const Chapter3Builtin* func, GMLReal sprite) {
    RValue arg = RValue_makeReal(sprite);
    return Chapter3_callReal(ctx, func, &arg, 1);
}

static void incrementVar(Instance* self, int32_t varId) {
    Instance_setSelfVar(self, varId, Chapter3_increment(Instance_getSelfVar(self, varId)));
}

static bool roaringStarStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    *outResult = RValue_makeUndefined();

    GMLReal x = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x");
    GMLReal y = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y");
    GMLReal cameraX = Chapter3_cameraX(runner);
    GMLReal cameraY = Chapter3_cameraY(runner);
    GMLReal spriteWidth = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width");
    bool outside = Chapter3_compare(ctx, x, cameraX - spriteWidth) < 0 ||
        Chapter3_compare(ctx, x, cameraX + Chapter3_cameraWidth(runner) + spriteWidth) > 0;
    if (!outside) {
        GMLReal spriteHeight = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height");
        outside = Chapter3_compare(ctx, y, cameraY - spriteHeight) < 0 ||
            Chapter3_compare(ctx, y, cameraY + 480 + spriteHeight) > 0;
    }
    if (outside) {
        if (RValue_toBool(Instance_getSelfVar(self, cache.outbound))) {
            Runner_destroyInstance(runner, self, true);
            return true;
        }
    } else {
        Instance_setSelfVar(self, cache.outbound, RValue_makeInt32(1));
    }

    GMLReal con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, 1) == 0) {
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_FRICTION, "friction", R(0.5));
        incrementVar(self, cache.con);
    } else if (Chapter3_compare(ctx, con, 2) == 0) {
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed"), 0) == 0 &&
            Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_GRAVITY, "gravity"), 0) == 0) {
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_GRAVITY, "gravity", R(0.1));
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_GRAVITY_DIRECTION, "gravity_direction",
                Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction") - 180);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_FRICTION, "friction", 0);
            if (RValue_toBool(Instance_getSelfVar(self, cache.split))) {
                GMLReal finalTime = 54 - Chapter3_getReal(self, cache.timer);
                GMLReal length = (R(0.5) * Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_GRAVITY, "gravity")) * (finalTime * finalTime);
                GMLReal gravityDirection = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_GRAVITY_DIRECTION, "gravity_direction");
                GMLReal subX = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x") - Chapter3_cameraX(runner)) +
                    lengthdir(ctx, &cache.lengthdirX, length, gravityDirection);
                GMLReal subY = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y") - Chapter3_cameraY(runner)) +
                    lengthdir(ctx, &cache.lengthdirY, length, gravityDirection);
                Chapter3_setReal(self, cache.finalx, subX - (subY / 2));
            }
        }
        incrementVar(self, cache.timer);
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 40) >= 0 &&
            !RValue_toBool(Instance_getSelfVar(self, cache.split))) {
            Instance_setSelfVar(self, cache.timer, RValue_makeReal(0));
            incrementVar(self, cache.con);
            if (RValue_toBool(Instance_getSelfVar(self, cache.playSound))) {
                RValue sound = RValue_makeReal((GMLReal)cache.sndFirework);
                RValue result = NativeScript_call(ctx, &cache.sndPlay, "gml_Script_snd_play", &sound, 1);
                RValue_free(&result);
            }
        }
        Chapter3_setReal(self, cache.growstart, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
    } else if (Chapter3_compare(ctx, con, R(2.5)) == 0) {
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.split), 1) == 0) {
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 0);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_GRAVITY, "gravity", 0);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", (GMLReal)cache.sprTop);
            Instance_setSelfVar(self, cache.timer, RValue_makeReal(-10));
            Instance_setSelfVar(self, cache.split, RValue_makeReal(2));
        }
        incrementVar(self, cache.timer);
        GMLReal eased = clamp01(ctx, Chapter3_getReal(self, cache.timer) / 20) - 1;
        RValue powerArgs[] = { RValue_makeReal(eased), RValue_makeReal(4) };
        GMLReal ease = -1 * (Chapter3_callReal(ctx, &cache.power, powerArgs, 2) - 1);
        Chapter3_setReal(self, cache.splitease, (ease * Chapter3_getReal(self, cache.splitmax)) *
            Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 20) == 0) {
            Instance_setSelfVar(self, cache.con, RValue_makeReal(3));
            Instance_setSelfVar(self, cache.timer, RValue_makeReal(0));
        }
    } else if (Chapter3_compare(ctx, con, 3) == 0) {
        incrementVar(self, cache.timer);
        GMLReal growstart = Chapter3_getReal(self, cache.growstart);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            growstart + clamp01(ctx, Chapter3_getReal(self, cache.timer) / 2));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            Chapter3_getReal(self, cache.growstart) + clamp01(ctx, Chapter3_getReal(self, cache.timer) / 2));

        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 3) == 0) {
            GMLReal angle = 90;
            for (int32_t i = 0; 6 > i; i++) {
                Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal((GMLReal)i));
                RValue childArgs[] = {
                    RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x")),
                    RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y")),
                    RValue_makeReal((GMLReal)cache.starchildObject)
                };
                RValue childId = NativeScript_call(ctx, &cache.childBullet, "gml_Script_scr_childbullet", childArgs, 3);
                Instance_setSelfVar(self, cache.child, childId);
                Instance* child = Chapter3_resolveInstance(runner, childId);
                RValue_free(&childId);
                if (child != nullptr) {
                    Chapter3_setBuiltin(ctx, child, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", angle);
                    Chapter3_setBuiltin(ctx, child, BUILTIN_VAR_DIRECTION, "direction", angle);
                    Chapter3_setBuiltin(ctx, child, BUILTIN_VAR_SPEED, "speed", 1);
                    Chapter3_setBuiltin(ctx, child, BUILTIN_VAR_FRICTION, "friction", -R(0.1));
                    Chapter3_setBuiltin(ctx, child, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
                        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale") * R(0.5));
                    Chapter3_setBuiltin(ctx, child, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
                        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale") * R(0.5));
                    Chapter3_setReal(child, cache.deceleration, R(0.15));
                }
                angle += (i == 1 || i == 4) ? 57 : 66;
            }
            Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal(6));
        }

        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 4) >= 0) {
            GMLReal sprite = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index");
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
                (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width") + 16) /
                spriteSize(ctx, &cache.spriteGetWidth, sprite));
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
                (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height") + 16) /
                spriteSize(ctx, &cache.spriteGetHeight, sprite));
            RValue afterimageId = NativeScript_call(ctx, &cache.afterimage, "gml_Script_scr_afterimage", nullptr, 0);
            Instance* afterimage = Chapter3_resolveInstance(runner, afterimageId);
            RValue_free(&afterimageId);
            if (afterimage != nullptr)
                Chapter3_setReal(afterimage, cache.fadeSpeed, Chapter3_getReal(afterimage, cache.fadeSpeed) * 3);
            Runner_destroyInstance(runner, self, true);
        }
    }
    return true;
}

static void drawSprite(VMContext* ctx, GMLReal sprite, GMLReal subimg, GMLReal x, GMLReal y, GMLReal xscale,
    GMLReal yscale, GMLReal angle, GMLReal color, GMLReal alpha) {
    RValue args[] = {
        RValue_makeReal(sprite), RValue_makeReal(subimg), RValue_makeReal(x), RValue_makeReal(y),
        RValue_makeReal(xscale), RValue_makeReal(yscale), RValue_makeReal(angle), RValue_makeReal(color),
        RValue_makeReal(alpha)
    };
    Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);
}

typedef struct {
    GMLReal sprite, xscale, yscale, screenX, screenY, splitease;
} StarDrawState;

static StarDrawState starDrawState(VMContext* ctx, Instance* self) {
    StarDrawState state;
    state.sprite = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index");
    state.xscale = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width") + 16) /
        spriteSize(ctx, &cache.spriteGetWidth, state.sprite);
    state.yscale = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height") + 16) /
        spriteSize(ctx, &cache.spriteGetHeight, state.sprite);
    state.screenX = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x") - Chapter3_cameraX(ctx->runner);
    state.screenY = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y") - Chapter3_cameraY(ctx->runner);
    state.splitease = Chapter3_getReal(self, cache.splitease);
    return state;
}

static bool roaringStarUser0(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    StarDrawState state = starDrawState(ctx, self);
    GMLReal angle = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle");
    GMLReal blend = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend");
    GMLReal alpha = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha");
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.split), 2) < 0) {
        drawSprite(ctx, state.sprite, 0, state.screenX, state.screenY, state.xscale, state.yscale, angle, blend, alpha);
    } else {
        drawSprite(ctx, (GMLReal)cache.sprTop, 0, state.screenX + (state.splitease / 2), state.screenY + state.splitease,
            state.xscale, state.yscale, angle, blend, alpha);
        drawSprite(ctx, (GMLReal)cache.sprBottom, 0, state.screenX - (state.splitease / 2), state.screenY - state.splitease,
            state.xscale, state.yscale, angle, blend, alpha);
    }
    return true;
}

static void setBlendmode(VMContext* ctx, GMLReal mode) {
    RValue arg = RValue_makeReal(mode);
    Chapter3_callVoid(ctx, &cache.gpuSetBlendmode, &arg, 1);
}

static void drawBeam(VMContext* ctx, GMLReal x, GMLReal y, GMLReal length, GMLReal angle, GMLReal alpha) {
    RValue args[] = {
        RValue_makeReal(x), RValue_makeReal(y), RValue_makeReal(length), RValue_makeReal(10),
        RValue_makeReal(angle), RValue_makeReal(16777215), RValue_makeReal(0), RValue_makeReal(alpha),
        RValue_makeBool(false)
    };
    RValue result = NativeScript_call(ctx, &cache.drawBeamColor, "gml_Script_scr_draw_beam_color", args, 9);
    RValue_free(&result);
}

static bool roaringStarUser1(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    StarDrawState state = starDrawState(ctx, self);
    GMLReal timer = Chapter3_getReal(self, cache.timer);
    GMLReal pulse = (GMLReal_sin(timer * 3) + 1) * R(0.25);
    GMLReal topX = state.screenX + (state.splitease / 2);
    GMLReal topY = state.screenY + state.splitease;
    GMLReal bottomX = state.screenX - (state.splitease / 2);
    GMLReal bottomY = state.screenY - state.splitease;
    GMLReal con = Chapter3_getReal(self, cache.con);
    bool con1 = Chapter3_compare(ctx, con, 1) == 0;
    bool con2 = Chapter3_compare(ctx, con, 2) == 0;
    bool con25 = Chapter3_compare(ctx, con, R(2.5)) == 0;
    bool con3 = Chapter3_compare(ctx, con, 3) == 0;
    bool con4 = Chapter3_compare(ctx, con, 4) == 0;

    if (con2 || con25 || con3) {
        GMLReal a = 1;
        GMLReal length = 120;
        if (con2) {
            a = clamp01(ctx, (timer / 30) - pulse);
            length = (50 * clamp01(ctx, (timer / 30) - (GMLReal_fmod(timer, 2) * R(0.75)))) + 50;
        }
        setBlendmode(ctx, 1);
        drawBeam(ctx, topX, topY, length, 90, a);
        drawBeam(ctx, bottomX, bottomY, length, 156, a);
        drawBeam(ctx, topX, topY, length, 24, a);
        drawBeam(ctx, bottomX, bottomY, length, 270, a);
        drawBeam(ctx, topX, topY, length, 336, a);
        drawBeam(ctx, bottomX, bottomY, length, 204, a);
        setBlendmode(ctx, 0);
    }

    bool split2 = Chapter3_compare(ctx, Chapter3_getReal(self, cache.split), 2) >= 0;
    if (con1 || con2 || con25) {
        RValue mergeArgs[] = { RValue_makeReal(8421504), RValue_makeReal(255), RValue_makeReal(clamp01(ctx, timer / 30)) };
        GMLReal color = Chapter3_callReal(ctx, &cache.mergeColor, mergeArgs, 3);
        drawSprite(ctx, state.sprite, 1, topX, topY, state.xscale + R(0.1), state.yscale + R(0.1), 0, 16777215, pulse);
        drawSprite(ctx, state.sprite, 0, topX, topY, state.xscale, state.yscale, 0, color, 1);
        if (split2) {
            drawSprite(ctx, (GMLReal)cache.sprBottom, 1, bottomX, bottomY, state.xscale + R(0.1), state.yscale + R(0.1), 0, 16777215, pulse);
            drawSprite(ctx, (GMLReal)cache.sprBottom, 0, bottomX, bottomY, state.xscale, state.yscale, 0, color, 1);
        }
    }

    if (con3 || con4) {
        GMLReal glow = (GMLReal_sin(timer * 6) + 1) * R(0.25);
        drawSprite(ctx, state.sprite, 2, topX, topY, state.xscale + R(0.1), state.yscale + R(0.1), 0, 16777215, glow);
        drawSprite(ctx, state.sprite, 2, topX, topY, state.xscale, state.yscale, 0, 16777215, 1);
        if (split2) {
            drawSprite(ctx, (GMLReal)cache.sprBottom, 2, bottomX, bottomY, state.xscale + R(0.1), state.yscale + R(0.1), 0, 16777215, glow);
            drawSprite(ctx, (GMLReal)cache.sprBottom, 2, bottomX, bottomY, state.xscale, state.yscale, 0, 16777215, 1);
        }
    }
    return true;
}

void NativeChapter3RoaringStar_register(VMContext* ctx) {
    cache.starchildObject = Chapter3_findObject(ctx, "obj_knight_pointing_starchild");
    cache.sprTop = Chapter3_findSprite(ctx, "spr_knight_bullet_star_top");
    cache.sprBottom = Chapter3_findSprite(ctx, "spr_knight_bullet_star_bottom");
    cache.sndFirework = Chapter3_findSound(ctx, "snd_explosion_firework");
    if (cache.starchildObject < 0 || cache.sprTop < 0 || cache.sprBottom < 0 || cache.sndFirework < 0) return;
    Chapter3_bindBuiltin(ctx, &cache.lengthdirX, "lengthdir_x");
    Chapter3_bindBuiltin(ctx, &cache.lengthdirY, "lengthdir_y");
    Chapter3_bindBuiltin(ctx, &cache.clamp, "clamp");
    Chapter3_bindBuiltin(ctx, &cache.power, "power");
    Chapter3_bindBuiltin(ctx, &cache.spriteGetWidth, "sprite_get_width");
    Chapter3_bindBuiltin(ctx, &cache.spriteGetHeight, "sprite_get_height");
    Chapter3_bindBuiltin(ctx, &cache.drawSpriteExt, "draw_sprite_ext");
    Chapter3_bindBuiltin(ctx, &cache.mergeColor, "merge_color");
    Chapter3_bindBuiltin(ctx, &cache.gpuSetBlendmode, "gpu_set_blendmode");
    if (cache.lengthdirX.func == nullptr || cache.lengthdirY.func == nullptr || cache.clamp.func == nullptr ||
        cache.power.func == nullptr || cache.spriteGetWidth.func == nullptr || cache.spriteGetHeight.func == nullptr ||
        cache.drawSpriteExt.func == nullptr || cache.mergeColor.func == nullptr || cache.gpuSetBlendmode.func == nullptr) return;

    cache.outbound = NativeScript_getSelfVarID(ctx, "outbound");
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.split = NativeScript_getSelfVarID(ctx, "split");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.finalx = NativeScript_getSelfVarID(ctx, "finalx");
    cache.playSound = NativeScript_getSelfVarID(ctx, "playSound");
    cache.growstart = NativeScript_getSelfVarID(ctx, "growstart");
    cache.splitease = NativeScript_getSelfVarID(ctx, "splitease");
    cache.splitmax = NativeScript_getSelfVarID(ctx, "splitmax");
    cache.loopIndex = NativeScript_getSelfVarID(ctx, "i");
    cache.child = NativeScript_getSelfVarID(ctx, "d");
    cache.deceleration = NativeScript_getSelfVarID(ctx, "deceleration");
    cache.fadeSpeed = NativeScript_getSelfVarID(ctx, "fadeSpeed");
    cache.sndPlay = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.childBullet = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.afterimage = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.drawBeamColor = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_roaring_star_Step_0", roaringStarStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_roaring_star_Other_10", roaringStarUser0);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_roaring_star_Other_11", roaringStarUser1);
}
