#include "nativeScripts/native_runtime.h"

#include "gml_array.h"
#include "instance.h"
#include "runner.h"

#if defined(__clang__) || defined(_MSC_VER)
#pragma float_control(precise, on)
#elif defined(__GNUC__)
#pragma GCC optimize ("no-fast-math", "fp-contract=off")
#endif

static struct {
    int32_t myself, attacked, hangcon, shootcon, shootmode, shootwait, shoottimer;
    int32_t shootcount, m, werewerewire, animAdd, threshold, isphase;
    int32_t monster, monsterStatus, myfight, mnfight, turntimer;
} cache;

static bool werewireStep(VMContext* ctx, RValue* outResult) {
    Instance* self = ctx->currentCallFrame->self;
    Instance* global = ctx->globalScopeInstance;
    GMLReal epsilon = ctx->runner->mathEpsilon;
    RValue monsters = Instance_getSelfVar(global, cache.monster);
    RValue statuses = Instance_getSelfVar(global, cache.monsterStatus);
    if (monsters.type != RVALUE_ARRAY || statuses.type != RVALUE_ARRAY) return false;

    int32_t index = RValue_toInt32(Instance_getSelfVar(self, cache.myself));
    if (NativeScript_compareReals(RValue_toReal(GMLArray_get(monsters.array, index)), 1, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(GMLArray_get(statuses.array, index)), 0, epsilon) == 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(global, cache.mnfight)), 2, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(global, cache.myfight)), 3, epsilon) == 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.attacked)), 1, epsilon) != 0 ||
        NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.hangcon)), 0, epsilon) != 0)
        return false;

    GMLReal state = RValue_toReal(Instance_getSelfVar(self, cache.shootcon));
    GMLReal m = RValue_toReal(Instance_getSelfVar(self, cache.m));
    if (NativeScript_compareReals(state, 0, epsilon) == 0) {
        if (NativeScript_compareReals(RValue_toReal(Instance_getSelfVar(self, cache.shootmode)), 1, epsilon) != 0)
            return false;
        GMLReal turn = RValue_toReal(Instance_getSelfVar(global, cache.turntimer));
        GMLReal wait = RValue_toReal(Instance_getSelfVar(self, cache.shootwait));
        if (NativeScript_compareReals(turn, 10, epsilon) >= 0) wait--;
        if (NativeScript_compareReals(wait, 0, epsilon) <= 0 &&
            NativeScript_compareReals(turn, 40, epsilon) >= 0) return false;

        Instance_setSelfVar(self, cache.shootwait, RValue_makeReal(wait));
        if (NativeScript_compareReals(turn, 40, epsilon) < 0)
            Instance_setSelfVar(self, cache.shootmode, RValue_makeReal(0));
    } else if (NativeScript_compareReals(state, 2, epsilon) == 0) {
        GMLReal werewerewire = RValue_toReal(Instance_getSelfVar(self, cache.werewerewire));
        GMLReal amount = NativeScript_compareReals(werewerewire, 1, epsilon) == 0 ? 0.5 : 0.25;
        float frame = self->imageIndex;
        if (NativeScript_compareReals(frame, 3, epsilon) < 0)
            frame = (float)((GMLReal)frame + amount * m);
        if (NativeScript_compareReals(frame, 3, epsilon) >= 0) return false;

        Instance_setSelfVar(self, cache.animAdd, RValue_makeReal(amount));
        self->imageIndex = frame;
    } else if (NativeScript_compareReals(state, 3, epsilon) == 0) {
        GMLReal timer = RValue_toReal(Instance_getSelfVar(self, cache.shoottimer)) - 1;
        GMLReal count = RValue_toReal(Instance_getSelfVar(self, cache.shootcount));
        if (NativeScript_compareReals(timer, 0, epsilon) <= 0 ||
            NativeScript_compareReals(count, 3, epsilon) >= 0) return false;

        Instance_setSelfVar(self, cache.shoottimer, RValue_makeReal(timer));
    } else if (NativeScript_compareReals(state, 4, epsilon) == 0) {
        GMLReal timer = RValue_toReal(Instance_getSelfVar(self, cache.shoottimer)) + 1;
        GMLReal werewerewire = RValue_toReal(Instance_getSelfVar(self, cache.werewerewire));
        GMLReal threshold = NativeScript_compareReals(werewerewire, 1, epsilon) == 0 ? 2 : 10;
        float frame = self->imageIndex;
        if (NativeScript_compareReals(timer, threshold / m, epsilon) >= 0) {
            frame = (float)((GMLReal)frame + (GMLReal)0.25 * m);
            if (NativeScript_compareReals(frame, 9, epsilon) >= 0) return false;
        }

        Instance_setSelfVar(self, cache.shoottimer, RValue_makeReal(timer));
        Instance_setSelfVar(self, cache.threshold, RValue_makeReal(threshold));
        self->imageIndex = frame;
    } else {
        return false;
    }

    Instance_setSelfVar(self, cache.isphase, RValue_makeReal(1));
    *outResult = RValue_makeUndefined();
    return true;
}

void NativeChapter2WerewireStep_register(VMContext* ctx) {
    if (!VM_registerNativeScript(ctx, "gml_Object_obj_werewire_enemy_Step_0", werewireStep)) return;

    cache.myself = NativeScript_getSelfVarID(ctx, "myself");
    cache.attacked = NativeScript_getSelfVarID(ctx, "attacked");
    cache.hangcon = NativeScript_getSelfVarID(ctx, "hangcon");
    cache.shootcon = NativeScript_getSelfVarID(ctx, "shootcon");
    cache.shootmode = NativeScript_getSelfVarID(ctx, "shootmode");
    cache.shootwait = NativeScript_getSelfVarID(ctx, "shootwait");
    cache.shoottimer = NativeScript_getSelfVarID(ctx, "shoottimer");
    cache.shootcount = NativeScript_getSelfVarID(ctx, "shootcount");
    cache.m = NativeScript_getSelfVarID(ctx, "m");
    cache.werewerewire = NativeScript_getSelfVarID(ctx, "werewerewire");
    cache.animAdd = NativeScript_getSelfVarID(ctx, "anim_add");
    cache.threshold = NativeScript_getSelfVarID(ctx, "threshold");
    cache.isphase = NativeScript_getSelfVarID(ctx, "__isphase");
    cache.monster = NativeScript_getGlobalVarID(ctx, "monster");
    cache.monsterStatus = NativeScript_getGlobalVarID(ctx, "monsterstatus");
    cache.myfight = NativeScript_getGlobalVarID(ctx, "myfight");
    cache.mnfight = NativeScript_getGlobalVarID(ctx, "mnfight");
    cache.turntimer = NativeScript_getGlobalVarID(ctx, "turntimer");
}
