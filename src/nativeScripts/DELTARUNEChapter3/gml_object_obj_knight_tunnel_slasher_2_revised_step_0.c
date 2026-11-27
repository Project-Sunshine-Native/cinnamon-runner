#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t knightEnemyObject, growtangleObject, bulletObject;
    int32_t sprPointOl, sprNoarm, sprArmpoint, sprBulletM, sprBulletL, sndShinkaAmbience;
    int32_t siner2, anchorX, anchorY, localTurntimer, turntimerLimit, nextUp, turnType, state, timer, fakeTimer;
    int32_t con, introtimer, knightafterimagerange, myattackchoice, oldPos, verticalPos, holeSize, firstStrike;
    int32_t damage, grazepoints, element, active, fake, r, g, b;
    int32_t siner, armpoint, armpointIndex, fade, fadeSpeed;
    Chapter3Builtin irandom, irandomRange, random, randomRange, choose, drawSpriteExt;
    NativeCallBinding getBox, fireBullet, lerpvar, approach, sndLoop, afterimage;
} cache;

static Instance* firstInstance(VMContext* ctx, int32_t objectIndex) {
    return Chapter3_resolveInstance(ctx->runner, RValue_makeReal((GMLReal)objectIndex));
}

static GMLReal getBox(VMContext* ctx, int32_t which) {
    RValue arg = RValue_makeInt32(which);
    RValue result = NativeScript_call(ctx, &cache.getBox, "gml_Script_scr_get_box", &arg, 1);
    GMLReal value = RValue_toReal(result);
    RValue_free(&result);
    return value;
}

static GMLReal callRandom(VMContext* ctx, const Chapter3Builtin* builtin, GMLReal a, GMLReal b, int32_t argCount) {
    RValue args[] = { RValue_makeReal(a), RValue_makeReal(b) };
    return Chapter3_callReal(ctx, builtin, args, argCount);
}

static GMLReal chooseSign(VMContext* ctx, int32_t first) {
    RValue args[] = { RValue_makeInt32(first), RValue_makeInt32(-first) };
    return Chapter3_callReal(ctx, &cache.choose, args, 2);
}

static GMLReal mean2(GMLReal a, GMLReal b) {
    GMLReal sum = R(0.0);
    sum += a;
    sum += b;
    return sum / 2;
}

static GMLReal max2(GMLReal a, GMLReal b) {
    GMLReal result = -INFINITY;
    if (a > result) result = a;
    if (b > result) result = b;
    return result;
}

static void lerpvarAs(VMContext* ctx, Instance* self, RValue* args, int32_t argCount) {
    CallFrame* frame = ctx->currentCallFrame;
    Instance* previousSelf = frame->self;
    Instance* previousOther = frame->other;
    frame->other = previousSelf;
    frame->self = self;
    RValue result = NativeScript_call(ctx, &cache.lerpvar, "gml_Script_scr_lerpvar", args, argCount);
    RValue_free(&result);
    frame->self = previousSelf;
    frame->other = previousOther;
}

static void lerpvarEase(VMContext* ctx, Instance* self, const char* name, RValue from, RValue to, int32_t time, int32_t ease, const char* mode) {
    RValue args[] = { RValue_makeString(name), from, to, RValue_makeInt32(time), RValue_makeInt32(ease), RValue_makeString(mode) };
    lerpvarAs(ctx, self, args, 6);
    RValue_free(&args[0]);
    RValue_free(&args[5]);
}

static bool isTurnType(Instance* self, const char* text) {
    return Chapter3_isString(Instance_getSelfVar(self, cache.turnType), text);
}

static void decorateBullet(VMContext* ctx, Instance* self, Instance* bullet, GMLReal angle, GMLReal y3, GMLReal dorifto, bool real) {
    if (!real) {
        Instance_setSelfVar(bullet, cache.active, RValue_makeInt32(0));
        Instance_setSelfVar(bullet, cache.fake, RValue_makeInt32(1));
    }
    Chapter3_setReal(bullet, cache.damage, 206);
    Chapter3_setReal(bullet, cache.grazepoints, 4);
    Chapter3_setReal(bullet, cache.element, 5);
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", angle);
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", 1);
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", 0);
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_IMAGE_INDEX, "image_index", 1);
    GMLReal size = real ? y3 : y3 * R(0.75);
    if (Chapter3_compare(ctx, size, 48) > 0)
        Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", (GMLReal)cache.sprBulletM);
    if (Chapter3_compare(ctx, size, 80) > 0)
        Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", (GMLReal)cache.sprBulletL);
    GMLReal spriteWidth = Chapter3_getBuiltin(ctx, bullet, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width");
    GMLReal endscale = real ? y3 / spriteWidth : (y3 / spriteWidth) * R(0.75);
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", real ? 15 : 0);
    lerpvarEase(ctx, bullet, "image_xscale", RValue_makeInt32(0), RValue_makeReal(endscale), 14, -1, "out");
    lerpvarEase(ctx, bullet, "image_alpha", RValue_makeInt32(0), RValue_makeInt32(1), 10, 2, "out");
    if (real) {
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.firstStrike), R(0.75)) >= 0 &&
            (isTurnType(self, "end") || isTurnType(self, "mid") || isTurnType(self, "short end"))) {
            Instance_setSelfVar(bullet, cache.active, RValue_makeInt32(0));
            Instance_setSelfVar(bullet, cache.fake, RValue_makeInt32(1));
            Chapter3_setReal(bullet, cache.r, 34);
            Chapter3_setReal(bullet, cache.g, 34);
            Chapter3_setReal(bullet, cache.b, 34);
        }
    } else {
        Chapter3_setReal(bullet, cache.r, 34);
        Chapter3_setReal(bullet, cache.g, 34);
        Chapter3_setReal(bullet, cache.b, 34);
        Instance* growtangle = firstInstance(ctx, cache.growtangleObject);
        Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_DEPTH, "depth",
            (growtangle != nullptr ? Chapter3_getBuiltin(ctx, growtangle, BUILTIN_VAR_DEPTH, "depth") : 0) - 1);
    }
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_GRAVITY_DIRECTION, "gravity_direction", 180);
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_GRAVITY, "gravity", R(0.4));
    Chapter3_setBuiltin(ctx, bullet, BUILTIN_VAR_VSPEED, "vspeed", real ? dorifto : dorifto * 2);
}

static Instance* fireBullet(VMContext* ctx, GMLReal x, GMLReal y, GMLReal speed) {
    RValue args[] = { RValue_makeReal(x), RValue_makeReal(y), RValue_makeInt32(cache.bulletObject), RValue_makeInt32(180), RValue_makeReal(speed) };
    RValue id = NativeScript_call(ctx, &cache.fireBullet, "gml_Script_scr_fire_bullet", args, 5);
    Instance* bullet = Chapter3_resolveInstance(ctx->runner, id);
    RValue_free(&id);
    return bullet;
}

static void spawnWave(VMContext* ctx, Instance* self) {
    GMLReal newpos = (Chapter3_getReal(self, cache.oldPos) + 15) + callRandom(ctx, &cache.irandom, 90, 0, 1);
    if (Chapter3_compare(ctx, newpos, 60) > 0) newpos -= 120;
    Instance_setSelfVar(self, cache.oldPos, Instance_getSelfVar(self, cache.verticalPos));
    Chapter3_setReal(self, cache.verticalPos, newpos);
    GMLReal oldPos = Chapter3_getReal(self, cache.oldPos);
    GMLReal verticalPos = newpos;
    GMLReal lo = oldPos - 50;
    GMLReal hi = oldPos + 50;
    if (lo > verticalPos) verticalPos = lo;
    if (verticalPos > hi) verticalPos = hi;
    Chapter3_setReal(self, cache.verticalPos, verticalPos);
    GMLReal holeDiff = GMLReal_fabs(oldPos - verticalPos);
    if (Chapter3_compare(ctx, holeDiff, 20) < 0) Chapter3_setReal(self, cache.holeSize, 36);
    else if (Chapter3_compare(ctx, holeDiff, 30) < 0) Chapter3_setReal(self, cache.holeSize, 44);
    else if (Chapter3_compare(ctx, holeDiff, 40) < 0) Chapter3_setReal(self, cache.holeSize, 52);
    else Chapter3_setReal(self, cache.holeSize, 60);

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.firstStrike), 0) > 0) {
        Chapter3_setReal(self, cache.verticalPos, callRandom(ctx, &cache.irandomRange, -15, 15, 2));
        Instance_setSelfVar(self, cache.oldPos, Instance_getSelfVar(self, cache.verticalPos));
        Chapter3_setReal(self, cache.holeSize, 100);
        GMLReal firstStrike = Chapter3_getReal(self, cache.firstStrike);
        if (Chapter3_compare(ctx, firstStrike, R(0.75)) == 0) Chapter3_setReal(self, cache.holeSize, 90);
        if (Chapter3_compare(ctx, firstStrike, R(0.5)) == 0) Chapter3_setReal(self, cache.holeSize, 75);
        if (Chapter3_compare(ctx, firstStrike, R(0.25)) == 0) Chapter3_setReal(self, cache.holeSize, 60);
        RValue approachArgs[] = { Instance_getSelfVar(self, cache.firstStrike), RValue_makeInt32(0), RValue_makeReal(R(0.25)) };
        RValue approached = NativeScript_call(ctx, &cache.approach, "gml_Script_scr_approach", approachArgs, 3);
        Instance_setSelfVar(self, cache.firstStrike, approached);
        RValue_free(&approached);
    }

    GMLReal box3 = getBox(ctx, 3);
    GMLReal box1 = getBox(ctx, 1);
    GMLReal mbox = mean2(box1, box3);
    GMLReal randomPart = callRandom(ctx, &cache.random, R(0.6), 0, 1);
    GMLReal dorifto = R(0.15) + (randomPart * chooseSign(ctx, 1));
    verticalPos = Chapter3_getReal(self, cache.verticalPos);
    GMLReal holeSize = Chapter3_getReal(self, cache.holeSize);

    if (Chapter3_compare(ctx, verticalPos, -20) > 0) {
        GMLReal y1 = (mbox + verticalPos) - (holeSize * R(0.5));
        GMLReal y2 = getBox(ctx, 1) - 40;
        GMLReal y3 = max2((y1 - y2) * (R(0.5) + callRandom(ctx, &cache.random, R(0.5), 0, 1)), 50);
        GMLReal y4 = y1 - y3;
        GMLReal bulletY = mean2(y1, y4);
        Instance* bullet = fireBullet(ctx, getBox(ctx, 0) + 40, bulletY, R(0.5));
        if (bullet != nullptr) decorateBullet(ctx, self, bullet, 270, y3, dorifto, true);
    }

    verticalPos = Chapter3_getReal(self, cache.verticalPos);
    holeSize = Chapter3_getReal(self, cache.holeSize);
    if (Chapter3_compare(ctx, verticalPos, 20) < 0) {
        GMLReal y1 = mbox + verticalPos + (holeSize * R(0.5));
        GMLReal y2 = getBox(ctx, 3) + 40;
        GMLReal y3 = max2((y2 - y1) * (R(0.5) + callRandom(ctx, &cache.random, R(0.5), 0, 1)), 60);
        GMLReal y4 = y1 + y3;
        GMLReal bulletY = mean2(y1, y4);
        Instance* bullet = fireBullet(ctx, getBox(ctx, 0) + 40, bulletY, R(0.5));
        if (bullet != nullptr) decorateBullet(ctx, self, bullet, 90, y3, dorifto, true);
    }

    Chapter3_setReal(self, cache.timer, 0);
}

static void spawnFake(VMContext* ctx, Instance* self) {
    GMLReal verticalPos2 = callRandom(ctx, &cache.irandomRange, -70, 70, 2);
    GMLReal box3 = getBox(ctx, 3);
    GMLReal box1 = getBox(ctx, 1);
    GMLReal mbox = mean2(box1, box3);
    GMLReal randomPart = callRandom(ctx, &cache.random, R(0.6), 0, 1);
    GMLReal dorifto = R(0.25) + (randomPart * chooseSign(ctx, 1));
    GMLReal y1 = (mbox + verticalPos2) - (Chapter3_getReal(self, cache.holeSize) * R(0.5));
    RValue chooseArgs[] = { RValue_makeReal(getBox(ctx, 1) - 40), RValue_makeReal(0) };
    chooseArgs[1] = RValue_makeReal(getBox(ctx, 3) + 40);
    GMLReal y2 = Chapter3_callReal(ctx, &cache.choose, chooseArgs, 2);
    GMLReal y3 = max2((y1 - y2) * (R(0.5) + callRandom(ctx, &cache.random, R(0.5), 0, 1)), 50);
    Instance* growtangle = firstInstance(ctx, cache.growtangleObject);
    GMLReal growtangleY = growtangle != nullptr ? Chapter3_getBuiltin(ctx, growtangle, BUILTIN_VAR_Y, "y") : 0;
    GMLReal offset = callRandom(ctx, &cache.randomRange, 20, 70, 2);
    GMLReal bulletY = growtangleY + (offset * chooseSign(ctx, -1));
    Instance* bullet = fireBullet(ctx, getBox(ctx, 0) + 40, bulletY, R(0.35));
    if (bullet != nullptr) decorateBullet(ctx, self, bullet, 90, y3, dorifto, false);
}

static void finalSlowBullet(VMContext* ctx, Instance* self, Instance* inst) {
    (void)self;
    if (!RValue_toBool(Instance_getSelfVar(inst, cache.active))) return;
    Chapter3_setBuiltin(ctx, inst, BUILTIN_VAR_GRAVITY, "gravity", 0);
    Chapter3_setBuiltin(ctx, inst, BUILTIN_VAR_HSPEED, "hspeed", Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_HSPEED, "hspeed") * R(0.9));
    Chapter3_setBuiltin(ctx, inst, BUILTIN_VAR_VSPEED, "vspeed", Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_VSPEED, "vspeed") * R(0.9));
}

static bool tunnelSlasherStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Instance* knight = firstInstance(ctx, cache.knightEnemyObject);
    if (knight == nullptr) return false;

    GMLReal turntimer = Chapter3_getReal(self, cache.localTurntimer) - 1;
    GMLReal nextUp = Chapter3_getReal(self, cache.nextUp);
    if (Chapter3_compare(ctx, turntimer, Chapter3_getReal(self, cache.turntimerLimit)) < 0 &&
        (Chapter3_compare(ctx, nextUp, 4) == 0 || Chapter3_compare(ctx, nextUp, 5) == 0)) return false;
    bool goFinal = Chapter3_compare(ctx, turntimer, 60) < 0 && (isTurnType(self, "full") || Chapter3_compare(ctx, nextUp, -1) == 0);
    bool inFinal = !goFinal && Chapter3_isString(Instance_getSelfVar(self, cache.state), "final");
    if (inFinal) {
        GMLReal nextTimer = Chapter3_getReal(self, cache.timer) + 1;
        static const GMLReal delegated[] = { 1, 12, 33, 52, 92 };
        for (int32_t i = 0; 5 > i; i++) {
            if (Chapter3_compare(ctx, nextTimer, delegated[i]) == 0) return false;
        }
    } else if (!goFinal && Chapter3_compare(ctx, turntimer, 40) < 0) {
        return false;
    }

    *outResult = RValue_makeUndefined();
    int32_t base = Runner_pushInstancesOfObject(runner, cache.knightEnemyObject);
    int32_t end = (int32_t)arrlen(runner->instanceSnapshots);
    for (int32_t i = base; end > i; i++) {
        if (runner->instanceSnapshots[i]->active) Chapter3_setReal(runner->instanceSnapshots[i], cache.siner2, 0);
    }
    Runner_popInstanceSnapshot(runner, base);
    Chapter3_setReal(self, cache.anchorX, Chapter3_getBuiltin(ctx, knight, BUILTIN_VAR_X, "x"));
    Chapter3_setReal(self, cache.anchorY, Chapter3_getBuiltin(ctx, knight, BUILTIN_VAR_Y, "y"));
    RValue localTurntimer = Instance_getSelfVar(self, cache.localTurntimer);
    Instance_setSelfVar(self, cache.localTurntimer, localTurntimer.type == RVALUE_INT32
        ? RValue_makeInt32(localTurntimer.int32 - 1) : RValue_makeReal(RValue_toReal(localTurntimer) - 1));

    if (goFinal) {
        Chapter3_setReal(self, cache.timer, 0);
        Chapter3_setReal(self, cache.fakeTimer, -99999);
        Chapter3_setReal(self, cache.localTurntimer, 99999);
        RValue finalState = RValue_makeString("final");
        Instance_setSelfVar(self, cache.state, finalState);
        RValue_free(&finalState);
        return true;
    }

    if (inFinal) {
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 12) < 0)
            Chapter3_with(ctx, self, cache.bulletObject, finalSlowBullet);
        return true;
    }

    GMLReal con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, 0) == 0) {
        Chapter3_setReal(self, cache.con, -1);
        Chapter3_setReal(self, cache.con, R(0.1));
    }

    con = Chapter3_getReal(self, cache.con);
    if (Chapter3_compare(ctx, con, R(0.1)) == 0 || Chapter3_compare(ctx, con, R(0.2)) == 0) {
        Instance_setSelfVar(self, cache.introtimer, Chapter3_increment(Instance_getSelfVar(self, cache.introtimer)));
        GMLReal introtimer = Chapter3_getReal(self, cache.introtimer);
        if (Chapter3_compare(ctx, introtimer, 5) == 0) {
            Chapter3_setReal(self, cache.con, R(0.2));
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", (GMLReal)cache.sprPointOl);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index", 0);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_SPEED, "image_speed", 0);
            lerpvarEase(ctx, self, "image_index", RValue_makeInt32(0), RValue_makeInt32(4), 15, 2, "out");
            GMLReal x = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x");
            lerpvarEase(ctx, self, "x", RValue_makeReal(x), RValue_makeReal(x + 40), 15, 2, "out");
            GMLReal y = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y");
            lerpvarEase(ctx, self, "y", RValue_makeReal(y), RValue_makeReal(getBox(ctx, 5) - 100), 15, 2, "out");
            Instance* attacker = firstInstance(ctx, cache.knightEnemyObject);
            if (attacker != nullptr && Chapter3_compare(ctx, Chapter3_getReal(attacker, cache.myattackchoice), 3) == 0) {
                RValue sound = RValue_makeInt32(cache.sndShinkaAmbience);
                RValue result = NativeScript_call(ctx, &cache.sndLoop, "gml_Script_snd_loop", &sound, 1);
                RValue_free(&result);
            }
        }
        if (Chapter3_compare(ctx, introtimer, 20) == 0) {
            RValue args[] = { RValue_makeString("knightafterimagerange"), Instance_getSelfVar(self, cache.knightafterimagerange),
                RValue_makeInt32(4), RValue_makeInt32(30) };
            lerpvarAs(ctx, self, args, 4);
            RValue_free(&args[0]);
        }
        if (Chapter3_compare(ctx, introtimer, 25) == 0) {
            Chapter3_setReal(self, cache.con, 1);
            Chapter3_setReal(self, cache.introtimer, 0);
        }
    }

    RValue conValue = Instance_getSelfVar(self, cache.con);
    bool running = (Chapter3_compare(ctx, RValue_toReal(conValue), R(0.2)) >= 0 &&
        (isTurnType(self, "full") || isTurnType(self, "start") || isTurnType(self, "short start"))) ||
        RValue_toBool(conValue);
    if (running) {
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        Instance_setSelfVar(self, cache.fakeTimer, Chapter3_increment(Instance_getSelfVar(self, cache.fakeTimer)));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 8) >= 0) spawnWave(ctx, self);
    }

    GMLReal fakeTimer = Chapter3_getReal(self, cache.fakeTimer);
    if (Chapter3_compare(ctx, fakeTimer, 8) > 0 && Chapter3_compare(ctx, GMLReal_fmod(fakeTimer + 8, 4), 0) == 0)
        spawnFake(ctx, self);
    return true;
}

static bool tunnelSlasherDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    Instance_setSelfVar(self, cache.siner, Chapter3_increment(Instance_getSelfVar(self, cache.siner)));
    GMLReal siner = Chapter3_getReal(self, cache.siner);
    GMLReal ymod = GMLReal_sin(siner / 30) * 8;
    GMLReal x = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x");
    GMLReal y = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y");
    GMLReal sprite = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index");
    if (Chapter3_compare(ctx, sprite, (GMLReal)cache.sprNoarm) == 0) {
        RValue args[] = {
            RValue_makeInt32(cache.sprArmpoint), Instance_getSelfVar(self, cache.armpointIndex),
            RValue_makeReal(x + 116), RValue_makeReal(y + 62 + ymod),
            RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale")),
            RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale")),
            Instance_getSelfVar(self, cache.armpoint),
            RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend")),
            RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"))
        };
        Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);
    }
    RValue args[] = {
        RValue_makeReal(sprite),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index")),
        RValue_makeReal(x), RValue_makeReal(y + ymod),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"))
    };
    Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);

    if (Chapter3_compare(ctx, GMLReal_fmod(siner, 4), 0) == 0 &&
        Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"), 0) != 0) {
        RValue id = NativeScript_call(ctx, &cache.afterimage, "gml_Script_scr_afterimage", nullptr, 0);
        Instance_setSelfVar(self, cache.fade, id);
        Instance* fade = Chapter3_resolveInstance(ctx->runner, id);
        RValue_free(&id);
        if (fade != nullptr) {
            Chapter3_setBuiltin(ctx, fade, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", R(0.6));
            Instance* knight = firstInstance(ctx, cache.knightEnemyObject);
            Chapter3_setBuiltin(ctx, fade, BUILTIN_VAR_DEPTH, "depth",
                (knight != nullptr ? Chapter3_getBuiltin(ctx, knight, BUILTIN_VAR_DEPTH, "depth") : 0) + 1);
            Chapter3_setReal(fade, cache.fadeSpeed, R(0.04));
            Chapter3_setBuiltin(ctx, fade, BUILTIN_VAR_HSPEED, "hspeed", 4);
            GMLReal range = Chapter3_getReal(self, cache.knightafterimagerange);
            Chapter3_setBuiltin(ctx, fade, BUILTIN_VAR_VSPEED, "vspeed", callRandom(ctx, &cache.randomRange, -range, range, 2));
        }
    }
    return true;
}

void NativeChapter3TunnelSlasher_register(VMContext* ctx) {
    cache.knightEnemyObject = Chapter3_findObject(ctx, "obj_knight_enemy");
    cache.growtangleObject = Chapter3_findObject(ctx, "obj_growtangle");
    cache.bulletObject = Chapter3_findObject(ctx, "obj_knight_diamondswordbullet_ext");
    cache.sprPointOl = Chapter3_findSprite(ctx, "spr_roaringknight_point_ol");
    cache.sprNoarm = Chapter3_findSprite(ctx, "spr_roaringknight_noarm");
    cache.sprArmpoint = Chapter3_findSprite(ctx, "spr_roaringknight_armpoint");
    cache.sprBulletM = Chapter3_findSprite(ctx, "spr_knight_diamondbullet_m");
    cache.sprBulletL = Chapter3_findSprite(ctx, "spr_knight_diamondbullet_l");
    cache.sndShinkaAmbience = Chapter3_findSound(ctx, "snd_shinka_ambience");
    if (cache.knightEnemyObject < 0 || cache.growtangleObject < 0 || cache.bulletObject < 0 || cache.sprPointOl < 0 ||
        cache.sprNoarm < 0 || cache.sprArmpoint < 0 || cache.sprBulletM < 0 || cache.sprBulletL < 0 || cache.sndShinkaAmbience < 0) return;
    if (!Chapter3_bindBuiltin(ctx, &cache.irandom, "irandom") ||
        !Chapter3_bindBuiltin(ctx, &cache.irandomRange, "irandom_range") ||
        !Chapter3_bindBuiltin(ctx, &cache.random, "random") ||
        !Chapter3_bindBuiltin(ctx, &cache.randomRange, "random_range") ||
        !Chapter3_bindBuiltin(ctx, &cache.choose, "choose") ||
        !Chapter3_bindBuiltin(ctx, &cache.drawSpriteExt, "draw_sprite_ext")) return;
    cache.getBox = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.fireBullet = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.lerpvar = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.approach = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.sndLoop = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.afterimage = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (!NativeScript_canCall(ctx, &cache.getBox, "gml_Script_scr_get_box") ||
        !NativeScript_canCall(ctx, &cache.fireBullet, "gml_Script_scr_fire_bullet") ||
        !NativeScript_canCall(ctx, &cache.lerpvar, "gml_Script_scr_lerpvar") ||
        !NativeScript_canCall(ctx, &cache.approach, "gml_Script_scr_approach") ||
        !NativeScript_canCall(ctx, &cache.sndLoop, "gml_Script_snd_loop") ||
        !NativeScript_canCall(ctx, &cache.afterimage, "gml_Script_scr_afterimage")) return;

    cache.siner2 = NativeScript_getSelfVarID(ctx, "siner2");
    cache.anchorX = NativeScript_getSelfVarID(ctx, "anchor_x");
    cache.anchorY = NativeScript_getSelfVarID(ctx, "anchor_y");
    cache.localTurntimer = NativeScript_getSelfVarID(ctx, "local_turntimer");
    cache.turntimerLimit = NativeScript_getSelfVarID(ctx, "turntimer_limit");
    cache.nextUp = NativeScript_getSelfVarID(ctx, "next_up");
    cache.turnType = NativeScript_getSelfVarID(ctx, "turn_type");
    cache.state = NativeScript_getSelfVarID(ctx, "state");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.fakeTimer = NativeScript_getSelfVarID(ctx, "fake_timer");
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.introtimer = NativeScript_getSelfVarID(ctx, "introtimer");
    cache.knightafterimagerange = NativeScript_getSelfVarID(ctx, "knightafterimagerange");
    cache.myattackchoice = NativeScript_getSelfVarID(ctx, "myattackchoice");
    cache.oldPos = NativeScript_getSelfVarID(ctx, "old_pos");
    cache.verticalPos = NativeScript_getSelfVarID(ctx, "vertical_pos");
    cache.holeSize = NativeScript_getSelfVarID(ctx, "hole_size");
    cache.firstStrike = NativeScript_getSelfVarID(ctx, "first_strike");
    cache.damage = NativeScript_getSelfVarID(ctx, "damage");
    cache.grazepoints = NativeScript_getSelfVarID(ctx, "grazepoints");
    cache.element = NativeScript_getSelfVarID(ctx, "element");
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.fake = NativeScript_getSelfVarID(ctx, "fake");
    cache.r = NativeScript_getSelfVarID(ctx, "r");
    cache.g = NativeScript_getSelfVarID(ctx, "g");
    cache.b = NativeScript_getSelfVarID(ctx, "b");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.armpoint = NativeScript_getSelfVarID(ctx, "armpoint");
    cache.armpointIndex = NativeScript_getSelfVarID(ctx, "armpoint_index");
    cache.fade = NativeScript_getSelfVarID(ctx, "fade");
    cache.fadeSpeed = NativeScript_getSelfVarID(ctx, "fadeSpeed");

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_tunnel_slasher_2_revised_Step_0", tunnelSlasherStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_knight_tunnel_slasher_2_revised_Draw_0", tunnelSlasherDraw);
}
