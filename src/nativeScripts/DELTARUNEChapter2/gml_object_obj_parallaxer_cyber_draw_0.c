#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

#ifdef PLATFORM_3DS
#include "3ds/3ds_renderer.h"
#endif

#include <string.h>

static struct {
    int32_t drawblack;
    int32_t blackTile;
    int32_t battleMazeRoom;
    int32_t bossSlideRoom;
} cache;

static void drawBlackTiles(Renderer* renderer, const Sprite* sprite, int32_t tpag,
    float x, float y, float xscale, float yscale, int32_t count, const float* view) {
#ifdef PLATFORM_3DS
    if (view != nullptr) {
        float left = x - sprite->originX * xscale;
        float top = y - sprite->originY * yscale;
        if (left >= view[2] || top >= view[3] ||
            left + sprite->width * xscale <= view[0] ||
            top + sprite->height * yscale <= view[1]) return;
    }
#else
    (void)view;
#endif

    if (sprite->nineSliceEnabled && (xscale != 1.0f || yscale != 1.0f)) {
        for (int32_t i = 0; i < count; ++i)
            Renderer_drawSpriteExt(renderer, cache.blackTile, 0, x, y,
                xscale, yscale, 0, 0xFFFFFF, 1.0f);
        return;
    }

    for (int32_t i = 0; i < count; ++i)
        renderer->vtable->drawSprite(renderer, tpag, x, y,
            (float)sprite->originX, (float)sprite->originY,
            xscale, yscale, 0.0f, 0xFFFFFF, 1.0f);
}

static bool parallaxerCyberDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    if (renderer == nullptr) return true;

    if (RValue_toBool(Instance_getSelfVar(self, cache.drawblack))) {
        GMLCamera* camera = Runner_getCameraForView(runner, 0);
        GMLReal camX = camera != nullptr ? (GMLReal)camera->viewX : -1;
        GMLReal camY = camera != nullptr ? (GMLReal)camera->viewY : -1;
        float left = (float)(camX - 80);
        float right = (float)(camX + 720);
        float bottom = (float)(camY + 600);

        renderer->drawColor = 0x532D2Du;
        renderer->vtable->drawRectangle(renderer, left,
            (float)(camY + 360 - camY / 40), right, bottom,
            renderer->drawColor, renderer->drawAlpha, false);
        renderer->drawAlpha = 0.3f;
        renderer->drawColor = 0;
        for (int32_t i = 0; i < 6; ++i)
            renderer->vtable->drawRectangle(renderer, left,
                (float)(camY + 340 + i * 20), right, bottom,
                0, 0.3f, false);
        renderer->drawAlpha = 1.0f;
    }

    int32_t room = runner->currentRoomIndex;
    if (room != cache.battleMazeRoom && room != cache.bossSlideRoom)
        return true;

    const Sprite* sprite = &ctx->dataWin->sprt.sprites[cache.blackTile];
    int32_t tpag = Renderer_resolveTPAGIndex(ctx->dataWin, cache.blackTile, 0);
    const float* view = nullptr;
#ifdef PLATFORM_3DS
    float visible[4];
    if (C3D_getTileCullRect(renderer, visible)) view = visible;
#endif

    if (room == cache.battleMazeRoom) {
        static const float y[] = {244, 280, 320, 360, 400};
        if (tpag >= 0) {
            for (int32_t i = 0; i < 5; ++i)
                drawBlackTiles(renderer, sprite, tpag, 0, y[i], 43,
                    i == 0 ? 0.9f : 1.0f, 2 * i + 1, view);
        }

        renderer->drawColor = 0;
        renderer->vtable->drawRectangle(renderer, -100, 360, 2000,
            (float)(runner->currentRoom->height + 10), 0,
            renderer->drawAlpha, false);
    } else {
        static const float x[] = {2480, 2440, 2400, 2360, 2320,
                                  840, 880, 920, 960, 1000};
        float height = (float)runner->currentRoom->height;
        if (tpag >= 0) {
            for (int32_t i = 0; i < 10; ++i)
                drawBlackTiles(renderer, sprite, tpag, x[i], 0, 1,
                    height, 9 - 2 * (i % 5), view);
        }
    }
    return true;
}

void NativeChapter2ParallaxerCyberDraw_register(VMContext* ctx) {
    cache.blackTile = -1;
    cache.battleMazeRoom = -1;
    cache.bossSlideRoom = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "spr_blackTile_20alpha") == 0) {
            cache.blackTile = (int32_t)i;
            break;
        }
    }
    for (uint32_t i = 0; i < ctx->dataWin->room.count; ++i) {
        const char* name = ctx->dataWin->room.rooms[i].name;
        if (strcmp(name, "room_dw_cyber_battle_maze_1") == 0)
            cache.battleMazeRoom = (int32_t)i;
        else if (strcmp(name, "room_dw_cyber_post_music_boss_slide") == 0)
            cache.bossSlideRoom = (int32_t)i;
    }

    if (cache.blackTile < 0 || cache.battleMazeRoom < 0 || cache.bossSlideRoom < 0 ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_parallaxer_cyber_Draw_0", parallaxerCyberDraw))
        return;

    cache.drawblack = NativeScript_getSelfVarID(ctx, "drawblack");
}
