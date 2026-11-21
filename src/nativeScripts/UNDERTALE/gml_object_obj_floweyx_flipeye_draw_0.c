#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "math_compat.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t con, md, frozen, desperate, siner, grgrgrgr, op;
    int32_t siner2, gr, grgr, grgrgr, durara, oner, wimpy, memorymode, opx, eba, ebb;
} cache;
static NativeVarBinding debugBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding holderBgBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding bgBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding eyeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding irisBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding pupilBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding holderBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding flashEyeBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding flashIrisBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding flashPupilBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding bulletBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding heartBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding chooseBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding colorBinding = NATIVE_CALL_BINDING_INIT;

static void chargeColors(VMContext* ctx, Instance* self) {
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner2)) + 1.2;
    Instance_setSelfVar(self, cache.siner2, RValue_makeReal(siner));
    Instance_setSelfVar(self, cache.md, RValue_makeReal(1.0));
    GMLReal colors[9];
    for (int i = 0; i < 9; ++i) colors[i] = 170.0 + GMLReal_sin((siner + i) / 2.0) * 70.0;
    const int channels[3][3] = {{0, 3, 6}, {1, 4, 7}, {2, 4, 8}};
    const int32_t variables[] = {cache.gr, cache.grgr, cache.grgrgr};
    for (int i = 0; i < 3; ++i) {
        RValue args[3] = {RValue_makeReal(colors[channels[i][0]]),
            RValue_makeReal(colors[channels[i][1]]), RValue_makeReal(colors[channels[i][2]])};
        RValue color = NativeScript_call(ctx, &colorBinding, "make_color_rgb", args, 3);
        Instance_setSelfVar(self, variables[i], color);
        RValue_free(&color);
    }
}

static void spawnEyeshot(VMContext* ctx, Instance* self, GMLReal x, int32_t variable, bool right) {
    Instance* bullet = Runner_createInstance(ctx->runner, x, self->y, bulletBinding.assetIndex);
    if (bullet != nullptr && ctx->creatorVarID >= 0)
        Instance_setSelfVar(bullet, ctx->creatorVarID, RValue_makeReal(self->instanceId));
    Instance_setSelfVar(self, variable,
        RValue_makeReal(bullet != nullptr ? (GMLReal)bullet->instanceId : (GMLReal)INSTANCE_NOONE));
    if (bullet == nullptr) return;
    Instance_setSelfVar(bullet, cache.memorymode, Instance_getSelfVar(self, cache.memorymode));
    Instance_setSelfVar(bullet, cache.durara, Instance_getSelfVar(self, cache.durara));
    Instance_setSelfVar(bullet, cache.oner, Instance_getSelfVar(self, cache.oner));
    GMLReal wimpy = RValue_toReal(Instance_getSelfVar(self, cache.wimpy));
    GMLReal epsilon = ctx->runner->mathEpsilon;
    bool strong = NativeScript_compareReals(wimpy, 0.0, epsilon) == 0;
    bool weak = NativeScript_compareReals(wimpy, 1.0, epsilon) == 0;
    if (!strong && !weak) return;
    Instance* heart = VM_findInstanceByTarget(ctx, heartBinding.assetIndex);
    GMLReal dx = (heart->x + 8.0) - bullet->x;
    GMLReal dy = (heart->y + 8.0) - bullet->y;
    GMLReal direction = GMLReal_atan2(-dy, dx) * (180.0 / M_PI);
    if (direction < 0.0) direction += 360.0;
    bullet->direction = (float)direction;
    GMLReal durara = RValue_toReal(Instance_getSelfVar(bullet, cache.durara));
    bullet->speed = (float)((strong ? 7.0 : 3.0) - durara * 0.1);
    bullet->friction = (float)(strong ? -0.2 + durara * 0.012 : -0.1 - durara * 0.02);
    GMLReal offset = 18.0 * durara;
    if (!right) offset = -offset;
    VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_DIRECTION, "direction",
        RValue_makeReal((GMLReal)bullet->direction + offset), -1);
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(bullet, cache.oner)), 1.0, epsilon) == 0)
        VMBuiltins_setVariable(ctx, bullet, BUILTIN_VAR_DIRECTION, "direction",
            RValue_makeReal((GMLReal)bullet->direction + (strong ? 9.0 : 14.0)), -1);
}

static bool flipeyeDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(NativeScript_getVariable(ctx, ctx->globalScopeInstance,
            &debugBinding, "debug")), 1.0, epsilon) == 0) return false;
    GMLReal frozen = RValue_toReal(Instance_getSelfVar(self, cache.frozen));
    bool moving = NativeScript_compareReals(frozen, 0.0, epsilon) == 0;
    if (!moving && NativeScript_compareReals(frozen, 4.0, epsilon) != 0) return false;
    if (!NativeScript_canResolveAsset(ctx, &holderBgBinding, "spr_flipeye_holder_bg") ||
        !NativeScript_canResolveAsset(ctx, &bgBinding, "spr_flipeye_bg") ||
        !NativeScript_canResolveAsset(ctx, &eyeBinding, "spr_flipeye_noiris") ||
        !NativeScript_canResolveAsset(ctx, &irisBinding, "spr_flipeye_iris") ||
        !NativeScript_canResolveAsset(ctx, &pupilBinding, "spr_flipeye_pupil") ||
        !NativeScript_canResolveAsset(ctx, &holderBinding, "spr_flipeye_holder") ||
        !NativeScript_canResolveAsset(ctx, &flashEyeBinding, "spr_flipeye_noiris_flash") ||
        !NativeScript_canResolveAsset(ctx, &flashIrisBinding, "spr_flipeye_iris_flash") ||
        !NativeScript_canResolveAsset(ctx, &flashPupilBinding, "spr_flipeye_pupil_flash") ||
        !NativeScript_canResolveAsset(ctx, &bulletBinding, "obj_eyeshot_a") ||
        !NativeScript_canResolveAsset(ctx, &heartBinding, "obj_vsflowey_heart")) return false;
    GMLReal con = RValue_toReal(Instance_getSelfVar(self, cache.con));
    if ((NativeScript_compareReals(con, 4.0, epsilon) == 0 ||
        NativeScript_compareReals(con, 5.0, epsilon) == 0) &&
        VM_findInstanceByTarget(ctx, heartBinding.assetIndex) == nullptr) return false;

    GMLReal desperate = RValue_toReal(Instance_getSelfVar(self, cache.desperate));
    bool normal = NativeScript_compareReals(desperate, 0.0, epsilon) == 0;
    bool urgent = NativeScript_compareReals(desperate, 1.0, epsilon) == 0;
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    if (NativeScript_compareReals(con, 0.0, epsilon) == 0 && moving) {
        siner += 1.0;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
    if (NativeScript_compareReals(con, 1.0, epsilon) == 0) {
        con = 3.0;
        Instance_setSelfVar(self, cache.con, RValue_makeReal(con));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(4.0), 4);
    }
    if (NativeScript_compareReals(con, 3.0, epsilon) == 0) chargeColors(ctx, self);
    if (NativeScript_compareReals(con, 4.0, epsilon) == 0) {
        con = 5.0;
        Instance_setSelfVar(self, cache.con, RValue_makeReal(con));
        Instance_setSelfVar(self, cache.durara, RValue_makeReal(0.0));
        RValue args[2] = {RValue_makeReal(0.0), RValue_makeReal(1.0)};
        RValue oner = NativeScript_call(ctx, &chooseBinding, "choose", args, 2);
        Instance_setSelfVar(self, cache.oner, oner);
        RValue_free(&oner);
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_ALARM, "alarm", RValue_makeReal(7.0), 4);
        self->imageBlend = 0xFFFFFFu;
    }
    if (NativeScript_compareReals(con, 5.0, epsilon) == 0) {
        Instance_setSelfVar(self, cache.opx, RValue_makeReal(0.0));
        spawnEyeshot(ctx, self, self->x, cache.eba, false);
        RValue op = Instance_getSelfVar(self, cache.op);
        Instance_setSelfVar(self, cache.opx, op);
        spawnEyeshot(ctx, self, self->x + RValue_toReal(op), cache.ebb, true);
        GMLReal durara = RValue_toReal(Instance_getSelfVar(self, cache.durara)) + 1.0;
        Instance_setSelfVar(self, cache.durara, RValue_makeReal(durara));
        Instance_setSelfVar(self, cache.md, RValue_makeReal(1.0));
        bool white = NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.gr)), 16777215.0, epsilon) == 0;
        Instance_setSelfVar(self, cache.gr, RValue_makeReal(white ? 255.0 : 16777215.0));
        Instance_setSelfVar(self, cache.grgr, RValue_makeReal(white ? 0.0 : 16777215.0));
        Instance_setSelfVar(self, cache.grgrgr, RValue_makeReal(white ? 0.0 : 16777215.0));
    }
    if (NativeScript_compareReals(con, 6.0, epsilon) == 0) {
        Instance_setSelfVar(self, cache.gr, RValue_makeReal(16777215.0));
        Instance_setSelfVar(self, cache.grgr, RValue_makeReal(16777215.0));
        Instance_setSelfVar(self, cache.grgrgr, RValue_makeReal(16777215.0));
        Instance_setSelfVar(self, cache.md, RValue_makeReal(0.0));
        self->imageBlend = 0xFFFFFFu;
        Instance_setSelfVar(self, cache.con, RValue_makeReal(0.0));
    }
    if (NativeScript_compareReals(con, 10.0, epsilon) == 0) chargeColors(ctx, self);
    if (urgent && moving) {
        siner += 0.5;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
    GMLReal md = RValue_toReal(Instance_getSelfVar(self, cache.md));
    bool plain = NativeScript_compareReals(md, 0.0, epsilon) == 0;
    bool flash = NativeScript_compareReals(md, 1.0, epsilon) == 0;
    int32_t eyeSprite = flash ? flashEyeBinding.assetIndex : eyeBinding.assetIndex;
    int32_t irisSprite = flash ? flashIrisBinding.assetIndex : irisBinding.assetIndex;
    int32_t pupilSprite = flash ? flashPupilBinding.assetIndex : pupilBinding.assetIndex;
    uint32_t eyeColor = flash ? RValue_toColour(Instance_getSelfVar(self, cache.grgrgr)) : self->imageBlend;
    uint32_t irisColor = flash ? RValue_toColour(Instance_getSelfVar(self, cache.grgr)) : self->imageBlend;
    uint32_t pupilColor = flash ? RValue_toColour(Instance_getSelfVar(self, cache.gr)) : self->imageBlend;
    GMLReal sine = GMLReal_sin(siner / 3.0);
    GMLReal cosine = GMLReal_cos(siner / 3.0);
    GMLReal holderAngle = GMLReal_sin(siner / 4.0) * 2.0;
    GMLReal eyeAngle = GMLReal_sin(siner / 2.0) * 4.0;
    uint32_t bgColor = RValue_toColour(Instance_getSelfVar(self, cache.grgrgrgr));
    int32_t frame = (int32_t)self->imageIndex;
    Renderer_drawSpriteExt(renderer, holderBgBinding.assetIndex, frame, self->x,
        (float)(self->y + cosine * 2.0), 0.8f, 0.8f, (float)holderAngle, bgColor, 1.0f);
    Renderer_drawSpriteExt(renderer, bgBinding.assetIndex, frame, (float)(self->x - 5.0),
        (float)(self->y + cosine * 3.0), 1.0f, 1.0f, (float)holderAngle, bgColor, 1.0f);
    if (plain || flash) Renderer_drawSpriteExt(renderer, eyeSprite, frame, self->x,
        (float)(-4.0 + self->y + sine * 2.0), 0.8f, 0.8f, (float)eyeAngle, eyeColor, 1.0f);
    if (plain || flash) Renderer_drawSpriteExt(renderer, irisSprite, frame, self->x,
        (float)(-6.0 + self->y + sine * 4.0), 0.8f, 0.8f, (float)eyeAngle, irisColor, 1.0f);
    if ((plain && normal) || flash) {
        Renderer_drawSpriteExt(renderer, pupilSprite, frame, self->x,
            (float)(-5.0 + self->y + sine * 2.0), (float)(0.8 - sine * 0.4),
            (float)(1.0 - sine * 0.4), 0.0f, pupilColor, 1.0f);
    }
    if (plain && urgent) {
        GMLReal scale = 0.8 - sine * 0.3;
        Renderer_drawSpriteExt(renderer, pupilBinding.assetIndex, frame, self->x,
            (float)(-5.0 + self->y + sine * 2.5), (float)scale, (float)scale, 0.0f, self->imageBlend, 1.0f);
    }
    Renderer_drawSpriteExt(renderer, holderBinding.assetIndex, frame, self->x,
        (float)(self->y + cosine * 2.0), 0.8f, 0.8f, (float)holderAngle, self->imageBlend, 1.0f);
    Instance_setSelfVar(self, cache.op, RValue_makeReal(126.0));
    GMLReal x = self->x + 126.0;
    Renderer_drawSpriteExt(renderer, holderBgBinding.assetIndex, frame, (float)x,
        (float)(self->y + cosine * 2.0), -0.8f, 0.8f, (float)-holderAngle, bgColor, 1.0f);
    Renderer_drawSpriteExt(renderer, bgBinding.assetIndex, frame, (float)(self->x + 5.0 + 126.0),
        (float)(self->y + cosine * 3.0), -1.0f, 1.0f, (float)holderAngle, bgColor, 1.0f);
    if (plain || flash) Renderer_drawSpriteExt(renderer, eyeSprite, frame, (float)x,
        (float)(-4.0 + self->y + sine * 2.0), -0.8f, 0.8f, (float)-eyeAngle, eyeColor, 1.0f);
    if (plain || flash) Renderer_drawSpriteExt(renderer, irisSprite, frame, (float)x,
        (float)(-6.0 + self->y + sine * 4.0), -0.8f, 0.8f, (float)-eyeAngle, irisColor, 1.0f);
    if ((plain && normal) || flash) {
        Renderer_drawSpriteExt(renderer, pupilSprite, frame, (float)x,
            (float)(-5.0 + self->y + sine * 2.0), (float)(-1.0 + cosine * 0.4),
            (float)(1.0 - cosine * 0.4), 0.0f, pupilColor, 1.0f);
    }
    if (plain && urgent) {
        GMLReal scale = 0.9 - cosine * 0.3;
        Renderer_drawSpriteExt(renderer, pupilBinding.assetIndex, frame, (float)x,
            (float)(-5.0 + self->y + sine * 2.5), (float)(-0.9 + cosine * 0.3),
            (float)scale, 0.0f, self->imageBlend, 1.0f);
    }
    Renderer_drawSpriteExt(renderer, holderBinding.assetIndex, frame, (float)x,
        (float)(self->y + cosine * 2.0), -0.8f, 0.8f, (float)-holderAngle, self->imageBlend, 1.0f);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeFloweyFlipeyeDraw_register(VMContext* ctx) {
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.md = NativeScript_getSelfVarID(ctx, "md");
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.desperate = NativeScript_getSelfVarID(ctx, "desperate");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.grgrgrgr = NativeScript_getSelfVarID(ctx, "grgrgrgr");
    cache.op = NativeScript_getSelfVarID(ctx, "op");
    cache.siner2 = NativeScript_getSelfVarID(ctx, "siner2");
    cache.gr = NativeScript_getSelfVarID(ctx, "gr");
    cache.grgr = NativeScript_getSelfVarID(ctx, "grgr");
    cache.grgrgr = NativeScript_getSelfVarID(ctx, "grgrgr");
    cache.durara = NativeScript_getSelfVarID(ctx, "durara");
    cache.oner = NativeScript_getSelfVarID(ctx, "oner");
    cache.wimpy = NativeScript_getSelfVarID(ctx, "wimpy");
    cache.memorymode = NativeScript_getSelfVarID(ctx, "memorymode");
    cache.opx = NativeScript_getSelfVarID(ctx, "opx");
    cache.eba = NativeScript_getSelfVarID(ctx, "eba");
    cache.ebb = NativeScript_getSelfVarID(ctx, "ebb");
    debugBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    holderBgBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    bgBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    eyeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    irisBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    pupilBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    holderBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    flashEyeBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    flashIrisBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    flashPupilBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    bulletBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    heartBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    chooseBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    colorBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_floweyx_flipeye_Draw_0", flipeyeDraw);
}
