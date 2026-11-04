#include "native_scripts.h"
#include "generated/generated_native_scripts.h"

void NativeScripts_registerAll(VMContext* ctx) {
    GeneratedNativeScripts_registerAll(ctx);
}
