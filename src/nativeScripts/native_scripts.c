#include "native_scripts.h"
#include "generated/generated_native_scripts.h"

void NativeSnowfloorDraw_register(VMContext* ctx);
void NativePiperSteamDraw_register(VMContext* ctx);
void NativeSteamplume2Create_register(VMContext* ctx);
void NativeSteamplume2Step_register(VMContext* ctx);
void NativeColorcycle_register(VMContext* ctx);
void NativeOssafeFillRectangle_register(VMContext* ctx);
void NativeFinalbarrierDraw_register(VMContext* ctx);
void NativeOrangeparticleStep_register(VMContext* ctx);
void NativeSinefireAsghelixStep_register(VMContext* ctx);
void NativeBaseWriterBc16_register(VMContext* ctx);
void NativeAsgorebBodyDraw_register(VMContext* ctx);
void NativeAsgorespearDraw_register(VMContext* ctx);
void NativeAsgorebulparentStep_register(VMContext* ctx);
void NativeOrangeparticlegenStep_register(VMContext* ctx);
void NativePurplegradienterDraw_register(VMContext* ctx);

void NativeScripts_registerAll(VMContext* ctx) {
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
    NativeAsgorebBodyDraw_register(ctx);
    NativeAsgorespearDraw_register(ctx);
    NativeAsgorebulparentStep_register(ctx);
    NativeOrangeparticlegenStep_register(ctx);
    NativePurplegradienterDraw_register(ctx);
}
