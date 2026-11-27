#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    Chapter3Builtin pointDirection, pointDistance, lengthdirX, lengthdirY;
    Chapter3Builtin drawGetAlpha, drawSetAlpha, drawSetColor, drawCircle, drawTriangle, drawTriangleColor;
} cache;

static void rotateLength(VMContext* ctx, GMLReal length, GMLReal angle, GMLReal* outX, GMLReal* outY) {
    if (Chapter3_compare(ctx, angle, 0) == 0) {
        *outX = length;
        *outY = 0;
        return;
    }
    RValue pointArgs[] = { RValue_makeReal(0), RValue_makeReal(0), RValue_makeReal(length), RValue_makeReal(0) };
    GMLReal dir = Chapter3_callReal(ctx, &cache.pointDirection, pointArgs, 4);
    GMLReal len = Chapter3_callReal(ctx, &cache.pointDistance, pointArgs, 4);
    RValue lengthArgs[] = { RValue_makeReal(len), RValue_makeReal(dir + angle) };
    *outX = Chapter3_callReal(ctx, &cache.lengthdirX, lengthArgs, 2);
    *outY = Chapter3_callReal(ctx, &cache.lengthdirY, lengthArgs, 2);
}

static void drawBeam(VMContext* ctx, bool colored) {
    GMLReal xx = RValue_toReal(NativeScript_getArgument(ctx, 0));
    GMLReal yy = RValue_toReal(NativeScript_getArgument(ctx, 1));
    GMLReal length = RValue_toReal(NativeScript_getArgument(ctx, 2));
    GMLReal width = RValue_toReal(NativeScript_getArgument(ctx, 3));
    GMLReal angle = RValue_toReal(NativeScript_getArgument(ctx, 4));
    int32_t alphaArg = colored ? 7 : 6;
    int32_t circleArg = colored ? 8 : 7;

    GMLReal end0x, end0y, end1x, end1y, end2x, end2y;
    rotateLength(ctx, length, angle, &end0x, &end0y);
    end0x += xx;
    end0y += yy;
    rotateLength(ctx, length, angle + (width / 2), &end1x, &end1y);
    end1x += xx;
    end1y += yy;
    rotateLength(ctx, length, angle - (width / 2), &end2x, &end2y);
    end2x += xx;
    end2y += yy;

    if (!colored) {
        RValue color = NativeScript_getArgument(ctx, 5);
        Chapter3_callVoid(ctx, &cache.drawSetColor, &color, 1);
    }
    RValue previousAlpha = Chapter3_call(ctx, &cache.drawGetAlpha, nullptr, 0);
    RValue alpha = NativeScript_getArgument(ctx, alphaArg);
    Chapter3_callVoid(ctx, &cache.drawSetAlpha, &alpha, 1);

    if (RValue_toBool(NativeScript_getArgument(ctx, circleArg))) {
        RValue circleArgs[] = { RValue_makeReal(end0x), RValue_makeReal(end0y), RValue_makeReal(width / 2), RValue_makeReal(0) };
        Chapter3_callVoid(ctx, &cache.drawCircle, circleArgs, 4);
    }

    if (colored) {
        RValue color1 = NativeScript_getArgument(ctx, 5);
        RValue color2 = NativeScript_getArgument(ctx, 6);
        RValue triangleArgs[] = {
            RValue_makeReal(xx), RValue_makeReal(yy), RValue_makeReal(end1x), RValue_makeReal(end1y),
            RValue_makeReal(end2x), RValue_makeReal(end2y), color1, color2, color2, RValue_makeReal(0)
        };
        Chapter3_callVoid(ctx, &cache.drawTriangleColor, triangleArgs, 10);
    } else {
        RValue triangleArgs[] = {
            RValue_makeReal(xx), RValue_makeReal(yy), RValue_makeReal(end1x), RValue_makeReal(end1y),
            RValue_makeReal(end2x), RValue_makeReal(end2y), RValue_makeReal(0)
        };
        Chapter3_callVoid(ctx, &cache.drawTriangle, triangleArgs, 7);
    }
    Chapter3_callVoid(ctx, &cache.drawSetAlpha, &previousAlpha, 1);
    RValue_free(&previousAlpha);
}

static bool scrDrawBeam(VMContext* ctx, RValue* outResult) {
    drawBeam(ctx, false);
    *outResult = RValue_makeUndefined();
    return true;
}

static bool scrDrawBeamColor(VMContext* ctx, RValue* outResult) {
    drawBeam(ctx, true);
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter3DrawBeam_register(VMContext* ctx) {
    Chapter3_bindBuiltin(ctx, &cache.pointDirection, "point_direction");
    Chapter3_bindBuiltin(ctx, &cache.pointDistance, "point_distance");
    Chapter3_bindBuiltin(ctx, &cache.lengthdirX, "lengthdir_x");
    Chapter3_bindBuiltin(ctx, &cache.lengthdirY, "lengthdir_y");
    Chapter3_bindBuiltin(ctx, &cache.drawGetAlpha, "draw_get_alpha");
    Chapter3_bindBuiltin(ctx, &cache.drawSetAlpha, "draw_set_alpha");
    Chapter3_bindBuiltin(ctx, &cache.drawSetColor, "draw_set_color");
    Chapter3_bindBuiltin(ctx, &cache.drawCircle, "draw_circle");
    Chapter3_bindBuiltin(ctx, &cache.drawTriangle, "draw_triangle");
    Chapter3_bindBuiltin(ctx, &cache.drawTriangleColor, "draw_triangle_color");
    if (cache.pointDirection.func == nullptr || cache.pointDistance.func == nullptr || cache.lengthdirX.func == nullptr ||
        cache.lengthdirY.func == nullptr || cache.drawGetAlpha.func == nullptr || cache.drawSetAlpha.func == nullptr ||
        cache.drawSetColor.func == nullptr || cache.drawCircle.func == nullptr || cache.drawTriangle.func == nullptr ||
        cache.drawTriangleColor.func == nullptr) return;

    (void)VM_registerNativeScript(ctx, "gml_Script_scr_draw_beam", scrDrawBeam);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_draw_beam_color", scrDrawBeamColor);
}
