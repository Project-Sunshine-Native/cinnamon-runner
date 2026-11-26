#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "runner.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define BULLET_VARS(X) \
    X(master) X(cupDistanceFromCenter) X(spin) X(circleX) X(circleY) X(fakedepth) \
    X(bouncing) X(bounceCurrent) X(depthSeparation) X(state) X(spinrate) X(hitType) \
    X(sizefactor) X(blendo) X(colClose) X(colFar) X(cutOff) X(bouncesLeft) \
    X(bounceHeight) X(bounceTime) X(bounceProgress) X(bouncenoise) X(bulletHitHeight) \
    X(groundForgiveness) X(cup_char) X(partySize) X(bulletsApplyToAll) X(bulletCollisionAngle)

static struct {
#define DECLARE_ID(name) int32_t name;
    BULLET_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t teacupObject;
} cache;

#define BULLET_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define BULLET_REAL(inst, name) RValue_toReal(BULLET_GET(inst, name))
#define BULLET_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static GMLReal bulletApproach(GMLReal value, GMLReal target, GMLReal amount, GMLReal epsilon) {
    if (NativeScript_compareReals(value, target, epsilon) < 0) {
        value += amount;
        return NativeScript_compareReals(value, target, epsilon) > 0 ? target : value;
    }
    value -= amount;
    return NativeScript_compareReals(value, target, epsilon) < 0 ? target : value;
}

static bool teacupBulletStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    GMLReal epsilon = runner->mathEpsilon;
    int32_t hitType = RValue_toInt32(BULLET_GET(self, hitType));
    if (hitType == 5 || self->spriteIndex < 0 ||
        (uint32_t)self->spriteIndex >= ctx->dataWin->sprt.count) return false;
    Instance* master = VM_findInstanceByTarget(ctx, RValue_toInt32(BULLET_GET(self, master)));
    Instance* ride = VM_findInstanceByTarget(ctx, cache.teacupObject);
    if (master == nullptr || ride == nullptr) return false;
    GMLReal distance = BULLET_REAL(self, cupDistanceFromCenter);
    if (NativeScript_compareReals(distance, 0, epsilon) == 0)
        distance = BULLET_REAL(master, cupDistanceFromCenter);
    GMLReal spin = BULLET_REAL(self, spin);
    GMLReal radians = spin * (M_PI / 180.0);
    GMLReal circleX = distance * GMLReal_cos(radians);
    GMLReal circleY = -(distance * (GMLReal)0.5) * GMLReal_sin(radians);
    if (GMLReal_fabs(circleX) < (GMLReal)1e-4) circleX = 0;
    if (GMLReal_fabs(circleY) < (GMLReal)1e-4) circleY = 0;
    GMLReal fakeDepth = circleY / 16;
    bool bouncing = RValue_toBool(BULLET_GET(self, bouncing));
    GMLReal bounce = BULLET_REAL(self, bounceCurrent);
    float ystart = bouncing ? master->y : self->ystart;
    float x = (float)((GMLReal)self->xstart + circleX);
    float y = (float)(((GMLReal)ystart + circleY) - bounce);
    GMLReal depth = master->depth - BULLET_REAL(master, depthSeparation) - circleY - 5;
    if (NativeScript_compareReals(BULLET_REAL(ride, state), 5, epsilon) == 0)
        spin += BULLET_REAL(self, spinrate);
    GMLReal size = fakeDepth / 8;
    float scale = (float)bulletApproach(self->imageXscale, 2 + size, (GMLReal)0.05, epsilon);
    GMLReal blend = (GMLReal)0.3 - size / 2;
    if (NativeScript_compareReals(blend, 0, epsilon) < 0) blend = 0;
    const Sprite* sprite = &ctx->dataWin->sprt.sprites[self->spriteIndex];
    GMLReal height = (GMLReal)sprite->height * scale;
    GMLReal offset = (GMLReal)sprite->originY * scale;
    GMLReal cutoff = ((y + height) - offset) - (master->y + circleY);
    cutoff = GMLReal_fmax(cutoff, 0);
    if (NativeScript_compareReals(cutoff, height, epsilon) > 0) return false;

    GMLReal bounces = BULLET_REAL(self, bouncesLeft);
    GMLReal bounceHeight = BULLET_REAL(self, bounceHeight);
    GMLReal bounceTime = BULLET_REAL(self, bounceTime);
    GMLReal progress = BULLET_REAL(self, bounceProgress);
    bool bounceNoise = false;
    if (hitType == 3) {
        if (NativeScript_compareReals(cutoff, 0, epsilon) > 0 &&
            NativeScript_compareReals(bounces, 0, epsilon) > 0 && !bouncing) {
            bounceNoise = true;
            bounces--;
            bouncing = true;
            bounceHeight = 128;
            bounceTime = 1;
            bounce = 0;
            progress = ((GMLReal)1 / 15) / bounceTime;
        }
        if (bouncing) {
            progress += ((GMLReal)1 / 30) / bounceTime;
            bounce = GMLReal_fabs(GMLReal_sin(progress * (GMLReal)M_PI) * bounceHeight);
            if (NativeScript_compareReals(progress, 1, epsilon) >= 0) {
                bounceNoise = true;
                bounces--;
                progress--;
                if (NativeScript_compareReals(bounces, 0, epsilon) < 0) {
                    bouncing = false;
                    bounce = 0;
                }
            }
        }
    }
    if (NativeScript_compareReals(ystart - bounce, master->y - BULLET_REAL(self, bulletHitHeight), epsilon) > 0 &&
        NativeScript_compareReals(cutoff / height, BULLET_REAL(self, groundForgiveness), epsilon) < 0) {
        RValue cups = BULLET_GET(master, cup_char);
        GMLArray* array = cups.type == RVALUE_ARRAY ? cups.array : nullptr;
        for (int32_t i = 0; NativeScript_compareReals(i, BULLET_REAL(master, partySize), epsilon) < 0; ++i) {
            if (!RValue_toBool(BULLET_GET(master, bulletsApplyToAll)) && i != 0) continue;
            Instance* cup = VM_findInstanceByTarget(ctx, RValue_toInt32(GMLArray_get(array, i)));
            if (cup == nullptr) return false;
            GMLReal angle = GMLReal_fmod(GMLReal_fmod(spin - BULLET_REAL(cup, spin), 360) + 540, 360) - 180;
            if (NativeScript_compareReals(GMLReal_fabs(angle), BULLET_REAL(self, bulletCollisionAngle), epsilon) < 0)
                return false;
        }
    }

    BULLET_SET(self, cupDistanceFromCenter, distance);
    BULLET_SET(self, circleX, circleX);
    BULLET_SET(self, circleY, circleY);
    BULLET_SET(self, fakedepth, fakeDepth);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(x), -1);
    if (RValue_toBool(BULLET_GET(self, bouncing))) self->ystart = ystart;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(y), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_DEPTH, "depth", RValue_makeReal(depth), -1);
    BULLET_SET(self, spin, spin);
    BULLET_SET(self, sizefactor, size);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale", RValue_makeReal(scale), -1);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale", RValue_makeReal(scale), -1);
    BULLET_SET(self, blendo, blend);
    self->imageBlend = Color_lerp(RValue_toColour(BULLET_GET(self, colClose)),
        RValue_toColour(BULLET_GET(self, colFar)), (float)blend);
    BULLET_SET(self, cutOff, cutoff);
    if (hitType == 3 && (RValue_toBool(BULLET_GET(self, bouncing)) || bounceNoise)) {
        BULLET_SET(self, bouncesLeft, bounces);
        BULLET_SET(self, bouncing, bouncing);
        BULLET_SET(self, bounceHeight, bounceHeight);
        BULLET_SET(self, bounceTime, bounceTime);
        BULLET_SET(self, bounceCurrent, bounce);
        BULLET_SET(self, bounceProgress, progress);
        if (bounceNoise) BULLET_SET(master, bouncenoise, 1);
    }
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2TeacupBulletStep_register(VMContext* ctx) {
    cache.teacupObject = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && strcmp(object->name, "obj_teacup") == 0)
            cache.teacupObject = (int32_t)i;
    }
    if (cache.teacupObject < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_teacup_bullet_Step_0", teacupBulletStep)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    BULLET_VARS(RESOLVE_SELF)
#undef RESOLVE_SELF
}
