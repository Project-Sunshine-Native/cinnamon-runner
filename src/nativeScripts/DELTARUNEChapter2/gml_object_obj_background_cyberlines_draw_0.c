#include "nativeScripts/native_runtime.h"
#include "nativeScripts/native_scripts.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "utils.h"

#ifdef PLATFORM_3DS
#include "3ds/3ds_renderer.h"
#endif

#include <math.h>
#include <string.h>

static struct {
    int32_t init, linesiner, liney, patha, pathb, xx, yy, i, j;
    int32_t shadoweffect, sprHeight, siner;
    int32_t clouds, buildings, ground, lights, teacup;
    int32_t sky, stars, blackSky, coasterTile, coasterCity;
    BuiltinFunc pathAdd, pathSetClosed, pathSetPrecision, pathAddPoint;
    uint32_t shadeColors[18];
    float shadeAlpha[18];
    float circleX[65], circleY[65];
    int32_t circleSegments;
#ifdef PLATFORM_3DS
    const VMContext* owner;
    int32_t parallaxObjects[7];
#endif
} cache;

#ifdef PLATFORM_3DS
bool NativeChapter2Parallax_isDrawObject(VMContext* ctx, int32_t objectIndex) {
    if (cache.owner != ctx) return false;
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr || strcmp(title, "DELTARUNE Chapter 2") != 0) return false;
    for (uint32_t i = 0; i < sizeof(cache.parallaxObjects) / sizeof(cache.parallaxObjects[0]); ++i) {
        if (cache.parallaxObjects[i] == objectIndex) return true;
    }
    return false;
}

#endif

static void drawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2, uint32_t color) {
    renderer->drawColor = color;
    renderer->vtable->drawRectangle(renderer, x1, y1, x2, y2, color, renderer->drawAlpha, false);
}

static void drawTiledArea(Renderer* renderer, int32_t spriteIndex, int32_t subimg,
    float x, float y, float right, float bottom, uint32_t color, float alpha,
    const float* view, bool shadowFollows) {
    DataWin* data = renderer->dataWin;
    if ((uint32_t)spriteIndex >= data->sprt.count) return;
    const Sprite* sprite = &data->sprt.sprites[spriteIndex];
    if (sprite->width <= 0 || sprite->height <= 0) return;
    int32_t tpagIndex = Renderer_resolveTPAGIndex(data, spriteIndex, subimg);
    if (tpagIndex < 0) return;
    const TexturePageItem* tpag = &data->tpag.items[tpagIndex];

    float sw = (float)sprite->width;
    float sh = (float)sprite->height;
#ifdef PLATFORM_3DS
    bool trimRows = spriteIndex == cache.clouds || spriteIndex == cache.buildings ||
        spriteIndex == cache.ground;
#else
    (void)shadowFollows;
#endif
    if (view != nullptr) {
        if (right < view[0] || bottom < view[1] || x >= view[2] || y >= view[3]) return;
        if (x < view[0]) x += floorf((view[0] - x) / sw) * sw;
        if (y < view[1]) y += floorf((view[1] - y) / sh) * sh;
    }

    float maxX = view != nullptr && right > view[2] ? view[2] : right;
    float maxY = view != nullptr && bottom > view[3] ? view[3] : bottom;
    for (float xx = x; xx <= maxX; xx += sw) {
        float width = right <= xx + sw ? right - xx + 1 : sw;
        for (float yy = y; yy <= maxY; yy += sh) {
            float height = bottom <= yy + sh ? bottom - yy + 1 : sh;
            float srcX = 0, srcY = 0, dstX = xx, dstY = yy;
            float clippedW = width, clippedH = height;
            if (tpag->targetX > 0) {
                dstX += tpag->targetX;
                clippedW -= tpag->targetX;
            }
            if (tpag->targetY > 0) {
                dstY += tpag->targetY;
                clippedH -= tpag->targetY;
            }
            if (clippedW > tpag->sourceWidth) clippedW = tpag->sourceWidth;
            if (clippedH > tpag->sourceHeight) clippedH = tpag->sourceHeight;
            if (clippedW <= 0 || clippedH <= 0) continue;
#ifdef PLATFORM_3DS
            if (trimRows && C3D_drawSpriteShadowPart(renderer, tpagIndex, clippedW,
                clippedH, dstX, dstY, color, alpha, shadowFollows))
                continue;
#endif
            renderer->vtable->drawSpritePart(renderer, tpagIndex, srcX, srcY,
                clippedW, clippedH, dstX, dstY, 1, 1, 0, 0, 0, color, alpha);
        }
    }
}

static void initializeLines(VMContext* ctx, Instance* self) {
    RValue lineValue = RValue_makeArray(GMLArray_create(ctx->dataWin, 20));
    RValue pathsA = RValue_makeArray(GMLArray_create(ctx->dataWin, 8));
    RValue pathsB = RValue_makeArray(GMLArray_create(ctx->dataWin, 8));

    for (int32_t i = 0; i < 20; ++i)
        GMLArray_set(lineValue.array, i, RValue_makeReal(200 + 40 * i * i));

    for (int32_t i = 0; i < 8; ++i) {
        RValue a = cache.pathAdd(ctx, nullptr, 0);
        RValue b = cache.pathAdd(ctx, nullptr, 0);
        GMLArray_set(pathsA.array, i, a);
        GMLArray_set(pathsB.array, i, b);
        RValue setArgs[2] = {a, RValue_makeReal(0)};
        cache.pathSetClosed(ctx, setArgs, 2);
        setArgs[0] = b;
        cache.pathSetClosed(ctx, setArgs, 2);
        setArgs[0] = a;
        setArgs[1] = RValue_makeReal(6);
        cache.pathSetPrecision(ctx, setArgs, 2);
        setArgs[0] = b;
        cache.pathSetPrecision(ctx, setArgs, 2);

        for (int32_t j = 0; j < 6; ++j) {
            RValue point[4] = {a, RValue_makeReal(i * 20 + i * j * j * 8),
                RValue_makeReal(-70 * j), RValue_makeReal(1)};
            cache.pathAddPoint(ctx, point, 4);
            point[0] = b;
            point[1] = RValue_makeReal(-(i * 20 + i * j * j * 8));
            cache.pathAddPoint(ctx, point, 4);
        }
    }

    Instance_setSelfVar(self, cache.liney, lineValue);
    Instance_setSelfVar(self, cache.patha, pathsA);
    Instance_setSelfVar(self, cache.pathb, pathsB);
    RValue_free(&lineValue);
    RValue_free(&pathsA);
    RValue_free(&pathsB);
    Instance_setSelfVar(self, cache.linesiner, RValue_makeReal(0));
    Instance_setSelfVar(self, cache.init, RValue_makeReal(1));
    Instance_setSelfVar(self, cache.j, RValue_makeReal(6));
}

static void drawPerspectivePath(Renderer* renderer, float x, float y, int32_t index,
    float direction, const float* view) {
    float endX = x + direction * (index * 5 * 5 * 8);
    float minX = x < endX ? x : endX;
    float maxX = x > endX ? x : endX;
    if (view != nullptr && (maxX < view[0] || minX > view[2] || y < view[1] || y - 350 > view[3]))
        return;

    float prevX = x, prevY = y;
#ifdef PLATFORM_3DS
    float segments[5][4];
    uint32_t count = 0;
#endif
    for (int32_t j = 1; j < 6; ++j) {
        float nextX = x + direction * (index * j * j * 8);
        float nextY = y - 70 * j;
        if (view == nullptr ||
            ((prevX >= view[0] - 2 || nextX >= view[0] - 2) &&
             (prevX <= view[2] + 2 || nextX <= view[2] + 2) &&
             (prevY >= view[1] - 2 || nextY >= view[1] - 2) &&
             (prevY <= view[3] + 2 || nextY <= view[3] + 2))) {
#ifdef PLATFORM_3DS
            segments[count][0] = prevX;
            segments[count][1] = prevY;
            segments[count][2] = nextX;
            segments[count++][3] = nextY;
#else
            renderer->vtable->drawLine(renderer, prevX, prevY, nextX, nextY,
                1.0f, renderer->drawColor, renderer->drawAlpha);
#endif
        }
        prevX = nextX;
        prevY = nextY;
    }
#ifdef PLATFORM_3DS
    C3D_drawLineSegments(renderer, segments, count, 1, renderer->drawColor, renderer->drawAlpha, view);
#endif
}

static void drawGridEllipse(Renderer* renderer, float cx, float cy, float rx, float ry,
    const float* view) {
    int32_t segments = Renderer_normalizeCirclePrecision(renderer->circlePrecision);
    if (cache.circleSegments != segments) {
        float step = 6.2831853f / (float)segments;
        for (int32_t i = 1; i <= segments; ++i) {
            float angle = step * (float)i;
            cache.circleX[i] = cosf(angle);
            cache.circleY[i] = sinf(angle);
        }
        cache.circleSegments = segments;
    }

    float prevX = cx + rx;
    float prevY = cy;
#ifdef PLATFORM_3DS
    float lines[64][4];
    uint32_t count = 0;
#endif
    for (int32_t i = 1; i <= segments; ++i) {
        float x = cx + rx * cache.circleX[i];
        float y = cy + ry * cache.circleY[i];
        if (view == nullptr ||
            ((prevX >= view[0] - 2 || x >= view[0] - 2) &&
             (prevX <= view[2] + 2 || x <= view[2] + 2) &&
             (prevY >= view[1] - 2 || y >= view[1] - 2) &&
             (prevY <= view[3] + 2 || y <= view[3] + 2))) {
#ifdef PLATFORM_3DS
            lines[count][0] = prevX;
            lines[count][1] = prevY;
            lines[count][2] = x;
            lines[count++][3] = y;
#else
            renderer->vtable->drawLine(renderer, prevX, prevY, x, y,
                1.0f, renderer->drawColor, renderer->drawAlpha);
#endif
        }
        prevX = x;
        prevY = y;
    }
#ifdef PLATFORM_3DS
    C3D_drawLineSegments(renderer, lines, count, 1, renderer->drawColor, renderer->drawAlpha, view);
#endif
}

static void drawLines(VMContext* ctx, Instance* self, GMLReal xx, GMLReal yy,
    float shadeY, const float* view) {
    Renderer* renderer = ctx->runner->renderer;
    float clippedView[4];
    if (renderer != nullptr && view != nullptr && view[0] >= -100 &&
        view[2] <= (float)ctx->runner->currentRoom->width + 100 &&
        renderer->vtable->gpuGetBlendMode(renderer) == bm_normal) {
        memcpy(clippedView, view, sizeof(clippedView));
        float coverY = (float)ctx->runner->currentRoom->height - 240;
        if (clippedView[3] > coverY) clippedView[3] = coverY;
        view = clippedView;
    }
    bool linesVisible = view == nullptr || view[1] < view[3];
    if (RValue_toReal(Instance_getSelfVar(self, cache.init)) == 0)
        initializeLines(ctx, self);

    GMLReal phase = RValue_toReal(Instance_getSelfVar(self, cache.linesiner)) + 1;
    Instance_setSelfVar(self, cache.linesiner, RValue_makeReal(phase));
    if (RValue_toReal(Instance_getSelfVar(self, cache.init)) != 1) {
        if (renderer != nullptr) renderer->drawColor = 0x00FF00;
        return;
    }

    RValue lines = Instance_getSelfVar(self, cache.liney);
    GMLArray* liney = lines.type == RVALUE_ARRAY ? lines.array : nullptr;
    if (liney == nullptr) return;
    GMLReal wrap = RValue_toReal(GMLArray_get(liney, 19));

    if (renderer != nullptr) {
        for (int32_t i = 0; i < 18; ++i) {
            float top = shadeY + i * 15;
            float bottom = top + 15;
            if (!linesVisible) continue;
            if (view != nullptr) {
                if (top < view[1]) top = view[1];
                if (bottom > view[3]) bottom = view[3];
                if (top >= bottom) continue;
            }
            renderer->drawAlpha = cache.shadeAlpha[i];
            drawRectangle(renderer, -100, top, (float)ctx->runner->currentRoom->width + 100,
                bottom, cache.shadeColors[i]);
        }

        renderer->drawColor = 0x00FF00;
        renderer->drawAlpha = (float)(0.5 + GMLReal_sin(phase / 12) * 0.1);
        float pathX = (float)(xx + 320);
        float pathY = (float)(yy + 320);
        if (ctx->runner->applyOffsetForPrimitives) {
            pathX += 1;
            pathY += 1;
        }
        for (int32_t i = 0; linesVisible && i < 8; ++i) {
            drawPerspectivePath(renderer, pathX, pathY, i, 1, view);
            drawPerspectivePath(renderer, pathX - 1, pathY, i, 1, view);
            drawPerspectivePath(renderer, pathX, pathY, i, -1, view);
            drawPerspectivePath(renderer, pathX + 1, pathY, i, -1, view);
        }
    }

    for (int32_t i = 0; i < 19; ++i) {
        GMLReal y = RValue_toReal(GMLArray_get(liney, i));
        y = y + (-400 - y) * 0.005;
        if (y <= -100) y += wrap;
        GMLArray_set(liney, i, RValue_makeReal(y));

        if (renderer != nullptr && linesVisible) {
            float x1 = (float)(xx - 100 + y / 2);
            float y1 = (float)(yy + y);
            float x2 = (float)(xx + 740 - y / 2);
            float y2 = (float)(yy + 600);
            if (view != nullptr &&
                ((x1 < view[0] && x2 < view[0]) ||
                 (x1 > view[2] && x2 > view[2]) ||
                 (y1 < view[1] && y2 < view[1]) ||
                 (y1 > view[3] && y2 > view[3]))) continue;
            if (ctx->runner->applyOffsetForPrimitives) {
                x1++; y1++; x2++; y2++;
            }
            drawGridEllipse(renderer, (x1 + x2) * 0.5f, (y1 + y2) * 0.5f,
                (x2 - x1) * 0.5f, (y2 - y1) * 0.5f, view);
        }
    }
    Instance_setSelfVar(self, cache.i, RValue_makeReal(19));
    if (renderer != nullptr) {
        renderer->drawAlpha = 1;
        drawRectangle(renderer, -100, (float)ctx->runner->currentRoom->height - 240,
            (float)ctx->runner->currentRoom->width + 100,
            (float)ctx->runner->currentRoom->height + 2, 0);
    }
}

static bool cyberlinesDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    if (self->depth != 1005400)
        Runner_moveInstanceToDepthLayer(runner, self, 1005400);

    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    GMLReal xx = camera != nullptr ? camera->viewX * 0.95 : -0.95;
    GMLReal yy = camera != nullptr ? camera->viewY : -1;
    Instance_setSelfVar(self, cache.xx, RValue_makeReal(xx));
    Instance_setSelfVar(self, cache.yy, RValue_makeReal(yy));

    float visible[4];
    const float* view = nullptr;
#ifdef PLATFORM_3DS
    if (runner->renderer != nullptr && C3D_getTileCullRect(runner->renderer, visible))
        view = visible;
#else
    (void)visible;
#endif
    drawLines(ctx, self, xx, yy, (float)(yy - 50), view);
    if (runner->renderer != nullptr) runner->renderer->drawAlpha = 1;
    return true;
}

static void drawTeacupSky(VMContext* ctx, Instance* self, const float* view) {
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    float width = (float)runner->currentRoom->width;
    drawRectangle(renderer, 0, 3000, width, 7500, Color_lerp(0x800000, 0, 0.7f));
    drawRectangle(renderer, 0, 7500, width, 8500, Color_lerp(0x800000, 0, 0.6f));
    drawRectangle(renderer, 0, 0, width, 4000, 0x1D0202);
    int32_t height = ctx->dataWin->sprt.sprites[cache.sky].height;
    drawTiledArea(renderer, cache.sky, 0, 0, 4000, (float)self->x + width,
        4000 + height - 1, 0xFFFFFF, 1, view, false);

    GMLCamera* camera = Runner_getCameraForView(runner, 0);
    GMLReal viewX = camera != nullptr ? camera->viewX : -1;
    GMLReal viewY = camera != nullptr ? camera->viewY : -1;
    GMLReal starX = viewX * 0.00125 + self->xstart;
    GMLReal starY = 3000 + viewY * 0.0125;
    drawTiledArea(renderer, cache.stars, 0, (float)starX, (float)starY,
        width, (float)(starY + 1000), 0xFFFFFF, 1, view, false);
    height = ctx->dataWin->sprt.sprites[cache.blackSky].height;
    drawTiledArea(renderer, cache.blackSky, 0, 0, 2100, (float)self->x + width,
        2100 + height - 1, 0xFFFFFF, 1, view, false);
    drawRectangle(renderer, 0, 0, width, 2100, 0);
    height = ctx->dataWin->sprt.sprites[cache.coasterCity].height;
    drawTiledArea(renderer, cache.coasterTile, 0, 2720, 1400, width,
        1400 + height, 0xFFFFFF, 1, view, false);
    drawTiledArea(renderer, cache.coasterCity, 0, 2720, 1488, width,
        1488 + height, 0xFFFFFF, 1, view, false);
}

static bool parallaxerLayerCyberDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = runner->renderer;
    *outResult = RValue_makeUndefined();
    int32_t spriteIndex = self->spriteIndex;
    if ((uint32_t)spriteIndex >= ctx->dataWin->sprt.count) return true;

    GMLReal spriteHeight = RValue_toReal(Instance_getSelfVar(self, cache.sprHeight));
    if (spriteHeight == 0) {
        spriteHeight = ctx->dataWin->sprt.sprites[spriteIndex].height;
        Instance_setSelfVar(self, cache.sprHeight, RValue_makeReal(spriteHeight));
    }

    int32_t shadow = RValue_toInt32(Instance_getSelfVar(self, cache.shadoweffect));
    float visible[4];
    const float* view = nullptr;
#ifdef PLATFORM_3DS
    if (renderer != nullptr && C3D_getTileCullRect(renderer, visible)) view = visible;
#else
    (void)visible;
#endif

    float x = (float)self->x, y = (float)self->y;
    float width = (float)runner->currentRoom->width;
    if (shadow == 0) {
        if (renderer != nullptr)
            drawTiledArea(renderer, spriteIndex, (int32_t)self->imageIndex,
                x, y, x + width, y + (float)spriteHeight, 0xFFFFFF, 1, view, false);
        return true;
    }

    if (shadow == 1) {
        if (spriteIndex == cache.buildings) {
            bool teacup = runner->currentRoomIndex == cache.teacup;
            if (renderer != nullptr && teacup) drawTeacupSky(ctx, self, view);
            GMLCamera* camera = Runner_getCameraForView(runner, 0);
            GMLReal xx = (camera != nullptr ? camera->viewX : -1) * 0.95;
            GMLReal yy = teacup ? 8500 : (camera != nullptr ? camera->viewY : -1);
            Instance_setSelfVar(self, cache.xx, RValue_makeReal(xx));
            Instance_setSelfVar(self, cache.yy, RValue_makeReal(yy));
            drawLines(ctx, self, xx, yy, (float)yy, view);
        }

        if (renderer != nullptr) {
            drawTiledArea(renderer, spriteIndex, 0, x, y, x + width,
                y + (float)spriteHeight, 0xFFFFFF, 1, view, true);
            drawTiledArea(renderer, spriteIndex, 0, x, y + 8, x + width,
                y + 8 + (float)spriteHeight, 0x808080, 1, view, false);
        }

        if (spriteIndex == cache.buildings) {
            GMLReal phase = RValue_toReal(Instance_getSelfVar(self, cache.siner)) + 1;
            Instance_setSelfVar(self, cache.siner, RValue_makeReal(phase));
            if (renderer != nullptr) {
                renderer->drawAlpha = 1;
                renderer->vtable->gpuSetBlendMode(renderer, bm_add);
                drawTiledArea(renderer, cache.lights, 0, x, y + 8, width,
                    y + 8 + (float)spriteHeight, 0xFFFFFF,
                    (float)(0.5 + GMLReal_sin(phase / 12) * 0.1), view, false);
                renderer->vtable->gpuSetBlendMode(renderer, bm_normal);
            }
        }
        if (renderer != nullptr && spriteIndex == cache.ground)
            drawRectangle(renderer, x, y + 440, x + width + 100,
                y + 500 + (float)runner->currentRoom->height + 2, 0);
        return true;
    }

    if (shadow == 2) {
        self->imageBlend = 0xFFFFFF;
        if (renderer != nullptr)
            Renderer_drawSpriteTiled(renderer, spriteIndex, (int32_t)self->imageIndex, x, y,
                1, 1, width, (float)runner->currentRoom->height, 0xFFFFFF, renderer->drawAlpha);
        self->imageBlend = 0x808080;
        if (renderer != nullptr)
            Renderer_drawSpriteTiled(renderer, spriteIndex, (int32_t)self->imageIndex, x, y + 8,
                1, 1, width, (float)runner->currentRoom->height, 0x404040, 1);
    }
    return true;
}

void NativeChapter2CyberBackgroundDraw_register(VMContext* ctx) {
#ifdef PLATFORM_3DS
    static const char* const parallaxNames[] = {
        "obj_background_cyberlines",
        "obj_parallaxer_layer_cyber",
        "obj_parallaxer_layer",
        "obj_parallaxer_cyber",
        "obj_parallaxer_cyber_2",
        "obj_parallaxer_mansion",
        "obj_city_parallaxStreet",
    };
    cache.owner = ctx;
    for (uint32_t i = 0; i < sizeof(cache.parallaxObjects) / sizeof(cache.parallaxObjects[0]); ++i)
        cache.parallaxObjects[i] = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        for (uint32_t j = 0; j < sizeof(parallaxNames) / sizeof(parallaxNames[0]); ++j) {
            if (strcmp(object->name, parallaxNames[j]) == 0)
                cache.parallaxObjects[j] = (int32_t)i;
        }
    }
#endif
    cache.circleSegments = 0;
    cache.clouds = cache.buildings = cache.ground = cache.lights = cache.teacup = -1;
    cache.sky = cache.stars = cache.blackSky = cache.coasterTile = cache.coasterCity = -1;
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const char* name = ctx->dataWin->sprt.sprites[i].name;
        if (name == nullptr) continue;
        if (strcmp(name, "spr_bg_cyber_parallax_clouds") == 0) cache.clouds = i;
        else if (strcmp(name, "spr_bg_cyber_parallax_buildings") == 0) cache.buildings = i;
        else if (strcmp(name, "spr_bg_cyber_parallax_ground") == 0) cache.ground = i;
        else if (strcmp(name, "spr_bg_cyber_parallax_buildings_lights") == 0) cache.lights = i;
        else if (strcmp(name, "spr_cyber_starry_sky") == 0) cache.sky = i;
        else if (strcmp(name, "spr_cyber_starry_stars") == 0) cache.stars = i;
        else if (strcmp(name, "spr_cyber_starry_sky_black") == 0) cache.blackSky = i;
        else if (strcmp(name, "spr_cyber_coaster_bg_tile") == 0) cache.coasterTile = i;
        else if (strcmp(name, "spr_cyber_coaster_bg_cityscape") == 0) cache.coasterCity = i;
    }
    for (uint32_t i = 0; i < ctx->dataWin->room.count; ++i)
        if (strcmp(ctx->dataWin->room.rooms[i].name, "room_dw_cyber_teacup_final") == 0)
            cache.teacup = i;

    for (int32_t i = 0; i < 18; ++i) {
        float alpha = 0;
        float blue = 0;
        for (int32_t j = i + 1; j <= 18; ++j) {
            uint32_t tint = Color_lerp(0x800000, 0, j * 0.05f);
            blue = blue * 0.9f + ((tint >> 16) & 0xFF) * 0.1f;
            alpha = alpha * 0.9f + 0.1f;
        }
        cache.shadeAlpha[i] = alpha;
        cache.shadeColors[i] = (uint32_t)roundf(blue / alpha) << 16;
    }

    if (cache.buildings < 0 || cache.ground < 0 || cache.lights < 0 ||
        cache.sky < 0 || cache.stars < 0 || cache.blackSky < 0 ||
        cache.coasterTile < 0 || cache.coasterCity < 0) return;

    cache.pathAdd = VM_findBuiltin(ctx, "path_add");
    cache.pathSetClosed = VM_findBuiltin(ctx, "path_set_closed");
    cache.pathSetPrecision = VM_findBuiltin(ctx, "path_set_precision");
    cache.pathAddPoint = VM_findBuiltin(ctx, "path_add_point");
    if (cache.pathAdd == nullptr || cache.pathSetClosed == nullptr ||
        cache.pathSetPrecision == nullptr || cache.pathAddPoint == nullptr) return;

    cache.init = NativeScript_getSelfVarID(ctx, "init");
    cache.linesiner = NativeScript_getSelfVarID(ctx, "linesiner");
    cache.liney = NativeScript_getSelfVarID(ctx, "liney");
    cache.patha = NativeScript_getSelfVarID(ctx, "patha");
    cache.pathb = NativeScript_getSelfVarID(ctx, "pathb");
    cache.xx = NativeScript_getSelfVarID(ctx, "xx");
    cache.yy = NativeScript_getSelfVarID(ctx, "yy");
    cache.i = NativeScript_getSelfVarID(ctx, "i");
    cache.j = NativeScript_getSelfVarID(ctx, "j");
    cache.shadoweffect = NativeScript_getSelfVarID(ctx, "shadoweffect");
    cache.sprHeight = NativeScript_getSelfVarID(ctx, "spr_height");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");

    VM_registerNativeScript(ctx, "gml_Object_obj_background_cyberlines_Draw_0", cyberlinesDraw);
    VM_registerNativeScript(ctx, "gml_Object_obj_parallaxer_layer_cyber_Draw_0", parallaxerLayerCyberDraw);
}
