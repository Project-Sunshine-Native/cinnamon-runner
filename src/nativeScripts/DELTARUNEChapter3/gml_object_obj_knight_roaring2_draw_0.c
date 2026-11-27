#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

#define R(value) ((GMLReal)(value))

static struct {
    int32_t mySurface, ballSurface, stop, doFakeScreen;
    int32_t fakeX, fakeY, ballCounter, ballSpeed, loopIndex, starFlicker, hsv, hsvSwitch;
    int32_t intensity, ballDarkness, lineTimer, r, g, b, knightSprite, knightSpriteImage;
    int32_t knightSpriteSpeed, intensify, bobbleCount, bobbleAmp, fakeAlpha, darkness;
    int32_t con, timer, active, globalTime;
    int32_t circleObject, particleObject, starObject, starchildObject, afterimageObject;
    int32_t afterimageGrowObject, heartObject;
    int32_t sprFlow, sprGrate, sprMarkerGradient, sprMarker;
    Chapter3Builtin surfaceExists, surfaceCreate, surfaceSetTarget, surfaceResetTarget, drawSelf;
    Chapter3Builtin drawClearAlpha, drawSpriteTiled, gpuSetBlendmode, gpuSetBlendmodeExt, drawCircleColor;
    Chapter3Builtin drawSpriteExt, gpuSetColorwriteenable, surfaceGetWidth, surfaceGetHeight;
    Chapter3Builtin makeColorHsv, makeColorRgb, drawSurfacePartExt, drawSurface, drawSurfaceExt;
    Chapter3Builtin lengthdirX, lengthdirY, spriteGetBboxLeft, spriteGetBboxTop, spriteGetHeight;
    Chapter3Builtin drawSpritePartExt, clamp;
    NativeCallBinding setBlendMode, pingpong, outline, remap, fillRectangle;
} cache;

static RValue selfVar(Instance* self, int32_t varId) {
    return Instance_getSelfVar(self, varId);
}

static GMLReal selfReal(Instance* self, int32_t varId) {
    return Chapter3_getReal(self, varId);
}

static void ensureSurface(VMContext* ctx, Instance* self, int32_t varId) {
    RValue surface = selfVar(self, varId);
    if (RValue_toBool(Chapter3_call(ctx, &cache.surfaceExists, &surface, 1))) return;
    // yo if this works LOL
    RValue size[] = { RValue_makeReal(Chapter3_cameraWidth(ctx->runner)), RValue_makeReal(Chapter3_cameraHeight(ctx->runner)) };
    RValue created = Chapter3_call(ctx, &cache.surfaceCreate, size, 2);
    Instance_setSelfVar(self, varId, created);
    RValue_free(&created);
}

static void setTarget(VMContext* ctx, Instance* self, int32_t varId) {
    RValue surface = selfVar(self, varId);
    Chapter3_callVoid(ctx, &cache.surfaceSetTarget, &surface, 1);
}

static void resetTarget(VMContext* ctx) {
    Chapter3_callVoid(ctx, &cache.surfaceResetTarget, nullptr, 0);
}

static void clearAlpha(VMContext* ctx) {
    RValue args[] = { RValue_makeInt32(0), RValue_makeInt32(0) };
    Chapter3_callVoid(ctx, &cache.drawClearAlpha, args, 2);
}

static void blendmode(VMContext* ctx, int32_t mode) {
    RValue arg = RValue_makeInt32(mode);
    Chapter3_callVoid(ctx, &cache.gpuSetBlendmode, &arg, 1);
}

static void colorWrite(VMContext* ctx, bool alpha) {
    RValue args[] = { RValue_makeInt32(1), RValue_makeInt32(1), RValue_makeInt32(1), RValue_makeInt32(alpha ? 1 : 0) };
    Chapter3_callVoid(ctx, &cache.gpuSetColorwriteenable, args, 4);
}

static void drawSpriteExt(VMContext* ctx, RValue sprite, RValue subimg, GMLReal x, GMLReal y, RValue xscale,
    RValue yscale, RValue angle, RValue color, RValue alpha) {
    RValue args[] = { sprite, subimg, RValue_makeReal(x), RValue_makeReal(y), xscale, yscale, angle, color, alpha };
    Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);
}

static RValue builtinValue(VMContext* ctx, Instance* inst, int16_t id, const char* name) {
    return VMBuiltins_getVariable(ctx, inst, id, name, -1);
}

static void drawInstanceScreen(VMContext* ctx, Instance* inst, bool useBlend) {
    RValue sprite = builtinValue(ctx, inst, BUILTIN_VAR_SPRITE_INDEX, "sprite_index");
    RValue subimg = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_INDEX, "image_index");
    RValue xscale = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale");
    RValue yscale = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale");
    RValue angle = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_ANGLE, "image_angle");
    RValue blend = useBlend ? builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_BLEND, "image_blend") : RValue_makeInt32(16777215);
    RValue alpha = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha");
    GMLReal x = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_X, "x") - Chapter3_cameraX(ctx->runner);
    GMLReal y = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_Y, "y") - Chapter3_cameraY(ctx->runner);
    drawSpriteExt(ctx, sprite, subimg, x, y, xscale, yscale, angle, blend, alpha);
    RValue_free(&sprite);
    RValue_free(&subimg);
    RValue_free(&xscale);
    RValue_free(&yscale);
    RValue_free(&angle);
    RValue_free(&blend);
    RValue_free(&alpha);
}

static void userEvent(VMContext* ctx, Instance* self, Instance* inst, int32_t subevent) {
    Runner_executeEventFromObject(ctx->runner, inst, self, inst->objectIndex, EVENT_OTHER, OTHER_USER0 + subevent);
}

static void circleBody(VMContext* ctx, Instance* self, Instance* inst) {
    userEvent(ctx, self, inst, 1);
}

static void particleBody(VMContext* ctx, Instance* self, Instance* inst) {
    (void)self;
    drawInstanceScreen(ctx, inst, true);
}

static void starEvent(VMContext* ctx, Instance* self, Instance* inst) {
    userEvent(ctx, self, inst, Chapter3_compare(ctx, Chapter3_getReal(inst, cache.con), 0) == 0 ? 0 : 1);
}

static void starPass1(VMContext* ctx, Instance* self, Instance* inst) {
    if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_BLEND, "image_blend"), 16777215) == 0) return;
    starEvent(ctx, self, inst);
}

static void starPass2(VMContext* ctx, Instance* self, Instance* inst) {
    if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_BLEND, "image_blend"), 4210752) == 0) return;
    starEvent(ctx, self, inst);
}

static void starPass3(VMContext* ctx, Instance* self, Instance* inst) {
    bool skipBlend = Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_BLEND, "image_blend"), 4210752) == 0 ||
        Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"), 1) > 0;
    if (skipBlend && Chapter3_compare(ctx, Chapter3_getReal(inst, cache.con), 1) < 0) return;
    starEvent(ctx, self, inst);
}

static void setBlendMode(VMContext* ctx, int32_t mode) {
    RValue arg = RValue_makeInt32(mode);
    RValue result = NativeScript_call(ctx, &cache.setBlendMode, "gml_Script_draw_set_blend_mode", &arg, 1);
    RValue_free(&result);
}

static void starchildBody(VMContext* ctx, Instance* self, Instance* inst) {
    (void)self;
    setBlendMode(ctx, 1);
    RValue pingArgs[] = { selfVar(inst, cache.timer), RValue_makeInt32(2) };
    RValue ping = NativeScript_call(ctx, &cache.pingpong, "gml_Script_scr_pingpong", pingArgs, 2);
    GMLReal glow = RValue_toReal(ping) / R(4);
    RValue_free(&ping);

    RValue sprite = builtinValue(ctx, inst, BUILTIN_VAR_SPRITE_INDEX, "sprite_index");
    RValue subimg = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_INDEX, "image_index");
    RValue xscale = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale");
    RValue yscale = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale");
    RValue angle = builtinValue(ctx, inst, BUILTIN_VAR_IMAGE_ANGLE, "image_angle");
    GMLReal alpha = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha");
    GMLReal x = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_X, "x") - Chapter3_cameraX(ctx->runner);
    GMLReal y = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_Y, "y") - Chapter3_cameraY(ctx->runner);
    RValue outlineArgs[] = {
        sprite, subimg, RValue_makeReal(x), RValue_makeReal(y), xscale, yscale, angle,
        RValue_makeInt32(16777215), RValue_makeReal(glow * alpha), xscale
    };
    RValue result = NativeScript_call(ctx, &cache.outline, "gml_Script_scr_draw_outline_ext", outlineArgs, 10);
    RValue_free(&result);

    GMLReal drawX = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_X, "x") - Chapter3_cameraX(ctx->runner);
    GMLReal drawY = Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_Y, "y") - Chapter3_cameraY(ctx->runner);
    drawSpriteExt(ctx, sprite, subimg, drawX, drawY, xscale, yscale, angle, RValue_makeInt32(16777215),
        RValue_makeReal(Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha")));
    RValue_free(&sprite);
    RValue_free(&subimg);
    RValue_free(&xscale);
    RValue_free(&yscale);
    RValue_free(&angle);
    setBlendMode(ctx, 0);

    RValue remapArgs[] = { RValue_makeInt32(45), RValue_makeInt32(60), RValue_makeInt32(1), RValue_makeInt32(0), selfVar(inst, cache.timer) };
    RValue remapped = NativeScript_call(ctx, &cache.remap, "gml_Script_remap", remapArgs, 5);
    RValue clampArgs[] = { remapped, RValue_makeInt32(0), RValue_makeInt32(1) };
    GMLReal newAlpha = Chapter3_callReal(ctx, &cache.clamp, clampArgs, 3);
    RValue_free(&remapped);
    Chapter3_setBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", newAlpha);
    if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"), 1) < 0)
        Instance_setSelfVar(inst, cache.active, RValue_makeInt32(0));
    if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"), 0) == 0)
        Runner_destroyInstance(ctx->runner, inst, true);
}

static void whiteBody(VMContext* ctx, Instance* self, Instance* inst) {
    (void)self;
    drawInstanceScreen(ctx, inst, false);
}

static void heartBody(VMContext* ctx, Instance* self, Instance* inst) {
    (void)self;
    Chapter3_callVoidAs(ctx, inst, &cache.drawSelf, nullptr, 0);
}

static GMLReal approach(VMContext* ctx, GMLReal value, GMLReal target, GMLReal amount) {
    if (Chapter3_compare(ctx, value, target) < 0) {
        value += amount;
        if (Chapter3_compare(ctx, value, target) > 0) return target;
    } else {
        value -= amount;
        if (Chapter3_compare(ctx, value, target) < 0) return target;
    }
    return value;
}

static void drawKnightRows(VMContext* ctx, Instance* self, GMLReal bboxLeft, GMLReal bboxTop, bool trail) {
    RValue knightSprite = selfVar(self, cache.knightSprite);
    for (int32_t a = 0;; a++) {
        Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal((GMLReal)a));
        RValue heightArg = selfVar(self, cache.knightSprite);
        if (Chapter3_compare(ctx, R(a), Chapter3_callReal(ctx, &cache.spriteGetHeight, &heightArg, 1)) >= 0) break;
        GMLReal globalTime = Chapter3_getReal(ctx->globalScopeInstance, cache.globalTime);
        GMLReal intensify = selfReal(self, cache.intensify);
        GMLReal offset;
        GMLReal alpha;
        if (trail) {
            GMLReal wave = GMLReal_sin((R(a) + (globalTime * R(4))) * R(0.15)) * (intensify - R(1.5)) * R(8);
            offset = (GMLReal_fmod(R(a), R(2)) == 0) ? (selfReal(self, cache.fakeX) - R(70)) + wave
                : selfReal(self, cache.fakeX) - R(70) - wave;
            alpha = selfReal(self, cache.fakeAlpha) * R(0.75);
        } else {
            offset = (selfReal(self, cache.fakeX) - R(70)) +
                (GMLReal_sin((R(a) + (globalTime * R(4))) * R(0.2)) * intensify * R(0.3));
            alpha = selfReal(self, cache.fakeAlpha);
        }
        GMLReal y = (selfReal(self, cache.fakeY) + (R(a) * R(2)) +
            (GMLReal_sin(selfReal(self, cache.bobbleCount) * R(0.1)) * selfReal(self, cache.bobbleAmp))) -
            R(10) - (bboxTop * R(2));
        RValue blend = builtinValue(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend");
        RValue args[] = {
            knightSprite, selfVar(self, cache.knightSpriteImage), RValue_makeReal(bboxLeft), RValue_makeInt32(a),
            RValue_makeInt32(70), RValue_makeInt32(1), RValue_makeReal(offset), RValue_makeReal(y),
            RValue_makeInt32(2), RValue_makeInt32(2), blend, RValue_makeReal(alpha)
        };
        Chapter3_callVoid(ctx, &cache.drawSpritePartExt, args, 12);
        RValue_free(&blend);
    }
}

static bool roaring2Draw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    if (RValue_toBool(selfVar(self, cache.doFakeScreen))) return false;
    *outResult = RValue_makeUndefined();
    if (RValue_toBool(selfVar(self, cache.stop))) {
        Chapter3_callVoid(ctx, &cache.drawSelf, nullptr, 0);
        return true;
    }

    ensureSurface(ctx, self, cache.mySurface);
    ensureSurface(ctx, self, cache.ballSurface);
    Chapter3_callVoid(ctx, &cache.drawSelf, nullptr, 0);

    setTarget(ctx, self, cache.ballSurface);
    clearAlpha(ctx);
    for (int32_t i = 0; 5 > i; i++) {
        if (i == 1) blendmode(ctx, 1);
        GMLReal globalTime = Chapter3_getReal(ctx->globalScopeInstance, cache.globalTime);
        RValue args[] = {
            RValue_makeInt32(cache.sprFlow), RValue_makeInt32(0),
            RValue_makeReal(selfReal(self, cache.fakeX) + (globalTime * R(2))), selfVar(self, cache.fakeY)
        };
        Chapter3_callVoid(ctx, &cache.drawSpriteTiled, args, 4);
    }
    blendmode(ctx, 0);

    GMLReal ballCounter = selfReal(self, cache.ballCounter) + selfReal(self, cache.ballSpeed);
    Chapter3_setReal(self, cache.ballCounter, ballCounter);
    if (Chapter3_compare(ctx, ballCounter, 0) < 0) Chapter3_setReal(self, cache.ballCounter, ballCounter += R(1800));
    if (Chapter3_compare(ctx, ballCounter, 1800) > 0) Chapter3_setReal(self, cache.ballCounter, ballCounter -= R(1800));

    RValue extArgs[] = { RValue_makeInt32(1), RValue_makeInt32(3) };
    Chapter3_callVoid(ctx, &cache.gpuSetBlendmodeExt, extArgs, 2);
    for (int32_t a = 0; 6 > a; a++) {
        Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal((GMLReal)a));
        GMLReal radius = R(1800) - GMLReal_fmod(selfReal(self, cache.ballCounter) + (R(300) * R(a)), R(1800));
        RValue args[] = {
            selfVar(self, cache.fakeX), RValue_makeReal(selfReal(self, cache.fakeY) + R(57)), RValue_makeReal(radius),
            RValue_makeInt32(16777215), RValue_makeInt32(5855577), RValue_makeBool(false)
        };
        Chapter3_callVoid(ctx, &cache.drawCircleColor, args, 6);
    }
    Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal(6));
    RValue lastCircle[] = {
        selfVar(self, cache.fakeX), RValue_makeReal(selfReal(self, cache.fakeY) + R(57)), RValue_makeInt32(640),
        RValue_makeInt32(16777215), RValue_makeInt32(0), RValue_makeBool(false)
    };
    Chapter3_callVoid(ctx, &cache.drawCircleColor, lastCircle, 6);
    blendmode(ctx, 0);
    resetTarget(ctx);

    setTarget(ctx, self, cache.mySurface);
    clearAlpha(ctx);
    RValue fillArgs[] = {
        RValue_makeInt32(0), RValue_makeInt32(0), RValue_makeReal(Chapter3_cameraWidth(runner)),
        RValue_makeReal(Chapter3_cameraHeight(runner)), RValue_makeInt32(0), RValue_makeInt32(0),
        RValue_makeInt32(0), RValue_makeInt32(0), RValue_makeBool(false)
    };
    RValue fillResult = NativeScript_call(ctx, &cache.fillRectangle, "gml_Script_ossafe_fill_rectangle_color", fillArgs, 9);
    RValue_free(&fillResult);

    if (!RValue_toBool(selfVar(self, cache.hsvSwitch)))
        Instance_setSelfVar(self, cache.hsv, Chapter3_increment(selfVar(self, cache.hsv)));
    else {
        RValue hsv = selfVar(self, cache.hsv);
        Instance_setSelfVar(self, cache.hsv, hsv.type == RVALUE_INT32 ? RValue_makeInt32(hsv.int32 - 1)
            : RValue_makeReal(RValue_toReal(hsv) - R(1)));
    }
    if (Chapter3_compare(ctx, selfReal(self, cache.hsv), 288) >= 0) Instance_setSelfVar(self, cache.hsvSwitch, RValue_makeInt32(1));
    if (Chapter3_compare(ctx, selfReal(self, cache.hsv), 128) <= 0) Instance_setSelfVar(self, cache.hsvSwitch, RValue_makeInt32(0));

    blendmode(ctx, 1);
    for (int32_t a = 0;; a++) {
        Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal((GMLReal)a));
        RValue ball = selfVar(self, cache.ballSurface);
        if (Chapter3_compare(ctx, R(a), Chapter3_callReal(ctx, &cache.surfaceGetHeight, &ball, 1)) >= 0) break;
        RValue hsvArgs[] = { RValue_makeReal(GMLReal_fmod(selfReal(self, cache.hsv), R(255))), RValue_makeInt32(255), RValue_makeInt32(255) };
        RValue color = Chapter3_call(ctx, &cache.makeColorHsv, hsvArgs, 3);
        GMLReal globalTime = Chapter3_getReal(ctx->globalScopeInstance, cache.globalTime);
        GMLReal intensity = selfReal(self, cache.intensity);
        GMLReal wave = (GMLReal_sin((R(a) + globalTime) * R(0.1)) * R(4) * intensity) +
            (GMLReal_sin((R(a) + globalTime) * R(0.35)) * R(0.5) * intensity);
        RValue partArgs[] = {
            ball, RValue_makeInt32(0), RValue_makeInt32(a), RValue_makeReal(Chapter3_callReal(ctx, &cache.surfaceGetWidth, &ball, 1)),
            RValue_makeInt32(1), RValue_makeReal(wave), RValue_makeInt32(a), RValue_makeInt32(1), RValue_makeInt32(1),
            color, selfVar(self, cache.ballDarkness)
        };
        Chapter3_callVoid(ctx, &cache.drawSurfacePartExt, partArgs, 11);
        RValue_free(&color);
    }
    blendmode(ctx, 0);
    resetTarget(ctx);

    setTarget(ctx, self, cache.ballSurface);
    clearAlpha(ctx);
    Chapter3_with(ctx, self, cache.circleObject, circleBody);
    Chapter3_with(ctx, self, cache.particleObject, particleBody);
    Chapter3_with(ctx, self, cache.starObject, starPass1);
    Chapter3_with(ctx, self, cache.starObject, starPass2);
    colorWrite(ctx, false);
    drawSpriteExt(ctx, RValue_makeInt32(cache.sprGrate), RValue_makeInt32(0), 0, selfReal(self, cache.starFlicker),
        RValue_makeInt32(2), RValue_makeInt32(2), RValue_makeInt32(0), RValue_makeInt32(0), RValue_makeInt32(1));
    Chapter3_setReal(self, cache.starFlicker, R(2) - selfReal(self, cache.starFlicker));
    colorWrite(ctx, true);
    Chapter3_with(ctx, self, cache.starObject, starPass3);
    Chapter3_with(ctx, self, cache.starchildObject, starchildBody);
    Chapter3_with(ctx, self, cache.afterimageObject, whiteBody);
    resetTarget(ctx);

    setTarget(ctx, self, cache.mySurface);
    blendmode(ctx, 1);
    RValue starArgs[] = { selfVar(self, cache.ballSurface), RValue_makeInt32(0), RValue_makeInt32(0) };
    Chapter3_callVoid(ctx, &cache.drawSurface, starArgs, 3);
    blendmode(ctx, 0);
    Chapter3_with(ctx, self, cache.afterimageGrowObject, whiteBody);

    if (Chapter3_compare(ctx, selfReal(self, cache.lineTimer), -1) > 0) {
        colorWrite(ctx, false);
        RValue rgbArgs[] = { selfVar(self, cache.r), selfVar(self, cache.g), selfVar(self, cache.b) };
        RValue color = Chapter3_call(ctx, &cache.makeColorRgb, rgbArgs, 3);
        RValue lengthArgs[] = { RValue_makeInt32(280), RValue_makeInt32(-63) };
        GMLReal lineX = (Chapter3_cameraWidth(runner) * R(0.5)) - Chapter3_callReal(ctx, &cache.lengthdirX, lengthArgs, 2);
        GMLReal lineY = (Chapter3_cameraHeight(runner) * R(0.5)) - Chapter3_callReal(ctx, &cache.lengthdirY, lengthArgs, 2);
        GMLReal lineTimer = selfReal(self, cache.lineTimer);
        GMLReal capped = Chapter3_compare(ctx, lineTimer, 16) < 0 ? lineTimer : R(16);
        GMLReal yscale = R(4) + (R(8) * (R(1) - (capped / R(16))));
        drawSpriteExt(ctx, RValue_makeInt32(cache.sprMarkerGradient), RValue_makeInt32(0), lineX, lineY,
            RValue_makeReal(lineTimer * R(1)), RValue_makeReal(yscale), RValue_makeInt32(-63), color, RValue_makeInt32(1));
        lineX = (Chapter3_cameraWidth(runner) * R(0.5)) - Chapter3_callReal(ctx, &cache.lengthdirX, lengthArgs, 2);
        lineY = (Chapter3_cameraHeight(runner) * R(0.5)) - Chapter3_callReal(ctx, &cache.lengthdirY, lengthArgs, 2);
        drawSpriteExt(ctx, RValue_makeInt32(cache.sprMarker), RValue_makeInt32(0), lineX, lineY,
            RValue_makeReal(lineTimer * R(1)), RValue_makeReal(yscale), RValue_makeInt32(-63), RValue_makeInt32(0), RValue_makeInt32(1));
        RValue_free(&color);
        colorWrite(ctx, true);
    }

    Chapter3_setReal(self, cache.knightSpriteImage, selfReal(self, cache.knightSpriteImage) + selfReal(self, cache.knightSpriteSpeed));
    if (Chapter3_compare(ctx, selfReal(self, cache.intensity), R(3.75)) < 0)
        Instance_setSelfVar(self, cache.intensify, selfVar(self, cache.intensity));
    else
        Chapter3_setReal(self, cache.intensify, approach(ctx, selfReal(self, cache.intensify), 0, R(0.1)));

    RValue knightSprite = selfVar(self, cache.knightSprite);
    GMLReal bboxLeft = Chapter3_callReal(ctx, &cache.spriteGetBboxLeft, &knightSprite, 1);
    GMLReal bboxTop = Chapter3_callReal(ctx, &cache.spriteGetBboxTop, &knightSprite, 1);
    if (Chapter3_compare(ctx, selfReal(self, cache.intensify), R(1.5)) > 0) drawKnightRows(ctx, self, bboxLeft, bboxTop, true);
    drawKnightRows(ctx, self, bboxLeft, bboxTop, false);
    resetTarget(ctx);

    RValue finalArgs[] = {
        selfVar(self, cache.mySurface), RValue_makeReal(Chapter3_cameraX(runner)), RValue_makeReal(Chapter3_cameraY(runner)),
        RValue_makeInt32(1), RValue_makeInt32(1), RValue_makeInt32(0), RValue_makeInt32(16777215), selfVar(self, cache.darkness)
    };
    Chapter3_callVoid(ctx, &cache.drawSurfaceExt, finalArgs, 8);
    Chapter3_with(ctx, self, cache.heartObject, heartBody);
    return true;
}

#define BIND(field, name) ok = Chapter3_bindBuiltin(ctx, &cache.field, name) && ok
#define VAR(field, name) cache.field = NativeScript_getSelfVarID(ctx, name)

void NativeChapter3Roaring2Draw_register(VMContext* ctx) {
    cache.circleObject = Chapter3_findObject(ctx, "obj_knight_circle");
    cache.particleObject = Chapter3_findObject(ctx, "obj_particle_generic");
    cache.starObject = Chapter3_findObject(ctx, "obj_knight_roaring_star");
    cache.starchildObject = Chapter3_findObject(ctx, "obj_knight_pointing_starchild");
    cache.afterimageObject = Chapter3_findObject(ctx, "obj_afterimage");
    cache.afterimageGrowObject = Chapter3_findObject(ctx, "obj_afterimage_grow");
    cache.heartObject = Chapter3_findObject(ctx, "obj_heart");
    cache.sprFlow = Chapter3_findSprite(ctx, "spr_knight_bullet_flow");
    cache.sprGrate = Chapter3_findSprite(ctx, "spr_knight_line_grate");
    cache.sprMarkerGradient = Chapter3_findSprite(ctx, "spr_rk_quickslash_marker_gradient");
    cache.sprMarker = Chapter3_findSprite(ctx, "spr_rk_quickslash_marker");
    if (cache.circleObject < 0 || cache.particleObject < 0 || cache.starObject < 0 || cache.starchildObject < 0 ||
        cache.afterimageObject < 0 || cache.afterimageGrowObject < 0 || cache.heartObject < 0 || cache.sprFlow < 0 ||
        cache.sprGrate < 0 || cache.sprMarkerGradient < 0 || cache.sprMarker < 0) return;

    bool ok = true;
    BIND(surfaceExists, "surface_exists");
    BIND(surfaceCreate, "surface_create");
    BIND(surfaceSetTarget, "surface_set_target");
    BIND(surfaceResetTarget, "surface_reset_target");
    BIND(drawSelf, "draw_self");
    BIND(drawClearAlpha, "draw_clear_alpha");
    BIND(drawSpriteTiled, "draw_sprite_tiled");
    BIND(gpuSetBlendmode, "gpu_set_blendmode");
    BIND(gpuSetBlendmodeExt, "gpu_set_blendmode_ext");
    BIND(drawCircleColor, "draw_circle_color");
    BIND(drawSpriteExt, "draw_sprite_ext");
    BIND(gpuSetColorwriteenable, "gpu_set_colorwriteenable");
    BIND(surfaceGetWidth, "surface_get_width");
    BIND(surfaceGetHeight, "surface_get_height");
    BIND(makeColorHsv, "make_color_hsv");
    BIND(makeColorRgb, "make_color_rgb");
    BIND(drawSurfacePartExt, "draw_surface_part_ext");
    BIND(drawSurface, "draw_surface");
    BIND(drawSurfaceExt, "draw_surface_ext");
    BIND(lengthdirX, "lengthdir_x");
    BIND(lengthdirY, "lengthdir_y");
    BIND(spriteGetBboxLeft, "sprite_get_bbox_left");
    BIND(spriteGetBboxTop, "sprite_get_bbox_top");
    BIND(spriteGetHeight, "sprite_get_height");
    BIND(drawSpritePartExt, "draw_sprite_part_ext");
    BIND(clamp, "clamp");
    if (!ok) return;

    VAR(mySurface, "my_surface");
    VAR(ballSurface, "ball_surface");
    VAR(stop, "stop");
    VAR(doFakeScreen, "do_fake_screen");
    VAR(fakeX, "fake_x");
    VAR(fakeY, "fake_y");
    VAR(ballCounter, "ball_counter");
    VAR(ballSpeed, "ball_speed");
    VAR(loopIndex, "a");
    VAR(starFlicker, "star_flicker");
    VAR(hsv, "hsv");
    VAR(hsvSwitch, "hsv_switch");
    VAR(intensity, "intensity");
    VAR(ballDarkness, "ball_darkness");
    VAR(lineTimer, "line_timer");
    VAR(r, "r");
    VAR(g, "g");
    VAR(b, "b");
    VAR(knightSprite, "knight_sprite");
    VAR(knightSpriteImage, "knight_sprite_image");
    VAR(knightSpriteSpeed, "knight_sprite_speed");
    VAR(intensify, "intensify");
    VAR(bobbleCount, "bobble_count");
    VAR(bobbleAmp, "bobble_amp");
    VAR(fakeAlpha, "fake_alpha");
    VAR(darkness, "darkness");
    VAR(con, "con");
    VAR(timer, "timer");
    VAR(active, "active");
    cache.globalTime = NativeScript_getGlobalVarID(ctx, "time");
    cache.setBlendMode = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.pingpong = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.outline = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.remap = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.fillRectangle = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_roaring2_Draw_0", roaring2Draw);
}
