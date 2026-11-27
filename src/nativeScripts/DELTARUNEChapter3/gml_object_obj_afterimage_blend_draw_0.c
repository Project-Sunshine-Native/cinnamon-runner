#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t blendmode, destBlend, fadeSpeed, con, whitealpha, solidcolor;
    Chapter3Builtin gpuGetBlendmode, gpuSetBlendmode, gpuSetBlendmodeExt, gpuSetFog, drawSelf, drawSpriteExt;
} cache;

static RValue addValues(RValue a, RValue b, GMLReal sign) {
    if (a.type == RVALUE_INT32 && b.type == RVALUE_INT32) return RValue_makeInt32(a.int32 + (int32_t)sign * b.int32);
    return RValue_makeReal(RValue_toReal(a) + sign * RValue_toReal(b));
}

static bool afterimageBlendDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    RValue previousBlendmode = Chapter3_call(ctx, &cache.gpuGetBlendmode, nullptr, 0);
    RValue destBlend = Instance_getSelfVar(self, cache.destBlend);
    if (Chapter3_compare(ctx, RValue_toReal(destBlend), -4) != 0) {
        RValue args[] = { Instance_getSelfVar(self, cache.blendmode), destBlend };
        Chapter3_callVoid(ctx, &cache.gpuSetBlendmodeExt, args, 2);
    } else {
        RValue args[] = { Instance_getSelfVar(self, cache.blendmode) };
        Chapter3_callVoid(ctx, &cache.gpuSetBlendmode, args, 1);
    }
    Chapter3_callVoid(ctx, &cache.drawSelf, nullptr, 0);
    Chapter3_callVoid(ctx, &cache.gpuSetBlendmode, &previousBlendmode, 1);
    RValue_free(&previousBlendmode);
    return true;
}

static bool afterimageFadeToWhiteDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    Chapter3_callVoid(ctx, &cache.drawSelf, nullptr, 0);

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 0) == 0)
        Instance_setSelfVar(self, cache.whitealpha, addValues(Instance_getSelfVar(self, cache.whitealpha), Instance_getSelfVar(self, cache.fadeSpeed), 1));

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.whitealpha), 1) >= 0) {
        Instance_setSelfVar(self, cache.con, RValue_makeReal(1));
        Instance_setSelfVar(self, cache.whitealpha, RValue_makeReal(1));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", 0);
    }

    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.con), 1) == 0) {
        Instance_setSelfVar(self, cache.whitealpha, addValues(Instance_getSelfVar(self, cache.whitealpha), Instance_getSelfVar(self, cache.fadeSpeed), -1));
        if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.whitealpha), 0) < 0)
            Runner_destroyInstance(ctx->runner, self, true);
    }

    RValue fogOn[] = { RValue_makeBool(true), RValue_makeInt32(16777215), RValue_makeInt32(0), RValue_makeInt32(1) };
    Chapter3_callVoid(ctx, &cache.gpuSetFog, fogOn, 4);
    RValue drawArgs[] = {
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend")),
        Instance_getSelfVar(self, cache.whitealpha)
    };
    Chapter3_callVoid(ctx, &cache.drawSpriteExt, drawArgs, 9);
    RValue fogOff[] = { RValue_makeBool(false), RValue_makeInt32(0), RValue_makeInt32(0), RValue_makeInt32(0) };
    Chapter3_callVoid(ctx, &cache.gpuSetFog, fogOff, 4);
    return true;
}

static bool afterimageGlowDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    RValue add = RValue_makeInt32(1);
    Chapter3_callVoid(ctx, &cache.gpuSetBlendmode, &add, 1);
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.solidcolor), -1) != 0) {
        RValue fogOn[] = { RValue_makeBool(true), Instance_getSelfVar(self, cache.solidcolor), RValue_makeInt32(0), RValue_makeInt32(0) };
        Chapter3_callVoid(ctx, &cache.gpuSetFog, fogOn, 4);
    }
    Chapter3_callVoid(ctx, &cache.drawSelf, nullptr, 0);
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.solidcolor), -1) != 0) {
        RValue fogOff[] = { RValue_makeBool(false), RValue_makeInt32(16777215), RValue_makeInt32(0), RValue_makeInt32(0) };
        Chapter3_callVoid(ctx, &cache.gpuSetFog, fogOff, 4);
    }
    RValue normal = RValue_makeInt32(0);
    Chapter3_callVoid(ctx, &cache.gpuSetBlendmode, &normal, 1);
    return true;
}

static bool boardAfterimageDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    GMLReal sprite = Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index");
    if (Chapter3_compare(ctx, sprite, -4) == 0) return true;
    RValue drawArgs[] = {
        RValue_makeReal(sprite),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index")),
        RValue_makeReal(GMLReal_bankersRound(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x") / 2) * 2),
        RValue_makeReal(GMLReal_bankersRound(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y") / 2) * 2),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"))
    };
    Chapter3_callVoid(ctx, &cache.drawSpriteExt, drawArgs, 9);
    return true;
}

void NativeChapter3AfterimageBlend_register(VMContext* ctx) {
    if (!Chapter3_bindBuiltin(ctx, &cache.gpuGetBlendmode, "gpu_get_blendmode") ||
        !Chapter3_bindBuiltin(ctx, &cache.gpuSetBlendmode, "gpu_set_blendmode") ||
        !Chapter3_bindBuiltin(ctx, &cache.gpuSetBlendmodeExt, "gpu_set_blendmode_ext") ||
        !Chapter3_bindBuiltin(ctx, &cache.gpuSetFog, "gpu_set_fog") ||
        !Chapter3_bindBuiltin(ctx, &cache.drawSelf, "draw_self") ||
        !Chapter3_bindBuiltin(ctx, &cache.drawSpriteExt, "draw_sprite_ext")) return;
    cache.blendmode = NativeScript_getSelfVarID(ctx, "blendmode");
    cache.destBlend = NativeScript_getSelfVarID(ctx, "dest_blend");
    cache.fadeSpeed = NativeScript_getSelfVarID(ctx, "fadeSpeed");
    cache.con = NativeScript_getSelfVarID(ctx, "con");
    cache.whitealpha = NativeScript_getSelfVarID(ctx, "whitealpha");
    cache.solidcolor = NativeScript_getSelfVarID(ctx, "solidcolor");

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_blend_Draw_0", afterimageBlendDraw);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_fade_to_white_Draw_0", afterimageFadeToWhiteDraw);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_glow_Draw_0", afterimageGlowDraw);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_board_afterimage_Draw_0", boardAfterimageDraw);
}
