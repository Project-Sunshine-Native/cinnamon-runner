#include "native_scripts.h"
#include "UNDERTALE/generated_native_scripts.h"
#include "vm.h"

#include <string.h>

// UNDERTALE native script registration
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

// DELTARUNE Chapter 1 native script registration
void NativePurplegrassDraw_register(VMContext* ctx);
void NativeBattlecontrollerDraw_register(VMContext* ctx);
void NativeWriterDraw_register(VMContext* ctx);
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

// DELTARUNE Chapter 2 native script registration
void NativeChapter2WriterDraw_register(VMContext* ctx);
void NativeChapter2DwTransitionLineStep_register(VMContext* ctx);
void NativeChapter2DwTransitionLineDraw_register(VMContext* ctx);
void NativeChapter2DwTransitionParticleStep_register(VMContext* ctx);
void NativeChapter2CaterpillarEndStep_register(VMContext* ctx);
void NativeChapter2HangplugDraw_register(VMContext* ctx);
void NativeChapter2CyberWallLightsDraw_register(VMContext* ctx);
void NativeChapter2MaincharaEndStep_register(VMContext* ctx);
void NativeChapter2TiledArea_register(VMContext* ctx);
void NativeChapter2ParallaxUpdate_register(VMContext* ctx);
void NativeChapter2SelectionMatrix_register(VMContext* ctx);
void NativeChapter2Charbox_register(VMContext* ctx);
void NativeChapter2BattlecontrollerDraw_register(VMContext* ctx);
void NativeChapter2DarkcontrollerDraw_register(VMContext* ctx);
void NativeChapter2BattlebackDraw_register(VMContext* ctx);
void NativeChapter2HeartStep_register(VMContext* ctx);
void NativeChapter2RegularBulletStep_register(VMContext* ctx);
void NativeChapter2TimeBeginStep_register(VMContext* ctx);
void NativeChapter2TeacupBulletStep_register(VMContext* ctx);
void NativeChapter2TeacupStep_register(VMContext* ctx);
void NativeChapter2TeacupDraw_register(VMContext* ctx);
void NativeChapter2ChaseenemyDraw_register(VMContext* ctx);
void NativeChapter2ChaseenemyStep_register(VMContext* ctx);
void NativeChapter2WerewireDraw_register(VMContext* ctx);
void NativeChapter2BulletInit_register(VMContext* ctx);
void NativeChapter2Beatbullet2_register(VMContext* ctx);
void NativeChapter2Beatflash_register(VMContext* ctx);
void NativeChapter2WerewireStep_register(VMContext* ctx);
void NativeChapter2WerewireLaserSquareStep_register(VMContext* ctx);
void NativeChapter2WerewireLaserCircleStep_register(VMContext* ctx);
void NativeChapter2ParallaxerCyberDraw_register(VMContext* ctx);
#ifdef PLATFORM_3DS
void NativeChapter2ParallaxerCyber2Draw_register(VMContext* ctx);
#endif
void NativeChapter2CyberBackgroundDraw_register(VMContext* ctx);
void NativeChapter2ParallaxerLayerDraw_register(VMContext* ctx);

// DELTARUNE Chapter 3 native script registration
void NativeChapter3Lerpvar_register(VMContext* ctx);
void NativeChapter3Afterimage_register(VMContext* ctx);
void NativeChapter3AfterimageScreen_register(VMContext* ctx);
void NativeChapter3AfterimageBlend_register(VMContext* ctx);
void NativeChapter3DrawBeam_register(VMContext* ctx);
void NativeChapter3RoaringStar_register(VMContext* ctx);
void NativeChapter3Roaring2Draw_register(VMContext* ctx);
void NativeChapter3TunnelSlasher_register(VMContext* ctx);
void NativeChapter3SwordTunnelSword_register(VMContext* ctx);
void NativeChapter3PointingStar_register(VMContext* ctx);
void NativeChapter3ViewGet_register(VMContext* ctx);

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

static void registerDeltaruneChapter2(VMContext* ctx) {
    NativeChapter2WriterDraw_register(ctx);
    NativeChapter2DwTransitionLineStep_register(ctx);
    NativeChapter2DwTransitionLineDraw_register(ctx);
    NativeChapter2DwTransitionParticleStep_register(ctx);
    NativeChapter2CaterpillarEndStep_register(ctx);
    NativeChapter2HangplugDraw_register(ctx);
    NativeChapter2CyberWallLightsDraw_register(ctx);
    NativeChapter2MaincharaEndStep_register(ctx);
    NativeChapter2TiledArea_register(ctx);
    NativeChapter2ParallaxUpdate_register(ctx);
    NativeChapter2SelectionMatrix_register(ctx);
    NativeChapter2Charbox_register(ctx);
    NativeChapter2BattlecontrollerDraw_register(ctx);
    NativeChapter2DarkcontrollerDraw_register(ctx);
    NativeChapter2BattlebackDraw_register(ctx);
    NativeChapter2HeartStep_register(ctx);
    NativeChapter2RegularBulletStep_register(ctx);
    NativeChapter2TimeBeginStep_register(ctx);
    NativeChapter2TeacupBulletStep_register(ctx);
    NativeChapter2TeacupStep_register(ctx);
    NativeChapter2TeacupDraw_register(ctx);
    NativeChapter2ChaseenemyDraw_register(ctx);
    NativeChapter2ChaseenemyStep_register(ctx);
    NativeChapter2WerewireDraw_register(ctx);
    NativeChapter2BulletInit_register(ctx);
    NativeChapter2Beatbullet2_register(ctx);
    NativeChapter2Beatflash_register(ctx);
    NativeChapter2WerewireStep_register(ctx);
    NativeChapter2WerewireLaserSquareStep_register(ctx);
    NativeChapter2WerewireLaserCircleStep_register(ctx);
    NativeChapter2ParallaxerCyberDraw_register(ctx);
#ifdef PLATFORM_3DS
    NativeChapter2ParallaxerCyber2Draw_register(ctx);
#endif
    NativeChapter2CyberBackgroundDraw_register(ctx);
    NativeChapter2ParallaxerLayerDraw_register(ctx);
}

static void registerDeltaruneChapter3(VMContext* ctx) {
    NativeChapter3Lerpvar_register(ctx);
    NativeChapter3Afterimage_register(ctx);
    NativeChapter3AfterimageScreen_register(ctx);
    NativeChapter3AfterimageBlend_register(ctx);
    NativeChapter3DrawBeam_register(ctx);
    NativeChapter3RoaringStar_register(ctx);
    NativeChapter3Roaring2Draw_register(ctx);
    NativeChapter3TunnelSlasher_register(ctx);
    NativeChapter3SwordTunnelSword_register(ctx);
    NativeChapter3PointingStar_register(ctx);
    NativeChapter3ViewGet_register(ctx);
}

// We dont want the scripts running on a different completely unrelated game, so we'll check to see if the name here matches the running games name in the GEN8 chunk. If not, dont run the scripts
void NativeScripts_registerAll(VMContext* ctx) {
    const char* title = ctx->dataWin->gen8.displayName;
    if (title == NULL) return;
    if (strcmp(title, "UNDERTALE") == 0) {
        registerUndertale(ctx);
    } else if (strcmp(title, "DELTARUNE Chapter 1") == 0) {
        registerDeltaruneChapter1(ctx);
    } else if (strcmp(title, "DELTARUNE Chapter 2") == 0) {
        registerDeltaruneChapter2(ctx);
    } else if (strcmp(title, "DELTARUNE Chapter 3") == 0) {
        registerDeltaruneChapter3(ctx);
    }
}
