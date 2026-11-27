#ifdef PLATFORM_3DS

#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#include <string.h>

static struct {
    int32_t drawblack, roomScale;
    float tileWidth;
    int32_t roomIndex, roomMode;
} cache;

static bool parallaxerCyber2Draw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    if (cache.roomIndex != runner->currentRoomIndex) {
        const char* roomName = runner->currentRoom->name;
        cache.roomIndex = runner->currentRoomIndex;
        if (strcmp(roomName, "room_dw_cyber_battle_maze_1") == 0 ||
            strcmp(roomName, "room_dw_cyber_tasque_battle") == 0 ||
            strcmp(roomName, "room_dw_cyber_maze_queenscreen") == 0)
            cache.roomMode = 1;
        else if (strcmp(roomName, "room_dw_cyber_teacup_final") == 0)
            cache.roomMode = 2;
        else if (strcmp(roomName, "room_dw_cyber_post_music_boss_slide") == 0)
            cache.roomMode = 3;
        else
            cache.roomMode = 0;
    }
    bool horizontal = cache.roomMode == 1;
    bool teacup = cache.roomMode == 2;
    bool slide = cache.roomMode == 3;
    Renderer* renderer = runner->renderer;
    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    if (renderer == nullptr || camera == nullptr || renderer->currentShader >= 0 ||
        renderer->d3d != nullptr ||
        camera->viewAngle != 0.0f || !renderer->vtable->gpuGetBlendEnable(renderer) ||
        renderer->vtable->gpuGetBlendMode(renderer) != bm_normal) return false;
    if (renderer->vtable->gpuGetAlphaTestEnable != nullptr &&
        renderer->vtable->gpuGetAlphaTestEnable(renderer)) return false;

    Instance* self = ctx->currentCallFrame->self;
    float width = (float)runner->currentRoom->width;
    float height = (float)runner->currentRoom->height;
    float camX = camera->viewX;
    float camY = camera->viewY;
    bool drawblack = RValue_toBool(Instance_getSelfVar(self, cache.drawblack));
    float tileWidth = 0.0f;
    if (horizontal || teacup) {
        tileWidth = cache.tileWidth * (float)RValue_toReal(
            Instance_getSelfVar(self, cache.roomScale));
        if (tileWidth <= 0.0f) return false;
    }

    float blackTop = teacup ? 8860.0f : 360.0f;
    float blackRight = teacup ? 2000.0f : width;
    float blackBottom = height + 10.0f;
    bool opaque = !slide && (drawblack || renderer->drawAlpha == 1.0f);
    bool coveredBands = opaque && camX + camera->viewWidth <= blackRight &&
        blackBottom >= blackTop + 80.0f;

    *outResult = RValue_makeUndefined();
    if (drawblack) {
        bool coveredOverlay = horizontal && coveredBands && camX >= -100.0f &&
            camY >= 0.0f && camY + camera->viewHeight <= blackBottom;
        if (coveredOverlay) {
            if (camY < 20.0f)
                renderer->vtable->drawRectangle(renderer, camX - 80.0f,
                    camY + 340.0f, camX + 720.0f, 360.0f, 0, 0.3f, false);
        } else {
            renderer->drawColor = 0x532D2Du;
            renderer->vtable->drawRectangle(renderer, camX - 80.0f,
                camY + 360.0f - camY / 40.0f, camX + 720.0f, camY + 600.0f,
                renderer->drawColor, renderer->drawAlpha, false);
            renderer->drawAlpha = 0.3f;
            renderer->drawColor = 0;
            static const float overlayAlpha[6] = {
                0.3f, 0.51f, 0.657f, 0.7599f, 0.83193f, 0.882351f};
            for (int32_t i = 0; i < 6; ++i) {
                float top = camY + 340.0f + i * 20.0f;
                float bottom = i < 5 ? top + 20.0f : camY + 600.0f;
                renderer->vtable->drawRectangle(renderer, camX - 80.0f,
                    top, camX + 720.0f, bottom, 0, overlayAlpha[i], false);
            }
        }
        renderer->drawColor = 0;
        renderer->drawAlpha = 1.0f;
    }

    static const float bandAlpha[5] = {
        0.2f, 0.488f, 0.67232f, 0.7902848f, 0.865782272f};
    if (horizontal || teacup) {
        static const float bandY[5] = {244.0f, 280.0f, 320.0f, 360.0f, 400.0f};
        float offset = teacup ? 8500.0f : 0.0f;
        int32_t count = coveredBands ? 3 : 5;
        for (int32_t i = 0; i < count; ++i) {
            float y = bandY[i] + offset;
            renderer->vtable->drawRectangle(renderer, 0.0f, y, tileWidth,
                y + (i == 0 ? 36.0f : 40.0f), 0, bandAlpha[i], false);
        }
        renderer->drawColor = 0;
        renderer->vtable->drawRectangle(renderer, -100.0f, blackTop, blackRight,
            blackBottom, 0, renderer->drawAlpha, false);
    } else if (slide) {
        static const float bandX[10] = {
            2480.0f, 2440.0f, 2400.0f, 2360.0f, 2320.0f,
            840.0f, 880.0f, 920.0f, 960.0f, 1000.0f};
        float bottom = height * 40.0f;
        for (int32_t i = 0; i < 10; ++i)
            renderer->vtable->drawRectangle(renderer, bandX[i], 0.0f,
                bandX[i] + 40.0f, bottom, 0, bandAlpha[4 - i % 5], false);
    }
    return true;
}

void NativeChapter2ParallaxerCyber2Draw_register(VMContext* ctx) {
    cache.tileWidth = 0.0f;
    cache.roomIndex = -1;
    cache.roomMode = 0;

    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "spr_blackTile_20alpha") == 0) {
            if (sprite->width != 40 || sprite->height != 40 ||
                sprite->originX != 0 || sprite->originY != 0 || sprite->nineSliceEnabled)
                return;
            cache.tileWidth = (float)sprite->width;
            break;
        }
    }
    if (cache.tileWidth == 0.0f || !VM_registerNativeScript(ctx,
            "gml_Object_obj_parallaxer_cyber_2_Draw_0", parallaxerCyber2Draw)) return;

    cache.drawblack = NativeScript_getSelfVarID(ctx, "drawblack");
    cache.roomScale = NativeScript_getSelfVarID(ctx, "room_scale");
}

#endif
