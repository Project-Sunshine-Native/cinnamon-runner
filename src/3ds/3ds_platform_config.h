#pragma once
#include <3ds.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#define KB (1024u)
#define MB (1024u * KB)
#define N3DS_ENABLE_DEBUG_BOTTOM_SCREEN 1
#define N3DS_HAS_BOTTOM_TARGET (!N3DS_DISABLE_GAME_BOTTOM_SCREEN || N3DS_ENABLE_DEBUG_BOTTOM_SCREEN)
#define N3DS_FORCE_TEXTURES_FCRAM 1
#define N3DS_C3D_COMMAND_BUFFER_SIZE 0x100000
#define N3DS_C3D_MAX_QUADS 6144u
#define N3DS_OLD3DS_APP_MEMORY_CAP_BYTES (80u * MB)
#define N3DS_NEW3DS_APP_MEMORY_CAP_BYTES (128u * MB)
#define N3DS_OLD3DS_TEXTURE_FCRAM_CACHE_MAX_BYTES (24u * MB)
#define N3DS_NEW3DS_TEXTURE_FCRAM_CACHE_MAX_BYTES (60u * MB)
// for whatever reason, new 3ds mode originally had a LOWER reserve amount (512kb) than old 3ds mode which caused streamed music to
// no longer play after the texture cache as close to full.
// why i decided to do that i have no fucking idea as it should be the other way around for a limit
// like that but it turns out 4mb reserve works fine on o3ds too soooo yeah oops! 

#define N3DS_TEXTURE_FCRAM_RESERVE_BYTES (4u * MB)

static inline bool n3ds_isNew3DSMode(void) {
    static bool cached = false;
    static bool didQuery = false;
    if (!didQuery) {
        bool isNew3DS = false;
        if (R_SUCCEEDED(APT_CheckNew3DS(&isNew3DS))) cached = isNew3DS;
        didQuery = true;
    }
    return cached;
}

static inline uint32_t n3ds_getAppMemoryCapBytes(void) {
    return n3ds_isNew3DSMode()
        ? N3DS_NEW3DS_APP_MEMORY_CAP_BYTES
        : N3DS_OLD3DS_APP_MEMORY_CAP_BYTES;
}

static inline uint32_t n3ds_getAppMemoryLimitBytes(void) {
    uint32_t regionSize = osGetMemRegionSize(MEMREGION_APPLICATION);
    uint32_t cap = n3ds_getAppMemoryCapBytes();
    return regionSize > 0u && regionSize < cap ? regionSize : cap;
}

static inline uint32_t n3ds_getUsedHeapBytes(void) {
    struct mallinfo memInfo = mallinfo();
    return memInfo.uordblks > 0 ? (uint32_t)memInfo.uordblks : 0u;
}

static inline uint32_t n3ds_getFreeHeapBudgetBytes(void) {
    uint32_t totalBytes = n3ds_getAppMemoryLimitBytes();
    uint32_t usedBytes = n3ds_getUsedHeapBytes();
    return totalBytes > usedBytes ? totalBytes - usedBytes : 0u;
}

static inline uint32_t n3ds_getFreeLinearBytes(void) {
    return (uint32_t)linearSpaceFree();
}

static inline uint32_t n3ds_getTextureCacheBytes(void) {
    return n3ds_isNew3DSMode()
        ? N3DS_NEW3DS_TEXTURE_FCRAM_CACHE_MAX_BYTES
        : N3DS_OLD3DS_TEXTURE_FCRAM_CACHE_MAX_BYTES;
}
