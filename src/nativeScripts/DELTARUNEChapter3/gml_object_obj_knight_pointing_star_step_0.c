#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t starchildObject, roaring2Object, followerObject, controllerObject, afterimageBlendObject;
    int32_t sprEasy, sprMask, sprTrail, sprExplosion, sndFirework;
    int32_t init, difficulty, grazetimer, grazed, con, growspeed, timer, playSound, growstart, side, active;
    int32_t loopIndex, child, lifetime, deceleration, minspeed, fadeSpeed;
    int32_t delay, subdelay, tracking, targetAngle, rotation, xscaleStart, yscaleStart, outline, ease, drawtimer;
    Chapter3Builtin instanceExists, spriteGetWidth, spriteGetHeight, mergeColor, lerp, angleDifference;
    Chapter3Builtin pointDirection, lengthdirX, lengthdirY, gpuSetFog, drawSpriteExt, eventInherited;
    NativeCallBinding blendMode, beam, easeIn, sndPlay, childBullet, afterimage, customAfterimage, onscreen;
} cache;

static GMLReal clamp01(GMLReal value) {
    if (0 > value) value = 0;
    if (value > 1) value = 1;
    return value;
}

static GMLReal builtinX(VMContext* ctx, Instance* inst) { return Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_X, "x"); }
static GMLReal builtinY(VMContext* ctx, Instance* inst) { return Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_Y, "y"); }
static GMLReal builtinAngle(VMContext* ctx, Instance* inst) { return Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ANGLE, "image_angle"); }

static GMLReal call2(VMContext* ctx, const Chapter3Builtin* builtin, GMLReal a, GMLReal b) {
    RValue args[] = { RValue_makeReal(a), RValue_makeReal(b) };
    return Chapter3_callReal(ctx, builtin, args, 2);
}

static GMLReal lerp3(VMContext* ctx, RValue a, RValue b, GMLReal t) {
    RValue args[] = { a, b, RValue_makeReal(t) };
    return Chapter3_callReal(ctx, &cache.lerp, args, 3);
}

static GMLReal mergeColor(VMContext* ctx, int32_t a, int32_t b, GMLReal amount) {
    RValue args[] = { RValue_makeInt32(a), RValue_makeInt32(b), RValue_makeReal(amount) };
    return Chapter3_callReal(ctx, &cache.mergeColor, args, 3);
}

static GMLReal sign(GMLReal value) {
    return (value > 0) ? 1 : ((0 > value) ? -1 : 0);
}

static GMLReal moveTowards(VMContext* ctx, GMLReal value, GMLReal target, GMLReal amount) {
    if (Chapter3_compare(ctx, value, target) == 0) return value;
    if (Chapter3_compare(ctx, value, target) > 0) {
        GMLReal result = -INFINITY;
        if (value - amount > result) result = value - amount;
        if (target > result) result = target;
        return result;
    }
    GMLReal result = INFINITY;
    if (result > value + amount) result = value + amount;
    if (result > target) result = target;
    return result;
}

static RValue scaleValue(RValue value, GMLReal factor, bool divide) {
    GMLReal real = RValue_toReal(value);
    return RValue_makeReal(divide ? real / factor : real * factor);
}

static void setBlendMode(VMContext* ctx, int32_t mode) {
    RValue arg = RValue_makeInt32(mode);
    RValue result = NativeScript_call(ctx, &cache.blendMode, "gml_Script_draw_set_blend_mode", &arg, 1);
    RValue_free(&result);
}

static void drawBeam(VMContext* ctx, GMLReal x, GMLReal y, RValue length, RValue angle, RValue color, GMLReal alpha) {
    RValue args[] = { RValue_makeReal(x), RValue_makeReal(y), length, RValue_makeInt32(10), angle, color,
        RValue_makeInt32(0), RValue_makeReal(alpha), RValue_makeBool(false) };
    RValue result = NativeScript_call(ctx, &cache.beam, "gml_Script_scr_draw_beam_color", args, 9);
    RValue_free(&result);
}

static GMLReal easeIn4(VMContext* ctx, GMLReal value) {
    RValue args[] = { RValue_makeReal(value), RValue_makeInt32(4) };
    RValue result = NativeScript_call(ctx, &cache.easeIn, "gml_Script_scr_ease_in", args, 2);
    GMLReal real = RValue_toReal(result);
    RValue_free(&result);
    return real;
}

static void drawSprite(VMContext* ctx, RValue sprite, RValue subimg, GMLReal x, GMLReal y, GMLReal xscale, GMLReal yscale,
    GMLReal angle, RValue color, RValue alpha) {
    RValue args[] = { sprite, subimg, RValue_makeReal(x), RValue_makeReal(y), RValue_makeReal(xscale),
        RValue_makeReal(yscale), RValue_makeReal(angle), color, alpha };
    Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);
}

static void starScale(VMContext* ctx, Instance* self, GMLReal* xscale, GMLReal* yscale) {
    RValue sprite = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    *xscale = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width") + 16) /
        Chapter3_callReal(ctx, &cache.spriteGetWidth, &sprite, 1);
    *yscale = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height") + 16) /
        Chapter3_callReal(ctx, &cache.spriteGetHeight, &sprite, 1);
}

static void spawnChildren(VMContext* ctx, Instance* self) {
    Runner* runner = ctx->runner;
    GMLReal difficulty = Chapter3_getReal(self, cache.difficulty);
    bool hard = Chapter3_compare(ctx, difficulty, 2) == 0;
    GMLReal angle = 90;
    if (hard) angle += Chapter3_getReal(self, cache.side);
    for (int32_t i = 0; 6 > i; i++) {
        Chapter3_setReal(self, cache.loopIndex, (GMLReal)i);
        RValue args[] = { RValue_makeReal(builtinX(ctx, self)), RValue_makeReal(builtinY(ctx, self)), RValue_makeInt32(cache.starchildObject) };
        RValue id = NativeScript_call(ctx, &cache.childBullet, "gml_Script_scr_childbullet", args, 3);
        Instance_setSelfVar(self, cache.child, id);
        Instance* d = Chapter3_resolveInstance(runner, id);
        RValue_free(&id);
        if (d != nullptr) {
            Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", angle);
            Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_DIRECTION, "direction", angle);
            if (Chapter3_compare(ctx, difficulty, 0) == 0 && Chapter3_compare(ctx, GMLReal_fmod((GMLReal)i, 2), 1) == 0) {
                Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_SPEED, "speed", 1);
                Chapter3_setReal(d, cache.lifetime, 30);
            } else {
                Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_SPEED, "speed", 4);
            }
            Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
                Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale") * R(0.5));
            Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
                Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale") * R(0.5));
            Chapter3_setReal(d, cache.deceleration, R(0.15));
            if (hard && Chapter3_compare(ctx, GMLReal_fmod((GMLReal)i, 3), 0) > 0) {
                Chapter3_setReal(d, cache.difficulty, -1);
                Chapter3_setReal(d, cache.lifetime, 30);
                Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_SPEED, "speed", 2);
                bool third = i == 1 || i == 4;
                GMLReal factor = third ? 3 : R(2) / R(3);
                Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_SPEED, "speed",
                    third ? Chapter3_getBuiltin(ctx, d, BUILTIN_VAR_SPEED, "speed") / factor
                          : Chapter3_getBuiltin(ctx, d, BUILTIN_VAR_SPEED, "speed") * factor);
                Instance_setSelfVar(d, cache.minspeed, scaleValue(Instance_getSelfVar(d, cache.minspeed), factor, third));
                Instance_setSelfVar(d, cache.deceleration, scaleValue(Instance_getSelfVar(d, cache.deceleration), factor, third));
                Chapter3_setBuiltin(ctx, d, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", (GMLReal)cache.sprTrail);
            } else {
                Instance_setSelfVar(d, cache.difficulty, Instance_getSelfVar(self, cache.difficulty));
            }
        }
        if (i == 1 || i == 4) angle += hard ? 180 : 48;
        else angle += hard ? 0 : 66;
    }
    Chapter3_setReal(self, cache.loopIndex, 6);
    Instance_setSelfVar(self, cache.active, RValue_makeInt32(0));
}

static bool pointingStarStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    *outResult = RValue_makeUndefined();
    GMLReal halfW = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width") / 2;
    GMLReal halfH = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height") / 2;
    GMLReal x = builtinX(ctx, self);
    GMLReal y = builtinY(ctx, self);
    if (Chapter3_compare(ctx, x, Chapter3_cameraX(runner) - halfW) < 0 ||
        Chapter3_compare(ctx, y, Chapter3_cameraY(runner) - halfH) < 0 ||
        Chapter3_compare(ctx, y, Chapter3_cameraY(runner) + 480 + halfH) > 0) {
        Runner_destroyInstance(runner, self, true);
        return true;
    }

    if (!RValue_toBool(Instance_getSelfVar(self, cache.init))) {
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.difficulty), 0) == 0)
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", (GMLReal)cache.sprEasy);
        Instance_setSelfVar(self, cache.init, RValue_makeInt32(1));
    }

    Instance_setSelfVar(self, cache.grazetimer, Chapter3_increment(Instance_getSelfVar(self, cache.grazetimer)));
    if (Chapter3_compare(ctx, GMLReal_fmod(Chapter3_getReal(self, cache.grazetimer), 4), 0) == 0)
        Chapter3_setReal(self, cache.grazed, 0);

    GMLReal con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, 0) == 0) {
        GMLReal growspeed = Chapter3_getReal(self, cache.growspeed);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale") + growspeed);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale") + growspeed);
    } else if (Chapter3_compare(ctx, con, 1) == 0) {
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_FRICTION, "friction", R(0.5));
        Instance_setSelfVar(self, cache.con, Chapter3_increment(Instance_getSelfVar(self, cache.con)));
    } else if (Chapter3_compare(ctx, con, 2) == 0) {
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_MASK_INDEX, "mask_index", (GMLReal)cache.sprMask);
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed"), 0) == 0) {
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_GRAVITY, "gravity", R(0.1));
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_GRAVITY_DIRECTION, "gravity_direction",
                Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction") - 180);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_FRICTION, "friction", 0);
        }
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 40) >= 0) {
            Chapter3_setReal(self, cache.timer, 0);
            Instance_setSelfVar(self, cache.con, Chapter3_increment(Instance_getSelfVar(self, cache.con)));
            if (RValue_toBool(Instance_getSelfVar(self, cache.playSound))) {
                RValue sound = RValue_makeInt32(cache.sndFirework);
                RValue result = NativeScript_call(ctx, &cache.sndPlay, "gml_Script_snd_play", &sound, 1);
                RValue_free(&result);
            }
        }
        Chapter3_setReal(self, cache.growstart, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
    } else if (Chapter3_compare(ctx, con, 3) == 0) {
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        GMLReal timer = Chapter3_getReal(self, cache.timer);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
            Chapter3_getReal(self, cache.growstart) + clamp01(timer / 2));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            Chapter3_getReal(self, cache.growstart) + clamp01(timer / 2));
        if (Chapter3_compare(ctx, timer, 3) == 0) spawnChildren(ctx, self);
        if (Chapter3_compare(ctx, timer, 4) >= 0) {
            GMLReal xscale, yscale;
            RValue sprite = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
            xscale = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width") + 16) /
                Chapter3_callReal(ctx, &cache.spriteGetWidth, &sprite, 1);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", xscale);
            yscale = (Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height") + 16) /
                Chapter3_callReal(ctx, &cache.spriteGetHeight, &sprite, 1);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", yscale);
            RValue id = NativeScript_call(ctx, &cache.afterimage, "gml_Script_scr_afterimage", nullptr, 0);
            Instance* aimage = Chapter3_resolveInstance(runner, id);
            RValue_free(&id);
            if (aimage != nullptr)
                Instance_setSelfVar(aimage, cache.fadeSpeed, scaleValue(Instance_getSelfVar(aimage, cache.fadeSpeed), 3, false));
            Runner_destroyInstance(runner, self, true);
        }
    }
    return true;
}

static bool pointingStarDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    GMLReal con = Chapter3_getReal(self, cache.con);
    RValue existsArg = RValue_makeBool(Chapter3_compare(ctx, con, 0) == 0);
    if (RValue_toBool(Chapter3_call(ctx, &cache.instanceExists, &existsArg, 1))) return true;

    GMLReal xscale, yscale;
    starScale(ctx, self, &xscale, &yscale);
    GMLReal timer = Chapter3_getReal(self, cache.timer);
    GMLReal color = mergeColor(ctx, 8421504, 255, clamp01(timer / 30));
    GMLReal alpha = (GMLReal_sin(timer * 3) + 1) * R(0.25);
    GMLReal x = builtinX(ctx, self);
    GMLReal y = builtinY(ctx, self);
    GMLReal difficulty = Chapter3_getReal(self, cache.difficulty);

    if (Chapter3_compare(ctx, con, 2) == 0 || Chapter3_compare(ctx, con, 3) == 0) {
        GMLReal a = 1;
        GMLReal length = 120;
        GMLReal prog = clamp01(timer / 30);
        if (Chapter3_compare(ctx, con, 2) == 0) {
            a = clamp01(prog - alpha);
            length = (50 * clamp01(prog - (GMLReal_fmod(timer, 2) * R(0.75)))) + 50;
        }
        GMLReal offset = 66;
        GMLReal sublength = Chapter3_compare(ctx, difficulty, 1) >= 0 ? length : length / 2;
        setBlendMode(ctx, 1);
        RValue beamColor = Chapter3_compare(ctx, difficulty, 2) >= 0 ? RValue_makeReal(color) : RValue_makeInt32(16777215);
        if (Chapter3_compare(ctx, difficulty, 2) == 0) {
            prog = easeIn4(ctx, clamp01(timer / 20));
            if (Chapter3_compare(ctx, con, 3) == 0) offset = 5;
            else offset = lerp3(ctx, RValue_makeInt32(66), RValue_makeInt32(5), prog);
            beamColor = RValue_makeReal(color);
            if (Chapter3_compare(ctx, timer, 30) >= 0) {
                prog = easeIn4(ctx, clamp01((timer - 30) / 10));
                length += (50 - (prog * 50));
            }
            GMLReal side = Chapter3_getReal(self, cache.side);
            drawBeam(ctx, x, y, RValue_makeReal(length), RValue_makeReal(90 + side), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(length), RValue_makeReal(-90 + side), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeReal(90 + side + offset), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeReal((90 + side) - offset), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeReal(-90 + side + offset), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeReal((-90 + side) - offset), beamColor, a);
        } else {
            drawBeam(ctx, x, y, RValue_makeReal(length), RValue_makeInt32(90), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeInt32(156), RValue_makeInt32(16777215), a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeInt32(24), RValue_makeInt32(16777215), a);
            drawBeam(ctx, x, y, RValue_makeReal(sublength), RValue_makeInt32(270), RValue_makeInt32(16777215), a);
            drawBeam(ctx, x, y, RValue_makeReal(length), RValue_makeInt32(336), beamColor, a);
            drawBeam(ctx, x, y, RValue_makeReal(length), RValue_makeInt32(204), beamColor, a);
        }
        setBlendMode(ctx, 0);
    }

    RValue sprite = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    GMLReal angle = builtinAngle(ctx, self);
    if (Chapter3_compare(ctx, con, 1) == 0 || Chapter3_compare(ctx, con, 2) == 0) {
        drawSprite(ctx, sprite, RValue_makeInt32(1), x, y, xscale + R(0.1), yscale + R(0.1), angle, RValue_makeInt32(16777215), RValue_makeReal(alpha));
        drawSprite(ctx, sprite, RValue_makeInt32(0), x, y, xscale, yscale, angle, RValue_makeReal(color), RValue_makeInt32(1));
    }
    if (Chapter3_compare(ctx, con, 3) == 0 || Chapter3_compare(ctx, con, 4) == 0) {
        drawSprite(ctx, sprite, RValue_makeInt32(2), x, y, xscale + R(0.1), yscale + R(0.1), angle, RValue_makeInt32(16777215),
            RValue_makeReal((GMLReal_sin(timer * 6) + 1) * R(0.25)));
        drawSprite(ctx, sprite, RValue_makeInt32(2), x, y, xscale, yscale, angle, RValue_makeInt32(16777215), RValue_makeInt32(1));
    }
    return true;
}

static bool pointingStarUser0(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    GMLReal xscale, yscale;
    starScale(ctx, self, &xscale, &yscale);
    RValue sprite = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    drawSprite(ctx, sprite, RValue_makeInt32(0), builtinX(ctx, self) - Chapter3_cameraX(ctx->runner),
        builtinY(ctx, self) - Chapter3_cameraY(ctx->runner), xscale, yscale, 0, RValue_makeInt32(16777215), RValue_makeInt32(1));
    return true;
}

static void addControllerDelay(VMContext* ctx, Instance* self, Instance* controller) {
    (void)ctx;
    RValue childDelay = Instance_getSelfVar(self, cache.delay);
    RValue controllerDelay = Instance_getSelfVar(controller, cache.delay);
    Instance_setSelfVar(self, cache.delay, childDelay.type == RVALUE_INT32 && controllerDelay.type == RVALUE_INT32
        ? RValue_makeInt32(childDelay.int32 + controllerDelay.int32)
        : RValue_makeReal(RValue_toReal(childDelay) + RValue_toReal(controllerDelay)));
    if (Chapter3_compare(ctx, Chapter3_getReal(controller, cache.subdelay), 4) == 0) {
        Chapter3_setReal(controller, cache.subdelay, 0);
        RValue value = Instance_getSelfVar(controller, cache.delay);
        Instance_setSelfVar(controller, cache.delay, value.type == RVALUE_INT32
            ? RValue_makeInt32(value.int32 + 5) : RValue_makeReal(RValue_toReal(value) + 5));
    } else {
        Instance_setSelfVar(controller, cache.subdelay, Chapter3_increment(Instance_getSelfVar(controller, cache.subdelay)));
        Instance_setSelfVar(controller, cache.delay, Chapter3_increment(Instance_getSelfVar(controller, cache.delay)));
    }
}

static bool starchildStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    bool roaring = Chapter3_instanceExists(ctx, RValue_makeReal((GMLReal)cache.roaring2Object));
    Instance* follower = Chapter3_resolveInstance(runner, RValue_makeReal((GMLReal)cache.followerObject));
    if (!roaring && follower == nullptr &&
        (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 0) > 0 || Chapter3_compare(ctx, Chapter3_getReal(self, cache.delay), 0) > 0))
        return false;
    *outResult = RValue_makeUndefined();

    if (!RValue_toBool(Instance_getSelfVar(self, cache.init))) {
        Instance_setSelfVar(self, cache.init, RValue_makeInt32(1));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.difficulty), 2) >= 0) {
            Chapter3_setReal(self, cache.delay, 25);
            Chapter3_with(ctx, self, cache.controllerObject, addControllerDelay);
        }
    }

    Chapter3_callVoid(ctx, &cache.eventInherited, nullptr, 0);
    if (Chapter3_instanceExists(ctx, RValue_makeReal((GMLReal)cache.roaring2Object))) return true;
    follower = Chapter3_resolveInstance(runner, RValue_makeReal((GMLReal)cache.followerObject));

    GMLReal con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, 2) <= 0 && Chapter3_compare(ctx, con, 3) <= 0) {
        GMLReal speed = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed");
        GMLReal minspeed = Chapter3_getReal(self, cache.minspeed);
        if (Chapter3_compare(ctx, speed, minspeed) > 0)
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", moveTowards(ctx, speed, minspeed, Chapter3_getReal(self, cache.deceleration)));
        if (Chapter3_compare(ctx, con, 0) == 0 && Chapter3_compare(ctx, Chapter3_getReal(self, cache.delay), 0) > 0) {
            Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
            if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), Chapter3_getReal(self, cache.delay)) >= 0) {
                RValue args[] = { RValue_makeInstanceRef((int32_t)self->instanceId), RValue_makeInt32(10) };
                RValue onscreen = NativeScript_call(ctx, &cache.onscreen, "gml_Script_scr_onscreen_tolerance", args, 2);
                bool visible = RValue_toBool(onscreen);
                RValue_free(&onscreen);
                if (!visible) Runner_destroyInstance(runner, self, true);
                Chapter3_setReal(self, cache.timer, 0);
                Chapter3_setReal(self, cache.con, 1);
            }
        }
    }

    con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, 1) >= 0 && Chapter3_compare(ctx, con, 3) <= 0 && follower != nullptr) {
        RValue dirArgs[] = { RValue_makeReal(builtinX(ctx, self)), RValue_makeReal(builtinY(ctx, self)),
            RValue_makeReal(builtinX(ctx, follower) + 10), RValue_makeReal(builtinY(ctx, follower) + 10) };
        Chapter3_setReal(self, cache.targetAngle, Chapter3_callReal(ctx, &cache.pointDirection, dirArgs, 4));
        if (Chapter3_compare(ctx, con, 2) >= 0 && RValue_toBool(Instance_getSelfVar(self, cache.tracking))) {
            GMLReal targetAngle = Chapter3_getReal(self, cache.targetAngle);
            GMLReal direction = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction");
            GMLReal difference = call2(ctx, &cache.angleDifference, targetAngle, direction);
            if (Chapter3_compare(ctx, GMLReal_fabs(difference), 90) < 0) {
                if (Chapter3_compare(ctx, con, 3) < 0) {
                    GMLReal diff = call2(ctx, &cache.angleDifference, targetAngle, direction);
                    GMLReal rotated = Chapter3_compare(ctx, GMLReal_fabs(diff), 2) > 0 ? direction + (sign(diff) * 2) : targetAngle;
                    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction", rotated);
                    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
                        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction"));
                } else if (Chapter3_compare(ctx, GMLReal_fabs(difference), 4) <= 0) {
                    Chapter3_setReal(self, cache.rotation, 0);
                } else if (Chapter3_compare(ctx, GMLReal_fabs(difference), 30) > 0) {
                    Chapter3_setReal(self, cache.rotation, sign(difference) * 2);
                } else {
                    Chapter3_setReal(self, cache.rotation, sign(difference));
                }
            } else if (Chapter3_compare(ctx, con, 3) >= 0) {
                Instance_setSelfVar(self, cache.tracking, RValue_makeInt32(0));
                Chapter3_setReal(self, cache.rotation, sign(Chapter3_getReal(self, cache.rotation)));
            }
        } else {
            GMLReal rotation = Chapter3_getReal(self, cache.rotation);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction",
                Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction") + rotation);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", builtinAngle(ctx, self) + rotation);
        }
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 1) == 0) {
        GMLReal direction = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction");
        GMLReal diff = call2(ctx, &cache.angleDifference, Chapter3_getReal(self, cache.targetAngle), direction);
        GMLReal lerped = lerp3(ctx, RValue_makeInt32(0), RValue_makeReal(diff), Chapter3_getReal(self, cache.timer) / 10);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", direction + lerped);
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 10) >= 0) {
            Chapter3_setReal(self, cache.timer, 0);
            Chapter3_setReal(self, cache.con, 2);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction", builtinAngle(ctx, self));
            Instance_setSelfVar(self, cache.tracking, RValue_makeInt32(1));
        }
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.xscaleStart), 0) == 0)
            Chapter3_setReal(self, cache.xscaleStart, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.yscaleStart), 0) == 0)
            Chapter3_setReal(self, cache.yscaleStart, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale"));
        GMLReal wave = GMLReal_cos((Chapter3_getReal(self, cache.timer) / 5) * R(3.14159265358979323846));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", Chapter3_getReal(self, cache.yscaleStart) * wave);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend", mergeColor(ctx, 16777215, 0, wave));
        RValue outlineArgs[] = { RValue_makeInt32(0), RValue_makeInt32(255), RValue_makeReal(wave) };
        RValue outline = Chapter3_call(ctx, &cache.mergeColor, outlineArgs, 3);
        Instance_setSelfVar(self, cache.outline, outline);
        RValue_free(&outline);
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 2) == 0) {
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 10) >= 0) {
            Chapter3_setReal(self, cache.timer, 0);
            Chapter3_setReal(self, cache.con, 3);
        }
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 1) >= 0 &&
        Chapter3_compare(ctx, Chapter3_getReal(self, cache.ease), 40) < 0) {
        GMLReal speed = (1 - (Chapter3_getReal(self, cache.ease) / 40)) * 2;
        GMLReal targetAngle = Chapter3_getReal(self, cache.targetAngle);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_X, "x", builtinX(ctx, self) - call2(ctx, &cache.lengthdirX, speed, targetAngle));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_Y, "y", builtinY(ctx, self) - call2(ctx, &cache.lengthdirY, speed, targetAngle));
        Instance_setSelfVar(self, cache.ease, Chapter3_increment(Instance_getSelfVar(self, cache.ease)));
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 3) == 0) {
        GMLReal speed = moveTowards(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed"), 25, R(0.5));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", speed);
        speed = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed");
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", Chapter3_getReal(self, cache.xscaleStart) + (speed / 60));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", Chapter3_getReal(self, cache.yscaleStart) - (speed / 90));
        RValue object = RValue_makeInt32(cache.afterimageBlendObject);
        RValue first = NativeScript_call(ctx, &cache.customAfterimage, "gml_Script_scr_custom_afterimage", &object, 1);
        RValue_free(&first);
        RValue id = NativeScript_call(ctx, &cache.customAfterimage, "gml_Script_scr_custom_afterimage", &object, 1);
        Instance* afbm = Chapter3_resolveInstance(runner, id);
        RValue_free(&id);
        if (afbm != nullptr) {
            Chapter3_setBuiltin(ctx, afbm, BUILTIN_VAR_IMAGE_BLEND, "image_blend", 255);
            Chapter3_setReal(afbm, cache.fadeSpeed, R(0.25));
            Chapter3_setBuiltin(ctx, afbm, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", R(0.5));
            Chapter3_setBuiltin(ctx, afbm, BUILTIN_VAR_DIRECTION, "direction", builtinAngle(ctx, self));
            Chapter3_setBuiltin(ctx, afbm, BUILTIN_VAR_SPEED, "speed", 1);
        }
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 4) == 0) {
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPEED, "speed", 0);
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 4) >= 0) Runner_destroyInstance(runner, self, true);
    }
    return true;
}

static bool starchildDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    GMLReal x = builtinX(ctx, self);
    GMLReal y = builtinY(ctx, self);
    GMLReal xscale = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale");
    GMLReal yscale = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale");
    GMLReal angle = builtinAngle(ctx, self);
    GMLReal con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, 4) == 0) {
        GMLReal scale = (yscale + xscale) / 2;
        drawSprite(ctx, RValue_makeInt32(cache.sprExplosion), Instance_getSelfVar(self, cache.timer), x, y, scale, scale,
            angle - 90, RValue_makeInt32(255), RValue_makeInt32(1));
        return true;
    }

    setBlendMode(ctx, 1);
    Instance_setSelfVar(self, cache.drawtimer, Chapter3_increment(Instance_getSelfVar(self, cache.drawtimer)));
    GMLReal drawtimer = Chapter3_getReal(self, cache.drawtimer);
    GMLReal loop = GMLReal_fmod(drawtimer, 4);
    if (0 > drawtimer) loop += 4;
    if (Chapter3_compare(ctx, loop, 2) > 0) loop = 4 - loop;
    GMLReal glow = loop / 4;
    RValue glowColor = RValue_makeInt32(16777215);
    if (Chapter3_compare(ctx, con, 1) >= 0) {
        if (Chapter3_compare(ctx, con, 1) > 0) glowColor = RValue_makeInt32(255);
        else glowColor = RValue_makeReal(mergeColor(ctx, 16777215, 255, Chapter3_getReal(self, cache.timer) / 10));
    }

    GMLReal alpha = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha");
    GMLReal outlineAlpha = glow * alpha;
    RValue fogOn[] = { RValue_makeBool(true), glowColor, RValue_makeInt32(0), RValue_makeInt32(0) };
    Chapter3_callVoid(ctx, &cache.gpuSetFog, fogOn, 4);
    GMLReal xdirA = xscale, xdirB = 0, ydirA = 0, ydirB = xscale;
    if (Chapter3_compare(ctx, GMLReal_fmod(angle, 90), 0) != 0) {
        xdirA = call2(ctx, &cache.lengthdirX, xscale, angle);
        xdirB = call2(ctx, &cache.lengthdirX, xscale, angle + 90);
        ydirA = call2(ctx, &cache.lengthdirY, xscale, angle + 90);
        ydirB = call2(ctx, &cache.lengthdirY, xscale, angle);
    }
    RValue sprite = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    RValue subimg = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index"));
    RValue white = RValue_makeInt32(16777215);
    RValue outlineAlphaValue = RValue_makeReal(alpha * outlineAlpha);
    drawSprite(ctx, sprite, subimg, x + xdirA, y + ydirA, xscale, yscale, angle, white, outlineAlphaValue);
    drawSprite(ctx, sprite, subimg, x - xdirA, y - ydirA, xscale, yscale, angle, white, outlineAlphaValue);
    drawSprite(ctx, sprite, subimg, x + xdirB, y + ydirB, xscale, yscale, angle, white, outlineAlphaValue);
    drawSprite(ctx, sprite, subimg, x - xdirB, y - ydirB, xscale, yscale, angle, white, outlineAlphaValue);
    RValue fogOff[] = { RValue_makeBool(false), RValue_makeInt32(16777215), RValue_makeInt32(0), RValue_makeInt32(0) };
    Chapter3_callVoid(ctx, &cache.gpuSetFog, fogOff, 4);

    if (Chapter3_compare(ctx, con, 0) > 0)
        drawSprite(ctx, sprite, RValue_makeInt32(1), x, y, xscale, yscale, angle, Instance_getSelfVar(self, cache.outline), RValue_makeReal(alpha));
    setBlendMode(ctx, 0);
    drawSprite(ctx, sprite, RValue_makeInt32(0), x, y, xscale, yscale, angle,
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend")), RValue_makeReal(alpha));

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.difficulty), 2) < 0) {
        GMLReal lifetime = Chapter3_getReal(self, cache.lifetime);
        GMLReal from = lifetime - 15;
        GMLReal inv = Chapter3_compare(ctx, lifetime, from) == 0 ? 0 : (drawtimer - from) / (lifetime - from);
        GMLReal remapped = lerp3(ctx, RValue_makeInt32(1), RValue_makeInt32(0), inv);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", clamp01(remapped));
        GMLReal newAlpha = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha");
        if (Chapter3_compare(ctx, newAlpha, 1) < 0) Instance_setSelfVar(self, cache.active, RValue_makeInt32(0));
        if (Chapter3_compare(ctx, newAlpha, 0) == 0) Runner_destroyInstance(ctx->runner, self, true);
    }
    return true;
}

void NativeChapter3PointingStar_register(VMContext* ctx) {
    cache.starchildObject = Chapter3_findObject(ctx, "obj_knight_pointing_starchild");
    cache.roaring2Object = Chapter3_findObject(ctx, "obj_knight_roaring2");
    cache.followerObject = Chapter3_findObject(ctx, "obj_heart_follower");
    cache.controllerObject = Chapter3_findObject(ctx, "obj_dbulletcontroller");
    cache.afterimageBlendObject = Chapter3_findObject(ctx, "obj_afterimage_blend");
    cache.sprEasy = Chapter3_findSprite(ctx, "spr_knight_bullet_star_easy");
    cache.sprMask = Chapter3_findSprite(ctx, "spr_knight_bullet_star_mask");
    cache.sprTrail = Chapter3_findSprite(ctx, "spr_knight_starchild_trail");
    cache.sprExplosion = Chapter3_findSprite(ctx, "spr_thrash_missile_explosion");
    cache.sndFirework = Chapter3_findSound(ctx, "snd_explosion_firework");
    if (cache.starchildObject < 0 || cache.roaring2Object < 0 || cache.followerObject < 0 || cache.controllerObject < 0 ||
        cache.afterimageBlendObject < 0 || cache.sprEasy < 0 || cache.sprMask < 0 || cache.sprTrail < 0 ||
        cache.sprExplosion < 0 || cache.sndFirework < 0) return;
    if (!Chapter3_bindBuiltin(ctx, &cache.instanceExists, "instance_exists") ||
        !Chapter3_bindBuiltin(ctx, &cache.spriteGetWidth, "sprite_get_width") ||
        !Chapter3_bindBuiltin(ctx, &cache.spriteGetHeight, "sprite_get_height") ||
        !Chapter3_bindBuiltin(ctx, &cache.mergeColor, "merge_color") ||
        !Chapter3_bindBuiltin(ctx, &cache.lerp, "lerp") ||
        !Chapter3_bindBuiltin(ctx, &cache.angleDifference, "angle_difference") ||
        !Chapter3_bindBuiltin(ctx, &cache.pointDirection, "point_direction") ||
        !Chapter3_bindBuiltin(ctx, &cache.lengthdirX, "lengthdir_x") ||
        !Chapter3_bindBuiltin(ctx, &cache.lengthdirY, "lengthdir_y") ||
        !Chapter3_bindBuiltin(ctx, &cache.gpuSetFog, "gpu_set_fog") ||
        !Chapter3_bindBuiltin(ctx, &cache.drawSpriteExt, "draw_sprite_ext") ||
        !Chapter3_bindBuiltin(ctx, &cache.eventInherited, "event_inherited")) return;
    cache.blendMode = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.beam = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.easeIn = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.sndPlay = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.childBullet = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.afterimage = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.customAfterimage = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.onscreen = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (!NativeScript_canCall(ctx, &cache.blendMode, "gml_Script_draw_set_blend_mode") ||
        !NativeScript_canCall(ctx, &cache.beam, "gml_Script_scr_draw_beam_color") ||
        !NativeScript_canCall(ctx, &cache.easeIn, "gml_Script_scr_ease_in") ||
        !NativeScript_canCall(ctx, &cache.sndPlay, "gml_Script_snd_play") ||
        !NativeScript_canCall(ctx, &cache.childBullet, "gml_Script_scr_childbullet") ||
        !NativeScript_canCall(ctx, &cache.afterimage, "gml_Script_scr_afterimage") ||
        !NativeScript_canCall(ctx, &cache.customAfterimage, "gml_Script_scr_custom_afterimage") ||
        !NativeScript_canCall(ctx, &cache.onscreen, "gml_Script_scr_onscreen_tolerance")) return;

    cache.init = NativeScript_getSelfVarID(ctx, "init");
    cache.difficulty = NativeScript_getSelfVarID(ctx, "difficulty");
    cache.grazetimer = NativeScript_getSelfVarID(ctx, "grazetimer");
    cache.grazed = NativeScript_getSelfVarID(ctx, "grazed");
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.growspeed = NativeScript_getSelfVarID(ctx, "growspeed");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.playSound = NativeScript_getSelfVarID(ctx, "playSound");
    cache.growstart = NativeScript_getSelfVarID(ctx, "growstart");
    cache.side = NativeScript_getSelfVarID(ctx, "side");
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.loopIndex = NativeScript_getSelfVarID(ctx, "i");
    cache.child = NativeScript_getSelfVarID(ctx, "d");
    cache.lifetime = NativeScript_getSelfVarID(ctx, "lifetime");
    cache.deceleration = NativeScript_getSelfVarID(ctx, "deceleration");
    cache.minspeed = NativeScript_getSelfVarID(ctx, "minspeed");
    cache.fadeSpeed = NativeScript_getSelfVarID(ctx, "fadeSpeed");
    cache.delay = NativeScript_getSelfVarID(ctx, "delay");
    cache.subdelay = NativeScript_getSelfVarID(ctx, "subdelay");
    cache.tracking = NativeScript_getSelfVarID(ctx, "tracking");
    cache.targetAngle = NativeScript_getSelfVarID(ctx, "target_angle");
    cache.rotation = NativeScript_getSelfVarID(ctx, "rotation");
    cache.xscaleStart = NativeScript_getSelfVarID(ctx, "xscale_start");
    cache.yscaleStart = NativeScript_getSelfVarID(ctx, "yscale_start");
    cache.outline = NativeScript_getSelfVarID(ctx, "outline");
    cache.ease = NativeScript_getSelfVarID(ctx, "ease");
    cache.drawtimer = NativeScript_getSelfVarID(ctx, "drawtimer");

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_pointing_star_Step_0", pointingStarStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_pointing_star_Draw_0", pointingStarDraw);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_pointing_star_Other_10", pointingStarUser0);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_pointing_starchild_Step_0", starchildStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_pointing_starchild_Draw_0", starchildDraw);
}
