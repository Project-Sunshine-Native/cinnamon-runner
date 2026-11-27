#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

#define VIEW_PROPERTY_COUNT 17

static struct {
    int32_t growtangleObject;
    int32_t wallDestroy, updateImageAngle, spin, spinSpeed, bottomFade, timer;
    Chapter3Builtin viewGetCamera, lerp;
    Chapter3Builtin properties[VIEW_PROPERTY_COUNT];
    bool usesCamera[VIEW_PROPERTY_COUNT];
} cache;

static bool viewGet(VMContext* ctx, int32_t property, RValue index, RValue* outResult) {
    if (0 > property || property >= VIEW_PROPERTY_COUNT) {
        *outResult = RValue_makeReal(-1);
        return true;
    }
    const Chapter3Builtin* builtin = &cache.properties[property];
    if (builtin->func == nullptr || (cache.usesCamera[property] && cache.viewGetCamera.func == nullptr)) return false;
    if (cache.usesCamera[property]) {
        RValue camera = Chapter3_call(ctx, &cache.viewGetCamera, &index, 1);
        *outResult = Chapter3_call(ctx, builtin, &camera, 1);
        RValue_free(&camera);
    } else {
        *outResult = Chapter3_call(ctx, builtin, &index, 1);
    }
    return true;
}

static int32_t propertyIndex(VMContext* ctx, RValue value) {
    if (value.type != RVALUE_REAL && value.type != RVALUE_INT32 && value.type != RVALUE_BOOL
#ifndef NO_RVALUE_INT64
        && value.type != RVALUE_INT64
#endif
        ) return -2;
    GMLReal real = RValue_toReal(value);
    for (int32_t i = 0; VIEW_PROPERTY_COUNT > i; i++) {
        if (Chapter3_compare(ctx, real, (GMLReal)i) == 0) return i;
    }
    return -1;
}

static bool scrViewGet(VMContext* ctx, RValue* outResult) {
    int32_t property = propertyIndex(ctx, NativeScript_getArgument(ctx, 0));
    if (property == -2) return false;
    return viewGet(ctx, property, NativeScript_getArgument(ctx, 1), outResult);
}

static bool viewCoordinate(VMContext* ctx, int32_t property, GMLReal* out) {
    RValue result;
    if (!viewGet(ctx, property, RValue_makeInt32(0), &result)) return false;
    *out = RValue_toReal(result);
    RValue_free(&result);
    return true;
}

static bool isNumber(RValue value) {
    return value.type == RVALUE_REAL || value.type == RVALUE_INT32 || value.type == RVALUE_BOOL
#ifndef NO_RVALUE_INT64
        || value.type == RVALUE_INT64
#endif
        ;
}

static bool regularBulletStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    RValue wallDestroy = Instance_getSelfVar(self, cache.wallDestroy);
    RValue updateImageAngle = Instance_getSelfVar(self, cache.updateImageAngle);
    RValue spin = Instance_getSelfVar(self, cache.spin);
    RValue bottomFade = Instance_getSelfVar(self, cache.bottomFade);
    if (!isNumber(wallDestroy) || !isNumber(updateImageAngle) || !isNumber(spin) || !isNumber(bottomFade)) return false;
    if (Chapter3_compare(ctx, RValue_toReal(spin), 1) == 0 && !isNumber(Instance_getSelfVar(self, cache.spinSpeed))) return false;
    if (cache.properties[0].func == nullptr || cache.properties[1].func == nullptr || cache.viewGetCamera.func == nullptr) return false;

    if (Chapter3_compare(ctx, RValue_toReal(wallDestroy), 1) == 0) {
        GMLReal view;
        viewCoordinate(ctx, 0, &view);
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x"), view - 80) < 0)
            Runner_destroyInstance(runner, self, true);
        viewCoordinate(ctx, 0, &view);
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x"), view + 760) > 0)
            Runner_destroyInstance(runner, self, true);
        viewCoordinate(ctx, 1, &view);
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y"), view - 80) < 0)
            Runner_destroyInstance(runner, self, true);
        viewCoordinate(ctx, 1, &view);
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y"), view + 580) > 0)
            Runner_destroyInstance(runner, self, true);
    }

    if (Chapter3_compare(ctx, RValue_toReal(updateImageAngle), 1) == 0)
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
            Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DIRECTION, "direction"));

    if (Chapter3_compare(ctx, RValue_toReal(spin), 1) == 0)
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
            Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle") + Chapter3_getReal(self, cache.spinSpeed));

    GMLReal fade = RValue_toReal(bottomFade);
    if (Chapter3_compare(ctx, fade, 0) != 0) {
        if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y"), Chapter3_cameraY(runner) + fade) > 0)
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha",
                Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha") * R(0.8));
    }
    *outResult = RValue_makeUndefined();
    return true;
}

static GMLReal lerp3(VMContext* ctx, GMLReal a, GMLReal b, GMLReal t) {
    RValue args[] = { RValue_makeReal(a), RValue_makeReal(b), RValue_makeReal(t) };
    return Chapter3_callReal(ctx, &cache.lerp, args, 3);
}

static bool diagonalBulletStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Instance* growtangle = Chapter3_resolveInstance(ctx->runner, RValue_makeReal((GMLReal)cache.growtangleObject));
    if (growtangle == nullptr) return false;
    *outResult = RValue_makeUndefined();
    Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
    GMLReal a = GMLReal_fabs(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x") -
        Chapter3_getBuiltin(ctx, growtangle, BUILTIN_VAR_X, "x")) / 300;
    if (0 > a) a = 0;
    if (a > 1) a = 1;
    GMLReal maxalpha = lerp3(ctx, R(1.3), 0, a);
    GMLReal t = GMLReal_fabs(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y") -
        Chapter3_getBuiltin(ctx, growtangle, BUILTIN_VAR_Y, "y")) / 200;
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", lerp3(ctx, maxalpha, 0, t));
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), 260) > 0)
        Runner_destroyInstance(ctx->runner, self, true);
    return true;
}

void NativeChapter3ViewGet_register(VMContext* ctx) {
    static const char* const names[VIEW_PROPERTY_COUNT] = {
        "camera_get_view_x", "camera_get_view_y", "camera_get_view_width", "camera_get_view_height",
        "camera_get_view_angle", "camera_get_view_border_x", "camera_get_view_border_y",
        "camera_get_view_speed_x", "camera_get_view_speed_y", "camera_get_view_target",
        "view_get_visible", "view_get_xport", "view_get_yport", "view_get_wport", "view_get_hport",
        "view_get_camera", "view_get_surface_id"
    };
    for (int32_t i = 0; VIEW_PROPERTY_COUNT > i; i++) {
        Chapter3_bindBuiltin(ctx, &cache.properties[i], names[i]);
        cache.usesCamera[i] = 10 > i;
    }
    Chapter3_bindBuiltin(ctx, &cache.viewGetCamera, "view_get_camera");
    cache.wallDestroy = NativeScript_getSelfVarID(ctx, "wall_destroy");
    cache.updateImageAngle = NativeScript_getSelfVarID(ctx, "updateimageangle");
    cache.spin = NativeScript_getSelfVarID(ctx, "spin");
    cache.spinSpeed = NativeScript_getSelfVarID(ctx, "spinspeed");
    cache.bottomFade = NativeScript_getSelfVarID(ctx, "bottomfade");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.growtangleObject = Chapter3_findObject(ctx, "obj_growtangle");

    (void)VM_registerNativeScript(ctx, "gml_Script___view_get", scrViewGet);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_regularbullet_Step_0", regularBulletStep);
    if (cache.growtangleObject >= 0 && Chapter3_bindBuiltin(ctx, &cache.lerp, "lerp"))
        (void)VM_registerNativeScript(ctx, "gml_Object_obj_diagonal_bullet_Step_0", diagonalBulletStep);
}
