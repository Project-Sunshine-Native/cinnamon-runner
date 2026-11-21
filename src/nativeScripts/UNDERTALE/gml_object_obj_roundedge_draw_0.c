#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"
#include "vm_builtins.h"

#include "stb_ds.h"

#include <math.h>

#include "3ds/3ds_renderer.h"

static NativeVarBinding edgeBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding partBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding w0Binding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding h0Binding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding wpBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding hpBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding lsideBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding rsideBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding sideBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding curxBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding sizeBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding colBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding colorBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding imageBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding rotspeedBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding ftimerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding falphaBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding alignconBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding specialnormalBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding turntimerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding mnfightBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding msgBinding = NATIVE_VAR_BINDING_INIT;
static NativeAssetBinding asrielBodyBinding = NATIVE_ASSET_BINDING_INIT;
static NativeCallBinding hsvBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding rectangleBinding = NATIVE_CALL_BINDING_INIT;
static NativeCallBinding gettextBinding = NATIVE_CALL_BINDING_INIT;

static bool roundedgeDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Runner* runner = ctx->runner;
    Renderer* renderer = runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;
    if (!NativeScript_canCall(ctx, &hsvBinding, "make_color_hsv") ||
        !NativeScript_canCall(ctx, &rectangleBinding, "gml_Script_ossafe_fill_rectangle") ||
        !NativeScript_canCall(ctx, &gettextBinding, "gml_Script_scr_gettext") ||
        !NativeScript_canResolveAsset(ctx, &asrielBodyBinding, "obj_asriel_body")) return false;

    GMLReal epsilon = runner->mathEpsilon;
    if (NativeScript_compareReals(RValue_toReal(NativeScript_getVariable(ctx, self,
            &edgeBinding, "edge")), 0.0, epsilon) == 0)
        NativeScript_setVariable(ctx, self, &edgeBinding, "edge", RValue_makeReal(0.1));

    GMLReal part = RValue_toReal(NativeScript_getVariable(ctx, self, &partBinding, "part"));
    if (NativeScript_compareReals(part, 0.0, epsilon) == 0) {
        part = 1.0;
        NativeScript_setVariable(ctx, self, &partBinding, "part", RValue_makeReal(part));
    }

    GMLReal wp = RValue_toReal(NativeScript_getVariable(ctx, self, &w0Binding, "w0")) / part;
    NativeScript_setVariable(ctx, self, &wpBinding, "wp", RValue_makeReal(wp));
    NativeScript_setVariable(ctx, self, &hpBinding, "hp", RValue_makeReal(
        RValue_toReal(NativeScript_getVariable(ctx, self, &h0Binding, "h0")) / part));

    GMLReal lside = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_VIEW_XVIEW, "view_xview", 0));
    GMLReal viewWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_VIEW_WVIEW, "view_wview", 0));
    NativeScript_setVariable(ctx, self, &lsideBinding, "lside", RValue_makeReal(lside));
    NativeScript_setVariable(ctx, self, &rsideBinding, "rside", RValue_makeReal(lside + viewWidth));
    NativeScript_setVariable(ctx, self, &sideBinding, "side", RValue_makeReal(0.0));
    NativeScript_setVariable(ctx, self, &curxBinding, "curx", RValue_makeReal(0.0));
    NativeScript_setVariable(ctx, self, &sizeBinding, "size", RValue_makeReal(1.0));

    GMLReal col = RValue_toReal(NativeScript_getVariable(ctx, self, &colBinding, "col")) + 1.0;
    RValue hsvArgs[3] = {RValue_makeReal(col), RValue_makeReal(233.0), RValue_makeReal(200.0)};
    RValue colorValue = NativeScript_call(ctx, &hsvBinding, "make_color_hsv", hsvArgs, 3);
    uint32_t color = RValue_toColour(colorValue);
    NativeScript_setVariable(ctx, self, &colorBinding, "color", colorValue);
    RValue_free(&colorValue);
    NativeScript_setVariable(ctx, self, &colBinding, "col",
        RValue_makeReal(col > 254.0 ? 0.0 : col));

    int32_t sprite = RValue_toInt32(NativeScript_getVariable(ctx, self, &imageBinding, "image"));
    int32_t subimg = (int32_t)self->imageIndex;
    GMLReal roomWidth = RValue_toReal(VMBuiltins_getVariable(ctx, self,
        BUILTIN_VAR_ROOM_WIDTH, "room_width", -1));
    bool drawn = false;
    if (part == 40.0) {
        int32_t tpagIndex = Renderer_resolveTPAGIndex(renderer->dataWin, sprite, subimg);
        drawn = C3D_drawRoundEdge(renderer, tpagIndex, self->x, wp, roomWidth,
            color, self->imageAlpha);
    }
    if (!drawn) {
        for (GMLReal i = 0.0; i < part; i += 1.0) {
            GMLReal offset = wp * i;
            GMLReal y = -offset / 2.0;
            Renderer_drawSpritePartExt(renderer, sprite, subimg,
                (float)(offset + self->x), 0.0f, (float)offset, 999.0f,
                (float)(roomWidth / 2.0 + offset - 6.0), (float)y,
                (float)i, (float)i, 0.0f, 0.0f, 0.0f, color, self->imageAlpha);
            Renderer_drawSpritePartExt(renderer, sprite, subimg,
                (float)(offset + self->x), 0.0f, (float)offset, 999.0f,
                (float)(roomWidth / 2.0 - offset + 6.0), (float)y,
                (float)-i, (float)i, 0.0f, 0.0f, 0.0f, color, self->imageAlpha);
        }
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal((GMLReal)ceil((double)(part > 0.0 ? part : 0.0))));

    GMLReal x = self->x + RValue_toReal(NativeScript_getVariable(ctx, self,
        &rotspeedBinding, "rotspeed"));
    if (x > 800.0) x -= 800.0;
    if (x < 0.0) x += 800.0;
    VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_X, "x", RValue_makeReal(x), -1);

    GMLReal ftimer = RValue_toReal(NativeScript_getVariable(ctx, self,
        &ftimerBinding, "ftimer")) + 1.0;
    NativeScript_setVariable(ctx, self, &ftimerBinding, "ftimer", RValue_makeReal(ftimer));
    if ((ftimer > 630.0 && ftimer < 671.0) || (ftimer >= 671.0 && ftimer < 685.0)) {
        GMLReal falpha = RValue_toReal(NativeScript_getVariable(ctx, self,
            &falphaBinding, "falpha"));
        if (ftimer < 671.0) {
            falpha += 0.025;
        } else {
            VMBuiltins_setVariable(ctx, self, BUILTIN_VAR_IMAGE_ALPHA, "image_alpha",
                RValue_makeReal(0.5), -1);
            falpha -= 0.1;
        }
        NativeScript_setVariable(ctx, self, &falphaBinding, "falpha", RValue_makeReal(falpha));
        renderer->drawAlpha = (float)falpha;
        renderer->drawColor = 0xFFFFFFu;
        RValue args[4] = {RValue_makeReal(-10.0), RValue_makeReal(-10.0),
            RValue_makeReal(999.0), RValue_makeReal(999.0)};
        RValue result = NativeScript_call(ctx, &rectangleBinding,
            "gml_Script_ossafe_fill_rectangle", args, 4);
        RValue_free(&result);
    }

    if (NativeScript_compareReals(ftimer, 671.0, epsilon) == 0) {
        int32_t base = Runner_pushInstancesOfObject(runner, asrielBodyBinding.assetIndex);
        int32_t count = (int32_t)arrlen(runner->instanceSnapshots);
        for (int32_t i = base; i < count; ++i) {
            Instance* body = runner->instanceSnapshots[i];
            if (!body->active) continue;
            NativeScript_setVariable(ctx, body, &alignconBinding, "aligncon", RValue_makeReal(4.0));
            NativeScript_setVariable(ctx, body, &specialnormalBinding, "specialnormal", RValue_makeReal(0.0));
        }
        Runner_popInstanceSnapshot(runner, base);
        NativeScript_setVariable(ctx, ctx->globalScopeInstance, &turntimerBinding,
            "turntimer", RValue_makeReal(-2.0));
        if (NativeScript_compareReals(RValue_toReal(NativeScript_getVariable(ctx,
                ctx->globalScopeInstance, &mnfightBinding, "mnfight")), 2.0, epsilon) == 0) {
            RValue key = RValue_makeString("obj_roundedge_135");
            RValue text = NativeScript_call(ctx, &gettextBinding,
                "gml_Script_scr_gettext", &key, 1);
            NativeScript_setVariableIndex(ctx, ctx->globalScopeInstance, &msgBinding,
                "msg", RValue_makeReal(0.0), text);
            RValue_free(&text);
        }
    }

    renderer->drawAlpha = 1.0f;
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeRoundedgeDraw_register(VMContext* ctx) {
    edgeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    partBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    w0Binding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    h0Binding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    wpBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    hpBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    lsideBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rsideBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    sideBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    curxBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    sizeBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    colBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    colorBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    imageBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    rotspeedBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    ftimerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    falphaBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    alignconBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    specialnormalBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    turntimerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    mnfightBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    msgBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    asrielBodyBinding = (NativeAssetBinding)NATIVE_ASSET_BINDING_INIT;
    hsvBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    rectangleBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    gettextBinding = (NativeCallBinding)NATIVE_CALL_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_roundedge_Draw_0", roundedgeDraw);
}
