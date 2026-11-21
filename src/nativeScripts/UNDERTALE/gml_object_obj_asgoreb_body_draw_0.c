#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static NativeVarBinding iBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding partBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding partxBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding partyBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding fakeanimBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding sinerBinding = NATIVE_VAR_BINDING_INIT;
static NativeVarBinding movingBinding = NATIVE_VAR_BINDING_INIT;

static bool asgorebBodyDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

    RValue part = NativeScript_getVariable(ctx, self, &partBinding, "part");
    RValue partx = NativeScript_getVariable(ctx, self, &partxBinding, "partx");
    RValue party = NativeScript_getVariable(ctx, self, &partyBinding, "party");
    GMLReal fakeanim = RValue_toReal(NativeScript_getVariable(ctx, self, &fakeanimBinding, "fakeanim"));
    int32_t subimage = (int32_t)fakeanim;
    if (subimage < 0) subimage = (int32_t)self->imageIndex;
    for (int32_t i = 0; i < 8; ++i) {
        RValue index = RValue_makeReal(i);
        NativeScript_setVariable(ctx, self, &iBinding, "i", index);
        int32_t sprite = RValue_toInt32(NativeScript_getIndex(part, index));
        GMLReal x = self->x + RValue_toReal(NativeScript_getIndex(partx, index));
        GMLReal y = self->y + RValue_toReal(NativeScript_getIndex(party, index));
        Renderer_drawSpriteExt(renderer, sprite, subimage, (float)x, (float)y,
            2.0f, 2.0f, 0.0f, 0xFFFFFFu, 1.0f);
    }
    NativeScript_setVariable(ctx, self, &iBinding, "i", RValue_makeReal(8.0));

    GMLReal siner = RValue_toReal(NativeScript_getVariable(ctx, self, &sinerBinding, "siner")) + 1.0;
    NativeScript_setVariable(ctx, self, &sinerBinding, "siner", RValue_makeReal(siner));
    NativeScript_setVariable(ctx, self, &fakeanimBinding, "fakeanim", RValue_makeReal(fakeanim + 0.1));
    if (RValue_toReal(NativeScript_getVariable(ctx, self, &movingBinding, "moving")) == 1.0) {
        GMLReal sine = GMLReal_sin(siner / 15.0);
        GMLReal cosine = GMLReal_cos(siner / 15.0);
        const int32_t indices[] = {7, 6, 5, 4, 3, 0};
        GMLReal offsets[] = {sine * 0.3, sine * 0.2, cosine * 0.1,
            cosine * 0.1, sine * 0.1, sine * 0.05};
        for (int i = 0; i < 6; ++i) {
            RValue index = RValue_makeReal(indices[i]);
            party = NativeScript_getVariable(ctx, self, &partyBinding, "party");
            GMLReal value = RValue_toReal(NativeScript_getIndex(party, index)) + offsets[i];
            NativeScript_setVariableIndex(ctx, self, &partyBinding, "party", index, RValue_makeReal(value));
        }
    }

    const int32_t sprites[] = {632, 640, 639, 638, 635, 634, 637, 636};
    for (int32_t i = 7; i >= 0; --i) {
        NativeScript_setVariableIndex(ctx, self, &partBinding, "part", RValue_makeReal(i),
            RValue_makeReal(sprites[i]));
    }
    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeAsgorebBodyDraw_register(VMContext* ctx) {
    iBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    partBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    partxBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    partyBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    fakeanimBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    sinerBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    movingBinding = (NativeVarBinding)NATIVE_VAR_BINDING_INIT;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_asgoreb_body_Draw_0", asgorebBodyDraw);
}
