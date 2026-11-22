#pragma once
#include <3ds.h>
#include <citro3d.h>
#define N3DS_NATIVE_TEXTURE_MAGIC 0x31545843u
#define N3DS_NATIVE_TEXTURE_VERSION 1u
#define N3DS_NATIVE_TEXTURE_HEADER_SIZE 32u
#define N3DS_NATIVE_TEXTURE_COMPRESSION_NONE 0u
#define N3DS_NATIVE_TEXTURE_COMPRESSION_LZ11 1u

enum {
    N3DS_NATIVE_TEXFMT_RGBA4444 = 2,
    N3DS_NATIVE_TEXFMT_L4 = 4,
    N3DS_NATIVE_TEXFMT_LA4 = 5,
    N3DS_NATIVE_TEXFMT_ETC1 = 12,
    N3DS_NATIVE_TEXFMT_ETC1A4 = 13,
};

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t nativeFormat;
    GPU_TEXCOLOR gpuFormat;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint32_t compression;
    uint32_t uncompressedDataSize;
} N3DSNativeTextureInfo;

static inline uint16_t n3ds_nextPow2(uint32_t value) {
    if (value == 0u || value > 1024u) return 0u;
    uint16_t result = 8u;
    while (result < value)
        result <<= 1u;
    return result;
}

bool n3ds_texParse(const void* data, size_t size, N3DSNativeTextureInfo* outInfo);
size_t n3ds_texUploadSize(
    const N3DSNativeTextureInfo* info, uint16_t texWidth, uint16_t texHeight);
bool n3ds_texLoad(const void* data, size_t size, C3D_Tex* outTex,
    bool preferVram, bool* outInVram, bool* outOfMemory);
