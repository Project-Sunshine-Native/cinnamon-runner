#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

typedef struct {
    const VMContext* owner;
    int32_t debuggo, siner, angle, armtest, color;
    int32_t xhand1, yhand1, rdistx, rdisty;
    int32_t point1x, point1y, armlength, armangle, armsize, armoff;
    int32_t partx, party;
} AsgoreSpearCache;

static AsgoreSpearCache cache;
static NativeAssetBinding bodyBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding armBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding rightHandBinding = NATIVE_ASSET_BINDING_INIT;
static NativeAssetBinding leftHandBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding lengthdirXBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding lengthdirYBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding distanceBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding directionBinding = NATIVE_CALL_BINDING_INIT;

static void resolveCache(VMContext* ctx) {
    if (cache.owner == ctx) return;
    cache.owner = ctx;
#define RESOLVE(field) cache.field = NativeScript_getSelfVarID(ctx, #field)
    RESOLVE(debuggo);
    RESOLVE(siner);
    RESOLVE(angle);
    RESOLVE(armtest);
    RESOLVE(color);
    RESOLVE(xhand1);
    RESOLVE(yhand1);
    RESOLVE(rdistx);
    RESOLVE(rdisty);
    RESOLVE(point1x);
    RESOLVE(point1y);
    RESOLVE(armlength);
    RESOLVE(armangle);
    RESOLVE(armsize);
    RESOLVE(armoff);
    RESOLVE(partx);
    RESOLVE(party);
#undef RESOLVE
}

static GMLReal getNumber(Instance* instance, int32_t id) {
    return RValue_toReal(Instance_getSelfVar(instance, id));
}

static void setNumber(Instance* instance, int32_t id, GMLReal value) {
    Instance_setSelfVar(instance, id, RValue_makeReal(value));
}

static GMLReal getArrayNumber(Instance* instance, int32_t id, int32_t index) {
    RValue value = Instance_getSelfVar(instance, id);
    return RValue_toReal(NativeScript_getIndex(value, RValue_makeReal(index)));
}

static bool asgorespearDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    resolveCache(ctx);
    // Debug controls spawn afterimages; keep those calls on the original bytecode path.
    if (getNumber(self, cache.debuggo) == 1.0) return false;
    if (!NativeScript_canResolveAsset(ctx, &armBinding, "spr_asgoreb_ballarm") ||
        !NativeScript_canResolveAsset(ctx, &rightHandBinding, "spr_spearhandr") ||
        !NativeScript_canResolveAsset(ctx, &leftHandBinding, "spr_spearhandl")) return false;

    Instance* body = nullptr;
    if (NativeScript_canResolveAsset(ctx, &bodyBinding, "obj_asgoreb_body") &&
        (uint32_t)bodyBinding.assetIndex < ctx->dataWin->objt.count) {
        Instance** instances = runner->instancesByObject[bodyBinding.assetIndex];
        for (ptrdiff_t i = 0; i < arrlen(instances); ++i) {
            if (instances[i]->active) {
                body = instances[i];
                break;
            }
        }
    }

    GMLReal siner = getNumber(self, cache.siner) + 1.0;
    setNumber(self, cache.siner, siner);
    GMLReal wave = GMLReal_sin(siner / 15.0);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + wave * 0.3), -1);
    GMLReal angle = getNumber(self, cache.angle) + wave * 0.02;
    setNumber(self, cache.angle, angle);

    RValue vectorArgs[2] = {RValue_makeReal(55.0), RValue_makeReal(angle)};
    GMLReal xhand1 = RValue_toReal(NativeScript_call(ctx, &lengthdirXBinding, "lengthdir_x", vectorArgs, 2));
    GMLReal yhand1 = RValue_toReal(NativeScript_call(ctx, &lengthdirYBinding, "lengthdir_y", vectorArgs, 2));
    setNumber(self, cache.xhand1, xhand1);
    setNumber(self, cache.yhand1, yhand1);
    GMLReal rdistx = self->x + xhand1 * 2.0;
    GMLReal rdisty = self->y + yhand1 * 2.0;
    setNumber(self, cache.rdistx, rdistx);
    setNumber(self, cache.rdisty, rdisty);

    if (getNumber(self, cache.armtest) == 1.0 && body != nullptr) {
        GMLReal point1y = getArrayNumber(body, cache.party, 5) + 64.0 + body->y;
        GMLReal point1x = getArrayNumber(body, cache.partx, 5) + 14.0 + body->x;
        setNumber(self, cache.point1y, point1y);
        setNumber(self, cache.point1x, point1x);
        RValue armArgs[4] = {RValue_makeReal(point1x), RValue_makeReal(point1y),
            RValue_makeReal(self->x - xhand1), RValue_makeReal(self->y - yhand1)};
        GMLReal armlength = RValue_toReal(NativeScript_call(ctx, &distanceBinding, "point_distance", armArgs, 4));
        GMLReal armangle = RValue_toReal(NativeScript_call(ctx, &directionBinding, "point_direction", armArgs, 4));
        GMLReal armsize = armlength / 40.0;
        if (armsize < 0.35) armsize = 0.0;
        setNumber(self, cache.armlength, armlength);
        setNumber(self, cache.armangle, armangle);
        setNumber(self, cache.armsize, armsize);
        Renderer_drawSpriteExt(renderer, armBinding.assetIndex, 0, (float)point1x, (float)point1y,
            (float)(armsize * 2.0), 2.0f, (float)armangle, 0xFFFFFFu, 1.0f);

        point1y = getArrayNumber(body, cache.party, 4) + 64.0 + body->y;
        point1x = getArrayNumber(body, cache.partx, 4) + 34.0 + body->x;
        setNumber(self, cache.point1y, point1y);
        setNumber(self, cache.point1x, point1x);
        armArgs[0] = RValue_makeReal(point1x);
        armArgs[1] = RValue_makeReal(point1y);
        armArgs[2] = RValue_makeReal(rdistx);
        armArgs[3] = RValue_makeReal(rdisty);
        armlength = RValue_toReal(NativeScript_call(ctx, &distanceBinding, "point_distance", armArgs, 4));
        setNumber(self, cache.armlength, armlength);
        if (armlength > 100.0) {
            GMLReal armoff = (armlength - 100.0) / 2.0;
            setNumber(self, cache.armoff, armoff);
            vectorArgs[0] = RValue_makeReal(55.0 - armoff);
            rdistx = self->x + RValue_toReal(NativeScript_call(ctx, &lengthdirXBinding,
                "lengthdir_x", vectorArgs, 2)) * 2.0;
            rdisty = self->y + RValue_toReal(NativeScript_call(ctx, &lengthdirYBinding,
                "lengthdir_y", vectorArgs, 2)) * 2.0;
            setNumber(self, cache.rdistx, rdistx);
            setNumber(self, cache.rdisty, rdisty);
            armArgs[2] = RValue_makeReal(rdistx);
            armArgs[3] = RValue_makeReal(rdisty);
            armlength = RValue_toReal(NativeScript_call(ctx, &distanceBinding, "point_distance", armArgs, 4));
            setNumber(self, cache.armlength, armlength);
        }
        armangle = RValue_toReal(NativeScript_call(ctx, &directionBinding, "point_direction", armArgs, 4));
        setNumber(self, cache.armangle, armangle);
        if (armangle > 100.0) {
            point1y -= 12.0;
            setNumber(self, cache.point1y, point1y);
        }
        armsize = armlength / 40.0;
        if (armsize < 0.6) armsize = 0.0;
        setNumber(self, cache.armsize, armsize);
        Renderer_drawSpriteExt(renderer, armBinding.assetIndex, 0, (float)point1x, (float)point1y,
            (float)(armsize * 2.0), 2.0f, (float)armangle, 0xFFFFFFu, 1.0f);
    }

    int32_t subimage = (int32_t)self->imageIndex;
    uint32_t color = RValue_toColour(Instance_getSelfVar(self, cache.color));
    Renderer_drawSpriteExt(renderer, self->spriteIndex, subimage, self->x, self->y,
        2.0f, 2.0f, (float)angle, color, 1.0f);
    Renderer_drawSpriteExt(renderer, rightHandBinding.assetIndex, subimage, (float)rdistx, (float)rdisty,
        2.0f, 2.0f, (float)angle, 0xFFFFFFu, 1.0f);
    Renderer_drawSpriteExt(renderer, leftHandBinding.assetIndex, subimage,
        (float)(self->x - xhand1), (float)(self->y - yhand1), 2.0f, 2.0f, (float)angle, 0xFFFFFFu, 1.0f);
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeAsgorespearDraw_register(VMContext* ctx) {
    cache.owner = nullptr;
    bodyBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    armBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    rightHandBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    leftHandBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    lengthdirXBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    lengthdirYBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    distanceBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    directionBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_asgorespear_Draw_0", asgorespearDraw);
}
