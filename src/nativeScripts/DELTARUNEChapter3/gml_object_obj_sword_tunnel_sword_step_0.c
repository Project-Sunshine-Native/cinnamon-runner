#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t heartObject, hitboxObject, sprSmallMask, sprTelegraph, sndJump, sndCut;
    int32_t speed, gravity, maxspeed, mydirection, con, timer, telegraph, anglespeed, targetangle, randx, randy;
    int32_t damage, active, create2ndHitbox, hitbox, inst, xrate, yrate, fade, telegraphalpha, loopIndex;
    Chapter3Builtin lengthdirX, lengthdirY, lerp, pointDirection, showDebugMessage, audioIsPlaying;
    Chapter3Builtin instanceCreateDepth, collisionLine, eventUser, mergeColor, drawSpriteExt, drawSelf;
    NativeCallBinding angleChange, sndPlayX, afterimageGrow, afterimage;
} cache;

static RValue addValues(RValue a, RValue b) {
    if (a.type == RVALUE_INT32 && b.type == RVALUE_INT32) return RValue_makeInt32(a.int32 + b.int32);
    return RValue_makeReal(RValue_toReal(a) + RValue_toReal(b));
}

static GMLReal call2(VMContext* ctx, const Chapter3Builtin* builtin, GMLReal a, GMLReal b) {
    RValue args[] = { RValue_makeReal(a), RValue_makeReal(b) };
    return Chapter3_callReal(ctx, builtin, args, 2);
}

static GMLReal lerp3(VMContext* ctx, RValue a, RValue b, GMLReal t) {
    RValue args[] = { a, b, RValue_makeReal(t) };
    return Chapter3_callReal(ctx, &cache.lerp, args, 3);
}

static GMLReal getX(VMContext* ctx, Instance* inst) { return Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_X, "x"); }
static GMLReal getY(VMContext* ctx, Instance* inst) { return Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_Y, "y"); }
static GMLReal getAngle(VMContext* ctx, Instance* inst) { return Chapter3_getBuiltin(ctx, inst, BUILTIN_VAR_IMAGE_ANGLE, "image_angle"); }

static void playIfSilent(VMContext* ctx, int32_t sound) {
    RValue soundArg = RValue_makeInt32(sound);
    if (RValue_toBool(Chapter3_call(ctx, &cache.audioIsPlaying, &soundArg, 1))) return;
    RValue args[] = { RValue_makeInt32(sound), RValue_makeInt32(1), RValue_makeReal(R(0.8)) };
    RValue result = NativeScript_call(ctx, &cache.sndPlayX, "gml_Script_snd_play_x", args, 3);
    RValue_free(&result);
}

static void setSmallMask(VMContext* ctx, Instance* self, Instance* heart) {
    (void)self;
    Chapter3_setBuiltin(ctx, heart, BUILTIN_VAR_MASK_INDEX, "mask_index", (GMLReal)cache.sprSmallMask);
}

static bool swordStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Instance* heart = Chapter3_resolveInstance(runner, RValue_makeReal((GMLReal)cache.heartObject));
    if (heart == nullptr) return false;
    *outResult = RValue_makeUndefined();

    Instance_setSelfVar(self, cache.speed, addValues(Instance_getSelfVar(self, cache.speed), Instance_getSelfVar(self, cache.gravity)));
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.speed), Chapter3_getReal(self, cache.maxspeed)) > 0)
        Instance_setSelfVar(self, cache.speed, Instance_getSelfVar(self, cache.maxspeed));

    GMLReal mydirection = Chapter3_getReal(self, cache.mydirection);
    GMLReal xadd = call2(ctx, &cache.lengthdirX, 1, mydirection);
    GMLReal yadd = call2(ctx, &cache.lengthdirY, 1, mydirection);

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 1) == 0) {
        Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
        GMLReal c = 10;
        GMLReal timer = Chapter3_getReal(self, cache.timer);
        if (Chapter3_compare(ctx, timer, 1) == 0) {
            Chapter3_setReal(self, cache.gravity, 0);
            Chapter3_setReal(self, cache.telegraph, 1);
        }
        if (Chapter3_compare(ctx, timer, 10 + (c / 2)) < 0) {
            Chapter3_setReal(self, cache.anglespeed, lerp3(ctx, RValue_makeInt32(8), RValue_makeInt32(0), timer / (10 + (c / 2))));
            GMLReal angle = getAngle(ctx, self);
            GMLReal targetDirection = Chapter3_callReal(ctx, &cache.pointDirection, (RValue[]) {
                RValue_makeReal(getX(ctx, self)), RValue_makeReal(getY(ctx, self)),
                RValue_makeReal(getX(ctx, heart) + 10 + Chapter3_getReal(self, cache.randx)),
                RValue_makeReal(getY(ctx, heart) + 10 + Chapter3_getReal(self, cache.randy)) }, 4);
            RValue changeArgs[] = { RValue_makeReal(angle), RValue_makeReal(targetDirection), Instance_getSelfVar(self, cache.anglespeed) };
            RValue change = NativeScript_call(ctx, &cache.angleChange, "gml_Script_scr_anglechange", changeArgs, 3);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", angle + RValue_toReal(change));
            RValue_free(&change);
            Instance_setSelfVar(self, cache.targetangle, addValues(Instance_getSelfVar(self, cache.targetangle), Instance_getSelfVar(self, cache.anglespeed)));
            RValue message = Instance_getSelfVar(self, cache.targetangle);
            Chapter3_callVoid(ctx, &cache.showDebugMessage, &message, 1);
        }
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction", getAngle(ctx, self));
        if (Chapter3_compare(ctx, timer, 10 + c) < 0)
            Chapter3_setReal(self, cache.speed, lerp3(ctx, Instance_getSelfVar(self, cache.speed), RValue_makeInt32(0), timer / 10));
        if (Chapter3_compare(ctx, timer, 11 + c) >= 0 && Chapter3_compare(ctx, timer, 15 + c) < 0) {
            playIfSilent(ctx, cache.sndJump);
            Chapter3_setReal(self, cache.speed, 2);
            xadd = call2(ctx, &cache.lengthdirX, 2, getAngle(ctx, self) + 180);
            yadd = call2(ctx, &cache.lengthdirY, 2, getAngle(ctx, self) + 180);
        }
        if (Chapter3_compare(ctx, timer, 15 + c) >= 0 && Chapter3_compare(ctx, timer, 20 + c) < 0) {
            xadd = 0;
            yadd = 0;
        }
        if (Chapter3_compare(ctx, timer, 20 + c) == 0) {
            RValue id = NativeScript_call(ctx, &cache.afterimageGrow, "gml_Script_scr_afterimage_grow", nullptr, 0);
            Instance_setSelfVar(self, cache.inst, id);
            Instance* grow = Chapter3_resolveInstance(runner, id);
            RValue_free(&id);
            if (grow != nullptr) {
                Chapter3_setReal(grow, cache.xrate, R(0.4));
                Chapter3_setReal(grow, cache.yrate, R(0.4));
                Chapter3_setReal(grow, cache.fade, R(0.2));
            }
        }
        if (Chapter3_compare(ctx, timer, 20 + c) >= 0) {
            playIfSilent(ctx, cache.sndCut);
            Chapter3_setReal(self, cache.telegraph, 0);
            Chapter3_setReal(self, cache.damage, 160);
            Chapter3_setReal(self, cache.speed, 80);
            xadd = call2(ctx, &cache.lengthdirX, 1, getAngle(ctx, self));
            yadd = call2(ctx, &cache.lengthdirY, 1, getAngle(ctx, self));
        }
    }

    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend", 16777215);
    GMLReal heartX = getX(ctx, heart);
    GMLReal x = getX(ctx, self);
    if (Chapter3_compare(ctx, x, heartX - 80) > 0 && Chapter3_compare(ctx, x, heartX + 80) < 0) {
        GMLReal heartY = getY(ctx, heart);
        GMLReal y = getY(ctx, self);
        if (Chapter3_compare(ctx, y, heartY + 80) < 0 && Chapter3_compare(ctx, y, heartY - 80) > 0) {
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend", 255);
            GMLReal remx = x;
            GMLReal remy = y;
            GMLReal steps = GMLReal_floor(Chapter3_getReal(self, cache.speed) / 8);
            if (!(steps > 1)) steps = 1;
            int64_t count = (int64_t)steps;
            for (int64_t i = 0; count > i; i++) {
                Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_X, "x", getX(ctx, self) + (xadd * 8));
                Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_Y, "y", getY(ctx, self) + (yadd * 8));
                Chapter3_setReal(self, cache.active, 1);
                bool noSecond = Chapter3_compare(ctx, Chapter3_getReal(self, cache.create2ndHitbox), 0) == 0;
                if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 1) == 0 &&
                    Chapter3_compare(ctx, Chapter3_getReal(self, cache.speed), 80) == 0 && noSecond) {
                    RValue createArgs[] = {
                        RValue_makeReal(getX(ctx, self)), RValue_makeReal(getY(ctx, self)),
                        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DEPTH, "depth") - 1),
                        RValue_makeInt32(cache.hitboxObject) };
                    RValue id = Chapter3_call(ctx, &cache.instanceCreateDepth, createArgs, 4);
                    Instance_setSelfVar(self, cache.hitbox, id);
                    Instance* hitbox = Chapter3_resolveInstance(runner, id);
                    RValue_free(&id);
                    if (hitbox != nullptr) {
                        Chapter3_setBuiltin(ctx, hitbox, BUILTIN_VAR_IMAGE_ANGLE, "image_angle", getAngle(ctx, self));
                        Chapter3_setBuiltin(ctx, hitbox, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", R(0.4));
                        Chapter3_setBuiltin(ctx, hitbox, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", 999);
                    }
                    Chapter3_with(ctx, self, cache.heartObject, setSmallMask);
                    Instance_setSelfVar(self, cache.create2ndHitbox, RValue_makeInt32(1));
                } else if (noSecond) {
                    GMLReal lx = getX(ctx, self);
                    GMLReal ly = getY(ctx, self);
                    GMLReal angle = getAngle(ctx, self);
                    RValue lineArgs[] = {
                        RValue_makeReal(lx), RValue_makeReal(ly),
                        RValue_makeReal(lx + call2(ctx, &cache.lengthdirX, 37, angle)),
                        RValue_makeReal(ly + call2(ctx, &cache.lengthdirY, 37, angle)),
                        RValue_makeInt32(cache.heartObject), RValue_makeInt32(0), RValue_makeBool(false) };
                    RValue hit = Chapter3_call(ctx, &cache.collisionLine, lineArgs, 7);
                    bool collided = RValue_toBool(hit);
                    RValue_free(&hit);
                    if (collided) {
                        RValue user = RValue_makeInt32(5);
                        Chapter3_callVoid(ctx, &cache.eventUser, &user, 1);
                    }
                }
                Chapter3_setReal(self, cache.active, 0);
            }
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_X, "x", remx);
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_Y, "y", remy);
        }
    }

    GMLReal speed = Chapter3_getReal(self, cache.speed);
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_X, "x", getX(ctx, self) + (xadd * speed));
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_Y, "y", getY(ctx, self) + (yadd * speed));
    RValue id = NativeScript_call(ctx, &cache.afterimage, "gml_Script_scr_afterimage", nullptr, 0);
    Instance* afterimage = Chapter3_resolveInstance(runner, id);
    RValue_free(&id);
    if (afterimage != nullptr) {
        Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", R(0.4));
        Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_X, "x",
            (getX(ctx, self) + Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_XPREVIOUS, "xprevious")) / 2);
        Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_Y, "y",
            (getY(ctx, self) + Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_YPREVIOUS, "yprevious")) / 2);
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 0) > 0)
            Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_BLEND, "image_blend", 16777215);
    }

    GMLReal cameraX = Chapter3_cameraX(runner);
    GMLReal cameraY = Chapter3_cameraY(runner);
    if (Chapter3_compare(ctx, getX(ctx, self), cameraX - 100) <= 0) Runner_destroyInstance(runner, self, true);
    if (Chapter3_compare(ctx, getX(ctx, self), cameraX + 740) >= 0) Runner_destroyInstance(runner, self, true);
    if (Chapter3_compare(ctx, getY(ctx, self), cameraY + 600) >= 0) Runner_destroyInstance(runner, self, true);
    if (Chapter3_compare(ctx, getY(ctx, self), cameraY - 250) <= 0) Runner_destroyInstance(runner, self, true);

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 0) == 0) {
        RValue yscale = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale"));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
            lerp3(ctx, yscale, RValue_makeReal(Chapter3_getReal(self, cache.speed) / 20), R(0.1)));
    }
    return true;
}

static bool swordDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    GMLReal telegraph = Chapter3_getReal(self, cache.telegraph);
    if (Chapter3_compare(ctx, telegraph, 0) == 0 && Chapter3_compare(ctx, Chapter3_getReal(self, cache.telegraphalpha), 0) > 0)
        Chapter3_setReal(self, cache.telegraphalpha, Chapter3_getReal(self, cache.telegraphalpha) - R(0.1));
    if (Chapter3_compare(ctx, telegraph, 1) == 0 && Chapter3_compare(ctx, Chapter3_getReal(self, cache.telegraphalpha), R(0.5)) < 0)
        Chapter3_setReal(self, cache.telegraphalpha, Chapter3_getReal(self, cache.telegraphalpha) + R(0.05));

    GMLReal x = getX(ctx, self);
    GMLReal y = getY(ctx, self);
    GMLReal angle = getAngle(ctx, self);
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.telegraphalpha), 0) > 0) {
        RValue args[] = {
            RValue_makeInt32(cache.sprTelegraph), RValue_makeInt32(0), RValue_makeReal(x), RValue_makeReal(y),
            RValue_makeInt32(999), RValue_makeReal(R(0.4)), RValue_makeReal(angle), RValue_makeInt32(255),
            Instance_getSelfVar(self, cache.telegraphalpha) };
        Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 0) > 0) {
        GMLReal timer = Chapter3_getReal(self, cache.timer);
        if (Chapter3_compare(ctx, timer, 10) > 0) timer = 10;
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend"), 255) == 0) {
            RValue mergeArgs[] = { RValue_makeInt32(255), RValue_makeInt32(16777215), RValue_makeReal(timer / 10) };
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend", Chapter3_callReal(ctx, &cache.mergeColor, mergeArgs, 3));
        }
    }

    RValue sprite = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    RValue subimg = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index"));
    RValue xprevious = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_XPREVIOUS, "xprevious"));
    RValue yprevious = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_YPREVIOUS, "yprevious"));
    RValue xscale = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
    RValue yscale = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale"));
    RValue blend = RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend"));
    for (int32_t i = 0; 10 > i; i++) {
        Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal((GMLReal)i));
        GMLReal t = (GMLReal)i / 10;
        RValue args[] = {
            sprite, subimg, RValue_makeReal(lerp3(ctx, xprevious, RValue_makeReal(x), t)),
            RValue_makeReal(lerp3(ctx, yprevious, RValue_makeReal(y), t)), xscale, yscale,
            RValue_makeReal(angle), blend, RValue_makeReal(t) };
        Chapter3_callVoid(ctx, &cache.drawSpriteExt, args, 9);
    }
    Instance_setSelfVar(self, cache.loopIndex, RValue_makeReal(10));
    Chapter3_callVoid(ctx, &cache.drawSelf, nullptr, 0);
    return true;
}

void NativeChapter3SwordTunnelSword_register(VMContext* ctx) {
    cache.heartObject = Chapter3_findObject(ctx, "obj_heart");
    cache.hitboxObject = Chapter3_findObject(ctx, "obj_sword_tunnel_hitbox");
    cache.sprSmallMask = Chapter3_findSprite(ctx, "spr_dodgeheart_smallmask");
    cache.sprTelegraph = Chapter3_findSprite(ctx, "spr_lasergun_laser_telegraph");
    cache.sndJump = Chapter3_findSound(ctx, "snd_knight_jump");
    cache.sndCut = Chapter3_findSound(ctx, "snd_knight_cut");
    if (cache.heartObject < 0 || cache.hitboxObject < 0 || cache.sprSmallMask < 0 || cache.sprTelegraph < 0 ||
        cache.sndJump < 0 || cache.sndCut < 0) return;
    if (!Chapter3_bindBuiltin(ctx, &cache.lengthdirX, "lengthdir_x") ||
        !Chapter3_bindBuiltin(ctx, &cache.lengthdirY, "lengthdir_y") ||
        !Chapter3_bindBuiltin(ctx, &cache.lerp, "lerp") ||
        !Chapter3_bindBuiltin(ctx, &cache.pointDirection, "point_direction") ||
        !Chapter3_bindBuiltin(ctx, &cache.showDebugMessage, "show_debug_message") ||
        !Chapter3_bindBuiltin(ctx, &cache.audioIsPlaying, "audio_is_playing") ||
        !Chapter3_bindBuiltin(ctx, &cache.instanceCreateDepth, "instance_create_depth") ||
        !Chapter3_bindBuiltin(ctx, &cache.collisionLine, "collision_line") ||
        !Chapter3_bindBuiltin(ctx, &cache.eventUser, "event_user") ||
        !Chapter3_bindBuiltin(ctx, &cache.mergeColor, "merge_color") ||
        !Chapter3_bindBuiltin(ctx, &cache.drawSpriteExt, "draw_sprite_ext") ||
        !Chapter3_bindBuiltin(ctx, &cache.drawSelf, "draw_self")) return;
    cache.angleChange = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.sndPlayX = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.afterimageGrow = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.afterimage = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (!NativeScript_canCall(ctx, &cache.angleChange, "gml_Script_scr_anglechange") ||
        !NativeScript_canCall(ctx, &cache.sndPlayX, "gml_Script_snd_play_x") ||
        !NativeScript_canCall(ctx, &cache.afterimageGrow, "gml_Script_scr_afterimage_grow") ||
        !NativeScript_canCall(ctx, &cache.afterimage, "gml_Script_scr_afterimage")) return;

    cache.speed = NativeScript_getSelfVarID(ctx, "_speed");
    cache.gravity = NativeScript_getSelfVarID(ctx, "_gravity");
    cache.maxspeed = NativeScript_getSelfVarID(ctx, "_maxspeed");
    cache.mydirection = NativeScript_getSelfVarID(ctx, "mydirection");
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.telegraph = NativeScript_getSelfVarID(ctx, "telegraph");
    cache.anglespeed = NativeScript_getSelfVarID(ctx, "anglespeed");
    cache.targetangle = NativeScript_getSelfVarID(ctx, "targetangle");
    cache.randx = NativeScript_getSelfVarID(ctx, "randx");
    cache.randy = NativeScript_getSelfVarID(ctx, "randy");
    cache.damage = NativeScript_getSelfVarID(ctx, "damage");
    cache.active = NativeScript_getSelfVarID(ctx, "active");
    cache.create2ndHitbox = NativeScript_getSelfVarID(ctx, "create_2nd_hitbox");
    cache.hitbox = NativeScript_getSelfVarID(ctx, "hitbox");
    cache.inst = NativeScript_getSelfVarID(ctx, "inst");
    cache.xrate = NativeScript_getSelfVarID(ctx, "xrate");
    cache.yrate = NativeScript_getSelfVarID(ctx, "yrate");
    cache.fade = NativeScript_getSelfVarID(ctx, "fade");
    cache.telegraphalpha = NativeScript_getSelfVarID(ctx, "telegraphalpha");
    cache.loopIndex = NativeScript_getSelfVarID(ctx, "i");

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sword_tunnel_sword_Step_0", swordStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sword_tunnel_sword_Draw_0", swordDraw);
}
