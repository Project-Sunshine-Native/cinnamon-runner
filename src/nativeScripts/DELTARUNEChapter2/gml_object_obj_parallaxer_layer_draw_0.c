#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#include <string.h>

static struct {
    int32_t shadoweffect, siner;
    int32_t buildings, lights;
} cache;

static bool parallaxerLayerDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    *outResult = RValue_makeUndefined();

    GMLReal shadow = RValue_toReal(Instance_getSelfVar(self, cache.shadoweffect));
    GMLReal phase = 0;
    if (shadow == 1) {
        phase = RValue_toReal(Instance_getSelfVar(self, cache.siner)) + 1;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(phase));
    }

    if (shadow == 2)
        self->imageBlend = 0xFFFFFF;

    if (renderer != nullptr && (shadow == 0 || shadow == 1 || shadow == 2)) {
        DataWin* data = ctx->dataWin;
        int32_t frame = (int32_t)self->imageIndex;
        int32_t tpag = Renderer_resolveTPAGIndex(data, self->spriteIndex, frame);
        float width = (float)ctx->runner->currentRoom->width;
        float height = (float)ctx->runner->currentRoom->height;
        float x = self->x, y = self->y;

        if (tpag >= 0) {
            const Sprite* sprite = &data->sprt.sprites[self->spriteIndex];
            float originX = (float)sprite->originX;
            float originY = (float)sprite->originY;

            if (shadow == 0 || shadow == 2)
                renderer->vtable->drawSpriteTiled(renderer, tpag, originX, originY,
                    x, y, 1, 1, true, true, width, height, 0xFFFFFF,
                    renderer->drawAlpha);
            else {
                renderer->vtable->drawSpriteTiled(renderer, tpag, originX, originY,
                    x, y, 1, 1, true, true, width, height, 0xFFFFFF, 1);
                renderer->vtable->drawSpriteTiled(renderer, tpag, originX, originY,
                    x, y + 8, 1, 1, true, true, width, height, 0x808080, 1);
            }

            if (shadow == 2)
                renderer->vtable->drawSpriteTiled(renderer, tpag, originX, originY,
                    x, y + 8, 1, 1, true, true, width, height, 0x404040, 1);
        }

        if (shadow == 1 && self->spriteIndex == cache.buildings && cache.lights >= 0) {
            int32_t lightsTPAG = Renderer_resolveTPAGIndex(data, cache.lights, frame);
            if (lightsTPAG >= 0) {
                const Sprite* lights = &data->sprt.sprites[cache.lights];
                renderer->vtable->gpuSetBlendMode(renderer, bm_add);
                renderer->vtable->drawSpriteTiled(renderer, lightsTPAG,
                    (float)lights->originX, (float)lights->originY,
                    x, y + 8, 1, 1, true, true, width, height,
                    0xFFFFFF, (float)(0.5 + GMLReal_sin(phase / 12) * 0.1));
                renderer->vtable->gpuSetBlendMode(renderer, bm_normal);
            }
        }
    }

    if (shadow == 2)
        self->imageBlend = 0x808080;
    return true;
}

void NativeChapter2ParallaxerLayerDraw_register(VMContext* ctx) {
    cache.buildings = -1;
    cache.lights = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const char* name = ctx->dataWin->sprt.sprites[i].name;
        if (name == nullptr) continue;
        if (strcmp(name, "spr_bg_cyber_parallax_buildings") == 0)
            cache.buildings = (int32_t)i;
        else if (strcmp(name, "spr_bg_cyber_parallax_buildings_lights") == 0)
            cache.lights = (int32_t)i;
    }

    cache.shadoweffect = NativeScript_getSelfVarID(ctx, "shadoweffect");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    VM_registerNativeScript(ctx, "gml_Object_obj_parallaxer_layer_Draw_0", parallaxerLayerDraw);
}
