#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"

#include <string.h>

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t boss, curAlpha, battleAlpha, battleMode, tileFade, tileArray;
    int32_t maxBattleAlpha, xx, yy;
    int32_t mainchara, caterpillar, eyeSprite;
    NativeCallBinding layerTileAlpha;
} cache;

static bool darkPonmanDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    GMLReal epsilon = ctx->runner->mathEpsilon;
    GMLReal battleAlpha = RValue_toReal(Instance_getSelfVar(self, cache.battleAlpha));
    GMLReal maxBattleAlpha = RValue_toReal(Instance_getSelfVar(self, cache.maxBattleAlpha));
    RValue boss = Instance_getSelfVar(self, cache.boss);
    if (boss.type != RVALUE_UNDEFINED && NativeScript_compareReals(RValue_toReal(boss), 1, epsilon) == 0) {
        Instance* mainchara = VM_findInstanceByTarget(ctx, cache.mainchara);
        if (mainchara == nullptr) return false;

        GMLReal previousAlpha = battleAlpha;
        Instance_setSelfVar(self, cache.curAlpha, RValue_makeReal(previousAlpha));
        RValue battleMode = Instance_getSelfVar(mainchara, cache.battleMode);
        if (battleMode.type != RVALUE_UNDEFINED &&
            NativeScript_compareReals(RValue_toReal(battleMode), 1, epsilon) == 0) {
            if (NativeScript_compareReals(battleAlpha, 0.5, epsilon) < 0) battleAlpha += 0.04;
        } else if (NativeScript_compareReals(battleAlpha, 0, epsilon) > 0) {
            battleAlpha -= 0.04;
        }
        Instance_setSelfVar(self, cache.battleAlpha, RValue_makeReal(battleAlpha));

        RValue tileFade = Instance_getSelfVar(self, cache.tileFade);
        if (tileFade.type != RVALUE_UNDEFINED &&
            NativeScript_compareReals(RValue_toReal(tileFade), 1, epsilon) == 0 &&
            NativeScript_compareReals(previousAlpha, battleAlpha, epsilon) != 0) {
            RValue tiles = Instance_getSelfVar(self, cache.tileArray);
            GMLArray* array = tiles.type == RVALUE_ARRAY ? tiles.array : nullptr;
            int32_t count = GMLArray_length1D(array);
            GMLReal alpha = battleAlpha * 2 * maxBattleAlpha;
            for (int32_t i = 0; i < count; ++i) {
                RValue args[2] = {GMLArray_get(array, i), RValue_makeReal(alpha)};
                RValue result = NativeScript_call(ctx, &cache.layerTileAlpha, "layer_tile_alpha", args, 2);
                RValue_free(&result);
            }
        }

        renderer->drawAlpha = (float)(battleAlpha * maxBattleAlpha);
        renderer->drawColor = 0;
        GMLCamera* camera = Runner_getCameraForView(ctx->runner, 0);
        GMLReal viewX = camera != nullptr ? (GMLReal)camera->viewX : -1;
        GMLReal viewY = camera != nullptr ? (GMLReal)camera->viewY : -1;
        renderer->vtable->drawRectangle(renderer, (float)(viewX - 100), (float)(viewY - 100),
            (float)(viewX + 740), (float)(viewY + 540), 0, renderer->drawAlpha, false);
        renderer->drawAlpha = 1.0f;

        Instance* caterpillar = VM_findInstanceByTarget(ctx, cache.caterpillar);
        if (caterpillar != nullptr) {
            uint32_t color = (uint32_t)Color_lerp(0xFFFFFF, 0, (float)(battleAlpha * maxBattleAlpha));
            caterpillar->imageBlend = NativeScript_compareReals(battleAlpha, 0, epsilon) <= 0
                ? 0xFFFFFF : color;
        }
    }

    GMLReal xx = RValue_toReal(Instance_getSelfVar(self, cache.xx));
    GMLReal yy = RValue_toReal(Instance_getSelfVar(self, cache.yy));
    Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
        self->x, self->y, self->imageXscale, self->imageYscale, (float)self->imageAngle,
        self->imageBlend, self->imageAlpha);
    Renderer_drawSpriteExt(renderer, cache.eyeSprite, (int32_t)self->imageIndex,
        (float)((GMLReal)self->x + xx), (float)((GMLReal)self->y + yy),
        self->imageXscale, self->imageYscale, (float)self->imageAngle, 0x0000FF, self->imageAlpha);

    *outResult = RValue_makeUndefined();
    return true;
}

void NativeDarkPonmanDraw_register(VMContext* ctx) {
    cache.mainchara = -1;
    cache.caterpillar = -1;
    cache.eyeSprite = -1;
    for (uint32_t i = 0; i < ctx->dataWin->objt.count; ++i) {
        const GameObject* object = &ctx->dataWin->objt.objects[i];
        if (!object->present) continue;
        if (strcmp(object->name, "obj_mainchara") == 0) cache.mainchara = (int32_t)i;
        if (strcmp(object->name, "obj_caterpillarchara") == 0) cache.caterpillar = (int32_t)i;
    }
    for (uint32_t i = 0; i < ctx->dataWin->sprt.count; ++i) {
        const Sprite* sprite = &ctx->dataWin->sprt.sprites[i];
        if (sprite->present && strcmp(sprite->name, "spr_ponman_eye_white") == 0) {
            cache.eyeSprite = (int32_t)i;
            break;
        }
    }
    if (cache.mainchara < 0 || cache.caterpillar < 0 || cache.eyeSprite < 0 ||
        !NativeScript_canCall(ctx, &cache.layerTileAlpha, "layer_tile_alpha") ||
        !VM_registerNativeScript(ctx, "gml_Object_obj_darkponman_ow_Draw_0", darkPonmanDraw)) return;

    cache.boss = NativeScript_getSelfVarID(ctx, "boss");
    cache.curAlpha = NativeScript_getSelfVarID(ctx, "cur_alpha");
    cache.battleAlpha = NativeScript_getSelfVarID(ctx, "battlealpha");
    cache.battleMode = NativeScript_getSelfVarID(ctx, "battlemode");
    cache.tileFade = NativeScript_getSelfVarID(ctx, "tile_fade");
    cache.tileArray = NativeScript_getSelfVarID(ctx, "tilearray");
    cache.maxBattleAlpha = NativeScript_getSelfVarID(ctx, "max_battlealpha");
    cache.xx = NativeScript_getSelfVarID(ctx, "xx");
    cache.yy = NativeScript_getSelfVarID(ctx, "yy");
}
