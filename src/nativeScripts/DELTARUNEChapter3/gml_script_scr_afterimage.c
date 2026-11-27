#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

#include "chapter3_native_common.h"

static struct {
    int32_t afterimageObject, afterimageGrowObject, boardAfterimageObject;
    int32_t afterimageRef, fadeSpeed, target, fade, xrate, yrate, destroytime;
    int32_t init, remalpha, timer, length, type, flickerrate;
    Chapter3InstanceCreator instanceCreate;
    Chapter3Builtin eventInherited;
} cache;

static RValue createAfterimageFrom(VMContext* ctx, RValue object, bool fast, GMLReal depthOffset) {
    Instance* self = ctx->currentCallFrame->self;
    RValue id = Chapter3_instanceCreate(ctx, &cache.instanceCreate,
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y")), object);
    Instance_setSelfVar(self, cache.afterimageRef, id);
    Instance* afterimage = Chapter3_resolveInstance(ctx->runner, id);
    if (afterimage == nullptr) return id;
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_SPRITE_INDEX, "sprite_index",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_INDEX, "image_index",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_BLEND, "image_blend",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_BLEND, "image_blend"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_SPEED, "image_speed", 0);
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_DEPTH, "depth",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DEPTH, "depth") + depthOffset);
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle"));
    if (fast) Chapter3_setReal(afterimage, cache.fadeSpeed, R(0.08));
    return id;
}

static RValue createAfterimage(VMContext* ctx, int32_t objectIndex, bool fast) {
    return createAfterimageFrom(ctx, RValue_makeReal((GMLReal)objectIndex), fast, 0);
}

static bool scrAfterimage(VMContext* ctx, RValue* outResult) {
    *outResult = createAfterimage(ctx, cache.afterimageObject, false);
    return true;
}

static bool scrAfterimageFast(VMContext* ctx, RValue* outResult) {
    *outResult = createAfterimage(ctx, cache.afterimageObject, true);
    return true;
}

static bool scrAfterimageGrow(VMContext* ctx, RValue* outResult) {
    *outResult = createAfterimage(ctx, cache.afterimageGrowObject, false);
    return true;
}

static bool scrCustomAfterimage(VMContext* ctx, RValue* outResult) {
    *outResult = createAfterimageFrom(ctx, NativeScript_getArgument(ctx, 0), false, 1);
    return true;
}

static bool scrAfterimageGrowAttached(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    int32_t argCount = ctx->currentCallFrame->scriptArgsCount;
    *outResult = RValue_makeUndefined();
    RValue id = createAfterimage(ctx, cache.afterimageGrowObject, false);
    Instance* afterimage = Chapter3_resolveInstance(ctx->runner, id);
    if (afterimage == nullptr) return true;
    Instance_setSelfVar(afterimage, cache.target, NativeScript_getArgument(ctx, 0));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_BLEND, "image_blend", RValue_toReal(NativeScript_getArgument(ctx, 1)));
    if (argCount > 2 && Chapter3_compare(ctx, RValue_toReal(NativeScript_getArgument(ctx, 2)), 1) == 0)
        Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_DEPTH, "depth", Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DEPTH, "depth") - 1);
    return true;
}

static bool afterimageStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha") - Chapter3_getReal(self, cache.fadeSpeed));
    if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"), 0) < 0)
        Runner_destroyInstance(ctx->runner, self, true);
    return true;
}

static bool afterimageGrowStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    *outResult = RValue_makeUndefined();

    RValue target = Instance_getSelfVar(self, cache.target);
    bool targetSet = target.type == RVALUE_UNDEFINED ||
        Chapter3_compare(ctx, RValue_toReal(target), -4) != 0;
    if (targetSet && Chapter3_instanceExists(ctx, target)) {
        Instance* followed = Chapter3_resolveInstance(runner, target);
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_X, "x", Chapter3_getBuiltin(ctx, followed, BUILTIN_VAR_X, "x"));
        Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_Y, "y", Chapter3_getBuiltin(ctx, followed, BUILTIN_VAR_Y, "y"));
    }

    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha") - Chapter3_getReal(self, cache.fade));
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale") + Chapter3_getReal(self, cache.xrate));
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale") + Chapter3_getReal(self, cache.yrate));

    if (Chapter3_compare(ctx, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"), 0) < 0)
        Runner_destroyInstance(runner, self, true);

    RValue destroytime = Instance_getSelfVar(self, cache.destroytime);
    if (Chapter3_compare(ctx, RValue_toReal(destroytime), -1) > 0) {
        RValue decremented = destroytime.type == RVALUE_INT32
            ? RValue_makeInt32(destroytime.int32 - 1)
            : RValue_makeReal(RValue_toReal(destroytime) - 1);
        Instance_setSelfVar(self, cache.destroytime, decremented);
    }
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.destroytime), 0) == 0)
        Runner_destroyInstance(runner, self, true);
    return true;
}

static bool scrBoardAfterimage(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    int32_t argCount = ctx->currentCallFrame->scriptArgsCount;
    RValue id = Chapter3_instanceCreate(ctx, &cache.instanceCreate,
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_X, "x")),
        RValue_makeReal(Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_Y, "y")),
        RValue_makeReal((GMLReal)cache.boardAfterimageObject));
    *outResult = id;
    Instance* afterimage = Chapter3_resolveInstance(ctx->runner, id);
    if (afterimage == nullptr) return true;
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_DEPTH, "depth",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_DEPTH, "depth"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_SPRITE_INDEX, "sprite_index",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_SPRITE_INDEX, "sprite_index"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_INDEX, "image_index",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_INDEX, "image_index"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_SPEED, "image_speed",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_SPEED, "image_speed"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale"));
    Chapter3_setBuiltin(ctx, afterimage, BUILTIN_VAR_IMAGE_ANGLE, "image_angle",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ANGLE, "image_angle"));
    if (argCount >= 1) Instance_setSelfVar(afterimage, cache.type, NativeScript_getArgument(ctx, 0));
    if (argCount >= 2) Instance_setSelfVar(afterimage, cache.length, NativeScript_getArgument(ctx, 1));
    if (argCount >= 3) Instance_setSelfVar(afterimage, cache.flickerrate, NativeScript_getArgument(ctx, 2));
    return true;
}

static bool boardAfterimageStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.init), 0) == 0) {
        Chapter3_setReal(self, cache.remalpha, Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha"));
        Instance_setSelfVar(self, cache.init, RValue_makeReal(1));
    }
    Instance_setSelfVar(self, cache.timer, Chapter3_increment(Instance_getSelfVar(self, cache.timer)));
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.timer), Chapter3_getReal(self, cache.length)) >= 0)
        Runner_destroyInstance(ctx->runner, self, true);
    if (Chapter3_compare(ctx, Chapter3_getReal(self, cache.type), 0) == 0) {
        GMLReal phase = GMLReal_fmod(Chapter3_getReal(self, cache.timer), Chapter3_getReal(self, cache.flickerrate));
        if (Chapter3_compare(ctx, phase, 0) == 0)
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", Chapter3_getReal(self, cache.remalpha));
        else
            Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha", 0);
    }
    return true;
}

static bool growingAfterimageStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    *outResult = RValue_makeUndefined();
    Chapter3_callVoid(ctx, &cache.eventInherited, nullptr, 0);
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_XSCALE, "image_xscale") + R(0.05));
    Chapter3_setBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale",
        Chapter3_getBuiltin(ctx, self, BUILTIN_VAR_IMAGE_YSCALE, "image_yscale") + R(0.05));
    return true;
}

void NativeChapter3Afterimage_register(VMContext* ctx) {
    cache.afterimageObject = Chapter3_findObject(ctx, "obj_afterimage");
    cache.afterimageGrowObject = Chapter3_findObject(ctx, "obj_afterimage_grow");
    cache.boardAfterimageObject = Chapter3_findObject(ctx, "obj_board_afterimage");
    if (cache.afterimageObject < 0 || cache.afterimageGrowObject < 0 || cache.boardAfterimageObject < 0) return;
    if (!Chapter3_bindInstanceCreate(ctx, &cache.instanceCreate) || !Chapter3_bindBuiltin(ctx, &cache.eventInherited, "event_inherited")) return;
    cache.afterimageRef = NativeScript_getSelfVarID(ctx, "afterimage");
    cache.fadeSpeed = NativeScript_getSelfVarID(ctx, "fadeSpeed");
    cache.target = NativeScript_getSelfVarID(ctx, "target");
    cache.fade = NativeScript_getSelfVarID(ctx, "fade");
    cache.xrate = NativeScript_getSelfVarID(ctx, "xrate");
    cache.yrate = NativeScript_getSelfVarID(ctx, "yrate");
    cache.destroytime = NativeScript_getSelfVarID(ctx, "destroytime");
    cache.init = NativeScript_getSelfVarID(ctx, "init");
    cache.remalpha = NativeScript_getSelfVarID(ctx, "remalpha");
    cache.timer = NativeScript_getSelfVarID(ctx, "timer");
    cache.length = NativeScript_getSelfVarID(ctx, "length");
    cache.type = NativeScript_getSelfVarID(ctx, "type");
    cache.flickerrate = NativeScript_getSelfVarID(ctx, "flickerrate");

    (void)VM_registerNativeScript(ctx, "gml_Script_scr_afterimage", scrAfterimage);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_afterimagefast", scrAfterimageFast);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_afterimage_grow", scrAfterimageGrow);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_custom_afterimage", scrCustomAfterimage);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_afterimage_grow_attached", scrAfterimageGrowAttached);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_Step_0", afterimageStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_grow_Step_0", afterimageGrowStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_afterimage_glow_Step_0", afterimageStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_tenna_afterimage_Step_0", afterimageStep);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_growing_afterimage_Step_0", growingAfterimageStep);
    (void)VM_registerNativeScript(ctx, "gml_Script_scr_board_afterimage", scrBoardAfterimage);
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_board_afterimage_Step_0", boardAfterimageStep);
}
