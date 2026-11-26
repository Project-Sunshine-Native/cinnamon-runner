#include "nativeScripts/native_runtime.h"

#include "collision.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "stb_ds.h"
#include "vm_builtins.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#define CUP_DRAW_VARS(X) \
    X(mode) X(held) X(riseSpeed) X(camFollowMultiplier) X(adjustForCamera) \
    X(heldAmount) X(startOffset) X(screwSegHeight) X(i) X(spin) X(shadowSurf) \
    X(master) X(closenessToRide) X(circleY) X(cutOff) X(spriteHeightRoot) X(lineToCenter) \
    X(drawtutorialarrows) X(drawtutorialarrows_alpha) X(idletimer) X(cupCharExists) X(debugHitThisFrame)

static struct {
#define DECLARE_ID(name) int32_t name;
    CUP_DRAW_VARS(DECLARE_ID)
#undef DECLARE_ID
    int32_t screw, screwEnd, base, platform, center, arrow, bulletObject, mazeRoom;
    BuiltinFunc surfaceCreate, surfaceExists, surfaceTarget, surfaceReset, drawSurface;
} cache;

#define CUP_GET(inst, name) Instance_getSelfVar((inst), cache.name)
#define CUP_REAL(inst, name) RValue_toReal(CUP_GET(inst, name))
#define CUP_SET(inst, name, value) Instance_setSelfVar((inst), cache.name, RValue_makeReal((GMLReal)(value)))

static void teacupDrawSprite(Renderer* renderer, Instance* self, int32_t sprite, int32_t frame, GMLReal y) {
    Renderer_drawSpriteExt(renderer, sprite, frame, self->x, (float)y,
        self->imageXscale, self->imageYscale, (float)self->imageAngle, self->imageBlend, self->imageAlpha);
}

static bool teacupDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = runner->renderer;
    if (renderer == nullptr) return false;
    GMLReal epsilon = runner->mathEpsilon;
    if (NativeScript_compareReals(CUP_REAL(self, mode), 0, epsilon) != 0) {
        if (RValue_toBool(CUP_GET(self, held))) {
            CUP_SET(self, adjustForCamera, CUP_REAL(self, adjustForCamera) +
                CUP_REAL(self, riseSpeed) * CUP_REAL(self, camFollowMultiplier));
        } else {
            CUP_SET(self, adjustForCamera, 0);
        }
    }
    GMLReal bottomY = (GMLReal)self->ystart + CUP_REAL(self, heldAmount);
    RValue offset = CUP_GET(self, startOffset);
    if (offset.type != RVALUE_UNDEFINED && NativeScript_compareReals(RValue_toReal(offset), 0, epsilon) > 0)
        bottomY += RValue_toReal(offset);
    RValue segments = NativeScript_binary(ctx, NATIVE_OP_INT_DIVIDE,
        RValue_makeReal(bottomY - self->y), CUP_GET(self, screwSegHeight));
    GMLReal total = RValue_toReal(segments) + 1;
    int32_t i = 0;
    for (; NativeScript_compareReals(i, total, epsilon) < 0; ++i) {
        CUP_SET(self, i, i);
        Renderer_drawSpriteExt(renderer, i == 0 ? cache.screwEnd : cache.screw, 0, self->x,
            (float)(bottomY + 24 - i * 30 - (i == 0 ? 2 : 0)), 2, 2, 0, 0xFFFFFF, 1);
    }
    CUP_SET(self, i, i);
    teacupDrawSprite(renderer, self, cache.base, 0, bottomY + 20);
    teacupDrawSprite(renderer, self, cache.platform, 0, self->y);
    RValue centerFrame = NativeScript_binary(ctx, NATIVE_OP_INT_DIVIDE,
        CUP_GET(self, spin), RValue_makeReal(22.5));
    int32_t frame = RValue_toInt32(centerFrame);
    if (frame < 0) frame = (int32_t)self->imageIndex;
    teacupDrawSprite(renderer, self, cache.center, frame, self->y);

    RValue surface = CUP_GET(self, shadowSurf);
    RValue exists = cache.surfaceExists(ctx, &surface, 1);
    bool haveSurface = RValue_toBool(exists);
    RValue_free(&exists);
    if (!haveSurface) {
        RValue args[] = {
            VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_WIDTH, "sprite_width", -1),
            VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_SPRITE_HEIGHT, "sprite_height", -1),
        };
        RValue created = cache.surfaceCreate(ctx, args, 2);
        Instance_setSelfVar(self, cache.shadowSurf, created);
        RValue_free(&created);
        surface = CUP_GET(self, shadowSurf);
    }
    RValue result = cache.surfaceTarget(ctx, &surface, 1);
    RValue_free(&result);
    renderer->vtable->clearScreen(renderer, 0, 0);
    renderer->drawColor = 0;
    renderer->vtable->gpuSetBlendModeExt(renderer, 2, 1, 2, 1);
    InstanceBBox bbox = Collision_computeBBox(runner, self);
    GMLReal left = bbox.valid ? bbox.left : self->x;
    GMLReal top = bbox.valid ? bbox.top : self->y;
    Instance** bullets = runner->instancesByObject[cache.bulletObject];
    size_t activeCount = 0;
    for (size_t j = 0; j < arrlenu(bullets); ++j)
        if (bullets[j]->active) activeCount++;
    for (size_t j = 0; j < arrlenu(bullets); ++j) {
        Instance* bullet = bullets[activeCount > 2 ? arrlenu(bullets) - 1 - j : j];
        if (!bullet->active || NativeScript_compareReals(CUP_REAL(bullet, master), self->instanceId, epsilon) != 0)
            continue;
        GMLReal closeness = ((GMLReal)bullet->ystart - (self->y - 300)) / 300;
        CUP_SET(bullet, closenessToRide, closeness);
        GMLReal alpha = closeness - CUP_REAL(bullet, cutOff) / CUP_REAL(bullet, spriteHeightRoot);
        GMLReal x = bullet->x - left;
        GMLReal y = (self->y + CUP_REAL(bullet, circleY)) - top;
        Renderer_drawSpriteExt(renderer, bullet->spriteIndex, (int32_t)bullet->imageIndex,
            (float)x, (float)y, bullet->imageXscale, bullet->imageYscale * 0.5f, 0, 0, (float)alpha);
        if (RValue_toBool(CUP_GET(bullet, lineToCenter))) {
            renderer->drawColor = 0;
            renderer->drawAlpha = (float)alpha;
            renderer->vtable->drawLine(renderer, (float)x, (float)y,
                (float)(bullet->xstart - left), (float)(self->y - top), 4, 0, renderer->drawAlpha);
            renderer->drawAlpha = 1;
        }
    }
    renderer->vtable->gpuSetBlendMode(renderer, 0);
    result = cache.surfaceReset(ctx, nullptr, 0);
    RValue_free(&result);
    RValue surfaceArgs[] = { CUP_GET(self, shadowSurf), RValue_makeReal(left), RValue_makeReal(top) };
    result = cache.drawSurface(ctx, surfaceArgs, 3);
    RValue_free(&result);
    if (runner->currentRoomIndex == cache.mazeRoom) {
        GMLReal alpha = CUP_REAL(self, drawtutorialarrows_alpha);
        bool show = NativeScript_compareReals(CUP_REAL(self, drawtutorialarrows), 1, epsilon) == 0;
        alpha += ((show ? 1 : 0) - alpha) * (show ? (GMLReal)0.15 : (GMLReal)0.35);
        CUP_SET(self, drawtutorialarrows_alpha, alpha);
        GMLReal wave = GMLReal_sin(CUP_REAL(self, idletimer) / 3) * 2;
        Renderer_drawSpriteExt(renderer, cache.arrow, 0, self->x + 68, (float)(self->y + 34 - wave),
            2, 2, 0, 0xFFFFFF, (float)alpha);
        Renderer_drawSpriteExt(renderer, cache.arrow, 0, self->x - 72, (float)(self->y + 34 + wave),
            -2, 2, 0, 0xFFFFFF, (float)alpha);
    }
    renderer->drawColor = 255;
    if (RValue_toBool(CUP_GET(self, cupCharExists)) && RValue_toBool(CUP_GET(self, debugHitThisFrame))) {
        Renderer_drawCircle(renderer, 20, self->y, 64, false);
        CUP_SET(self, debugHitThisFrame, 0);
    }
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2TeacupDraw_register(VMContext* ctx) {
    static const char* const sprites[] = {
        "spr_teacup_screw", "spr_teacup_screw_end", "spr_teacup_base",
        "spr_teacup_platform", "spr_teacup_center", "spr_teacuparrow",
    };
    int32_t* ids[] = { &cache.screw, &cache.screwEnd, &cache.base, &cache.platform, &cache.center, &cache.arrow };
    for (int32_t j = 0; j < 6; ++j) {
        *ids[j] = -1;
        for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
            const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
            if (sprite->present && strcmp(sprite->name, sprites[j]) == 0) {
                *ids[j] = (int32_t)i;
                break;
            }
        }
        if (*ids[j] < 0) return;
    }
    cache.bulletObject = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (object->present && strcmp(object->name, "obj_teacup_bullet") == 0)
            cache.bulletObject = (int32_t)i;
    }
    cache.mazeRoom = -1;
    for (uint32_t i = 0; i < ctx->dataWin->room.count; ++i) {
        if (strcmp(ctx->dataWin->room.rooms[i].name, "room_dw_cyber_battle_maze_1") == 0)
            cache.mazeRoom = (int32_t)i;
    }
    cache.surfaceCreate = VM_findBuiltin(ctx, "surface_create");
    cache.surfaceExists = VM_findBuiltin(ctx, "surface_exists");
    cache.surfaceTarget = VM_findBuiltin(ctx, "surface_set_target");
    cache.surfaceReset = VM_findBuiltin(ctx, "surface_reset_target");
    cache.drawSurface = VM_findBuiltin(ctx, "draw_surface");
    if (cache.bulletObject < 0 || cache.surfaceCreate == nullptr || cache.surfaceExists == nullptr ||
        cache.surfaceTarget == nullptr || cache.surfaceReset == nullptr || cache.drawSurface == nullptr ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_teacup_Draw_0", teacupDraw)) return;

#define RESOLVE_SELF(name) cache.name = NativeScript_getSelfVarID(ctx, #name);
    CUP_DRAW_VARS(RESOLVE_SELF)
#undef RESOLVE_SELF
}
