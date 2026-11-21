#include "nativeScripts/native_runtime.h"

bool NativeBaseWriter_drawBc16(VMContext* ctx, RValue* outResult);

void NativeBaseWriterBc16_register(VMContext* ctx) {
    if (ctx->dataWin->gen8.wadVersion != 16) return;
    (void)VM_registerNativeScript(ctx, "gml_Object_obj_base_writer_Draw_0", NativeBaseWriter_drawBc16);
}
