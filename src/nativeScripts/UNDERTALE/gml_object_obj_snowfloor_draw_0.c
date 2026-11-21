// taken from https://github.com/Project-Sunshine-Native/cinnamon/tree/UNDERTALE-3DS because i was lazy
#include "nativeScripts/native_runtime.h"

#include "collision.h"
#include "gml_array.h"
#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

#include <stdlib.h>
#include <string.h>

static NativeVarBinding doDrawBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding snowXBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding snowYBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding moveMeBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding movingBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding flagBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding mainCharaBinding = NATIVE_ASSET_BINDING_INIT;

static GMLArray* ensureArray(VMContext* ctx, Instance* instance,
    NativeVarBinding* binding, const char* name) {
    if (shgeti(ctx->varNameMap, (char*)name) < 0) return nullptr;
    RValue value = NativeScript_getVariable(ctx, instance, binding, name);
    if (value.type == RVALUE_ARRAY && value.array != nullptr) return value.array;

    value = RValue_makeArray(GMLArray_create(ctx->dataWin, 0));
    GMLArray* array = value.array;
    NativeScript_setVariable(ctx, instance, binding, name, value);
    RValue_free(&value);
    return array;
}

static void drawSnowDot(Renderer* renderer, GMLReal x, GMLReal y) {
#ifdef PLATFORM_3DS
    // Approximate the tiny filled circle with three thin bands.
    renderer->vtable->drawRectangle(renderer, (float)(x - 2.0), (float)(y - 2.5),
        (float)(x + 2.0), (float)(y - 1.5), renderer->drawColor, renderer->drawAlpha, false);
    renderer->vtable->drawRectangle(renderer, (float)(x - 2.5), (float)(y - 1.5),
        (float)(x + 2.5), (float)(y + 1.5), renderer->drawColor, renderer->drawAlpha, false);
    renderer->vtable->drawRectangle(renderer, (float)(x - 2.0), (float)(y + 1.5),
        (float)(x + 2.0), (float)(y + 2.5), renderer->drawColor, renderer->drawAlpha, false);
#else
    Renderer_drawCircle(renderer, (float)x, (float)y, 2.8f, false);
#endif
}

static bool snowfloorDraw(VMContext* ctx, RValue* outResult) {
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    Instance* self = ctx->currentCallFrame->self;
    if (self == nullptr || renderer == nullptr) return false;

    GMLArray* dodraw = ensureArray(ctx, self, &doDrawBinding, "dodraw");
    GMLArray* snowx = ensureArray(ctx, self, &snowXBinding, "snowx");
    GMLArray* snowy = ensureArray(ctx, self, &snowYBinding, "snowy");
    GMLArray* moveme = ensureArray(ctx, self, &moveMeBinding, "moveme");
    if (dodraw == nullptr || snowx == nullptr || snowy == nullptr || moveme == nullptr) {
        *outResult = RValue_makeUndefined();
        return true;
    }

    Instance* mainChara = nullptr;
    if (NativeScript_canResolveAsset(ctx, &mainCharaBinding, "obj_mainchara")) {
        int32_t objectIndex = mainCharaBinding.assetIndex;
        if ((uint32_t)objectIndex < ctx->dataWin->objt.count) {
            Instance** instances = runner->instancesByObject[objectIndex];
            for (ptrdiff_t i = 0; i < arrlen(instances); ++i) {
                if (instances[i]->active) {
                    mainChara = instances[i];
                    break;
                }
            }
        }
    }
    bool mainCharaMoving = mainChara != nullptr &&
        RValue_toInt32(NativeScript_getVariable(ctx, mainChara, &movingBinding, "moving")) == 1;
    GMLReal bboxLeft = 0.0, bboxRight = 0.0, bboxTop = 0.0, bboxBottom = 0.0;
    if (mainChara != nullptr) {
        bboxLeft = RValue_toReal(VMBuiltins_getVariable(ctx, mainChara, BUILTIN_VAR_BBOX_LEFT, "bbox_left", -1));
        bboxRight = RValue_toReal(VMBuiltins_getVariable(ctx, mainChara, BUILTIN_VAR_BBOX_RIGHT, "bbox_right", -1));
        bboxTop = RValue_toReal(VMBuiltins_getVariable(ctx, mainChara, BUILTIN_VAR_BBOX_TOP, "bbox_top", -1));
        bboxBottom = RValue_toReal(VMBuiltins_getVariable(ctx, mainChara, BUILTIN_VAR_BBOX_BOTTOM, "bbox_bottom", -1));
    }
    bool isSnowPuzzleRoom = runner->currentRoom != nullptr && runner->currentRoom->name != nullptr &&
        strcmp(runner->currentRoom->name, "room_tundra_snowpuzz") == 0;

    renderer->drawColor = 0xFFFFFFu;
    for (int32_t yy = 0; yy < 5; ++yy) {
        for (int32_t xx = 0; xx < 5; ++xx) {
            int32_t index = yy * GML_LEGACY_ARRAY_STRIDE + xx;
            GMLReal dotX = RValue_toReal(GMLArray_get(snowx, index));
            GMLReal dotY = RValue_toReal(GMLArray_get(snowy, index));
            GMLReal move = RValue_toReal(GMLArray_get(moveme, index));

            if (RValue_toInt32(GMLArray_get(dodraw, index)) == 1) drawSnowDot(renderer, dotX, dotY);

            if (mainChara != nullptr && Collision_circleOverlapsInstance(runner, mainChara, dotX, dotY, 2.0)) {
                move = GMLReal_floor(((GMLReal)rand() / (GMLReal)RAND_MAX) * 4.0) + 2.0;
                GMLArray_set(moveme, index, RValue_makeReal(move));
            }

            if (move > 1.0) {
                if (mainCharaMoving) {
                    if (isSnowPuzzleRoom) {
                        GMLArray* globalFlag = ensureArray(ctx, ctx->globalScopeInstance, &flagBinding, "flag");
                        if (globalFlag != nullptr && (int32_t)RValue_toReal(GMLArray_get(globalFlag, 64)) == 0) {
                            GMLArray_set(globalFlag, 64, RValue_makeReal(-1.0));
                        }
                    }

                    if (bboxLeft > dotX) dotX -= move;
                    if (bboxRight < dotX) dotX += move;
                    if (bboxTop > dotY) dotY -= move;
                    if (bboxBottom < dotY) dotY += move;

                    dotX += (((GMLReal)rand() / (GMLReal)RAND_MAX) * move - move / 2.0) / 2.0;
                    dotY += (((GMLReal)rand() / (GMLReal)RAND_MAX) * move - move / 2.0) / 2.0;
                    GMLArray_set(snowx, index, RValue_makeReal(dotX));
                    GMLArray_set(snowy, index, RValue_makeReal(dotY));
                }

                move -= 1.0;
                GMLArray_set(moveme, index, RValue_makeReal(move));
            }
        }
    }
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeSnowfloorDraw_register(VMContext* ctx) {
    doDrawBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    snowXBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    snowYBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    moveMeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    movingBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    flagBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    mainCharaBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_snowfloor_Draw_0", snowfloorDraw);
}
