#include "native_scripts.h"
#include "generated/generated_native_scripts.h"

void NativeSnowfloorDraw_register(VMContext* ctx);
void NativePiperSteamDraw_register(VMContext* ctx);
void NativeSteamplume2Create_register(VMContext* ctx);
void NativeSteamplume2Step_register(VMContext* ctx);

void NativeScripts_registerAll(VMContext* ctx) {
    GeneratedNativeScripts_registerAll(ctx);
    NativeSnowfloorDraw_register(ctx);
    NativePiperSteamDraw_register(ctx);
    NativeSteamplume2Create_register(ctx);
    NativeSteamplume2Step_register(ctx);
}
