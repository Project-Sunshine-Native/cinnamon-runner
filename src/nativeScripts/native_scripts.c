#include "native_scripts.h"
#include "UNDERTALE/generated_native_scripts.h"
#include "vm.h"

#include <string.h>

void NativeSnowfloorDraw_register(VMContext* ctx);
void NativePiperSteamDraw_register(VMContext* ctx);
void NativeSteamplume2Create_register(VMContext* ctx);
void NativeSteamplume2Step_register(VMContext* ctx);
void NativeColorcycle_register(VMContext* ctx);
void NativeOssafeFillRectangle_register(VMContext* ctx);
void NativeFinalbarrierDraw_register(VMContext* ctx);
void NativeOrangeparticleStep_register(VMContext* ctx);
void NativeSinefireAsghelixStep_register(VMContext* ctx);
void NativeHelixFiregenAlarm_register(VMContext* ctx);
void NativeSinefireAsghelixCreate_register(VMContext* ctx);
void NativeBaseWriterBc16_register(VMContext* ctx);
void NativeAsgorebBodyDraw_register(VMContext* ctx);
void NativeAsgorespearDraw_register(VMContext* ctx);
void NativeAsgorebulparentStep_register(VMContext* ctx);
void NativeOrangeparticlegenStep_register(VMContext* ctx);
void NativePurplegradienterDraw_register(VMContext* ctx);
void NativeSidedFireStep_register(VMContext* ctx);
void NativeSidedFireDraw_register(VMContext* ctx);
void NativeSidestalkDraw_register(VMContext* ctx);
void NativeVinesFloweyDraw_register(VMContext* ctx);
void NativeBgpipeDraw_register(VMContext* ctx);
void NativeFleshfaceDraw_register(VMContext* ctx);
void NativeFloweyarmDraw_register(VMContext* ctx);
void NativeFloweypipetestDraw_register(VMContext* ctx);
void NativeFloweypipetest2Draw_register(VMContext* ctx);
void NativeFloweypipetest3Draw_register(VMContext* ctx);
void NativeEyeshotStep_register(VMContext* ctx);
void NativeEyeshotCreate_register(VMContext* ctx);
void NativeSpinbulletHugeStep_register(VMContext* ctx);
void NativeFloweynukeStep_register(VMContext* ctx);
void NativeHandforkStep_register(VMContext* ctx);
void NativeFloweyFlameStep_register(VMContext* ctx);
void NativeFloweyFlameCreate_register(VMContext* ctx);
void NativeFloweyFlameCollision_register(VMContext* ctx);
void NativeFloweyBulletDamage_register(VMContext* ctx);
void NativeFloweyFlamethrowerAlarm_register(VMContext* ctx);
void NativeMouthballStep_register(VMContext* ctx);
void NativeMouthballTrailStep_register(VMContext* ctx);
void NativeGigavineStep_register(VMContext* ctx);
void NativeGigavinePreviewDraw_register(VMContext* ctx);
void NativeSpinbulletGeneratorStep_register(VMContext* ctx);
void Native6KnifeStep_register(VMContext* ctx);
void Native6GloveBossStep_register(VMContext* ctx);
void Native6GlovePartStep_register(VMContext* ctx);
void Native6ShoePartStep_register(VMContext* ctx);
void Native6ShoeMusicnoteStep_register(VMContext* ctx);
void Native6BookWordbulletDraw_register(VMContext* ctx);
void Native6PanFireStep_register(VMContext* ctx);
void Native6GunStep_register(VMContext* ctx);
void Native6HealStep_register(VMContext* ctx);
void NativeFloweyLefteyeDraw_register(VMContext* ctx);
void NativeFloweyFlipeyeDraw_register(VMContext* ctx);
void NativeFloweyMouthDraw_register(VMContext* ctx);
void NativeFloweyTvDraw_register(VMContext* ctx);
void NativeConveyorParentDraw_register(VMContext* ctx);
void NativeTrueLavawaverDraw_register(VMContext* ctx);
void NativeTrueAntiwaverDraw_register(VMContext* ctx);
void NativeCogsmallDraw_register(VMContext* ctx);
void NativeHotlandBottomedgeDraw_register(VMContext* ctx);
void NativeBouncersteamStep_register(VMContext* ctx);
void NativeHotlandRededgeDraw_register(VMContext* ctx);
void NativeBrightwaterfallDraw_register(VMContext* ctx);
void NativeGlowparticle1Step_register(VMContext* ctx);
void NativeRoundedgeDraw_register(VMContext* ctx);
void NativePurplegrassDraw_register(VMContext* ctx);
void NativeBattlecontrollerDraw_register(VMContext* ctx);
void NativeWriterDraw_register(VMContext* ctx);
void NativeChapter2WriterDraw_register(VMContext* ctx);
void NativeChapter2DwTransitionLineStep_register(VMContext* ctx);
void NativeChapter2DwTransitionLineDraw_register(VMContext* ctx);
void NativeChapter2DwTransitionParticleStep_register(VMContext* ctx);
void NativeCharbox_register(VMContext* ctx);
void NativeBattlebackDraw_register(VMContext* ctx);
void NativeSelectionMatrix_register(VMContext* ctx);
void NativeMaincharaEndStep_register(VMContext* ctx);
void NativeTimeBeginStep_register(VMContext* ctx);
void NativeRegularBulletStep_register(VMContext* ctx);
void NativeHeartStep_register(VMContext* ctx);
void NativeDarkcontrollerDraw_register(VMContext* ctx);
void NativeCaterpillarEndStep_register(VMContext* ctx);
void NativeDeviceChoiceDraw_register(VMContext* ctx);
void NativeDarkPonmanDraw_register(VMContext* ctx);
void NativeDarkPonmanStep_register(VMContext* ctx);
void NativeLightfairyDraw_register(VMContext* ctx);
void NativeLightfairyStep_register(VMContext* ctx);
void NativeScissordancerStep_register(VMContext* ctx);

static void registerUndertale(VMContext* ctx) {
    GeneratedNativeScripts_registerAll(ctx);
    NativeBaseWriterBc16_register(ctx);
    NativeSnowfloorDraw_register(ctx);
    NativePiperSteamDraw_register(ctx);
    NativeSteamplume2Create_register(ctx);
    NativeSteamplume2Step_register(ctx);
    NativeColorcycle_register(ctx);
    NativeOssafeFillRectangle_register(ctx);
    NativeFinalbarrierDraw_register(ctx);
    NativeOrangeparticleStep_register(ctx);
    NativeSinefireAsghelixStep_register(ctx);
    NativeHelixFiregenAlarm_register(ctx);
    NativeSinefireAsghelixCreate_register(ctx);
    NativeAsgorebBodyDraw_register(ctx);
    NativeAsgorespearDraw_register(ctx);
    NativeAsgorebulparentStep_register(ctx);
    NativeOrangeparticlegenStep_register(ctx);
    NativePurplegradienterDraw_register(ctx);
    NativeSidedFireStep_register(ctx);
    NativeSidedFireDraw_register(ctx);
    NativeSidestalkDraw_register(ctx);
    NativeVinesFloweyDraw_register(ctx);
    NativeBgpipeDraw_register(ctx);
    NativeFleshfaceDraw_register(ctx);
    NativeFloweyarmDraw_register(ctx);
    NativeFloweypipetestDraw_register(ctx);
    NativeFloweypipetest2Draw_register(ctx);
    NativeFloweypipetest3Draw_register(ctx);
    NativeEyeshotStep_register(ctx);
    NativeEyeshotCreate_register(ctx);
    NativeSpinbulletHugeStep_register(ctx);
    NativeFloweynukeStep_register(ctx);
    NativeHandforkStep_register(ctx);
    NativeFloweyFlameStep_register(ctx);
    NativeFloweyFlameCreate_register(ctx);
    NativeFloweyFlameCollision_register(ctx);
    NativeFloweyBulletDamage_register(ctx);
    NativeFloweyFlamethrowerAlarm_register(ctx);
    NativeMouthballStep_register(ctx);
    NativeMouthballTrailStep_register(ctx);
    NativeGigavineStep_register(ctx);
    NativeGigavinePreviewDraw_register(ctx);
    NativeSpinbulletGeneratorStep_register(ctx);
    Native6KnifeStep_register(ctx);
    Native6GloveBossStep_register(ctx);
    Native6GlovePartStep_register(ctx);
    Native6ShoePartStep_register(ctx);
    Native6ShoeMusicnoteStep_register(ctx);
    Native6BookWordbulletDraw_register(ctx);
    Native6PanFireStep_register(ctx);
    Native6GunStep_register(ctx);
    Native6HealStep_register(ctx);
    NativeFloweyLefteyeDraw_register(ctx);
    NativeFloweyFlipeyeDraw_register(ctx);
    NativeFloweyMouthDraw_register(ctx);
    NativeFloweyTvDraw_register(ctx);
    NativeConveyorParentDraw_register(ctx);
    NativeTrueLavawaverDraw_register(ctx);
    NativeTrueAntiwaverDraw_register(ctx);
    NativeCogsmallDraw_register(ctx);
    NativeHotlandBottomedgeDraw_register(ctx);
    NativeBouncersteamStep_register(ctx);
    NativeHotlandRededgeDraw_register(ctx);
    NativeBrightwaterfallDraw_register(ctx);
    NativeGlowparticle1Step_register(ctx);
    NativeRoundedgeDraw_register(ctx);
}

static void registerDeltaruneChapter1(VMContext* ctx) {
    NativePurplegrassDraw_register(ctx);
    NativeBattlecontrollerDraw_register(ctx);
    NativeWriterDraw_register(ctx);
    NativeCharbox_register(ctx);
    NativeBattlebackDraw_register(ctx);
    NativeSelectionMatrix_register(ctx);
    NativeMaincharaEndStep_register(ctx);
    NativeTimeBeginStep_register(ctx);
    NativeRegularBulletStep_register(ctx);
    NativeHeartStep_register(ctx);
    NativeDarkcontrollerDraw_register(ctx);
    NativeCaterpillarEndStep_register(ctx);
    NativeDeviceChoiceDraw_register(ctx);
    NativeDarkPonmanDraw_register(ctx);
    NativeDarkPonmanStep_register(ctx);
    NativeLightfairyDraw_register(ctx);
    NativeLightfairyStep_register(ctx);
    NativeScissordancerStep_register(ctx);
}

// We dont want the scripts running on a different completely unrelated game, so we'll check to see if the name here matches the games GEN8 name
void NativeScripts_registerAll(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == nullptr) return;
    if (strcmp(title, "UNDERTALE") == 0) {
        registerUndertale(ctx);
    } else if (strcmp(title, "DELTARUNE Chapter 1") == 0) {
        registerDeltaruneChapter1(ctx);
    } else if (strcmp(title, "DELTARUNE Chapter 2") == 0) {
        NativeChapter2WriterDraw_register(ctx);
        NativeChapter2DwTransitionLineStep_register(ctx);
        NativeChapter2DwTransitionLineDraw_register(ctx);
        NativeChapter2DwTransitionParticleStep_register(ctx);
    }
}
