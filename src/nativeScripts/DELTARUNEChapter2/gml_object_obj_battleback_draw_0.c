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
    int32_t xx, yy, siner, siner2, destroy, bgtype;
    int32_t gridSprite, dojoSprite, dojoRoom;
#ifdef PLATFORM_3DS
    const VMContext* owner;
    int32_t objectIndex, fighting, wallLightsObject;
    bool* pausedObjects;
#endif
} cache;

// Quick optimization for battles. Tiles used to still draw in battles (even though they cant be seen), but that causes lag, and i dont like the lag, so instead..
// If the battle background is fully opaque, it will simply just not draw the tiles while in battles so there isn't any extra work needing to be done.
#ifdef PLATFORM_3DS
static bool getOpaqueBackdropDepth(Runner* runner, int32_t* depth) {
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

    *depth = layer->depth;
    return true;
}

bool NativeChapter2Battleback_getOpaqueDepth(Runner* runner, int32_t* depth) {
    int32_t backdropDepth;
    if (!getOpaqueBackdropDepth(runner, &backdropDepth)) return false;

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

    *depth = backdropDepth;
    return true;
}

bool NativeChapter2Battleback_isPausedObject(VMContext* ctx, int32_t objectIndex) {
    return cache.owner == ctx && objectIndex >= 0 &&
        (size_t)objectIndex < arrlenu(cache.pausedObjects) && cache.pausedObjects[objectIndex];
}

bool NativeChapter2Battleback_shouldSkipEvent(Runner* runner, Instance* instance,
    int32_t eventType, int32_t eventSubtype) {
    if (eventType != EVENT_STEP && eventType != EVENT_DRAW) return false;
    if (eventType == EVENT_DRAW && eventSubtype != DRAW_NORMAL &&
        eventSubtype != DRAW_BEGIN && eventSubtype != DRAW_END) return false;
    bool wallLightsDraw = cache.owner == runner->vmContext && eventType == EVENT_DRAW &&
        instance->objectIndex == cache.wallLightsObject;
    if (!wallLightsDraw &&
        !NativeChapter2Battleback_isPausedObject(runner->vmContext, instance->objectIndex))
        return false;

    int32_t depth;
    if (eventType == EVENT_DRAW)
        return NativeChapter2Battleback_getOpaqueDepth(runner, &depth) && instance->depth > depth;

    if (!getOpaqueBackdropDepth(runner, &depth) || instance->depth <= depth) return false;
    if (runner->freeCamPanX != 0 || runner->freeCamPanY != 0 ||
        (runner->freeCamZoom > 0 && runner->freeCamZoom != 1)) return false;
    if (runner->viewsEnabled) {
        for (int32_t i = 0; i < MAX_VIEWS; ++i) {
            if (!runner->views[i].enabled) continue;
            GMLCamera* camera = Runner_getCameraForView(runner, i);
            if (camera == nullptr || runner->views[i].surfaceId != -1 ||
                camera->viewAngle != 0) return false;

            float left = camera->viewX - runner->widescreenExtraWidth;
            float top = camera->viewY - runner->widescreenExtraHeight;
            float right = camera->viewX + camera->viewWidth + runner->widescreenExtraWidth;
            float bottom = camera->viewY + camera->viewHeight + runner->widescreenExtraHeight;
            Room* room = runner->currentRoom;
            if (left < -10 || top < -10 || right > room->width + 10 ||
                bottom > room->height + 10) return false;
        }
    }
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
    GMLReal viewX = camera != nullptr ? (GMLReal)camera->viewX : -1;
    GMLReal viewY = camera != nullptr ? (GMLReal)camera->viewY : -1;
    Instance_setSelfVar(self, cache.xx, RValue_makeReal(viewX));
    Instance_setSelfVar(self, cache.yy, RValue_makeReal(viewY));

    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner)) + (GMLReal)0.5;
    GMLReal siner2 = RValue_toReal(Instance_getSelfVar(self, cache.siner2)) + 1;
    Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    Instance_setSelfVar(self, cache.siner2, RValue_makeReal(siner2));

    if (runner->currentRoomIndex == cache.dojoRoom)
        Instance_setSelfVar(self, cache.bgtype, RValue_makeReal(1));

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

    GMLReal bgtype = RValue_toReal(Instance_getSelfVar(self, cache.bgtype));
    if (NativeScript_compareReals(bgtype, 0, epsilon) == 0) {
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
    } else if (NativeScript_compareReals(bgtype, 1, epsilon) == 0) {
        GMLReal scaleX = 2 + GMLReal_sin(siner / 2) * (GMLReal)0.008;
        GMLReal scaleY = 2 + GMLReal_cos(siner / 2) * (GMLReal)0.008;
        GMLReal viewWidth = camera != nullptr ? (GMLReal)camera->viewWidth : -1;
        Renderer_drawSpriteExt(renderer, cache.dojoSprite, 0, (float)(viewX + viewWidth / 2),
            (float)(viewY + 340),
            (float)scaleX, (float)scaleY, 0, self->imageBlend, self->imageAlpha);
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

void NativeChapter2BattlebackDraw_register(VMContext* ctx) {
#ifdef PLATFORM_3DS
    cache.owner = nullptr;
    arrfree(cache.pausedObjects);
#endif
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 2") != 0 ||
        !DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)) return;

    cache.gridSprite = -1;
    cache.dojoSprite = -1;
    const Sprt* sprt = &ctx->dataWin->sprt;
    for (uint32_t i = 0; i < sprt->count; ++i) {
        const Sprite* sprite = &sprt->sprites[i];
        if (!sprite->present) continue;
        if (strcmp(sprite->name, "bg_battleback1") == 0)
            cache.gridSprite = (int32_t)i;
        else if (strcmp(sprite->name, "spr_dojo_battlebg") == 0)
            cache.dojoSprite = (int32_t)i;
    }
    cache.dojoRoom = -1;
    for (uint32_t i = 0; i < ctx->dataWin->room.count; ++i) {
        if (strcmp(ctx->dataWin->room.rooms[i].name, "room_dw_castle_dojo") == 0) {
            cache.dojoRoom = (int32_t)i;
            break;
        }
    }
    if (cache.gridSprite < 0 || cache.dojoSprite < 0 || cache.dojoRoom < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_battleback_Draw_0", battlebackDraw)) return;

    cache.xx = NativeScript_getSelfVarID(ctx, "xx");
    cache.yy = NativeScript_getSelfVarID(ctx, "yy");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.siner2 = NativeScript_getSelfVarID(ctx, "siner2");
    cache.destroy = NativeScript_getSelfVarID(ctx, "destroy");
    cache.bgtype = NativeScript_getSelfVarID(ctx, "bgtype");
#ifdef PLATFORM_3DS
    cache.owner = ctx;
    cache.fighting = VM_getOrAllocateVarID(ctx, "fighting");
    cache.objectIndex = -1;
    cache.wallLightsObject = -1;
    const Objt* objt = &ctx->dataWin->objt;
    arrsetlen(cache.pausedObjects, objt->count);
    for (uint32_t i = 0; i < objt->count; ++i) {
        cache.pausedObjects[i] = false;
        const GameObject* concreteObject = &objt->objects[i];
        if (concreteObject->present && strcmp(concreteObject->name, "obj_cyber_wall_lights") == 0)
            cache.wallLightsObject = (int32_t)i;
        int32_t parent = (int32_t)i;
        for (uint32_t remaining = objt->count;
            remaining > 0 && parent >= 0 && (uint32_t)parent < objt->count; --remaining) {
            const GameObject* object = &objt->objects[parent];
            if (!object->present) break;
            const char* name = object->name;
            if (strstr(name, "parallax") != nullptr ||
                strncmp(name, "obj_teacup", strlen("obj_teacup")) == 0 ||
                strcmp(name, "obj_chaseenemy") == 0 ||
                strcmp(name, "obj_background_cyberlines") == 0) {
                cache.pausedObjects[i] = true;
                break;
            }
            parent = object->parentId;
        }
    }
    for (uint32_t i = 0; i < objt->count; ++i) {
        const GameObject* object = &objt->objects[i];
        if (object->present && strcmp(object->name, "obj_battleback") == 0) {
            cache.objectIndex = (int32_t)i;
            break;
        }
    }
#endif
}
