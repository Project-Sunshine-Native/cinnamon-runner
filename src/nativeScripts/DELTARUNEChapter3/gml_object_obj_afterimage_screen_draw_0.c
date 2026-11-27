#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

#ifdef PLATFORM_3DS
#define SCREEN_COPY_SCALE R(0.5)
#else
#define SCREEN_COPY_SCALE R(1)
#endif

static struct {
    int32_t screenObject, heartObject;
    int32_t mySurface, xscale, yscale, alpha, anchorX, anchorY, xrate, yrate, faderate, drawEnd;
    Chapter3Builtin surfaceExists, surfaceCreate, surfaceFree, surfaceSetTarget, surfaceResetTarget;
    Chapter3Builtin surfaceGetWidth, surfaceGetHeight, drawSurface, drawSurfaceExt, instanceNumber, drawSelf;
    const VMContext* sharedOwner;
    int32_t sharedSurface;
} cache;

static RValue callBuiltin(VMContext* ctx, const Chapter3Builtin* builtin, RValue* args, int32_t argCount) {
    return Chapter3_call(ctx, builtin, args, argCount);
}

static bool surfaceExists(VMContext* ctx, RValue surface) {
    RValue result = callBuiltin(ctx, &cache.surfaceExists, &surface, 1);
    bool exists = RValue_toBool(result);
    RValue_free(&result);
    return exists;
}

static GMLReal surfaceSize(VMContext* ctx, const Chapter3Builtin* func, RValue surface) {
    RValue result = callBuiltin(ctx, func, &surface, 1);
    GMLReal size = RValue_toReal(result);
    RValue_free(&result);
    return size;
}

static RValue sharedSurface(VMContext* ctx, RValue applicationSurface) {
    if (cache.sharedOwner != ctx) {
        cache.sharedOwner = ctx;
        cache.sharedSurface = -1;
    }
    RValue shared = RValue_makeReal((GMLReal)cache.sharedSurface);
    GMLReal width = surfaceSize(ctx, &cache.surfaceGetWidth, applicationSurface) * SCREEN_COPY_SCALE;
    GMLReal height = surfaceSize(ctx, &cache.surfaceGetHeight, applicationSurface) * SCREEN_COPY_SCALE;
    if (cache.sharedSurface >= 0 && surfaceExists(ctx, shared)) {
        if (surfaceSize(ctx, &cache.surfaceGetWidth, shared) == width &&
            surfaceSize(ctx, &cache.surfaceGetHeight, shared) == height)
            return shared;
        RValue result = callBuiltin(ctx, &cache.surfaceFree, &shared, 1);
        RValue_free(&result);
    }
    RValue sizeArgs[] = { RValue_makeReal(width), RValue_makeReal(height) };
    RValue created = callBuiltin(ctx, &cache.surfaceCreate, sizeArgs, 2);
    cache.sharedSurface = RValue_toInt32(created);
    RValue_free(&created);
    return RValue_makeReal((GMLReal)cache.sharedSurface);
}

static void drawScreenAfterimage(VMContext* ctx, Instance* self) {
    RValue applicationSurface = VMBuiltins_getVariable(ctx, self, BUILTIN_VAR_APPLICATION_SURFACE, "application_surface", -1);
    RValue shared = sharedSurface(ctx, applicationSurface);

    RValue result = callBuiltin(ctx, &cache.surfaceSetTarget, &shared, 1);
    RValue_free(&result);
    if (SCREEN_COPY_SCALE == R(1)) {
        RValue copyArgs[] = { applicationSurface, RValue_makeReal(0), RValue_makeReal(0) };
        result = callBuiltin(ctx, &cache.drawSurface, copyArgs, 3);
    } else {
        RValue copyArgs[] = {
            applicationSurface, RValue_makeReal(0), RValue_makeReal(0), RValue_makeReal(SCREEN_COPY_SCALE),
            RValue_makeReal(SCREEN_COPY_SCALE), RValue_makeReal(0), RValue_makeReal(16777215),
            RValue_makeReal(ctx->runner->renderer->drawAlpha)
        };
        result = callBuiltin(ctx, &cache.drawSurfaceExt, copyArgs, 8);
    }
    RValue_free(&result);
    result = callBuiltin(ctx, &cache.surfaceResetTarget, nullptr, 0);
    RValue_free(&result);

    GMLReal xscale = Chapter3_getReal(self, cache.xscale);
    GMLReal yscale = Chapter3_getReal(self, cache.yscale);
    RValue drawArgs[] = {
        shared,
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x") - (Chapter3_getReal(self, cache.anchorX) * xscale)),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y") - (Chapter3_getReal(self, cache.anchorY) * yscale)),
        RValue_makeReal(xscale / SCREEN_COPY_SCALE),
        RValue_makeReal(yscale / SCREEN_COPY_SCALE),
        RValue_makeReal(0),
        RValue_makeReal(16777215),
        Instance_getSelfVar(self, cache.alpha)
    };
    result = callBuiltin(ctx, &cache.drawSurfaceExt, drawArgs, 8);
    RValue_free(&result);
    RValue_free(&applicationSurface);
}

static bool screenAfterimageDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    if (RValue_toBool(Instance_getSelfVar(self, cache.drawEnd))) return true;
    drawScreenAfterimage(ctx, self);
    return true;
}

static void drawHeart(VMContext* ctx, Instance* self, Instance* heart) {
    (void)self;
    Chapter3_callVoidAs(ctx, heart, &cache.drawSelf, nullptr, 0);
}

static bool screenAfterimageDrawEnd(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    if (!RValue_toBool(Instance_getSelfVar(self, cache.drawEnd))) return true;
    drawScreenAfterimage(ctx, self);
    Chapter3_with(ctx, self, cache.heartObject, drawHeart);
    return true;
}

static bool screenAfterimageStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    Chapter3_setReal(self, cache.xscale, Chapter3_getReal(self, cache.xscale) + Chapter3_getReal(self, cache.xrate));
    Chapter3_setReal(self, cache.yscale, Chapter3_getReal(self, cache.yscale) + Chapter3_getReal(self, cache.yrate));

    GMLReal alpha = Chapter3_getReal(self, cache.alpha);
    GMLReal faderate = Chapter3_getReal(self, cache.faderate);
    if (Chapter3_compare(ctx, alpha, 0) < 0) {
        alpha += faderate;
        if (Chapter3_compare(ctx, alpha, 0) > 0) alpha = 0;
    } else {
        alpha -= faderate;
        if (Chapter3_compare(ctx, alpha, 0) < 0) alpha = 0;
    }
    Chapter3_setReal(self, cache.alpha, alpha);

    if (Chapter3_compare(ctx, alpha, 0) == 0) Runner_destroyInstance(ctx->runner, self, true);
    return true;
}

static bool screenAfterimageCleanUp(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    RValue mySurface = Instance_getSelfVar(self, cache.mySurface);
    if (surfaceExists(ctx, mySurface)) {
        RValue result = callBuiltin(ctx, &cache.surfaceFree, &mySurface, 1);
        RValue_free(&result);
    }

    if (cache.sharedOwner != ctx || cache.sharedSurface < 0) return true;
    RValue objectArg = RValue_makeReal((GMLReal)cache.screenObject);
    RValue count = callBuiltin(ctx, &cache.instanceNumber, &objectArg, 1);
    bool lastOne = RValue_toReal(count) <= 1;
    RValue_free(&count);
    if (!lastOne) return true;
    RValue shared = RValue_makeReal((GMLReal)cache.sharedSurface);
    if (surfaceExists(ctx, shared)) {
        RValue result = callBuiltin(ctx, &cache.surfaceFree, &shared, 1);
        RValue_free(&result);
    }
    cache.sharedSurface = -1;
    return true;
}

void NativeChapter3AfterimageScreen_register(VMContext* ctx) {
    cache.screenObject = Chapter3_findObject(ctx, "obj_afterimage_screen");
    cache.heartObject = Chapter3_findObject(ctx, "obj_heart");
    if (cache.screenObject < 0 || cache.heartObject < 0) return;
    Chapter3_bindBuiltin(ctx, &cache.surfaceExists, "surface_exists");
    Chapter3_bindBuiltin(ctx, &cache.surfaceCreate, "surface_create");
    Chapter3_bindBuiltin(ctx, &cache.surfaceFree, "surface_free");
    Chapter3_bindBuiltin(ctx, &cache.surfaceSetTarget, "surface_set_target");
    Chapter3_bindBuiltin(ctx, &cache.surfaceResetTarget, "surface_reset_target");
    Chapter3_bindBuiltin(ctx, &cache.surfaceGetWidth, "surface_get_width");
    Chapter3_bindBuiltin(ctx, &cache.surfaceGetHeight, "surface_get_height");
    Chapter3_bindBuiltin(ctx, &cache.drawSurface, "draw_surface");
    Chapter3_bindBuiltin(ctx, &cache.drawSurfaceExt, "draw_surface_ext");
    Chapter3_bindBuiltin(ctx, &cache.instanceNumber, "instance_number");
    Chapter3_bindBuiltin(ctx, &cache.drawSelf, "draw_self");
    if (cache.surfaceExists.func == nullptr || cache.surfaceCreate.func == nullptr || cache.surfaceFree.func == nullptr ||
        cache.surfaceSetTarget.func == nullptr || cache.surfaceResetTarget.func == nullptr ||
        cache.surfaceGetWidth.func == nullptr || cache.surfaceGetHeight.func == nullptr ||
        cache.drawSurface.func == nullptr || cache.drawSurfaceExt.func == nullptr || cache.instanceNumber.func == nullptr || cache.drawSelf.func == nullptr) return;

    cache.mySurface = NativeScript_getSelfVarID(ctx, "my_surface");
    cache.xscale = NativeScript_getSelfVarID(ctx, "xscale");
    cache.yscale = NativeScript_getSelfVarID(ctx, "yscale");
    cache.alpha = NativeScript_getSelfVarID(ctx, "alpha");
    cache.anchorX = NativeScript_getSelfVarID(ctx, "anchor_x");
    cache.anchorY = NativeScript_getSelfVarID(ctx, "anchor_y");
    cache.xrate = NativeScript_getSelfVarID(ctx, "xrate");
    cache.yrate = NativeScript_getSelfVarID(ctx, "yrate");
    cache.faderate = NativeScript_getSelfVarID(ctx, "faderate");
    cache.drawEnd = NativeScript_getSelfVarID(ctx, "draw_end");
    cache.sharedOwner = nullptr;
    cache.sharedSurface = -1;

    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_screen_Step_0", screenAfterimageStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_screen_Draw_0", screenAfterimageDraw);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_screen_Draw_73", screenAfterimageDrawEnd);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_screen_CleanUp_0", screenAfterimageCleanUp);
}
