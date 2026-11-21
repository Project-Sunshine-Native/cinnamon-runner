#include "nativeScripts/native_runtime.h"
#include "nativeScripts/native_scripts.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#ifdef PLATFORM_3DS
#include "3ds/3ds_renderer.h"
#include "stb_ds.h"
#endif

#include <string.h>

static struct {
    int32_t xx, yy, siner, siner2, destroy;
    int32_t gridSprite;
#ifdef PLATFORM_3DS
    const VMContext* owner;
    int32_t objectIndex, fighting;
#endif
} cache;

// Quick optimization for battles. Tiles used to still draw in battles (even though they cant be seen), but that causes lag, and i dont like the lag, so instead..
// If the battle background is fully opaque, it will simply just not draw the tiles while in battles so there isn't any extra work not needing to be done.
#ifdef PLATFORM_3DS
bool NativeBattleback_getOpaqueDepth(Runner* runner, int32_t* depth) {
    VMContext* ctx = runner->vmContext;
    if (cache.owner != ctx || cache.objectIndex < 0 || !runner->drawEnabled) return false;

    Instance** backdrops = runner->instancesByObject[cache.objectIndex];
    if (arrlen(backdrops) != 1) return false;
    Instance* backdrop = backdrops[0];
    if (!backdrop->active || !backdrop->visible || backdrop->destroyed ||
        backdrop->sequenceControlled || backdrop->objectIndex != cache.objectIndex ||
        !(backdrop->imageAlpha >= 1.0f)) return false;

    RValue destroy = Instance_getSelfVar(backdrop, cache.destroy);
    RValue fighting = Instance_getSelfVar(ctx->globalScopeInstance, cache.fighting);
    if (destroy.type == RVALUE_UNDEFINED || RValue_toReal(destroy) != 0 ||
        RValue_toReal(fighting) != 1) return false;

    RuntimeLayer* layer = Runner_findRuntimeLayerById(runner, backdrop->layer);
    if (layer == nullptr || !layer->visible || !layer->automaticDepth ||
        layer->shaderIndex != -1 || layer->beginScript >= 0 || layer->endScript >= 0) return false;

    bool present = false;
    for (size_t i = 0; i < arrlenu(layer->elements); ++i) {
        const RuntimeLayerElement* element = &layer->elements[i];
        if (element->type == RuntimeLayerElementType_Instance && (uint32_t)element->instanceId == backdrop->instanceId) {
            present = true;
            break;
        }
    }
    if (!present) return false;

    Renderer* renderer = runner->renderer;
    if (renderer == nullptr || renderer->currentShader != -1 || renderer->d3d != nullptr ||
        Runner_surfaceGetTarget(runner) != runner->applicationSurfaceId) return false;
    if (renderer->vtable->gpuGetAlphaTestEnable != nullptr &&
        renderer->vtable->gpuGetAlphaTestEnable(renderer)) return false;
    if (renderer->vtable->gpuGetBlendEnable(renderer)) {
        if (renderer->vtable->gpuGetBlendMode(renderer) != bm_normal) return false;
        BlendFactors blend = renderer->vtable->gpuGetBlendFactors(renderer);
        if (blend.srcAlpha != bm_one || blend.dstAlpha != bm_inv_src_alpha) return false;
    }
    bool red, green, blue, alpha;
    renderer->vtable->gpuGetColorWriteEnable(renderer, &red, &green, &blue, &alpha);
    if (!red || !green || !blue || !alpha) return false;

    float view[4];
    Room* room = runner->currentRoom;
    if (!C3D_getTileCullRect(renderer, view) || view[0] < -10.0f || view[1] < -10.0f ||
        view[2] > (float)room->width + 10.0f || view[3] > (float)room->height + 10.0f) return false;

    *depth = layer->depth;
    return true;
}
#endif

static bool battlebackDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    Room* room = runner->currentRoom;
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    Instance_setSelfVar(self, cache.xx, RValue_makeReal(camera != nullptr ? (GMLReal)camera->viewX : -1));
    Instance_setSelfVar(self, cache.yy, RValue_makeReal(camera != nullptr ? (GMLReal)camera->viewY : -1));

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner)) + (GMLReal)0.5;
    GMLReal siner2 = RValue_toReal(Instance_getSelfVar(self, cache.siner2)) + 1;
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    Instance_setSelfVar(self, cache.siner2, RValue_makeReal(siner2));

    renderer->drawAlpha = self->imageAlpha;
    renderer->drawColor = 0;
    renderer->vtable->drawRectangle(renderer, -10.0f, -10.0f,
        (float)((GMLReal)room->width + 10), (float)((GMLReal)room->height + 10),
        renderer->drawColor, renderer->drawAlpha, false);
    renderer->drawAlpha = 1.0f;

    GMLReal epsilon = runner->mathEpsilon;
    RValue destroy = Instance_getSelfVar(self, cache.destroy);
    GMLReal fadingOut = RValue_toReal(destroy);
    if (destroy.type != RVALUE_UNDEFINED && NativeScript_compareReals(fadingOut, 0, epsilon) == 0 &&
        NativeScript_compareReals(self->imageAlpha, 1, epsilon) <= 0) {
        self->imageAlpha = (float)((GMLReal)self->imageAlpha + (GMLReal)0.1);
    }

    DataWin* dataWin = renderer->dataWin;
    int32_t tpag = Renderer_resolveSpriteTPAGIndex(dataWin, cache.gridSprite);
    if (tpag >= 0) {
        const Sprite* sprite = &dataWin->sprt.sprites[cache.gridSprite];
        float originX = (float)sprite->originX, originY = (float)sprite->originY;
        float w = (float)room->width, h = (float)room->height;
        uint32_t tint = RValue_toColour(RValue_makeReal((GMLReal)self->imageBlend));
        float first = (float)GMLReal_bankersRound(-100 + siner);
        float secondX = (float)GMLReal_bankersRound(-200 - siner2);
        float secondY = (float)GMLReal_bankersRound(-210 - siner2);
        renderer->vtable->drawSpriteTiled(renderer, tpag, originX, originY, first, first,
            1.0f, 1.0f, true, true, w, h, tint, self->imageAlpha * 0.5f);
        renderer->vtable->drawSpriteTiled(renderer, tpag, originX, originY, secondX, secondY,
            1.0f, 1.0f, true, true, w, h, tint, self->imageAlpha);
    }

    if (NativeScript_compareReals(siner, 100, epsilon) >= 0) {
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner - 100));
    }
    if (NativeScript_compareReals(siner2, 100, epsilon) >= 0) {
        Instance_setSelfVar(self, cache.siner2, RValue_makeReal(siner2 - 100));
    }
    if (destroy.type != RVALUE_UNDEFINED && NativeScript_compareReals(fadingOut, 1, epsilon) == 0) {
        self->imageAlpha = (float)((GMLReal)self->imageAlpha - (GMLReal)0.1);
        if (NativeScript_compareReals(self->imageAlpha, 0, epsilon) <= 0) {
            Runner_destroyInstance(runner, self, true);
        }
    }

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeBattlebackDraw_register(VMContext* ctx) {
#ifdef PLATFORM_3DS
    cache.owner = nullptr;
#endif
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 1") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    const Sprt* sprt = &ctx->dataWin->sprt;
    cache.gridSprite = -1;
    for (uint32_t i = 0; i < sprt->count; ++i) {
        const Sprite* sprite = &sprt->sprites[i];
        if (sprite->present && strcmp(sprite->name, "bg_battleback1") == 0) {
            cache.gridSprite = (int32_t)i;
            break;
        }
    }
    if (cache.gridSprite < 0 || !VM_registerNativeScript(ctx, "gml_Object_obj_battleback_Draw_0", battlebackDraw)) return;

    cache.xx = NativeScript_getSelfVarID(ctx, "xx");
    cache.yy = NativeScript_getSelfVarID(ctx, "yy");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.siner2 = NativeScript_getSelfVarID(ctx, "siner2");
    cache.destroy = NativeScript_getSelfVarID(ctx, "destroy");
#ifdef PLATFORM_3DS
    cache.owner = ctx;
    cache.fighting = VM_getOrAllocateVarID(ctx, "fighting");
    cache.objectIndex = -1;
    const Objt* objt = &ctx->dataWin->objt;
    for (uint32_t i = 0; i < objt->count; ++i) {
        const GameObject* object = &objt->objects[i];
        if (object->present && strcmp(object->name, "obj_battleback") == 0) {
            cache.objectIndex = (int32_t)i;
            break;
        }
    }
#endif
}
