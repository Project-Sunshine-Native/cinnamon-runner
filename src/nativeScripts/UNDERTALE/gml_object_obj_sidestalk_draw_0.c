#include "nativeScripts/native_runtime.h"

#include "instance.h"
#include "renderer.h"
#include "runner.h"

static struct {
    int32_t frozen, siner, growth, ssx, ssy;
} cache;

static bool sidestalkDraw(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Renderer* renderer = ctx->runner->renderer;
    if (self == nullptr || renderer == nullptr) return false;

#ifdef ENABLE_VM_GML_PROFILER
    Profiler_enter(ctx->profiler, "sidestalk:vars");
#endif
    GMLReal frozen = RValue_toReal(Instance_getSelfVar(self, cache.frozen));
    GMLReal siner = RValue_toReal(Instance_getSelfVar(self, cache.siner));
    if (frozen == 0.0) {
        siner += 1.0;
        Instance_setSelfVar(self, cache.siner, RValue_makeReal(siner));
    }
#ifdef ENABLE_VM_GML_PROFILER
    Profiler_exit(ctx->profiler);
    Profiler_enter(ctx->profiler, "sidestalk:trig");
#endif
    GMLReal growth = 1.0 + GMLReal_cos(siner / 5.0) * 0.01;
    GMLReal ssx = GMLReal_sin(siner / 3.0) * 2.0 * self->imageXscale;
    GMLReal ssy = GMLReal_cos(siner / 3.0) * 2.0;
#ifdef ENABLE_VM_GML_PROFILER
    Profiler_exit(ctx->profiler);
    Profiler_enter(ctx->profiler, "sidestalk:state");
#endif
    Instance_setSelfVar(self, cache.growth, RValue_makeReal(growth));
    Instance_setSelfVar(self, cache.ssx, RValue_makeReal(ssx));
    Instance_setSelfVar(self, cache.ssy, RValue_makeReal(ssy));
    self->imageSpeed = frozen == 0.0 ? 3.0f : 1.0f;
#ifdef ENABLE_VM_GML_PROFILER
    Profiler_exit(ctx->profiler);
    Profiler_enter(ctx->profiler, "sidestalk:sprite");
#endif
    Renderer_drawSpriteExt(renderer, self->spriteIndex, (int32_t)self->imageIndex,
        (float)(self->x - ssx), (float)(self->y + ssy), self->imageXscale,
        (float)growth, 0.0f, self->imageBlend, 1.0f);
#ifdef ENABLE_VM_GML_PROFILER
    Profiler_exit(ctx->profiler);
#endif

    *outResult = DataWin_isVersionAtLeast(ctx->dataWin, 2, 3, 1, 0)
        ? RValue_makeUndefined() : RValue_makeReal(0.0);
    return true;
}

void NativeSidestalkDraw_register(VMContext* ctx) {
    cache.frozen = NativeScript_getSelfVarID(ctx, "frozen");
    cache.siner = NativeScript_getSelfVarID(ctx, "siner");
    cache.growth = NativeScript_getSelfVarID(ctx, "growth");
    cache.ssx = NativeScript_getSelfVarID(ctx, "ssx");
    cache.ssy = NativeScript_getSelfVarID(ctx, "ssy");
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_sidestalk_Draw_0", sidestalkDraw);
}
