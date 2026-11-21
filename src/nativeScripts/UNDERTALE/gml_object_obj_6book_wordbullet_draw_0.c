#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

static struct {
    int32_t counter, word, width, factor, type, shake;
} cache;
static NativeAssetBinding fontBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding setfontBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding widthBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding textBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding randomBinding = NATIVE_CALL_BINDING_INIT;

static bool bookWordbulletDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canResolveAsset(ctx, &fontBinding, "fnt_maintext") ||
        !NativeScript_canCall(ctx, &setfontBinding, "gml_Script_scr_setfont")) return false;

    GMLReal counter = RValue_toReal(Instance_getSelfVar(self, cache.counter)) + 1.0;
    Instance_setSelfVar(self, cache.counter, RValue_makeReal(counter));
    RValue font = RValue_makeReal(fontBinding.assetIndex);
    RValue result = NativeScript_call(ctx, &setfontBinding, "gml_Script_scr_setfont", &font, 1);
    RValue_free(&result);
    RValue word = Instance_getSelfVar(self, cache.word);
    GMLReal width = RValue_toReal(NativeScript_call(ctx, &widthBinding, "string_width", &word, 1));
    Instance_setSelfVar(self, cache.width, RValue_makeReal(width));
    GMLReal factor = 100.0 / width;
    Instance_setSelfVar(self, cache.factor, RValue_makeReal(factor));
    renderer->drawColor = self->imageBlend;
    RValue textArgs[6] = {
        RValue_makeReal(self->x), RValue_makeReal(self->y), word,
        RValue_makeReal(factor), RValue_makeReal(4.2), RValue_makeReal(0.0),
    };
    result = NativeScript_call(ctx, &textBinding, "draw_text_transformed", textArgs, 6);
    RValue_free(&result);
    GMLReal epsilon = ctx->runner->mathEpsilon;
    if (NativeScript_compareReals(self->hspeed, 0.0, epsilon) > 0 &&
        NativeScript_compareReals(self->x, 405.0, epsilon) > 0)
        Runner_destroyInstance(ctx->runner, self, true);
    if (NativeScript_compareReals(self->hspeed, 0.0, epsilon) < 0 &&
        NativeScript_compareReals(self->x, 120.0, epsilon) < 0)
        Runner_destroyInstance(ctx->runner, self, true);
    if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.type)), 2.0, epsilon) == 0) {
        GMLReal shake = RValue_toReal(Instance_getSelfVar(self, cache.shake)) + 0.2;
        Instance_setSelfVar(self, cache.shake, RValue_makeReal(shake));
        RValue arg = RValue_makeReal(shake);
        GMLReal x1 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        GMLReal x2 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(self->x + (x1 - x2)), -1);
        GMLReal y1 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        GMLReal y2 = RValue_toReal(NativeScript_call(ctx, &randomBinding, "random", &arg, 1));
        VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_Y, "y", RValue_makeReal(self->y + (y1 - y2)), -1);
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void Native6BookWordbulletDraw_register(VMContext* ctx) {
    cache.counter = NativeScript_getSelfVarID(ctx, "counter");
    cache.word = NativeScript_getSelfVarID(ctx, "word");
    cache.width = NativeScript_getSelfVarID(ctx, "width");
    cache.factor = NativeScript_getSelfVarID(ctx, "factor");
    cache.type = NativeScript_getSelfVarID(ctx, "type");
    cache.shake = NativeScript_getSelfVarID(ctx, "shake");
    fontBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    setfontBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    widthBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    textBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    randomBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_6book_wordbullet_Draw_0", bookWordbulletDraw);
}
