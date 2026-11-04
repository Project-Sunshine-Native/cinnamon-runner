#include "3ds_native_texture.h"
#include "3ds_platform_config.h"
#include "lz11/lz11.h"
#include <3ds/gpu/gx.h>
#include <stdlib.h>
#include <string.h>

static uint16_t n3ds_texReadLe16(const uint8_t* src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t n3ds_texReadLe32(const uint8_t* src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 24);
}

static bool n3ds_texMapFormat(uint32_t nativeFormat, GPU_TEXCOLOR* outFormat) {
    if (outFormat == NULL) return false;
    switch (nativeFormat) {
    case N3DS_NATIVE_TEXFMT_RGBA4444:
        *outFormat = GPU_RGBA4;
        return true;
    case N3DS_NATIVE_TEXFMT_LA4:
        *outFormat = GPU_LA4;
        return true;
    case N3DS_NATIVE_TEXFMT_L4:
        *outFormat = GPU_L4;
        return true;
    case N3DS_NATIVE_TEXFMT_ETC1:
        *outFormat = GPU_ETC1;
        return true;
    case N3DS_NATIVE_TEXFMT_ETC1A4:
        *outFormat = GPU_ETC1A4;
        return true;
    default:
        return false;
    }
}

static size_t n3ds_texTileSizeBytes(uint32_t nativeFormat) {
    switch (nativeFormat) {
    case N3DS_NATIVE_TEXFMT_L4:
        return 32u;
    case N3DS_NATIVE_TEXFMT_LA4:
        return 64u;
    case N3DS_NATIVE_TEXFMT_ETC1:
        return 32u;
    case N3DS_NATIVE_TEXFMT_ETC1A4:
        return 64u;
    default:
        return 128u;
    }
}

static bool n3ds_texUploadLevel0(C3D_Tex* tex, const void* src, bool texInVram) {
    if (tex == NULL || src == NULL) return false;
    if (texInVram) {
        GSPGPU_FlushDataCache(src, (u32)tex->size);
        C3D_TexUpload(tex, src);
        return true;
    }
    memcpy(tex->data, src, tex->size);
    return true;
}

static bool n3ds_texInit(C3D_Tex* tex, uint16_t width, uint16_t height,
    GPU_TEXCOLOR format, bool preferVram, bool* outInVram) {
    bool inVram = false;
#if N3DS_FORCE_TEXTURES_FCRAM
    preferVram = false;
#endif
    if (preferVram) inVram = C3D_TexInitVRAM(tex, width, height, format);
    if (!inVram && !C3D_TexInit(tex, width, height, format)) return false;
    if (outInVram != NULL) *outInVram = inVram;
    return true;
}

static void n3ds_texFinish(C3D_Tex* tex) {
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
}

static bool n3ds_texParseHeader(
    const uint8_t* bytes, size_t fileSize, N3DSNativeTextureInfo* outInfo) {
    if (bytes == NULL || fileSize < N3DS_NATIVE_TEXTURE_HEADER_SIZE || outInfo == NULL)
        return false;
    uint32_t magic = n3ds_texReadLe32(bytes + 0);
    uint16_t version = n3ds_texReadLe16(bytes + 4);
    uint16_t headerSize = n3ds_texReadLe16(bytes + 6);
    uint16_t width = n3ds_texReadLe16(bytes + 8);
    uint16_t height = n3ds_texReadLe16(bytes + 10);
    uint32_t textureFormat = n3ds_texReadLe32(bytes + 12);
    uint32_t dataOffset = n3ds_texReadLe32(bytes + 16);
    uint32_t dataSize = n3ds_texReadLe32(bytes + 20);
    uint32_t compression = n3ds_texReadLe32(bytes + 24);
    uint32_t uncompressedDataSize = n3ds_texReadLe32(bytes + 28);
    GPU_TEXCOLOR gpuFormat = GPU_RGBA4;

    if (magic != N3DS_NATIVE_TEXTURE_MAGIC || version != N3DS_NATIVE_TEXTURE_VERSION ||
        headerSize < N3DS_NATIVE_TEXTURE_HEADER_SIZE) {
        return false;
    }
    if (!n3ds_texMapFormat(textureFormat, &gpuFormat)) return false;
    if (width == 0 || height == 0) return false;
    if (compression != N3DS_NATIVE_TEXTURE_COMPRESSION_NONE &&
        compression != N3DS_NATIVE_TEXTURE_COMPRESSION_LZ11) {
        return false;
    }
    if (compression == N3DS_NATIVE_TEXTURE_COMPRESSION_NONE) {
        if (uncompressedDataSize != 0u && uncompressedDataSize != dataSize) return false;
        uncompressedDataSize = dataSize;
    }
    if (uncompressedDataSize == 0u) return false;
    if (dataOffset < headerSize || (size_t)headerSize > fileSize || (size_t)dataOffset > fileSize ||
        (size_t)dataSize > fileSize - (size_t)dataOffset)
        return false;

    outInfo->width = width;
    outInfo->height = height;
    outInfo->nativeFormat = textureFormat;
    outInfo->gpuFormat = gpuFormat;
    outInfo->dataOffset = dataOffset;
    outInfo->dataSize = dataSize;
    outInfo->compression = compression;
    outInfo->uncompressedDataSize = uncompressedDataSize;
    return true;
}

bool n3ds_texParse(const void* data, size_t size, N3DSNativeTextureInfo* outInfo) {
    return n3ds_texParseHeader(data, size, outInfo);
}

size_t n3ds_texUploadSize(
    const N3DSNativeTextureInfo* info, uint16_t texWidth, uint16_t texHeight) {
    if (info == NULL || texWidth == 0 || texHeight == 0) return 0u;
    size_t tileCols = ((size_t)texWidth + 7u) / 8u;
    size_t tileRows = ((size_t)texHeight + 7u) / 8u;
    size_t tileSize = n3ds_texTileSizeBytes(info->nativeFormat);
    if (tileCols > SIZE_MAX / tileRows || tileCols * tileRows > SIZE_MAX / tileSize) {
        return 0u;
    }
    return tileCols * tileRows * tileSize;
}

bool n3ds_texLoad(const void* data, size_t size, C3D_Tex* outTex,
    bool preferVram, bool* outInVram, bool* outOfMemory) {
    if (outOfMemory != NULL) *outOfMemory = false;
    N3DSNativeTextureInfo info;
    if (outTex == NULL || !n3ds_texParse(data, size, &info)) return false;

    memset(outTex, 0, sizeof(*outTex));
    const uint8_t* src = (const uint8_t*)data + info.dataOffset;
    uint16_t texWidth = n3ds_nextPow2(info.width);
    uint16_t texHeight = n3ds_nextPow2(info.height);
    if (texWidth == 0 || texHeight == 0) return false;

    size_t expectedSize = n3ds_texUploadSize(&info, texWidth, texHeight);
    if (expectedSize == 0u || info.uncompressedDataSize != expectedSize) return false;

    bool inVram = false;
    if (!n3ds_texInit(outTex, texWidth, texHeight, info.gpuFormat, preferVram, &inVram)) {
        if (outOfMemory != NULL) *outOfMemory = true;
        return false;
    }
    if (outTex->size != expectedSize) {
        C3D_TexDelete(outTex);
        memset(outTex, 0, sizeof(*outTex));
        return false;
    }

    bool loaded = false;
    uint8_t* decompressed = NULL;
    if (info.compression == N3DS_NATIVE_TEXTURE_COMPRESSION_LZ11) {
        if (!inVram) {
            loaded = LZ11_decompress(src, info.dataSize, outTex->data, outTex->size);
        } else {
            decompressed = malloc(expectedSize);
            if (decompressed == NULL) {
                if (outOfMemory != NULL) *outOfMemory = true;
            } else {
                loaded = LZ11_decompress(src, info.dataSize, decompressed, expectedSize) &&
                    n3ds_texUploadLevel0(outTex, decompressed, true);
            }
        }
    } else {
        loaded = n3ds_texUploadLevel0(outTex, src, inVram);
    }
    free(decompressed);

    if (!loaded) {
        C3D_TexDelete(outTex);
        memset(outTex, 0, sizeof(*outTex));
        return false;
    }
    n3ds_texFinish(outTex);

    if (outInVram != NULL) *outInVram = inVram;
    return true;
}
