#include "nativeScripts/native_runtime.h"

#include "collision.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define CHASE_DRAW_VARS(X) \
    X(drawoffscreen) X(dodraw) X(tolerance) X(drawx) X(drawscale) X(facing) \
    X(specialspritecase) X(drawsiner) X(tempx) X(superscalex) X(superdrawx) \
    X(superscalexb) X(i) X(walk_index) X(mymidx) X(mymidy)

static struct {
#define DECLARE_ID(name) int32_t name;
    CHASE_DRAW_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t tasque, tasqueFlipped;
    NativeCallBinding onscreen, outline;
} cache;

#define CHASE_GET(name) Instance_getSelfVar(self, cache.name)
#define CHASE_REAL(name) RValue_toReal(CHASE_GET(name))
#define CHASE_SET(name, value) Instance_setSelfVar(self, cache.name, RValue_makeReal((GMLReal)(value)))

static bool chaseEqual(RValue value, GMLReal number, GMLReal epsilon) {
    return value.type != RVALUE_UNDEFINED && value.type != RVALUE_STRING &&
        NativeScript_compareReals(RValue_toReal(value), number, epsilon) == 0;
}

static void chaseCenteredSprite(Renderer* renderer, Instance* self, int32_t frame,
    GMLReal x, GMLReal y, GMLReal scaleX, GMLReal scaleY) {
    const Sprite* sprite = &renderer->dataWin->sprt.sprites[self->spriteIndex];
    GMLReal xoff = (GMLReal)sprite->originX * self->imageXscale;
    GMLReal yoff = (GMLReal)sprite->originY * self->imageYscale;
    GMLReal width = (GMLReal)sprite->width * self->imageXscale;
    GMLReal height = (GMLReal)sprite->width * self->imageYscale;
    Renderer_drawSpriteExt(renderer, self->spriteIndex, frame,
        (float)(x - ((width - xoff) * (scaleX - self->imageXscale)) / 2),
        (float)(y - ((height - yoff) * (scaleY - self->imageYscale)) / 2),
        (float)scaleX, (float)scaleY, (float)self->imageAngle, 255, self->imageAlpha * 0.2f);
}

static bool chaseenemyDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (renderer == nullptr || self->spriteIndex < 0 ||
        (uint32_t)self->spriteIndex >= ctx->dataWin->sprt.count) return false;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (chaseEqual(CHASE_GET(drawoffscreen), 0, epsilon)) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[self->spriteIndex];
        GMLReal tolerance = (GMLReal)sprite->height * self->imageYscale +
            (GMLReal)sprite->width * self->imageXscale;
        CHASE_SET(dodraw, 1);
        CHASE_SET(tolerance, tolerance);
        RValue args[] = { RValue_makeReal(self->instanceId), RValue_makeReal(tolerance) };
        RValue result = NativeScript_call(ctx, &cache.onscreen, "gml_Script_scr_onscreen_tolerance", args, 2);
        if (!RValue_toBool(result)) CHASE_SET(dodraw, 0);
        RValue_free(&result);
    } else {
        CHASE_SET(dodraw, 1);
    }
    *outResult = RValue_makeUndefined();
    if (!RValue_toBool(CHASE_GET(dodraw))) return true;

    bool normal = chaseEqual(CHASE_GET(specialspritecase), 0, epsilon);
    GMLReal scale = 1, drawx = 0;
    if (chaseEqual(CHASE_GET(facing), 1, epsilon) && normal) {
        scale = -1;
        drawx = (GMLReal)ctx->dataWin->sprt.sprites[self->spriteIndex].width * self->imageXscale;
    }
    CHASE_SET(drawx, drawx);
    CHASE_SET(drawscale, scale);
    GMLReal siner = CHASE_REAL(drawsiner) + (GMLReal)0.25;
    CHASE_SET(drawsiner, siner);
    GMLReal originalX = self->x;
    CHASE_SET(tempx, originalX);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(originalX + drawx), -1);
    GMLReal superScale = 0, superX = 0;
    CHASE_SET(superscalexb, 1);
    if (normal && !chaseEqual(CHASE_GET(facing), 0, epsilon)) {
        superScale = -4;
        superX = -(GMLReal)ctx->dataWin->sprt.sprites[self->spriteIndex].width * self->imageXscale * 2;
        CHASE_SET(superscalexb, -1);
    }
    CHASE_SET(superscalex, superScale);
    CHASE_SET(superdrawx, superX);
    RValue special = CHASE_GET(specialspritecase);
    if (special.type == RVALUE_STRING && strcmp(special.string, "tasque") == 0) {
        int32_t sprite = self->spriteIndex;
        if (chaseEqual(CHASE_GET(facing), 1, epsilon)) sprite = cache.tasqueFlipped;
        if (chaseEqual(CHASE_GET(facing), 0, epsilon)) sprite = cache.tasque;
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index", RValue_makeInt32(sprite), -1);
    }
    const Sprite* sprite = &ctx->dataWin->sprt.sprites[self->spriteIndex];
    GMLReal width = (GMLReal)sprite->width * self->imageXscale;
    GMLReal height = (GMLReal)sprite->height * self->imageYscale;
    int32_t frame = RValue_toInt32(CHASE_GET(walk_index));
    if (frame < 0) frame = (int32_t)self->imageIndex;
    renderer->vtable->gpuSetBlendMode(renderer, 1);
    for (int32_t i = 0; i < 5; ++i) {
        CHASE_SET(i, i);
        GMLReal aura = i * 9 + GMLReal_fmod(siner * 3, 9);
        GMLReal ax = aura * (GMLReal)0.75 + GMLReal_sin(aura / 4) * 4;
        GMLReal ay = 45 * (-GMLReal_cos((aura / 45) * (GMLReal)1.5707963267948966) + 1);
        GMLReal yscale = GMLReal_fmin(80 / height, 1);
        Renderer_drawSpriteExt(renderer, self->spriteIndex, frame,
            (float)(self->x - (ax / 180) * scale * width),
            (float)(self->y - (ay / 82) * height * yscale),
            (float)((self->imageXscale + ax / 36) * scale),
            (float)(self->imageYscale + (ay / 36) * yscale),
            (float)self->imageAngle, 255, (float)(self->imageAlpha * (1 - ay / 45) * (GMLReal)0.5));
    }
    CHASE_SET(i, 5);
    renderer->vtable->gpuSetBlendMode(renderer, 0);
    GMLReal xmult = GMLReal_fmin((70 / width) * 4, 4);
    GMLReal ymult = GMLReal_fmin((80 / height) * 5, 5);
    GMLReal ysmult = GMLReal_fmin((80 / height) * (GMLReal)0.2, (GMLReal)0.2);
    GMLReal sine = GMLReal_sin(siner / 5), cosine = GMLReal_cos(siner / 5);
    chaseCenteredSprite(renderer, self, frame, self->x + superX + sine * xmult,
        self->y + cosine * ymult, self->imageXscale + superScale, self->imageYscale + sine * ysmult);
    chaseCenteredSprite(renderer, self, frame, self->x + superX - sine * xmult,
        self->y - cosine * ymult, self->imageXscale + superScale, self->imageYscale - sine * ysmult);
    RValue args[] = {
        RValue_makeInt32(self->spriteIndex), CHASE_GET(walk_index),
        RValue_makeReal(self->x), RValue_makeReal(self->y),
        RValue_makeReal(self->imageXscale * scale), RValue_makeReal(self->imageYscale),
        RValue_makeReal(self->imageAngle), RValue_makeReal(255),
        RValue_makeReal((GMLReal)self->imageAlpha * (GMLReal)0.3), RValue_makeReal(2),
    };
    RValue result = NativeScript_call(ctx, &cache.outline, "gml_Script_scr_draw_outline_ext", args, 10);
    RValue_free(&result);
    Renderer_drawSpriteExt(renderer, self->spriteIndex, frame, self->x, self->y,
        (float)(self->imageXscale * scale), self->imageYscale, (float)self->imageAngle,
        self->imageBlend, self->imageAlpha);
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(originalX), -1);
    CHASE_SET(mymidx, self->x + width / 2);
    InstanceBBox bbox = Collision_computeBBox(ctx->runner, self);
    CHASE_SET(mymidy, self->y + ((bbox.valid ? bbox.bottom : self->y) - self->y) / 2);
    return true;
}

void NativeChapter2ChaseenemyDraw_register(VMContext* ctx) {
    cache.tasque = -1;
    cache.tasqueFlipped = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (!sprite->present) continue;
        if (strcmp(sprite->name, "spr_tasque_idle") == 0) cache.tasque = (int32_t)i;
        else if (strcmp(sprite->name, "spr_tasque_idle_flipped") == 0) cache.tasqueFlipped = (int32_t)i;
    }
    cache.onscreen = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    cache.outline = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    if (cache.tasque < 0 || cache.tasqueFlipped < 0 ||
        !NativeScript_canCall(ctx, &cache.onscreen, "gml_Script_scr_onscreen_tolerance") ||
        !NativeScript_canCall(ctx, &cache.outline, "gml_Script_scr_draw_outline_ext") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_chaseenemy_Draw_0", chaseenemyDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    CHASE_DRAW_VARS(RESOLVE_SELF)
#undef RESOLVE_SELF
}
