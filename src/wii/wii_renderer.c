#include "wii_renderer.h"
#include "lz11/lz11.h"

#include "../runner.h"
#include "../log.h"
#include "../text_utils.h"
#include "../utils.h"

#include <assert.h>
#include <gccore.h>
#include <limits.h>
#include <math.h>
#include <malloc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stb/ds/stb_ds.h>

#define WII_ATLAS_MAGIC 0x5441334Eu
#define WII_ATLAS_VERSION 2u
#define WII_ATLAS_DIRECTORY_MAGIC 0x4441334Eu
#define WII_ATLAS_DIRECTORY_VERSION 2u
#define WII_ATLAS_BLOCK_TPAG_BINDINGS 0x47415054u
#define WII_ATLAS_BLOCK_ROOM_MANIFEST 0x4D4F4F52u
#define WII_TPAG_BINDINGS_VERSION 2u
#define WII_TPAG_BINDINGS_ROW_SIZE 16u
#define WII_TPAG_SOURCE_NONE 0u
#define WII_TPAG_SOURCE_ATLAS 2u
#define WII_TPAG_SOURCE_SURFACE 3u
#define WII_ROOM_MANIFEST_MAGIC 0x4D52334Eu
#define WII_ROOM_MANIFEST_VERSION 2u
#define WII_NATIVE_TEXTURE_MAGIC 0x31545843u
#define WII_NATIVE_TEXTURE_VERSION 1u
#define WII_NATIVE_TEXFMT_CI4 7u
#define WII_NATIVE_TEXFMT_CI8 8u
#define WII_NATIVE_TEXFMT_RGB5A3 3u
#define WII_NATIVE_TEXFMT_CMPR 14u
#define WII_NATIVE_TEXTURE_COMPRESSION_NONE 0u
#define WII_NATIVE_TEXTURE_COMPRESSION_LZ11 1u
#define WII_CI4_TLUT_BYTES 32u
#define WII_CI8_TLUT_BYTES 512u
#define WII_ATLAS_PAGE_CACHE_BUDGET_BYTES (20u * 1024u * 1024u)
#define WII_RENDER_BATCH_MAX_QUADS 256u
#define WII_GENERIC_ALLOC_ALIGNMENT 32u

typedef enum {
    WII_RENDER_BATCH_NONE = 0,
    WII_RENDER_BATCH_SOLID,
    WII_RENDER_BATCH_TEXTURE_PAGE,
    WII_RENDER_BATCH_SURFACE,
} WiiRenderBatchType;

typedef struct {
    float x;
    float y;
    float u;
    float v;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} WiiRenderBatchVertex;

typedef struct {
    WiiRenderBatchType type;
    bool blend;
    bool replace;
    void* texture;
    uint32_t quadCount;
    WiiRenderBatchVertex vertices[WII_RENDER_BATCH_MAX_QUADS * 4u];
} WiiRenderBatch;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t format;
    uint32_t dataOffset;
    uint32_t dataSize;
    uint8_t* ci8Data;
    uint32_t ci8DataSize;
    uint32_t paddedWidth;
    uint32_t paddedHeight;
    uint32_t palette[256];
    uint64_t lastUse;
    uint32_t lastLoadAttemptFrame;
    GXTexObj texObj;
    GXTlutObj tlutObj;
    bool gxReady;
    bool loadFailed;
} WiiTexturePage;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t fragmentStart;
    uint16_t fragmentCount;
    uint16_t resizeDivisor;
} WiiAtlasItem;

typedef struct {
    uint8_t source;
    uint8_t flags;
    uint16_t subtexIndex;
    uint16_t resizeDivisor;
    uint32_t atlasItemIndex;
} WiiTpagBinding;

typedef struct {
    uint16_t atlasId;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t sourceX;
    uint16_t sourceY;
} WiiAtlasFragment;

typedef struct {
    WiiTexturePage* page;
    int32_t sourceLeft;
    int32_t sourceTop;
    int32_t sourceRight;
    int32_t sourceBottom;
    int32_t textureX;
    int32_t textureY;
    int32_t textureWidth;
    int32_t textureHeight;
} WiiAtlasFragmentClip;

typedef struct {
    float x1;
    float y1;
    float x2;
    float y2;
    float x3;
    float y3;
    float x4;
    float y4;
} WiiQuad;

typedef struct {
    int16_t bgDef;
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
    uint16_t atlasId;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t fragmentStart;
    uint16_t fragmentCount;
} WiiAtlasTileEntry;

typedef struct {
    int16_t bgDef;
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
} WiiAtlasTileKey;

typedef struct {
    WiiAtlasTileKey key;
    uint32_t value;
} WiiAtlasTileLookupEntry;

typedef struct {
    uint32_t roomIndex;
    uint32_t pageStart;
    uint32_t pageCount;
} WiiRoomManifestEntry;

typedef struct {
    uint16_t pageIndex;
    uint8_t flags;
    uint8_t reserved;
} WiiRoomManifestPageRef;

typedef struct {
    bool loaded;
    char* path;
    WiiRoomManifestEntry* entries;
    uint32_t entryCount;
    WiiRoomManifestPageRef* pageRefs;
    uint32_t pageRefCount;
    int32_t* entryByRoomIndex;
    uint32_t entryByRoomIndexCount;
} WiiRoomManifest;

typedef struct {
    uint32_t pageIndex;
    uint32_t dataOffset;
} WiiAtlasPageLoadRef;

typedef struct {
    bool used;
    int32_t width;
    int32_t height;
    uint16_t texWidth;
    uint16_t texHeight;
    uint32_t texFormat;
    uint32_t textureSize;
    uint8_t* pixels;
    GXTexObj texObj;
    bool textureReady;
    bool dirty;
    bool hasContents;
} WiiSurface;

typedef struct {
    int32_t activeViewX;
    int32_t activeViewY;
    float activeViewScaleX;
    float activeViewScaleY;
    float activeViewOffsetX;
    float activeViewOffsetY;
    Matrix4f activeProjectionWorldToPixel;
    bool activeProjectionUsesMatrix;
    int32_t activeProjectionTargetSurfaceId;
    float gxProjectionLeft;
    float gxProjectionTop;
    float gxProjectionRight;
    float gxProjectionBottom;
    float gxViewportX;
    float gxViewportY;
    float gxViewportW;
    float gxViewportH;
    uint32_t gxScissorX;
    uint32_t gxScissorY;
    uint32_t gxScissorW;
    uint32_t gxScissorH;
} WiiRenderState;

typedef struct WiiRenderStateSnapshot WiiRenderStateSnapshot;

struct WiiRenderer {
    Renderer base;
    char* dataWinPath;
    char* atlasPath;
    FILE* atlasFile;
    uint8_t* atlasData;
    uint32_t atlasDataSize;
    uint32_t atlasFileSize;
    bool cacheAtlas;
    uint32_t atlasTextureCacheBytes;
    WiiTexturePage* pages;
    uint32_t pageCount;
    WiiAtlasItem* items;
    uint32_t itemCount;
    WiiTpagBinding* tpagBindings;
    uint32_t tpagBindingCount;
    int32_t* tpagOwnerSprite;
    uint32_t tpagOwnerSpriteCount;
    bool* warmedSprites;
    uint32_t warmedSpriteCount;
    uint32_t originalSpriteCount;
    uint32_t originalTpagCount;
    WiiAtlasFragment* fragments;
    uint32_t fragmentCount;
    WiiAtlasTileEntry* tileEntries;
    uint32_t tileEntryCount;
    WiiAtlasTileLookupEntry* tileLookup;
    uint32_t embeddedTpagBindingOffset;
    uint32_t embeddedTpagBindingSize;
    uint32_t embeddedRoomManifestOffset;
    uint32_t embeddedRoomManifestSize;
    WiiRoomManifest roomManifest;
    bool* roomWantedPages;
    int32_t activeRoomIndex;
    WiiSurface* surfaces;
    uint32_t surfaceCount;
    int32_t applicationSurfaceId;
    int32_t targetSurfaceId;
    int32_t frameGameW;
    int32_t frameGameH;
    int32_t frameBufferW;
    int32_t frameBufferH;
    int32_t outputX;
    int32_t outputY;
    int32_t outputW;
    int32_t outputH;
    WiiRenderState renderState;
    uint8_t clearR;
    uint8_t clearG;
    uint8_t clearB;
    uint32_t frameCounter;
    uint64_t usageCounter;
    uint32_t residentAtlasBytes;
    WiiRenderBatch batch;
    WiiRenderStateSnapshot* targetStack;
};

typedef struct WiiRenderStateSnapshot {
    int32_t targetSurfaceId;
    int32_t cportX;
    int32_t cportY;
    int32_t cportW;
    int32_t cportH;
    WiiRenderState renderState;
    Matrix4f gmlMatrices[MATRICES_MAX];
    int32_t cameraCurrent;
} WiiRenderStateSnapshot;

static void GX_drawSurfaceColor(Renderer* base, int32_t surfaceID, int32_t srcLeft,
    int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale,
    float yscale, float angleDeg, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, float alpha);
static void GX_surfaceResize(
    Renderer* base, int32_t surfaceID, int32_t width, int32_t height);
static bool GX_loadAtlasPage(WiiRenderer* renderer, uint32_t pageIndex);
static bool GX_loadRoomAtlasPage(WiiRenderer* renderer, uint32_t pageIndex);
static void GX_applyProjection(
    Renderer* base, const Matrix4f* viewMatrix, const Matrix4f* projectionMatrix);
static void GX_flushBatch(WiiRenderer* renderer);
static bool GX_evictOldestAtlasPage(WiiRenderer* renderer, uint32_t protectedPage);
static WiiRenderBatchVertex* GX_reserveBatchQuad(
    WiiRenderer* renderer, WiiRenderBatchType type, void* texture, bool blend, bool replace);
static void GX_setBatchVertex(WiiRenderer* renderer, WiiRenderBatchVertex* vertex, float x,
    float y, float u, float v, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
static void GX_enqueueSolidQuad(WiiRenderer* renderer, bool replace, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, uint32_t c1, uint32_t c2,
    uint32_t c3, uint32_t c4, uint8_t alpha);

static RuntimeView* GX_currentRuntimeView(WiiRenderer* renderer) {
    if (renderer->base.runner == NULL) return NULL;
    Runner* runner = renderer->base.runner;
    int32_t viewCurrent = runner->viewsEnabled ? runner->viewCurrent : 0;
    if (viewCurrent < 0 || viewCurrent >= MAX_VIEWS) viewCurrent = 0;
    return &runner->views[viewCurrent];
}

static GMLCamera* GX_currentViewCamera(WiiRenderer* renderer) {
    RuntimeView* view = GX_currentRuntimeView(renderer);
    if (renderer->base.runner == NULL || view == NULL) return NULL;
    renderer->base.cameraCurrent = view->cameraId;
    return Runner_getCameraById(renderer->base.runner, renderer->base.cameraCurrent);
}

static GMLCamera* GX_findProjectionCamera(
    WiiRenderer* renderer, const Matrix4f* viewMatrix, const Matrix4f* projectionMatrix) {
    if (renderer->base.runner == NULL) return NULL;
    Runner* runner = renderer->base.runner;
    for (int32_t i = 0; i < MAX_DEFAULT_ROOM_CAMERAS; ++i) {
        GMLCamera* camera = &runner->defaultCameras[i];
        if (!camera->allocated) continue;
        if (viewMatrix == &camera->viewMatrix || projectionMatrix == &camera->projectionMatrix)
            return camera;
    }
    for (int32_t i = 0; i < MAX_USER_CAMERAS; ++i) {
        GMLCamera* camera = &runner->userCameras[i];
        if (!camera->allocated) continue;
        if (viewMatrix == &camera->viewMatrix || projectionMatrix == &camera->projectionMatrix)
            return camera;
    }
    if (runner->surfaceCamera.allocated &&
        (viewMatrix == &runner->surfaceCamera.viewMatrix ||
            projectionMatrix == &runner->surfaceCamera.projectionMatrix))
        return &runner->surfaceCamera;
    if (runner->guiCamera.allocated &&
        (viewMatrix == &runner->guiCamera.viewMatrix ||
            projectionMatrix == &runner->guiCamera.projectionMatrix))
        return &runner->guiCamera;
    return Runner_getCameraById(runner, renderer->base.cameraCurrent);
}

static bool GX_matrixAlmostEqual(const Matrix4f* a, const Matrix4f* b) {
    if (a == NULL || b == NULL) return false;
    for (uint32_t i = 0u; i < 16u; ++i) {
        if (fabsf(a->m[i] - b->m[i]) > 0.0001f) return false;
    }
    return true;
}

static bool GX_cameraIsSimple(const GMLCamera* camera) {
    if (camera == NULL) return false;
    GMLCamera simple = *camera;
    Runner_updateCameraViewSimple(&simple);
    return GX_matrixAlmostEqual(&camera->viewMatrix, &simple.viewMatrix) &&
        GX_matrixAlmostEqual(&camera->projectionMatrix, &simple.projectionMatrix);
}

static uint16_t GX_readLe16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

static uint32_t GX_readLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
        ((uint32_t)p[3] << 24u);
}

static uint8_t* GX_allocMem1TextureData(WiiRenderer* renderer, uint32_t size) {
    if (size == 0u) return NULL;
    uint8_t* data = memalign(WII_GENERIC_ALLOC_ALIGNMENT, size);
    while (data == NULL && GX_evictOldestAtlasPage(renderer, UINT32_MAX)) {
        data = memalign(WII_GENERIC_ALLOC_ALIGNMENT, size);
    }
    return data;
}

static void GX_freeMem1TextureData(void* ptr) {
    free(ptr);
}

static uint8_t* GX_allocMem2AtlasData(uint32_t size) {
    if (size == 0u) return NULL;
    return memalign(WII_GENERIC_ALLOC_ALIGNMENT, size);
}

static void GX_freeMem2AtlasData(void* ptr) {
    free(ptr);
}

static uint32_t GX_hashU16(uint32_t hash, uint16_t value) {
    hash ^= (uint8_t)(value & 0xFFu);
    hash *= 16777619u;
    hash ^= (uint8_t)((value >> 8u) & 0xFFu);
    hash *= 16777619u;
    return hash;
}

static uint32_t GX_hashI16(uint32_t hash, int16_t value) {
    return GX_hashU16(hash, (uint16_t)value);
}

static uint32_t GX_computeTpagHash(const DataWin* dataWin) {
    uint32_t hash = 2166136261u;
    if (dataWin == NULL) return hash;
    hash ^= dataWin->tpag.count;
    hash *= 16777619u;
    for (uint32_t i = 0u; i < dataWin->tpag.count; ++i) {
        const TexturePageItem* item = &dataWin->tpag.items[i];
        hash = GX_hashU16(hash, item->sourceX);
        hash = GX_hashU16(hash, item->sourceY);
        hash = GX_hashU16(hash, item->sourceWidth);
        hash = GX_hashU16(hash, item->sourceHeight);
        hash = GX_hashU16(hash, item->targetX);
        hash = GX_hashU16(hash, item->targetY);
        hash = GX_hashU16(hash, item->targetWidth);
        hash = GX_hashU16(hash, item->targetHeight);
        hash = GX_hashU16(hash, item->boundingWidth);
        hash = GX_hashU16(hash, item->boundingHeight);
        hash = GX_hashI16(hash, item->texturePageId);
    }
    return hash != 0u ? hash : 1u;
}

static uint8_t* GX_maybeDecompressLz11(
    WiiRenderer* renderer, const uint8_t* data, uint32_t size, uint32_t* outSize) {
    if (outSize != NULL) *outSize = size;
    if (data == NULL || size == 0u) return NULL;
    if (data[0] != 0x11u) {
        uint8_t* copy = GX_allocMem1TextureData(renderer, size);
        if (copy == NULL) return NULL;
        memcpy(copy, data, size);
        return copy;
    }
    uint32_t decompressedSize = 0u;
    if (!LZ11_getDecompressedSize(data, size, &decompressedSize)) return NULL;
    uint8_t* out = GX_allocMem1TextureData(renderer, decompressedSize);
    if (out == NULL) return NULL;
    if (!LZ11_decompress(data, size, out, decompressedSize)) {
        GX_freeMem1TextureData(out);
        return NULL;
    }
    if (outSize != NULL) *outSize = decompressedSize;
    return out;
}

static bool GX_readAtlasRange(
    WiiRenderer* renderer, uint32_t offset, uint32_t size, uint8_t* outData) {
    FILE* file = renderer->atlasFile;
    if (file == NULL || outData == NULL || size == 0u || offset > renderer->atlasFileSize ||
        size > renderer->atlasFileSize - offset) return false;
    clearerr(file);
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(outData, 1, size, file) == size;
}

static size_t GX_readStreamBytes(FILE* file, uint8_t* dst, size_t size) {
    if (file == NULL || dst == NULL || size == 0u) return 0u;
    size_t total = 0u;
    while (total < size) {
        size_t chunk = size - total;
        if (chunk > 256u * 1024u) chunk = 256u * 1024u;
        size_t got = fread(dst + total, 1, chunk, file);
        if (got == 0u) break;
        total += got;
    }
    return total;
}

static bool GX_atlasRangeInRam(
    const WiiRenderer* renderer, uint32_t offset, uint32_t size, const uint8_t** outBytes) {
    if (outBytes != NULL) *outBytes = NULL;
    if (renderer->atlasData == NULL || size == 0u) return false;
    if (offset > renderer->atlasDataSize || size > renderer->atlasDataSize - offset) return false;
    if (outBytes != NULL) *outBytes = renderer->atlasData + offset;
    return true;
}

static uint32_t GX_rgb5a3ToRgba(uint16_t v) {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    if ((v & 0x8000u) != 0u) {
        r = (uint8_t)(((v >> 10u) & 31u) * 255u / 31u);
        g = (uint8_t)(((v >> 5u) & 31u) * 255u / 31u);
        b = (uint8_t)((v & 31u) * 255u / 31u);
        a = 255u;
    } else {
        a = (uint8_t)(((v >> 12u) & 7u) * 255u / 7u);
        r = (uint8_t)(((v >> 8u) & 15u) * 255u / 15u);
        g = (uint8_t)(((v >> 4u) & 15u) * 255u / 15u);
        b = (uint8_t)((v & 15u) * 255u / 15u);
    }
    return (uint32_t)r | ((uint32_t)g << 8u) | ((uint32_t)b << 16u) | ((uint32_t)a << 24u);
}

static bool GX_storeCi8Texture(
    WiiTexturePage* page, uint8_t* data, uint32_t dataSize, uint32_t width, uint32_t height) {
    if (page == NULL || data == NULL || dataSize < WII_CI8_TLUT_BYTES || width == 0u ||
        height == 0u)
        return false;
    uint32_t paddedWidth = (width + 7u) & ~7u;
    uint32_t paddedHeight = (height + 3u) & ~3u;
    uint32_t indexBytes = paddedWidth * paddedHeight;
    if (dataSize < WII_CI8_TLUT_BYTES + indexBytes) return false;

    for (uint32_t i = 0u; i < 256u; ++i) {
        uint16_t value = ((uint16_t)data[i * 2u] << 8u) | data[i * 2u + 1u];
        page->palette[i] = GX_rgb5a3ToRgba(value);
    }
    GX_freeMem1TextureData(page->ci8Data);
    page->ci8Data = data;
    page->ci8DataSize = dataSize;
    page->paddedWidth = paddedWidth;
    page->paddedHeight = paddedHeight;
    page->format = WII_NATIVE_TEXFMT_CI8;
    DCFlushRange(data, dataSize);
    GX_InitTlutObj(&page->tlutObj, data, GX_TL_RGB5A3, 256);
    GX_InitTexObjCI(&page->texObj, data + WII_CI8_TLUT_BYTES, (uint16_t)paddedWidth,
        (uint16_t)paddedHeight, GX_TF_CI8, GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT0);
    GX_InitTexObjFilterMode(&page->texObj, GX_NEAR, GX_NEAR);
    page->gxReady = true;
    return true;
}

static bool GX_storeCi4Texture(
    WiiTexturePage* page, uint8_t* data, uint32_t dataSize, uint32_t width, uint32_t height) {
    if (page == NULL || data == NULL || dataSize < WII_CI4_TLUT_BYTES || width == 0u ||
        height == 0u)
        return false;
    uint32_t paddedWidth = (width + 7u) & ~7u;
    uint32_t paddedHeight = (height + 7u) & ~7u;
    uint32_t indexBytes = (paddedWidth * paddedHeight) / 2u;
    if (dataSize < WII_CI4_TLUT_BYTES + indexBytes) return false;

    memset(page->palette, 0, sizeof(page->palette));
    for (uint32_t i = 0u; i < 16u; ++i) {
        uint16_t value = ((uint16_t)data[i * 2u] << 8u) | data[i * 2u + 1u];
        page->palette[i] = GX_rgb5a3ToRgba(value);
    }
    GX_freeMem1TextureData(page->ci8Data);
    page->ci8Data = data;
    page->ci8DataSize = dataSize;
    page->paddedWidth = paddedWidth;
    page->paddedHeight = paddedHeight;
    page->format = WII_NATIVE_TEXFMT_CI4;
    DCFlushRange(data, dataSize);
    GX_InitTlutObj(&page->tlutObj, data, GX_TL_RGB5A3, 16);
    GX_InitTexObjCI(&page->texObj, data + WII_CI4_TLUT_BYTES, (uint16_t)paddedWidth,
        (uint16_t)paddedHeight, GX_TF_CI4, GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT0);
    GX_InitTexObjFilterMode(&page->texObj, GX_NEAR, GX_NEAR);
    page->gxReady = true;
    return true;
}

static bool GX_storeCmprTexture(
    WiiTexturePage* page, uint8_t* data, uint32_t dataSize, uint32_t width, uint32_t height) {
    if (page == NULL || data == NULL || width == 0u || height == 0u) return false;
    uint32_t paddedWidth = (width + 7u) & ~7u;
    uint32_t paddedHeight = (height + 7u) & ~7u;
    uint32_t expectedSize = (paddedWidth / 8u) * (paddedHeight / 8u) * 32u;
    if (dataSize < expectedSize) return false;

    GX_freeMem1TextureData(page->ci8Data);
    page->ci8Data = data;
    page->ci8DataSize = dataSize;
    page->paddedWidth = paddedWidth;
    page->paddedHeight = paddedHeight;
    page->format = WII_NATIVE_TEXFMT_CMPR;
    DCFlushRange(data, dataSize);
    GX_InitTexObj(&page->texObj, data, (uint16_t)paddedWidth, (uint16_t)paddedHeight, GX_TF_CMPR,
        GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&page->texObj, GX_NEAR, GX_NEAR);
    page->gxReady = true;
    return true;
}

static bool GX_storeRgb5a3Texture(
    WiiTexturePage* page, uint8_t* data, uint32_t dataSize, uint32_t width, uint32_t height) {
    if (page == NULL || data == NULL || width == 0u || height == 0u) return false;
    uint32_t paddedWidth = (width + 3u) & ~3u;
    uint32_t paddedHeight = (height + 3u) & ~3u;
    uint32_t expectedSize = paddedWidth * paddedHeight * 2u;
    if (dataSize < expectedSize) return false;

    GX_freeMem1TextureData(page->ci8Data);
    page->ci8Data = data;
    page->ci8DataSize = dataSize;
    page->paddedWidth = paddedWidth;
    page->paddedHeight = paddedHeight;
    page->format = WII_NATIVE_TEXFMT_RGB5A3;
    DCFlushRange(data, dataSize);
    GX_InitTexObj(&page->texObj, data, (uint16_t)paddedWidth, (uint16_t)paddedHeight, GX_TF_RGB5A3,
        GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&page->texObj, GX_NEAR, GX_NEAR);
    page->gxReady = true;
    return true;
}

static uint32_t GX_nativeTextureDataSize(
    uint32_t format, uint32_t width, uint32_t height) {
    if (width == 0u || height == 0u) return 0u;
    uint32_t paddedWidth = (width + 7u) & ~7u;
    uint32_t paddedHeight8 = (height + 7u) & ~7u;
    if (format == WII_NATIVE_TEXFMT_CI4)
        return WII_CI4_TLUT_BYTES + (paddedWidth * paddedHeight8) / 2u;
    if (format == WII_NATIVE_TEXFMT_CI8)
        return WII_CI8_TLUT_BYTES + paddedWidth * ((height + 3u) & ~3u);
    if (format == WII_NATIVE_TEXFMT_CMPR) return (paddedWidth / 8u) * (paddedHeight8 / 8u) * 32u;
    if (format == WII_NATIVE_TEXFMT_RGB5A3)
        return ((width + 3u) & ~3u) * ((height + 3u) & ~3u) * 2u;
    return 0u;
}

static bool GX_decodePage(
    WiiRenderer* renderer, WiiTexturePage* page, const uint8_t* blob, uint32_t blobSize) {
    if (page == NULL || blob == NULL || blobSize < 32u) return false;
    if (GX_readLe32(blob + 0u) != WII_NATIVE_TEXTURE_MAGIC) return false;
    if (GX_readLe16(blob + 4u) != WII_NATIVE_TEXTURE_VERSION) return false;
    uint32_t format = GX_readLe32(blob + 12u);
    uint32_t dataOffset = GX_readLe32(blob + 16u);
    uint32_t dataSize = GX_readLe32(blob + 20u);
    uint32_t compression = GX_readLe32(blob + 24u);
    uint32_t uncompressedSize = GX_readLe32(blob + 28u);
    if (format != WII_NATIVE_TEXFMT_CI4 && format != WII_NATIVE_TEXFMT_CI8 &&
        format != WII_NATIVE_TEXFMT_CMPR && format != WII_NATIVE_TEXFMT_RGB5A3) {
        logError("Wii renderer: unsupported atlas page texture format %u; regenerate Wii assets "
            "for CI4/CI8/CMPR/RGB5A3 atlas pages.\n",
            format);
        return false;
    }
    if (dataOffset > blobSize || dataSize > blobSize - dataOffset) return false;

    uint32_t expectedTextureDataSize =
        GX_nativeTextureDataSize(format, page->width, page->height);
    if (expectedTextureDataSize == 0u) return false;

    uint8_t* textureData = NULL;
    uint32_t textureDataSize = dataSize;
    if (compression == WII_NATIVE_TEXTURE_COMPRESSION_LZ11) {
        textureData = GX_maybeDecompressLz11(renderer, blob + dataOffset, dataSize, &textureDataSize);
        if (textureData == NULL || textureDataSize != uncompressedSize) {
            GX_freeMem1TextureData(textureData);
            return false;
        }
    } else if (compression == WII_NATIVE_TEXTURE_COMPRESSION_NONE) {
        if (uncompressedSize != 0u && uncompressedSize != dataSize) return false;
        textureDataSize = dataSize;
        textureData = GX_allocMem1TextureData(renderer, dataSize);
        if (textureData == NULL) return false;
        memcpy(textureData, blob + dataOffset, dataSize);
    } else {
        logError("Wii renderer: unsupported native texture compression %u\n", compression);
        return false;
    }
    if (textureDataSize != expectedTextureDataSize) {
        logError("Wii renderer: native texture payload size mismatch fmt=%u page=%ux%u got=%u "
            "expected=%u\n",
            format, (unsigned int)page->width, (unsigned int)page->height, textureDataSize,
            expectedTextureDataSize);
        GX_freeMem1TextureData(textureData);
        return false;
    }

    bool ok = false;
    if (format == WII_NATIVE_TEXFMT_CMPR) {
        ok = GX_storeCmprTexture(page, textureData, textureDataSize, page->width, page->height);
    } else if (format == WII_NATIVE_TEXFMT_RGB5A3) {
        ok = GX_storeRgb5a3Texture(page, textureData, textureDataSize, page->width, page->height);
    } else if (format == WII_NATIVE_TEXFMT_CI4) {
        ok = GX_storeCi4Texture(page, textureData, textureDataSize, page->width, page->height);
    } else {
        ok = GX_storeCi8Texture(page, textureData, textureDataSize, page->width, page->height);
    }
    if (!ok) GX_freeMem1TextureData(textureData);
    return ok;
}

static char* GX_dirnameCopy(const char* path) {
    char* copy = safeStrdup(path != NULL ? path : "data.win");
    for (char* cursor = copy; *cursor != '\0'; ++cursor)
        if (*cursor == '\\') *cursor = '/';
    char* slash = strrchr(copy, '/');
    if (slash != NULL)
        slash[1] = '\0';
    else
        copy[0] = '\0';
    return copy;
}

static void GX_destroyRoomManifest(WiiRenderer* renderer) {
    free(renderer->roomManifest.path);
    free(renderer->roomManifest.entries);
    free(renderer->roomManifest.pageRefs);
    free(renderer->roomManifest.entryByRoomIndex);
    memset(&renderer->roomManifest, 0, sizeof(renderer->roomManifest));
}

static void GX_buildRoomManifestIndex(WiiRenderer* renderer) {
    if (!renderer->roomManifest.loaded || renderer->roomManifest.entries == NULL)
        return;
    free(renderer->roomManifest.entryByRoomIndex);
    renderer->roomManifest.entryByRoomIndex = NULL;
    renderer->roomManifest.entryByRoomIndexCount = 0u;

    DataWin* dataWin = renderer->base.dataWin;
    uint32_t roomCount = dataWin != NULL ? dataWin->room.count : 0u;
    if (roomCount == 0u) {
        for (uint32_t i = 0u; i < renderer->roomManifest.entryCount; ++i) {
            uint32_t roomIndex = renderer->roomManifest.entries[i].roomIndex;
            if (roomIndex >= roomCount) roomCount = roomIndex + 1u;
        }
    }
    if (roomCount == 0u) return;

    renderer->roomManifest.entryByRoomIndex = safeMalloc((size_t)roomCount * sizeof(int32_t));
    renderer->roomManifest.entryByRoomIndexCount = roomCount;
    for (uint32_t i = 0u; i < roomCount; ++i)
        renderer->roomManifest.entryByRoomIndex[i] = -1;
    for (uint32_t i = 0u; i < renderer->roomManifest.entryCount; ++i) {
        uint32_t roomIndex = renderer->roomManifest.entries[i].roomIndex;
        if (roomIndex < roomCount) renderer->roomManifest.entryByRoomIndex[roomIndex] = (int32_t)i;
    }
}

static bool GX_loadRoomManifestFile(
    WiiRenderer* renderer, FILE* file, const char* path) {
    if (file == NULL) return false;
    uint8_t header[32];
    bool ok = fread(header, 1, 32u, file) == 32u;
    uint32_t magic = ok ? GX_readLe32(header + 0u) : 0u;
    uint32_t version = ok ? GX_readLe32(header + 4u) : 0u;
    if (!ok || magic != WII_ROOM_MANIFEST_MAGIC || version != WII_ROOM_MANIFEST_VERSION) {
        return false;
    }

    uint32_t entryCount = GX_readLe32(header + 8u);
    uint32_t pageRefCount = GX_readLe32(header + 12u);

    WiiRoomManifestEntry* entries =
        safeCalloc(entryCount > 0u ? entryCount : 1u, sizeof(WiiRoomManifestEntry));
    WiiRoomManifestPageRef* pageRefs =
        safeCalloc(pageRefCount > 0u ? pageRefCount : 1u, sizeof(WiiRoomManifestPageRef));

    uint32_t entryRowSize = 36u;
    for (uint32_t i = 0u; i < entryCount; ++i) {
        uint8_t row[36];
        if (fread(row, 1, entryRowSize, file) != entryRowSize) {
            ok = false;
            break;
        }
        entries[i].roomIndex = GX_readLe32(row + 0u);
        entries[i].pageStart = GX_readLe32(row + 4u);
        entries[i].pageCount = GX_readLe32(row + 8u);
    }

    for (uint32_t i = 0u; ok && i < pageRefCount; ++i) {
        uint8_t row[4];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) {
            ok = false;
            break;
        }
        pageRefs[i].pageIndex = GX_readLe16(row + 0u);
        pageRefs[i].flags = row[2];
        pageRefs[i].reserved = row[3];
    }

    if (!ok) {
        free(entries);
        free(pageRefs);
        return false;
    }

    GX_destroyRoomManifest(renderer);
    renderer->roomManifest.path = safeStrdup(path);
    renderer->roomManifest.entries = entries;
    renderer->roomManifest.entryCount = entryCount;
    renderer->roomManifest.pageRefs = pageRefs;
    renderer->roomManifest.pageRefCount = pageRefCount;
    renderer->roomManifest.loaded = true;
    GX_buildRoomManifestIndex(renderer);
    return true;
}

static bool GX_loadEmbeddedRoomManifest(WiiRenderer* renderer) {
    if (renderer->atlasPath == NULL || renderer->embeddedRoomManifestOffset == 0u ||
        renderer->embeddedRoomManifestSize < 28u) {
        return true;
    }
    FILE* file = fopen(renderer->atlasPath, "rb");
    if (file == NULL) return false;
    bool ok = fseek(file, (long)renderer->embeddedRoomManifestOffset, SEEK_SET) == 0 &&
        GX_loadRoomManifestFile(renderer, file, renderer->atlasPath);
    fclose(file);
    return ok;
}

static bool GX_loadRoomManifest(WiiRenderer* renderer) {
    return GX_loadEmbeddedRoomManifest(renderer);
}

static void GX_loadAtlasDirectory(
    WiiRenderer* renderer, FILE* file, uint32_t pageDataEnd, uint32_t fileSize) {
    if (file == NULL || pageDataEnd >= fileSize || fileSize < 16u) return;
    uint32_t footerOffset = fileSize - 16u;
    uint8_t footer[16];
    if (fseek(file, (long)footerOffset, SEEK_SET) != 0 ||
        fread(footer, 1, sizeof(footer), file) != sizeof(footer))
        return;
    if (GX_readLe32(footer + 0u) != WII_ATLAS_DIRECTORY_MAGIC ||
        GX_readLe32(footer + 4u) != WII_ATLAS_DIRECTORY_VERSION) {
        return;
    }

    uint32_t blockCount = GX_readLe32(footer + 8u);
    uint32_t directoryOffset = GX_readLe32(footer + 12u);
    uint64_t directorySize = (uint64_t)blockCount * 16u;
    if (blockCount == 0u || blockCount > 32u || directoryOffset < pageDataEnd ||
        directoryOffset > footerOffset ||
        directorySize > (uint64_t)(footerOffset - directoryOffset)) {
        return;
    }

    if (fseek(file, (long)directoryOffset, SEEK_SET) != 0) return;
    for (uint32_t i = 0u; i < blockCount; ++i) {
        uint8_t row[16];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) return;
        uint32_t type = GX_readLe32(row + 0u);
        uint32_t version = GX_readLe32(row + 4u);
        uint32_t offset = GX_readLe32(row + 8u);
        uint32_t size = GX_readLe32(row + 12u);
        if (size == 0u || offset < pageDataEnd || offset > directoryOffset ||
            size > directoryOffset - offset)
            return;

        if (type == WII_ATLAS_BLOCK_TPAG_BINDINGS && version == WII_TPAG_BINDINGS_VERSION) {
            renderer->embeddedTpagBindingOffset = offset;
            renderer->embeddedTpagBindingSize = size;
        } else if (type == WII_ATLAS_BLOCK_ROOM_MANIFEST && version == WII_ROOM_MANIFEST_VERSION) {
            renderer->embeddedRoomManifestOffset = offset;
            renderer->embeddedRoomManifestSize = size;
        }
    }
}

typedef struct {
    uint32_t fileSize;
    uint32_t pageDataBase;
    uint32_t pageDataEnd;
} WiiAtlasLoadInfo;

static bool GX_readAtlasHeader(
    WiiRenderer* renderer, FILE* file, WiiAtlasLoadInfo* info) {
    if (fseek(file, 0, SEEK_END) != 0) return false;
    long fileSize = ftell(file);
    if (fileSize < 28 || fileSize > INT32_MAX) return false;
    rewind(file);

    uint8_t header[28];
    if (fread(header, 1, sizeof(header), file) != sizeof(header) || header[0] == 0x11u ||
        GX_readLe32(header + 0u) != WII_ATLAS_MAGIC ||
        GX_readLe16(header + 4u) != WII_ATLAS_VERSION) {
        return false;
    }
    renderer->pageCount = GX_readLe16(header + 6u);
    renderer->itemCount = GX_readLe32(header + 8u);
    renderer->fragmentCount = GX_readLe32(header + 12u);
    renderer->tileEntryCount = GX_readLe32(header + 16u);
    uint32_t tpagHash = GX_readLe32(header + 24u);
    if (renderer->base.dataWin != NULL &&
        tpagHash != GX_computeTpagHash(renderer->base.dataWin)) {
        return false;
    }

    size_t pageDataBase = 28u + (size_t)renderer->pageCount * 16u +
        (size_t)renderer->itemCount * 12u + (size_t)renderer->fragmentCount * 14u +
        (size_t)renderer->tileEntryCount * 26u;
    if (pageDataBase > (size_t)fileSize) return false;
    info->fileSize = (uint32_t)fileSize;
    info->pageDataBase = (uint32_t)pageDataBase;
    info->pageDataEnd = info->pageDataBase;
    return true;
}

static void GX_allocateAtlasTables(WiiRenderer* renderer, const char* path) {
    renderer->pages = safeCalloc(
        renderer->pageCount > 0u ? renderer->pageCount : 1u, sizeof(WiiTexturePage));
    renderer->items = safeCalloc(
        renderer->itemCount > 0u ? renderer->itemCount : 1u, sizeof(WiiAtlasItem));
    renderer->fragments = safeCalloc(
        renderer->fragmentCount > 0u ? renderer->fragmentCount : 1u,
        sizeof(WiiAtlasFragment));
    renderer->tileEntries = safeCalloc(
        renderer->tileEntryCount > 0u ? renderer->tileEntryCount : 1u,
        sizeof(WiiAtlasTileEntry));
    renderer->roomWantedPages = safeCalloc(
        renderer->pageCount > 0u ? renderer->pageCount : 1u, sizeof(bool));
    renderer->atlasPath = safeStrdup(path);
}

static bool GX_readAtlasPages(
    WiiRenderer* renderer, FILE* file, WiiAtlasLoadInfo* info) {
    for (uint32_t i = 0u; i < renderer->pageCount; ++i) {
        uint8_t row[16];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) return false;
        WiiTexturePage* page = &renderer->pages[i];
        page->width = GX_readLe16(row + 0u);
        page->height = GX_readLe16(row + 2u);
        page->format = GX_readLe32(row + 4u);
        page->dataOffset = GX_readLe32(row + 8u);
        page->dataSize = GX_readLe32(row + 12u);
        if (page->dataOffset < info->pageDataBase || page->dataOffset > info->fileSize ||
            page->dataSize > info->fileSize - page->dataOffset) {
            return false;
        }
        uint32_t pageEnd = page->dataOffset + page->dataSize;
        if (pageEnd > info->pageDataEnd) info->pageDataEnd = pageEnd;
    }
    return true;
}

static bool GX_readAtlasItems(WiiRenderer* renderer, FILE* file) {
    for (uint32_t i = 0u; i < renderer->itemCount; ++i) {
        uint8_t row[12];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) return false;
        renderer->items[i].width = GX_readLe16(row + 0u);
        renderer->items[i].height = GX_readLe16(row + 2u);
        renderer->items[i].fragmentStart = GX_readLe32(row + 4u);
        renderer->items[i].fragmentCount = GX_readLe16(row + 8u);
        renderer->items[i].resizeDivisor = GX_readLe16(row + 10u);
    }
    return true;
}

static bool GX_readAtlasFragments(WiiRenderer* renderer, FILE* file) {
    for (uint32_t i = 0u; i < renderer->fragmentCount; ++i) {
        uint8_t row[14];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) return false;
        renderer->fragments[i].atlasId = GX_readLe16(row + 0u);
        renderer->fragments[i].x = GX_readLe16(row + 2u);
        renderer->fragments[i].y = GX_readLe16(row + 4u);
        renderer->fragments[i].width = GX_readLe16(row + 6u);
        renderer->fragments[i].height = GX_readLe16(row + 8u);
        renderer->fragments[i].sourceX = GX_readLe16(row + 10u);
        renderer->fragments[i].sourceY = GX_readLe16(row + 12u);
    }
    return true;
}

static WiiAtlasTileKey GX_atlasTileKey(
    int16_t bgDef, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH) {
    return (WiiAtlasTileKey){bgDef, srcX, srcY, srcW, srcH};
}

static bool GX_readAtlasTiles(WiiRenderer* renderer, FILE* file) {
    for (uint32_t i = 0u; i < renderer->tileEntryCount; ++i) {
        uint8_t row[26];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) return false;
        renderer->tileEntries[i].bgDef = (int16_t)GX_readLe16(row + 0u);
        renderer->tileEntries[i].srcX = GX_readLe16(row + 2u);
        renderer->tileEntries[i].srcY = GX_readLe16(row + 4u);
        renderer->tileEntries[i].srcW = GX_readLe16(row + 6u);
        renderer->tileEntries[i].srcH = GX_readLe16(row + 8u);
        renderer->tileEntries[i].atlasId = GX_readLe16(row + 10u);
        renderer->tileEntries[i].x = GX_readLe16(row + 12u);
        renderer->tileEntries[i].y = GX_readLe16(row + 14u);
        renderer->tileEntries[i].width = GX_readLe16(row + 16u);
        renderer->tileEntries[i].height = GX_readLe16(row + 18u);
        renderer->tileEntries[i].fragmentStart = GX_readLe32(row + 20u);
        renderer->tileEntries[i].fragmentCount = GX_readLe16(row + 24u);
        const WiiAtlasTileEntry* entry = &renderer->tileEntries[i];
        WiiAtlasTileKey key = GX_atlasTileKey(
            entry->bgDef, entry->srcX, entry->srcY, entry->srcW, entry->srcH);
        if (hmgeti(renderer->tileLookup, key) < 0)
            hmput(renderer->tileLookup, key, i);
    }
    return true;
}

static void GX_cacheAtlas(WiiRenderer* renderer, FILE* file) {
    if (!renderer->cacheAtlas || renderer->atlasFileSize == 0u) return;
    uint8_t* atlasData = GX_allocMem2AtlasData(renderer->atlasFileSize);
    if (atlasData != NULL && fseek(file, 0, SEEK_SET) == 0 &&
        GX_readStreamBytes(file, atlasData, renderer->atlasFileSize) == renderer->atlasFileSize) {
        renderer->atlasData = atlasData;
        renderer->atlasDataSize = renderer->atlasFileSize;
        return;
    }
    GX_freeMem2AtlasData(atlasData);
    renderer->cacheAtlas = false;
}

static void GX_clearAtlas(WiiRenderer* renderer) {
    if (renderer->atlasFile != NULL) fclose(renderer->atlasFile);
    renderer->atlasFile = NULL;
    for (uint32_t i = 0u; i < renderer->pageCount; ++i) {
        uint8_t* pageData = renderer->pages != NULL ? renderer->pages[i].ci8Data : NULL;
        GX_freeMem1TextureData(pageData);
    }
    free(renderer->pages);
    free(renderer->items);
    free(renderer->fragments);
    free(renderer->tileEntries);
    hmfree(renderer->tileLookup);
    free(renderer->roomWantedPages);
    free(renderer->atlasPath);
    GX_freeMem2AtlasData(renderer->atlasData);
    renderer->pages = NULL;
    renderer->items = NULL;
    renderer->fragments = NULL;
    renderer->tileEntries = NULL;
    renderer->roomWantedPages = NULL;
    renderer->atlasPath = NULL;
    renderer->atlasData = NULL;
    renderer->atlasDataSize = 0u;
    renderer->atlasFileSize = 0u;
    renderer->pageCount = 0u;
    renderer->itemCount = 0u;
    renderer->fragmentCount = 0u;
    renderer->tileEntryCount = 0u;
    renderer->embeddedTpagBindingOffset = 0u;
    renderer->embeddedTpagBindingSize = 0u;
    renderer->embeddedRoomManifestOffset = 0u;
    renderer->embeddedRoomManifestSize = 0u;
}

static bool GX_loadAtlas(WiiRenderer* renderer) {
    char* dir = GX_dirnameCopy(renderer->dataWinPath);
    char path[1024];
    snprintf(path, sizeof(path), "%sgfx/atlas.bin", dir);
    free(dir);

    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    setvbuf(file, NULL, _IOFBF, 128u * 1024u);
    WiiAtlasLoadInfo info = {0};
    bool ok = GX_readAtlasHeader(renderer, file, &info);
    if (ok) {
        GX_allocateAtlasTables(renderer, path);
        ok = GX_readAtlasPages(renderer, file, &info) &&
            GX_readAtlasItems(renderer, file) &&
            GX_readAtlasFragments(renderer, file) &&
            GX_readAtlasTiles(renderer, file);
    }
    if (ok) {
        GX_loadAtlasDirectory(renderer, file, info.pageDataEnd, info.fileSize);
        renderer->atlasFileSize = info.fileSize;
        GX_cacheAtlas(renderer, file);
    }
    if (ok && renderer->atlasData == NULL) {
        renderer->atlasFile = file;
    } else {
        fclose(file);
    }
    if (!ok) GX_clearAtlas(renderer);
    return ok;
}

static bool GX_atlasItemIsDrawable(const WiiRenderer* renderer, uint32_t itemIndex) {
    if (itemIndex >= renderer->itemCount) return false;
    const WiiAtlasItem* item = &renderer->items[itemIndex];
    if (item->fragmentCount == 0u || item->fragmentStart >= renderer->fragmentCount) return false;
    for (uint32_t i = 0u; i < item->fragmentCount; ++i) {
        uint32_t fragmentIndex = item->fragmentStart + i;
        if (fragmentIndex >= renderer->fragmentCount) return false;
        if (renderer->fragments[fragmentIndex].atlasId >= renderer->pageCount) return false;
    }
    return true;
}

static void GX_destroyTpagBindings(WiiRenderer* renderer) {
    free(renderer->tpagBindings);
    renderer->tpagBindings = NULL;
    renderer->tpagBindingCount = 0u;
}

static bool GX_loadTpagBindingsFile(
    WiiRenderer* renderer, const DataWin* dataWin, FILE* file) {
    if (dataWin == NULL || file == NULL) return false;
    uint8_t header[16];
    bool ok = fread(header, 1, sizeof(header), file) == sizeof(header);
    uint32_t version = ok ? GX_readLe32(header + 0u) : 0u;
    uint32_t tpagCount = ok ? GX_readLe32(header + 4u) : 0u;
    uint32_t tpagHash = ok ? GX_readLe32(header + 8u) : 0u;
    uint32_t rowSize = ok ? GX_readLe32(header + 12u) : 0u;
    if (!ok || version != WII_TPAG_BINDINGS_VERSION || tpagCount != dataWin->tpag.count ||
        tpagHash != GX_computeTpagHash(dataWin) || rowSize != WII_TPAG_BINDINGS_ROW_SIZE) {
        return false;
    }

    WiiTpagBinding* bindings = safeCalloc(tpagCount > 0u ? tpagCount : 1u, sizeof(WiiTpagBinding));
    for (uint32_t i = 0u; i < tpagCount; ++i) {
        uint8_t row[WII_TPAG_BINDINGS_ROW_SIZE];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) {
            ok = false;
            break;
        }
        WiiTpagBinding* binding = &bindings[i];
        binding->source = row[0];
        binding->flags = row[1];
        binding->subtexIndex = GX_readLe16(row + 2u);
        binding->resizeDivisor = GX_readLe16(row + 4u);
        if (binding->resizeDivisor == 0u) binding->resizeDivisor = 1u;
        binding->atlasItemIndex = GX_readLe32(row + 8u);
        if (binding->source == WII_TPAG_SOURCE_ATLAS) {
            if (!GX_atlasItemIsDrawable(renderer, binding->atlasItemIndex)) {
                ok = false;
                break;
            }
        } else if (binding->source == WII_TPAG_SOURCE_NONE) {
            binding->atlasItemIndex = UINT32_MAX;
        } else {
            binding->source = WII_TPAG_SOURCE_NONE;
            binding->atlasItemIndex = UINT32_MAX;
        }
    }

    if (!ok) {
        free(bindings);
        return false;
    }

    GX_destroyTpagBindings(renderer);
    renderer->tpagBindings = bindings;
    renderer->tpagBindingCount = tpagCount;
    return true;
}

static bool GX_loadEmbeddedTpagBindings(WiiRenderer* renderer, const DataWin* dataWin) {
    if (dataWin == NULL || renderer->atlasPath == NULL ||
        renderer->embeddedTpagBindingOffset == 0u || renderer->embeddedTpagBindingSize < 16u) {
        return false;
    }
    FILE* file = fopen(renderer->atlasPath, "rb");
    if (file == NULL) return false;
    bool ok = fseek(file, (long)renderer->embeddedTpagBindingOffset, SEEK_SET) == 0 &&
        GX_loadTpagBindingsFile(renderer, dataWin, file);
    fclose(file);
    return ok;
}

static bool GX_loadTpagBindings(WiiRenderer* renderer, const DataWin* dataWin) {
    if (dataWin == NULL) return false;
    return GX_loadEmbeddedTpagBindings(renderer, dataWin);
}

static const WiiAtlasItem* GX_resolveAtlasItem(
    const WiiRenderer* renderer, int32_t tpagIndex, uint32_t* outItemIndex) {
    if (outItemIndex != NULL) *outItemIndex = UINT32_MAX;
    if (tpagIndex < 0) return NULL;
    if ((uint32_t)tpagIndex < renderer->tpagBindingCount && renderer->tpagBindings != NULL) {
        const WiiTpagBinding* binding = &renderer->tpagBindings[tpagIndex];
        if (binding->source != WII_TPAG_SOURCE_ATLAS ||
            binding->atlasItemIndex >= renderer->itemCount)
            return NULL;
        if (outItemIndex != NULL) *outItemIndex = binding->atlasItemIndex;
        return &renderer->items[binding->atlasItemIndex];
    }
    if ((uint32_t)tpagIndex >= renderer->itemCount) return NULL;
    if (outItemIndex != NULL) *outItemIndex = (uint32_t)tpagIndex;
    return &renderer->items[tpagIndex];
}

static bool GX_ensureTpagBindingCount(WiiRenderer* renderer, uint32_t count) {
    if (count > renderer->tpagBindingCount) {
        uint32_t oldCount = renderer->tpagBindingCount;
        renderer->tpagBindings =
            safeRealloc(renderer->tpagBindings, (size_t)count * sizeof(WiiTpagBinding));
        for (uint32_t i = oldCount; i < count; ++i) {
            renderer->tpagBindings[i] = (WiiTpagBinding){
                .source = WII_TPAG_SOURCE_NONE,
                .flags = 0u,
                .subtexIndex = 0u,
                .resizeDivisor = 1u,
                .atlasItemIndex = UINT32_MAX,
            };
        }
        renderer->tpagBindingCount = count;
    }
    if (renderer->tpagOwnerSprite != NULL && count > renderer->tpagOwnerSpriteCount) {
        uint32_t oldCount = renderer->tpagOwnerSpriteCount;
        renderer->tpagOwnerSprite =
            safeRealloc(renderer->tpagOwnerSprite, (size_t)count * sizeof(int32_t));
        for (uint32_t i = oldCount; i < count; ++i)
            renderer->tpagOwnerSprite[i] = -1;
        renderer->tpagOwnerSpriteCount = count;
    }
    return true;
}

static uint32_t GX_findOrAllocTpagSlot(WiiRenderer* renderer, DataWin* dataWin) {
    if (dataWin == NULL) return UINT32_MAX;
    for (uint32_t i = renderer->originalTpagCount; i < dataWin->tpag.count; ++i) {
        bool freeBinding = i >= renderer->tpagBindingCount ||
            renderer->tpagBindings[i].source == WII_TPAG_SOURCE_NONE;
        if (dataWin->tpag.items[i].texturePageId == -1 && freeBinding) return i;
    }

    uint32_t newIndex = dataWin->tpag.count++;
    dataWin->tpag.items =
        safeRealloc(dataWin->tpag.items, (size_t)dataWin->tpag.count * sizeof(TexturePageItem));
    memset(&dataWin->tpag.items[newIndex], 0, sizeof(TexturePageItem));
    dataWin->tpag.items[newIndex].texturePageId = -1;
    if (!GX_ensureTpagBindingCount(renderer, dataWin->tpag.count)) return UINT32_MAX;
    return newIndex;
}

static WiiSurface* GX_surfaceForTpag(WiiRenderer* renderer, int32_t tpagIndex) {
    if (tpagIndex < 0 || (uint32_t)tpagIndex >= renderer->tpagBindingCount ||
        renderer->tpagBindings == NULL)
        return NULL;
    WiiTpagBinding* binding = &renderer->tpagBindings[tpagIndex];
    if (binding->source != WII_TPAG_SOURCE_SURFACE ||
        binding->subtexIndex >= renderer->surfaceCount)
        return NULL;
    WiiSurface* surface = &renderer->surfaces[binding->subtexIndex];
    return surface->used ? surface : NULL;
}

static void GX_buildTpagOwners(WiiRenderer* renderer, const DataWin* dataWin) {
    if (dataWin == NULL) return;
    free(renderer->tpagOwnerSprite);
    free(renderer->warmedSprites);
    renderer->tpagOwnerSprite = NULL;
    renderer->tpagOwnerSpriteCount = 0u;
    renderer->warmedSprites = NULL;
    renderer->warmedSpriteCount = 0u;

    renderer->tpagOwnerSprite = safeMalloc(
        dataWin->tpag.count > 0u ? (size_t)dataWin->tpag.count * sizeof(int32_t) : sizeof(int32_t));
    renderer->tpagOwnerSpriteCount = dataWin->tpag.count;
    for (uint32_t i = 0u; i < dataWin->tpag.count; ++i)
        renderer->tpagOwnerSprite[i] = -1;

    for (uint32_t spriteIndex = 0u; spriteIndex < dataWin->sprt.count; ++spriteIndex) {
        const Sprite* sprite = &dataWin->sprt.sprites[spriteIndex];
        for (uint32_t frameIndex = 0u; frameIndex < sprite->textureCount; ++frameIndex) {
            int32_t tpagIndex = sprite->tpagIndices != NULL ? sprite->tpagIndices[frameIndex] : -1;
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= dataWin->tpag.count) continue;
            int32_t* owner = &renderer->tpagOwnerSprite[tpagIndex];
            if (*owner == -1) {
                *owner = (int32_t)spriteIndex;
            } else if (*owner != (int32_t)spriteIndex) {
                *owner = -2;
            }
        }
    }

    renderer->warmedSpriteCount = dataWin->sprt.count;
    renderer->warmedSprites = safeCalloc(
        renderer->warmedSpriteCount > 0u ? renderer->warmedSpriteCount : 1u, sizeof(bool));
}

static void GX_markAtlasItemPages(WiiRenderer* renderer, const WiiAtlasItem* item) {
    if (item == NULL) return;
    for (uint32_t i = 0u; i < item->fragmentCount; ++i) {
        uint32_t fragmentIndex = item->fragmentStart + i;
        if (fragmentIndex >= renderer->fragmentCount) continue;
        uint16_t pageIndex = renderer->fragments[fragmentIndex].atlasId;
        if (pageIndex >= renderer->pageCount) continue;
        if (renderer->roomWantedPages != NULL) renderer->roomWantedPages[pageIndex] = true;
        GX_loadRoomAtlasPage(renderer, pageIndex);
    }
}

static void GX_prewarmTpagSprites(WiiRenderer* renderer, int32_t tpagIndex) {
    if (renderer->base.dataWin == NULL || tpagIndex < 0 || renderer->tpagOwnerSprite == NULL ||
        (uint32_t)tpagIndex >= renderer->base.dataWin->tpag.count) {
        return;
    }
    int32_t spriteIndex = renderer->tpagOwnerSprite[tpagIndex];
    if (spriteIndex < 0 || (uint32_t)spriteIndex >= renderer->base.dataWin->sprt.count ||
        (uint32_t)spriteIndex >= renderer->warmedSpriteCount || renderer->warmedSprites == NULL ||
        renderer->warmedSprites[spriteIndex]) {
        return;
    }

    Sprite* sprite = &renderer->base.dataWin->sprt.sprites[spriteIndex];
    for (uint32_t frameIndex = 0u; frameIndex < sprite->textureCount; ++frameIndex) {
        int32_t frameTpagIndex = sprite->tpagIndices != NULL ? sprite->tpagIndices[frameIndex] : -1;
        const WiiAtlasItem* item = GX_resolveAtlasItem(renderer, frameTpagIndex, NULL);
        GX_markAtlasItemPages(renderer, item);
    }
    renderer->warmedSprites[spriteIndex] = true;
}

static void GX_unloadAtlasPage(WiiRenderer* renderer, uint32_t pageIndex) {
    if (pageIndex >= renderer->pageCount) return;
    WiiTexturePage* page = &renderer->pages[pageIndex];
    if (page->ci8Data == NULL) return;
    uint32_t size = page->ci8DataSize;
    GX_freeMem1TextureData(page->ci8Data);
    page->ci8Data = NULL;
    page->ci8DataSize = 0u;
    page->gxReady = false;
    if (renderer->residentAtlasBytes >= size) {
        renderer->residentAtlasBytes -= size;
    } else {
        renderer->residentAtlasBytes = 0u;
    }
}

static bool GX_evictOldestAtlasPage(WiiRenderer* renderer, uint32_t protectedPage) {
    uint32_t bestIndex = UINT32_MAX;
    uint64_t oldest = UINT64_MAX;
    for (uint32_t i = 0u; i < renderer->pageCount; ++i) {
        WiiTexturePage* page = &renderer->pages[i];
        if (i == protectedPage || page->ci8Data == NULL) continue;
        if (bestIndex == UINT32_MAX || page->lastUse < oldest) {
            bestIndex = i;
            oldest = page->lastUse;
        }
    }
    if (bestIndex == UINT32_MAX) return false;
    GX_flushBatch(renderer);
    GX_DrawDone();
    GX_InvalidateTexAll();
    GX_unloadAtlasPage(renderer, bestIndex);
    return true;
}

static void GX_evictAtlasPages(
    WiiRenderer* renderer, uint32_t neededBytes, uint32_t protectedPage) {
    if (renderer->pageCount == 0u) return;
    uint32_t budget = renderer->atlasTextureCacheBytes != 0u
        ? renderer->atlasTextureCacheBytes
        : WII_ATLAS_PAGE_CACHE_BUDGET_BYTES;
    if (neededBytes > budget) neededBytes = budget;
    while (renderer->residentAtlasBytes > budget - neededBytes) {
        if (!GX_evictOldestAtlasPage(renderer, protectedPage)) break;
    }
}

static bool GX_loadAtlasPage(WiiRenderer* renderer, uint32_t pageIndex) {
    if (renderer->atlasPath == NULL || pageIndex >= renderer->pageCount)
        return false;
    WiiTexturePage* page = &renderer->pages[pageIndex];
    page->lastUse = ++renderer->usageCounter;
    if (page->ci8Data != NULL) return true;
    if (page->loadFailed && page->lastLoadAttemptFrame == renderer->frameCounter) return false;
    bool reportFailure = !page->loadFailed;
    page->lastLoadAttemptFrame = renderer->frameCounter;
    if (page->dataSize == 0u) {
        page->loadFailed = true;
        return false;
    }

    uint32_t decodedSize =
        GX_nativeTextureDataSize(page->format, page->width, page->height);
    GX_evictAtlasPages(renderer, decodedSize, pageIndex);
    const uint8_t* packedBytes = NULL;
    uint8_t* blob = NULL;
    bool ok = GX_atlasRangeInRam(renderer, page->dataOffset, page->dataSize, &packedBytes);
    if (!ok) {
        blob = GX_allocMem2AtlasData(page->dataSize);
        while (blob == NULL && GX_evictOldestAtlasPage(renderer, pageIndex)) {
            blob = GX_allocMem2AtlasData(page->dataSize);
        }
        ok = blob != NULL &&
            GX_readAtlasRange(renderer, page->dataOffset, page->dataSize, blob);
        packedBytes = ok ? blob : NULL;
    }
    ok = packedBytes != NULL && GX_decodePage(renderer, page, packedBytes, page->dataSize);
    GX_freeMem2AtlasData(blob);
    if (!ok) {
        GX_unloadAtlasPage(renderer, pageIndex);
        page->loadFailed = true;
        if (reportFailure)
            logError(
                "Wii renderer: atlas page %u load failed (packed=%u decoded=%u resident=%u).\n",
                pageIndex, page->dataSize, decodedSize, renderer->residentAtlasBytes);
    } else {
        page->loadFailed = false;
        renderer->residentAtlasBytes += page->ci8DataSize;
        GX_evictAtlasPages(renderer, 0u, pageIndex);
    }
    return ok;
}

static bool GX_loadRoomAtlasPage(WiiRenderer* renderer, uint32_t pageIndex) {
    if (pageIndex >= renderer->pageCount) return false;
    if (renderer->roomWantedPages != NULL) renderer->roomWantedPages[pageIndex] = true;
    return GX_loadAtlasPage(renderer, pageIndex);
}

static int GX_compareAtlasPageLoadRef(const void* lhs, const void* rhs) {
    const WiiAtlasPageLoadRef* a = (const WiiAtlasPageLoadRef*)lhs;
    const WiiAtlasPageLoadRef* b = (const WiiAtlasPageLoadRef*)rhs;
    if (a->dataOffset < b->dataOffset) return -1;
    if (a->dataOffset > b->dataOffset) return 1;
    if (a->pageIndex < b->pageIndex) return -1;
    if (a->pageIndex > b->pageIndex) return 1;
    return 0;
}

static const WiiRoomManifestEntry* GX_findRoomManifestEntry(
    const WiiRenderer* renderer, int32_t roomIndex) {
    if (!renderer->roomManifest.loaded || roomIndex < 0) return NULL;
    if (renderer->roomManifest.entryByRoomIndex != NULL &&
        (uint32_t)roomIndex < renderer->roomManifest.entryByRoomIndexCount) {
        int32_t entryIndex = renderer->roomManifest.entryByRoomIndex[roomIndex];
        if (entryIndex >= 0 && (uint32_t)entryIndex < renderer->roomManifest.entryCount) {
            return &renderer->roomManifest.entries[entryIndex];
        }
        return NULL;
    }
    for (uint32_t i = 0u; i < renderer->roomManifest.entryCount; ++i) {
        if ((int32_t)renderer->roomManifest.entries[i].roomIndex == roomIndex)
            return &renderer->roomManifest.entries[i];
    }
    return NULL;
}

void GX_syncRoom(WiiRenderer* renderer, int32_t roomIndex) {
    assert(renderer != NULL);
    if (roomIndex < 0 || renderer->activeRoomIndex == roomIndex) return;
    renderer->activeRoomIndex = roomIndex;
    if (renderer->pages != NULL) {
        for (uint32_t i = 0u; i < renderer->pageCount; ++i)
            renderer->pages[i].loadFailed = false;
    }
    if (renderer->roomWantedPages != NULL)
        memset(renderer->roomWantedPages, 0, sizeof(bool) * renderer->pageCount);
    if (renderer->warmedSprites != NULL)
        memset(renderer->warmedSprites, 0, sizeof(bool) * renderer->warmedSpriteCount);

    const WiiRoomManifestEntry* entry = GX_findRoomManifestEntry(renderer, roomIndex);
    if (entry == NULL || entry->pageCount == 0u ||
        entry->pageStart >= renderer->roomManifest.pageRefCount) {
        GX_evictAtlasPages(renderer, 0u, UINT32_MAX);
        return;
    }

    uint32_t maxRefs = entry->pageCount;
    if (maxRefs > renderer->roomManifest.pageRefCount - entry->pageStart) {
        maxRefs = renderer->roomManifest.pageRefCount - entry->pageStart;
    }

    bool* seen = calloc(renderer->pageCount > 0u ? renderer->pageCount : 1u, sizeof(bool));
    WiiAtlasPageLoadRef* refs = malloc((size_t)maxRefs * sizeof(WiiAtlasPageLoadRef));
    if (seen == NULL || refs == NULL) {
        free(seen);
        free(refs);
        return;
    }

    uint32_t refCount = 0u;
    for (uint32_t i = 0u; i < maxRefs; ++i) {
        uint32_t refIndex = entry->pageStart + i;
        uint16_t pageIndex = renderer->roomManifest.pageRefs[refIndex].pageIndex;
        if (pageIndex >= renderer->pageCount) continue;
        if (renderer->roomWantedPages != NULL) renderer->roomWantedPages[pageIndex] = true;
        if (seen[pageIndex]) continue;
        seen[pageIndex] = true;
        refs[refCount++] = (WiiAtlasPageLoadRef){
            .pageIndex = pageIndex,
            .dataOffset = renderer->pages[pageIndex].dataOffset,
        };
    }

    if (refCount > 1u) qsort(refs, refCount, sizeof(refs[0]), GX_compareAtlasPageLoadRef);
    for (uint32_t i = 0u; i < refCount; ++i) {
        GX_loadAtlasPage(renderer, refs[i].pageIndex);
    }

    free(seen);
    free(refs);
    GX_evictAtlasPages(renderer, 0u, UINT32_MAX);
}

static WiiSurface* GX_getTarget(WiiRenderer* renderer) {
    if (renderer->targetSurfaceId < 0 ||
        (uint32_t)renderer->targetSurfaceId >= renderer->surfaceCount)
        return NULL;
    WiiSurface* surface = &renderer->surfaces[renderer->targetSurfaceId];
    return surface->used ? surface : NULL;
}

static uint16_t GX_alignSurfaceDim(int32_t value) {
    if (value <= 0) return 4u;
    uint32_t v = (uint32_t)value;
    v = (v + 3u) & ~3u;
    return v > UINT16_MAX ? UINT16_MAX : (uint16_t)v;
}

static uint32_t GX_surfaceTextureSize(
    uint16_t width, uint16_t height, uint32_t texFormat) {
    return GX_GetTexBufferSize(width, height, texFormat, GX_FALSE, 0);
}

static void GX_initSurfaceTexture(WiiSurface* surface) {
    if (surface == NULL || surface->pixels == NULL || surface->texWidth == 0u ||
        surface->texHeight == 0u)
        return;
    GX_InitTexObj(&surface->texObj, surface->pixels, surface->texWidth, surface->texHeight,
        surface->texFormat, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjFilterMode(&surface->texObj, GX_NEAR, GX_NEAR);
    surface->textureReady = true;
}

static void GX_syncSurfaceStorage(WiiRenderer* renderer) {
    GX_flushBatch(renderer);
    GX_DrawDone();
    GX_InvalidateTexAll();
}

static bool GX_allocSurfacePixels(
    WiiRenderer* renderer, WiiSurface* surface, int32_t width, int32_t height,
    uint32_t texFormat) {
    if (surface == NULL || width <= 0 || height <= 0) return false;
    uint16_t texWidth = GX_alignSurfaceDim(width);
    uint16_t texHeight = GX_alignSurfaceDim(height);
    uint32_t textureSize = GX_surfaceTextureSize(texWidth, texHeight, texFormat);
    if (textureSize == 0u) return false;
    uint8_t* pixels = memalign(32, textureSize);
    if (pixels == NULL) return false;
    memset(pixels, 0, textureSize);
    DCFlushRange(pixels, textureSize);
    if (surface->pixels != NULL) GX_syncSurfaceStorage(renderer);
    free(surface->pixels);
    surface->width = width;
    surface->height = height;
    surface->texWidth = texWidth;
    surface->texHeight = texHeight;
    surface->texFormat = texFormat;
    surface->textureSize = textureSize;
    surface->pixels = pixels;
    surface->dirty = false;
    surface->hasContents = false;
    GX_initSurfaceTexture(surface);
    return true;
}

static void GX_freeSurfaceStorage(WiiRenderer* renderer, WiiSurface* surface) {
    if (surface == NULL) return;
    if (surface->pixels != NULL) GX_syncSurfaceStorage(renderer);
    free(surface->pixels);
    memset(surface, 0, sizeof(*surface));
}

static uint32_t GX_rgb5a3Offset(const WiiSurface* surface, uint32_t x, uint32_t y) {
    uint32_t tilesX = ((uint32_t)surface->texWidth + 3u) / 4u;
    uint32_t tileBase = ((y / 4u) * tilesX + (x / 4u)) * 32u;
    return tileBase + (((y & 3u) * 4u + (x & 3u)) * 2u);
}

static uint16_t GX_readRGB5A3(const WiiSurface* surface, uint32_t x, uint32_t y) {
    if (surface == NULL || surface->pixels == NULL || x >= (uint32_t)surface->texWidth ||
        y >= (uint32_t)surface->texHeight)
        return 0u;
    uint32_t offset = GX_rgb5a3Offset(surface, x, y);
    return ((uint16_t)surface->pixels[offset] << 8u) | (uint16_t)surface->pixels[offset + 1u];
}

static void GX_unpackRGB5A3(
    uint16_t value, uint8_t* outR, uint8_t* outG, uint8_t* outB, uint8_t* outA);

static uint32_t GX_rgba8Offset(
    const WiiSurface* surface, uint32_t x, uint32_t y, uint32_t planeOffset) {
    uint32_t tilesX = ((uint32_t)surface->texWidth + 3u) / 4u;
    uint32_t tileBase = ((y / 4u) * tilesX + (x / 4u)) * 64u;
    return tileBase + planeOffset + (((y & 3u) * 4u + (x & 3u)) * 2u);
}

static void GX_readSurfaceRGBA(const WiiSurface* surface, uint32_t x, uint32_t y,
    uint8_t* outR, uint8_t* outG, uint8_t* outB, uint8_t* outA) {
    if (surface == NULL || surface->pixels == NULL || x >= (uint32_t)surface->texWidth ||
        y >= (uint32_t)surface->texHeight) {
        if (outR != NULL) *outR = 0u;
        if (outG != NULL) *outG = 0u;
        if (outB != NULL) *outB = 0u;
        if (outA != NULL) *outA = 0u;
        return;
    }
    if (surface->texFormat == GX_TF_RGBA8) {
        uint32_t arOffset = GX_rgba8Offset(surface, x, y, 0u);
        uint32_t gbOffset = GX_rgba8Offset(surface, x, y, 32u);
        if (outA != NULL) *outA = surface->pixels[arOffset + 0u];
        if (outR != NULL) *outR = surface->pixels[arOffset + 1u];
        if (outG != NULL) *outG = surface->pixels[gbOffset + 0u];
        if (outB != NULL) *outB = surface->pixels[gbOffset + 1u];
        return;
    }
    GX_unpackRGB5A3(GX_readRGB5A3(surface, x, y), outR, outG, outB, outA);
}

static void GX_unpackRGB5A3(
    uint16_t value, uint8_t* outR, uint8_t* outG, uint8_t* outB, uint8_t* outA) {
    if ((value & 0x8000u) != 0u) {
        uint8_t r5 = (uint8_t)((value >> 10u) & 0x1Fu);
        uint8_t g5 = (uint8_t)((value >> 5u) & 0x1Fu);
        uint8_t b5 = (uint8_t)(value & 0x1Fu);
        if (outR != NULL) *outR = (uint8_t)((r5 << 3u) | (r5 >> 2u));
        if (outG != NULL) *outG = (uint8_t)((g5 << 3u) | (g5 >> 2u));
        if (outB != NULL) *outB = (uint8_t)((b5 << 3u) | (b5 >> 2u));
        if (outA != NULL) *outA = 255u;
        return;
    }
    uint8_t a3 = (uint8_t)((value >> 12u) & 0x7u);
    uint8_t r4 = (uint8_t)((value >> 8u) & 0xFu);
    uint8_t g4 = (uint8_t)((value >> 4u) & 0xFu);
    uint8_t b4 = (uint8_t)(value & 0xFu);
    if (outR != NULL) *outR = (uint8_t)((r4 << 4u) | r4);
    if (outG != NULL) *outG = (uint8_t)((g4 << 4u) | g4);
    if (outB != NULL) *outB = (uint8_t)((b4 << 4u) | b4);
    if (outA != NULL) *outA = (uint8_t)((a3 * 255u) / 7u);
}

static void GX_flushSurfaceTexture(WiiSurface* surface) {
    if (surface == NULL || surface->pixels == NULL) return;
    if (surface->dirty) {
        DCFlushRange(surface->pixels, surface->textureSize);
        surface->dirty = false;
    }
    if (!surface->textureReady) GX_initSurfaceTexture(surface);
}

static int32_t GX_createSurfaceStorage(
    WiiRenderer* renderer, int32_t width, int32_t height, uint32_t texFormat) {
    if (width <= 0 || height <= 0) return -1;
    for (uint32_t i = 0u; i < renderer->surfaceCount; ++i) {
        if (!renderer->surfaces[i].used) {
            memset(&renderer->surfaces[i], 0, sizeof(renderer->surfaces[i]));
            renderer->surfaces[i].used = true;
            if (!GX_allocSurfacePixels(
                    renderer, &renderer->surfaces[i], width, height, texFormat)) {
                memset(&renderer->surfaces[i], 0, sizeof(renderer->surfaces[i]));
                return -1;
            }
            return (int32_t)i;
        }
    }
    GX_flushBatch(renderer);
    renderer->surfaces =
        safeRealloc(renderer->surfaces, (renderer->surfaceCount + 1u) * sizeof(WiiSurface));
    WiiSurface* surface = &renderer->surfaces[renderer->surfaceCount];
    memset(surface, 0, sizeof(*surface));
    surface->used = true;
    if (!GX_allocSurfacePixels(renderer, surface, width, height, texFormat)) {
        memset(surface, 0, sizeof(*surface));
        return -1;
    }
    return (int32_t)renderer->surfaceCount++;
}

static uint8_t GX_colorR(uint32_t color) {
    return (uint8_t)(color & 0xFFu);
}
static uint8_t GX_colorG(uint32_t color) {
    return (uint8_t)((color >> 8u) & 0xFFu);
}
static uint8_t GX_colorB(uint32_t color) {
    return (uint8_t)((color >> 16u) & 0xFFu);
}

static uint16_t GX_scaleDownHalf16(uint16_t value) {
    return value > 1u ? (uint16_t)((value + 1u) / 2u) : 1u;
}

static bool GX_isHostTarget(WiiRenderer* renderer) {
    if (renderer->targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER ||
        renderer->targetSurfaceId == APPLICATION_SURFACE_ID)
        return true;
    return renderer->targetSurfaceId >= 0 &&
        (uint32_t)renderer->targetSurfaceId < renderer->surfaceCount &&
        renderer->surfaces[renderer->targetSurfaceId].used;
}

static void GX_setProjectionScissor(WiiRenderer* renderer, float left, float top,
    float right, float bottom, float viewportX, float viewportY, float viewportW, float viewportH,
    uint32_t scissorX, uint32_t scissorY, uint32_t scissorW, uint32_t scissorH) {
    WiiRenderState* state = &renderer->renderState;
    state->gxProjectionLeft = left;
    state->gxProjectionTop = top;
    state->gxProjectionRight = right;
    state->gxProjectionBottom = bottom;
    state->gxViewportX = viewportX;
    state->gxViewportY = viewportY;
    state->gxViewportW = viewportW;
    state->gxViewportH = viewportH;
    state->gxScissorX = scissorX;
    state->gxScissorY = scissorY;
    state->gxScissorW = scissorW;
    state->gxScissorH = scissorH;

    Mtx identity;
    guMtxIdentity(identity);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);

    Mtx44 projection;
    guOrtho(projection, top, bottom, left, right, 0.0f, 1.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_SetViewport(viewportX, viewportY, viewportW + 0.05f, viewportH + 0.05f, 0.0f, 1.0f);
    GX_SetScissor(scissorX, scissorY, scissorW, scissorH);
}

static void GX_setGXProjection(WiiRenderer* renderer, float left, float top, float right,
    float bottom, float viewportX, float viewportY, float viewportW, float viewportH) {
    uint32_t scissorX = viewportX > 0.0f ? (uint32_t)viewportX : 0u;
    uint32_t scissorY = viewportY > 0.0f ? (uint32_t)viewportY : 0u;
    uint32_t scissorW = viewportW > 0.0f ? (uint32_t)viewportW : 1u;
    uint32_t scissorH = viewportH > 0.0f ? (uint32_t)viewportH : 1u;
    GX_setProjectionScissor(renderer, left, top, right, bottom, viewportX, viewportY,
        viewportW, viewportH, scissorX, scissorY, scissorW, scissorH);
}

static void GX_updateGmlMatrixState(Renderer* base, const Matrix4f* viewMatrix,
    const Matrix4f* projectionMatrix, Matrix4f* outWorldToClip) {
    if (viewMatrix == NULL || projectionMatrix == NULL) return;
    Matrix4f world = base->gmlMatrices[MATRIX_WORLD];
    Matrix4f worldView;
    Matrix4f_multiply(&worldView, viewMatrix, &world);
    Matrix4f worldToClip;
    Matrix4f_multiply(&worldToClip, projectionMatrix, &worldView);

    base->gmlMatrices[MATRIX_VIEW] = *viewMatrix;
    base->gmlMatrices[MATRIX_PROJECTION] = *projectionMatrix;
    base->gmlMatrices[MATRIX_WORLD_VIEW] = worldView;
    base->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = worldToClip;
    if (outWorldToClip != NULL) *outWorldToClip = worldToClip;
}

static void GX_setViewProjection(
    WiiRenderer* renderer, float viewX, float viewY, float viewW, float viewH, float viewAngle) {
    if (viewW == 0.0f) viewW = 1.0f;
    if (viewH == 0.0f) viewH = 1.0f;

    Matrix4f view;
    Matrix4f projection;
    Matrix4f worldToClip;
    Matrix4f_identity(&view);
    Matrix4f_viewProjection(&projection, viewX, viewY, viewW, viewH, viewAngle);
    GX_updateGmlMatrixState(&renderer->base, &view, &projection, &worldToClip);
}

static float GX_outputX(const WiiRenderer* renderer) {
    return renderer->outputW > 0 ? (float)renderer->outputX : 0.0f;
}

static float GX_outputY(const WiiRenderer* renderer) {
    return renderer->outputH > 0 ? (float)renderer->outputY : 0.0f;
}

static float GX_outputW(const WiiRenderer* renderer) {
    if (renderer->outputW <= 0) {
        return renderer->frameBufferW > 0 ? (float)renderer->frameBufferW
                                          : (float)WII_FRAMEBUFFER_WIDTH;
    }
    return (float)renderer->outputW;
}

static float GX_outputH(const WiiRenderer* renderer) {
    if (renderer->outputH <= 0) {
        return renderer->frameBufferH > 0 ? (float)renderer->frameBufferH
                                          : (float)WII_FRAMEBUFFER_HEIGHT;
    }
    return (float)renderer->outputH;
}

static void GX_setFrameProjection(WiiRenderer* renderer, float width, float height) {
    if (width <= 0.0f) width = (float)WII_FRAMEBUFFER_WIDTH;
    if (height <= 0.0f) height = (float)WII_FRAMEBUFFER_HEIGHT;
    float viewportX = GX_outputX(renderer);
    float viewportY = GX_outputY(renderer);
    float viewportW = GX_outputW(renderer);
    float viewportH = GX_outputH(renderer);
    GX_setGXProjection(
        renderer, 0.0f, 0.0f, width, height, viewportX, viewportY, viewportW, viewportH);
    GX_setViewProjection(renderer, 0.0f, 0.0f, width, height, 0.0f);
}

static void GX_prepareSolidGX(void) {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(
        GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(0);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
}

static void GX_prepareSolidReplace(void) {
    GX_prepareSolidGX();
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
}

static void GX_drawSolidRectGX(
    WiiRenderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha) {
    uint8_t a = alpha >= 1.0f ? 255u : (uint8_t)(alpha <= 0.0f ? 0u : alpha * 255.0f);
    if (a == 0u) return;
    if (x2 < x1) {
        float tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y2 < y1) {
        float tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    GX_enqueueSolidQuad(
        renderer, false, x1, y1, x2, y1, x2, y2, x1, y2, color, color, color, color, a);
}

static void GX_drawSolidRectReplace(
    WiiRenderer* renderer, float x1, float y1, float x2, float y2, uint32_t color, float alpha) {
    uint8_t a = alpha >= 1.0f ? 255u : (uint8_t)(alpha <= 0.0f ? 0u : alpha * 255.0f);
    if (x2 < x1) {
        float tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y2 < y1) {
        float tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    GX_enqueueSolidQuad(
        renderer, true, x1, y1, x2, y1, x2, y2, x1, y2, color, color, color, color, a);
}

static uint8_t GX_alphaToU8(float alpha) {
    return alpha >= 1.0f ? 255u : (uint8_t)(alpha <= 0.0f ? 0u : alpha * 255.0f);
}

static void GX_makeSpritePartQuad(float srcW, float srcH, float x, float y,
    float xscale, float yscale, float angleDeg, float pivotX, float pivotY, float* x1, float* y1,
    float* x2, float* y2, float* x3, float* y3, float* x4, float* y4) {
    float qx1 = x;
    float qy1 = y;
    float qx2 = x + srcW * xscale;
    float qy2 = y;
    float qx3 = qx2;
    float qy3 = y + srcH * yscale;
    float qx4 = x;
    float qy4 = qy3;
    if (fabsf(angleDeg) <= 0.0001f) {
        *x1 = qx1;
        *y1 = qy1;
        *x2 = qx2;
        *y2 = qy2;
        *x3 = qx3;
        *y3 = qy3;
        *x4 = qx4;
        *y4 = qy4;
        return;
    }

    float angleRad = -angleDeg * (float)M_PI / 180.0f;
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);
    float dx = qx1 - pivotX;
    float dy = qy1 - pivotY;
    *x1 = cosA * dx - sinA * dy + pivotX;
    *y1 = sinA * dx + cosA * dy + pivotY;
    dx = qx2 - pivotX;
    dy = qy2 - pivotY;
    *x2 = cosA * dx - sinA * dy + pivotX;
    *y2 = sinA * dx + cosA * dy + pivotY;
    dx = qx3 - pivotX;
    dy = qy3 - pivotY;
    *x3 = cosA * dx - sinA * dy + pivotX;
    *y3 = sinA * dx + cosA * dy + pivotY;
    dx = qx4 - pivotX;
    dy = qy4 - pivotY;
    *x4 = cosA * dx - sinA * dy + pivotX;
    *y4 = sinA * dx + cosA * dy + pivotY;
}

static void GX_projectPoint(WiiRenderer* renderer, float* x, float* y) {
    WiiRenderState* state = &renderer->renderState;
    if (x == NULL || y == NULL || !state->activeProjectionUsesMatrix ||
        state->activeProjectionTargetSurfaceId != renderer->targetSurfaceId) {
        return;
    }

    float tx;
    float ty;
    Matrix4f_transformPoint(&state->activeProjectionWorldToPixel, *x, *y, &tx, &ty);
    *x = tx;
    *y = ty;
}

static void GX_emitSolidVertex(
    WiiRenderer* renderer, float x, float y, uint32_t color, uint8_t alpha) {
    GX_projectPoint(renderer, &x, &y);
    GX_Position3f32(x, y, 0.0f);
    GX_Color4u8(GX_colorR(color), GX_colorG(color), GX_colorB(color), alpha);
}

static void GX_drawSolidTriangle(WiiRenderer* renderer, float x1, float y1, float x2,
    float y2, float x3, float y3, uint32_t c1, uint32_t c2, uint32_t c3, float alpha) {
    uint8_t a = GX_alphaToU8(alpha);
    if (a == 0u) return;
    GX_flushBatch(renderer);
    GX_prepareSolidGX();
    GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
    GX_emitSolidVertex(renderer, x1, y1, c1, a);
    GX_emitSolidVertex(renderer, x2, y2, c2, a);
    GX_emitSolidVertex(renderer, x3, y3, c3, a);
    GX_End();
}

static void GX_prepareTextured(WiiRenderer* renderer, WiiTexturePage* page) {
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(
        GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    if (page->format == WII_NATIVE_TEXFMT_CI4 || page->format == WII_NATIVE_TEXFMT_CI8)
        GX_LoadTlut(&page->tlutObj, GX_TLUT0);
    u8 filter = renderer->base.texFilter ? GX_LINEAR : GX_NEAR;
    GX_InitTexObjFilterMode(&page->texObj, filter, filter);
    GX_LoadTexObj(&page->texObj, GX_TEXMAP0);
}

static void GX_drawCi8Quad(WiiRenderer* renderer, WiiTexturePage* page,
    int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH, float x1, float y1, float x2, float y2,
    float x3, float y3, float x4, float y4, bool flipX, bool flipY, uint32_t color, float alpha) {
    if (page == NULL || !page->gxReady || srcW <= 0 || srcH <= 0 || alpha <= 0.0f) return;

    float u0 = (float)srcX / (float)page->paddedWidth;
    float v0 = (float)srcY / (float)page->paddedHeight;
    float u1 = (float)(srcX + srcW) / (float)page->paddedWidth;
    float v1 = (float)(srcY + srcH) / (float)page->paddedHeight;
    if (flipX) {
        float tmp = u0;
        u0 = u1;
        u1 = tmp;
    }
    if (flipY) {
        float tmp = v0;
        v0 = v1;
        v1 = tmp;
    }

    uint8_t a = alpha >= 1.0f ? 255u : (uint8_t)(alpha * 255.0f);
    if (a == 0u) return;
    uint8_t r = GX_colorR(color);
    uint8_t g = GX_colorG(color);
    uint8_t b = GX_colorB(color);
    WiiRenderBatchVertex* v =
        GX_reserveBatchQuad(renderer, WII_RENDER_BATCH_TEXTURE_PAGE, page, true, false);
    if (v == NULL) return;
    GX_setBatchVertex(renderer, &v[0], x1, y1, u0, v0, r, g, b, a);
    GX_setBatchVertex(renderer, &v[1], x2, y2, u1, v0, r, g, b, a);
    GX_setBatchVertex(renderer, &v[2], x3, y3, u1, v1, r, g, b, a);
    GX_setBatchVertex(renderer, &v[3], x4, y4, u0, v1, r, g, b, a);
}

static void GX_drawCi8QuadGX(WiiRenderer* renderer, WiiTexturePage* page, int32_t srcX,
    int32_t srcY, int32_t srcW, int32_t srcH, float x, float y, float dstW, float dstH, bool flipX,
    bool flipY, uint32_t color, float alpha) {
    if (dstW == 0.0f || dstH == 0.0f) return;
    if (dstW < 0.0f) {
        x += dstW;
        dstW = -dstW;
    }
    if (dstH < 0.0f) {
        y += dstH;
        dstH = -dstH;
    }
    GX_drawCi8Quad(renderer, page, srcX, srcY, srcW, srcH, x, y, x + dstW, y,
        x + dstW, y + dstH, x, y + dstH, flipX, flipY, color, alpha);
}

static void GX_prepareSurfaceTexture(
    WiiRenderer* renderer, WiiSurface* surface, bool blend) {
    if (surface == NULL || surface->pixels == NULL) return;
    GX_flushSurfaceTexture(surface);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(
        GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, blend ? GX_MODULATE : GX_REPLACE);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(
        blend ? GX_BM_BLEND : GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    u8 filter = renderer->base.texFilter ? GX_LINEAR : GX_NEAR;
    GX_InitTexObjFilterMode(&surface->texObj, filter, filter);
    GX_LoadTexObj(&surface->texObj, GX_TEXMAP0);
}

static void GX_emitBatchVertex(const WiiRenderBatchVertex* vertex) {
    GX_Position3f32(vertex->x, vertex->y, 0.0f);
    GX_Color4u8(vertex->r, vertex->g, vertex->b, vertex->a);
}

static void GX_emitTexturedVertex(const WiiRenderBatchVertex* vertex) {
    GX_Position3f32(vertex->x, vertex->y, 0.0f);
    GX_Color4u8(vertex->r, vertex->g, vertex->b, vertex->a);
    GX_TexCoord2f32(vertex->u, vertex->v);
}

static void GX_flushBatch(WiiRenderer* renderer) {
    if (renderer->batch.type == WII_RENDER_BATCH_NONE ||
        renderer->batch.quadCount == 0u)
        return;
    uint32_t vertexCount = renderer->batch.quadCount * 4u;
    switch (renderer->batch.type) {
    case WII_RENDER_BATCH_SOLID:
        if (renderer->batch.replace)
            GX_prepareSolidReplace();
        else
            GX_prepareSolidGX();
        GX_Begin(GX_QUADS, GX_VTXFMT0, vertexCount);
        for (uint32_t i = 0u; i < vertexCount; ++i)
            GX_emitBatchVertex(&renderer->batch.vertices[i]);
        GX_End();
        break;
    case WII_RENDER_BATCH_TEXTURE_PAGE: {
        WiiTexturePage* page = (WiiTexturePage*)renderer->batch.texture;
        if (page != NULL && page->gxReady) {
            GX_prepareTextured(renderer, page);
            GX_Begin(GX_QUADS, GX_VTXFMT0, vertexCount);
            for (uint32_t i = 0u; i < vertexCount; ++i)
                GX_emitTexturedVertex(&renderer->batch.vertices[i]);
            GX_End();
        }
        break;
    }
    case WII_RENDER_BATCH_SURFACE: {
        WiiSurface* surface = (WiiSurface*)renderer->batch.texture;
        if (surface != NULL && surface->pixels != NULL) {
            GX_prepareSurfaceTexture(renderer, surface, renderer->batch.blend);
            GX_Begin(GX_QUADS, GX_VTXFMT0, vertexCount);
            for (uint32_t i = 0u; i < vertexCount; ++i)
                GX_emitTexturedVertex(&renderer->batch.vertices[i]);
            GX_End();
        }
        break;
    }
    default:
        break;
    }
    renderer->batch.type = WII_RENDER_BATCH_NONE;
    renderer->batch.blend = false;
    renderer->batch.replace = false;
    renderer->batch.texture = NULL;
    renderer->batch.quadCount = 0u;
}

static bool GX_batchMatches(
    WiiRenderer* renderer, WiiRenderBatchType type, void* texture, bool blend, bool replace) {
    return renderer->batch.type == type && renderer->batch.texture == texture &&
        renderer->batch.blend == blend && renderer->batch.replace == replace;
}

static WiiRenderBatchVertex* GX_reserveBatchQuad(
    WiiRenderer* renderer, WiiRenderBatchType type, void* texture, bool blend, bool replace) {
    if (!GX_batchMatches(renderer, type, texture, blend, replace) ||
        renderer->batch.quadCount >= WII_RENDER_BATCH_MAX_QUADS) {
        GX_flushBatch(renderer);
        renderer->batch.type = type;
        renderer->batch.texture = texture;
        renderer->batch.blend = blend;
        renderer->batch.replace = replace;
        renderer->batch.quadCount = 0u;
    }
    WiiRenderBatchVertex* vertices = &renderer->batch.vertices[renderer->batch.quadCount * 4u];
    renderer->batch.quadCount++;
    return vertices;
}

static void GX_setBatchVertex(WiiRenderer* renderer, WiiRenderBatchVertex* vertex, float x,
    float y, float u, float v, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    GX_projectPoint(renderer, &x, &y);
    vertex->x = x;
    vertex->y = y;
    vertex->u = u;
    vertex->v = v;
    vertex->r = r;
    vertex->g = g;
    vertex->b = b;
    vertex->a = a;
}

static void GX_enqueueSolidQuad(WiiRenderer* renderer, bool replace, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, uint32_t c1, uint32_t c2,
    uint32_t c3, uint32_t c4, uint8_t alpha) {
    if (alpha == 0u && !replace) return;
    WiiRenderBatchVertex* v =
        GX_reserveBatchQuad(renderer, WII_RENDER_BATCH_SOLID, NULL, true, replace);
    if (v == NULL) return;
    GX_setBatchVertex(
        renderer, &v[0], x1, y1, 0.0f, 0.0f, GX_colorR(c1), GX_colorG(c1), GX_colorB(c1), alpha);
    GX_setBatchVertex(
        renderer, &v[1], x2, y2, 0.0f, 0.0f, GX_colorR(c2), GX_colorG(c2), GX_colorB(c2), alpha);
    GX_setBatchVertex(
        renderer, &v[2], x3, y3, 0.0f, 0.0f, GX_colorR(c3), GX_colorG(c3), GX_colorB(c3), alpha);
    GX_setBatchVertex(
        renderer, &v[3], x4, y4, 0.0f, 0.0f, GX_colorR(c4), GX_colorG(c4), GX_colorB(c4), alpha);
}

static void GX_drawSurfaceQuad(WiiRenderer* renderer, WiiSurface* surface,
    int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, uint32_t color, float alpha,
    bool blend) {
    if (surface == NULL || surface->pixels == NULL || srcWidth <= 0 || srcHeight <= 0 ||
        alpha <= 0.0f)
        return;
    GX_flushSurfaceTexture(surface);
    float u0 = (float)srcLeft / (float)surface->texWidth;
    float v0 = (float)srcTop / (float)surface->texHeight;
    float u1 = (float)(srcLeft + srcWidth) / (float)surface->texWidth;
    float v1 = (float)(srcTop + srcHeight) / (float)surface->texHeight;
    uint8_t a = GX_alphaToU8(alpha);
    if (a == 0u) return;
    uint8_t r = GX_colorR(color);
    uint8_t g = GX_colorG(color);
    uint8_t b = GX_colorB(color);
    WiiRenderBatchVertex* v =
        GX_reserveBatchQuad(renderer, WII_RENDER_BATCH_SURFACE, surface, blend, false);
    if (v == NULL) return;
    GX_setBatchVertex(renderer, &v[0], x1, y1, u0, v0, r, g, b, a);
    GX_setBatchVertex(renderer, &v[1], x2, y2, u1, v0, r, g, b, a);
    GX_setBatchVertex(renderer, &v[2], x3, y3, u1, v1, r, g, b, a);
    GX_setBatchVertex(renderer, &v[3], x4, y4, u0, v1, r, g, b, a);
}

static void GX_drawSurfaceRect(WiiRenderer* renderer, WiiSurface* surface,
    int32_t srcLeft, int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y,
    float dstW, float dstH, uint32_t color, float alpha, bool blend) {
    if (dstW == 0.0f || dstH == 0.0f) return;
    if (dstW < 0.0f) {
        x += dstW;
        dstW = -dstW;
    }
    if (dstH < 0.0f) {
        y += dstH;
        dstH = -dstH;
    }
    GX_drawSurfaceQuad(renderer, surface, srcLeft, srcTop, srcWidth, srcHeight, x, y,
        x + dstW, y, x + dstW, y + dstH, x, y + dstH, color, alpha, blend);
}

static void GX_setActiveProjection(WiiRenderer* renderer, int32_t viewX, int32_t viewY,
    float scaleX, float scaleY, float offsetX, float offsetY) {
    WiiRenderState* state = &renderer->renderState;
    state->activeViewX = viewX;
    state->activeViewY = viewY;
    state->activeViewScaleX = scaleX != 0.0f ? scaleX : 1.0f;
    state->activeViewScaleY = scaleY != 0.0f ? scaleY : 1.0f;
    state->activeViewOffsetX = offsetX;
    state->activeViewOffsetY = offsetY;
    state->activeProjectionUsesMatrix = false;
    state->activeProjectionTargetSurfaceId = renderer->targetSurfaceId;
}

static bool GX_setClipProjection(WiiRenderer* renderer,
    const Matrix4f* worldToClip, float viewportX, float viewportY, float viewportW,
    float viewportH) {
    if (worldToClip == NULL || viewportW <= 0.0f || viewportH <= 0.0f)
        return false;
    if (!Matrix4f_isAffine2D(worldToClip)) return false;

    Matrix4f clipToPort;
    Matrix4f_identity(&clipToPort);
    clipToPort.m[0] = viewportW * 0.5f;
    clipToPort.m[5] = viewportH * -0.5f;
    clipToPort.m[12] = viewportX + viewportW * 0.5f;
    clipToPort.m[13] = viewportY + viewportH * 0.5f;

    Matrix4f screenTransform;
    Matrix4f_multiply(&screenTransform, &clipToPort, worldToClip);
    if (!Matrix4f_isAffine2D(&screenTransform)) return false;

    WiiSurface* target = GX_getTarget(renderer);
    float targetW = target != NULL
        ? (float)target->width
        : (renderer->frameBufferW > 0
                ? (float)renderer->frameBufferW
                : (float)WII_FRAMEBUFFER_WIDTH);
    float targetH = target != NULL
        ? (float)target->height
        : (renderer->frameBufferH > 0
                ? (float)renderer->frameBufferH
                : (float)WII_FRAMEBUFFER_HEIGHT);
    if (targetW <= 0.0f) targetW = 1.0f;
    if (targetH <= 0.0f) targetH = 1.0f;

    uint32_t scissorX = viewportX > 0.0f ? (uint32_t)floorf(viewportX) : 0u;
    uint32_t scissorY = viewportY > 0.0f ? (uint32_t)floorf(viewportY) : 0u;
    uint32_t scissorW = viewportW > 0.0f ? (uint32_t)ceilf(viewportW) : 1u;
    uint32_t scissorH = viewportH > 0.0f ? (uint32_t)ceilf(viewportH) : 1u;
    GX_setProjectionScissor(renderer, 0.0f, 0.0f, targetW, targetH, 0.0f, 0.0f, targetW,
        targetH, scissorX, scissorY, scissorW, scissorH);

    renderer->renderState.activeProjectionWorldToPixel = screenTransform;
    renderer->renderState.activeProjectionUsesMatrix = true;
    renderer->renderState.activeProjectionTargetSurfaceId = renderer->targetSurfaceId;
    return true;
}

static void GX_captureRenderState(WiiRenderer* renderer, WiiRenderStateSnapshot* state) {
    if (state == NULL) return;
    state->targetSurfaceId = renderer->targetSurfaceId;
    state->cportX = renderer->base.CPortX;
    state->cportY = renderer->base.CPortY;
    state->cportW = renderer->base.CPortW;
    state->cportH = renderer->base.CPortH;
    state->renderState = renderer->renderState;
    memcpy(state->gmlMatrices, renderer->base.gmlMatrices, sizeof(state->gmlMatrices));
    state->cameraCurrent = renderer->base.cameraCurrent;
}

static bool GX_surfaceIdIsLive(WiiRenderer* renderer, int32_t surfaceID) {
    return surfaceID >= 0 && (uint32_t)surfaceID < renderer->surfaceCount &&
        renderer->surfaces[surfaceID].used;
}

static bool GX_targetIsSurface(WiiRenderer* renderer) {
    return GX_surfaceIdIsLive(renderer, renderer->targetSurfaceId);
}

static void GX_setEfbFormatForTarget(WiiRenderer* renderer, const WiiSurface* surface) {
    (void)renderer;
    (void)surface;
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
}

static void GX_finishSurfaceTarget(WiiRenderer* renderer) {
    WiiSurface* surface = GX_getTarget(renderer);
    if (surface == NULL || surface->pixels == NULL) return;
    GX_flushBatch(renderer);
    GX_SetTexCopySrc(0, 0, surface->width, surface->height);
    GX_SetTexCopyDst(surface->width, surface->height, surface->texFormat, GX_FALSE);
    GX_CopyTex(surface->pixels, GX_FALSE);
    GX_PixModeSync();
    GX_DrawDone();
    DCInvalidateRange(surface->pixels, surface->textureSize);
    GX_initSurfaceTexture(surface);
    surface->dirty = false;
    surface->hasContents = true;
}

static bool GX_bindHostTarget(WiiRenderer* renderer) {
    GX_flushBatch(renderer);
    if (GX_targetIsSurface(renderer)) GX_finishSurfaceTarget(renderer);
    renderer->targetSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    GX_setEfbFormatForTarget(renderer, NULL);
    GX_setFrameProjection(
        renderer, (float)renderer->frameGameW, (float)renderer->frameGameH);
    float viewportW = GX_outputW(renderer);
    float viewportH = GX_outputH(renderer);
    GX_setActiveProjection(renderer, 0, 0,
        renderer->frameGameW > 0 ? viewportW / (float)renderer->frameGameW : 1.0f,
        renderer->frameGameH > 0 ? viewportH / (float)renderer->frameGameH : 1.0f,
        GX_outputX(renderer), GX_outputY(renderer));
    renderer->base.CPortX = (int32_t)GX_outputX(renderer);
    renderer->base.CPortY = (int32_t)GX_outputY(renderer);
    renderer->base.CPortW = (int32_t)viewportW;
    renderer->base.CPortH = (int32_t)viewportH;
    return true;
}

static bool GX_bindSurfaceTarget(WiiRenderer* renderer, int32_t surfaceID) {
    if (!GX_surfaceIdIsLive(renderer, surfaceID)) return false;
    GX_flushBatch(renderer);
    if (renderer->targetSurfaceId == surfaceID) {
        WiiSurface* surface = &renderer->surfaces[surfaceID];
        GX_setEfbFormatForTarget(renderer, surface);
        GX_setGXProjection(renderer, 0.0f, 0.0f, (float)surface->width,
            (float)surface->height, 0.0f, 0.0f, (float)surface->width, (float)surface->height);
        GX_setViewProjection(
            renderer, 0.0f, 0.0f, (float)surface->width, (float)surface->height, 0.0f);
        GX_setActiveProjection(renderer, 0, 0, 1.0f, 1.0f, 0.0f, 0.0f);
        renderer->base.CPortX = 0;
        renderer->base.CPortY = 0;
        renderer->base.CPortW = surface->width;
        renderer->base.CPortH = surface->height;
        return true;
    }
    if (GX_targetIsSurface(renderer)) GX_finishSurfaceTarget(renderer);
    renderer->targetSurfaceId = surfaceID;
    WiiSurface* surface = &renderer->surfaces[surfaceID];
    GX_setEfbFormatForTarget(renderer, surface);
    GX_setGXProjection(renderer, 0.0f, 0.0f, (float)surface->width, (float)surface->height,
        0.0f, 0.0f, (float)surface->width, (float)surface->height);
    GX_setViewProjection(
        renderer, 0.0f, 0.0f, (float)surface->width, (float)surface->height, 0.0f);
    GX_setActiveProjection(renderer, 0, 0, 1.0f, 1.0f, 0.0f, 0.0f);
    renderer->base.CPortX = 0;
    renderer->base.CPortY = 0;
    renderer->base.CPortW = surface->width;
    renderer->base.CPortH = surface->height;
    if (surface->hasContents) {
        GX_drawSurfaceRect(renderer, surface, 0, 0, surface->width, surface->height,
            0.0f, 0.0f, (float)surface->width, (float)surface->height, 0xFFFFFFu, 1.0f, false);
    } else {
        GX_drawSolidRectReplace(
            renderer, 0.0f, 0.0f, (float)surface->width, (float)surface->height, 0x000000u, 0.0f);
    }
    return true;
}

static void GX_restoreRenderState(
    WiiRenderer* renderer, const WiiRenderStateSnapshot* state) {
    if (state == NULL) return;
    if (state->targetSurfaceId >= 0 &&
        GX_surfaceIdIsLive(renderer, state->targetSurfaceId)) {
        (void)GX_bindSurfaceTarget(renderer, state->targetSurfaceId);
    } else {
        (void)GX_bindHostTarget(renderer);
    }

    GX_flushBatch(renderer);
    renderer->base.CPortX = state->cportX;
    renderer->base.CPortY = state->cportY;
    renderer->base.CPortW = state->cportW;
    renderer->base.CPortH = state->cportH;
    renderer->renderState = state->renderState;
    memcpy(renderer->base.gmlMatrices, state->gmlMatrices, sizeof(state->gmlMatrices));
    renderer->base.cameraCurrent = state->cameraCurrent;

    const WiiRenderState* renderState = &state->renderState;
    GX_setProjectionScissor(renderer, renderState->gxProjectionLeft,
        renderState->gxProjectionTop, renderState->gxProjectionRight,
        renderState->gxProjectionBottom, renderState->gxViewportX, renderState->gxViewportY,
        renderState->gxViewportW, renderState->gxViewportH, renderState->gxScissorX,
        renderState->gxScissorY, renderState->gxScissorW, renderState->gxScissorH);
}

static bool GX_nextClippedAtlasFragment(WiiRenderer* renderer,
    const WiiAtlasItem* item, uint32_t* offset, int32_t srcX, int32_t srcY, int32_t srcW,
    int32_t srcH, WiiAtlasFragmentClip* clip) {
    uint16_t divisor = item->resizeDivisor > 1u ? item->resizeDivisor : 1u;
    while (*offset < item->fragmentCount) {
        uint32_t fragmentIndex = item->fragmentStart + (*offset)++;
        if (fragmentIndex >= renderer->fragmentCount) continue;
        const WiiAtlasFragment* frag = &renderer->fragments[fragmentIndex];
        if (frag->atlasId >= renderer->pageCount) continue;

        int32_t left = srcX > (int32_t)frag->sourceX ? srcX : (int32_t)frag->sourceX;
        int32_t top = srcY > (int32_t)frag->sourceY ? srcY : (int32_t)frag->sourceY;
        int32_t right = srcX + srcW < (int32_t)frag->sourceX + frag->width
            ? srcX + srcW
            : (int32_t)frag->sourceX + frag->width;
        int32_t bottom = srcY + srcH < (int32_t)frag->sourceY + frag->height
            ? srcY + srcH
            : (int32_t)frag->sourceY + frag->height;
        if (right <= left || bottom <= top) continue;

        int32_t logicalLeft = left - (int32_t)frag->sourceX;
        int32_t logicalTop = top - (int32_t)frag->sourceY;
        int32_t logicalRight = right - (int32_t)frag->sourceX;
        int32_t logicalBottom = bottom - (int32_t)frag->sourceY;
        int32_t textureX = frag->x + logicalLeft;
        int32_t textureY = frag->y + logicalTop;
        int32_t textureWidth = right - left;
        int32_t textureHeight = bottom - top;
        if (divisor == 2u) {
            int32_t physicalWidth = (int32_t)GX_scaleDownHalf16(frag->width);
            int32_t physicalHeight = (int32_t)GX_scaleDownHalf16(frag->height);
            int32_t physicalLeft = (int32_t)((int64_t)logicalLeft * physicalWidth / frag->width);
            int32_t physicalTop = (int32_t)((int64_t)logicalTop * physicalHeight / frag->height);
            int32_t physicalRight = (int32_t)(((int64_t)logicalRight * physicalWidth +
                frag->width - 1) / frag->width);
            int32_t physicalBottom = (int32_t)(((int64_t)logicalBottom * physicalHeight +
                frag->height - 1) / frag->height);
            textureX = frag->x + physicalLeft;
            textureY = frag->y + physicalTop;
            textureWidth = physicalRight - physicalLeft;
            textureHeight = physicalBottom - physicalTop;
        }
        if (!GX_loadRoomAtlasPage(renderer, frag->atlasId)) continue;

        *clip = (WiiAtlasFragmentClip){
            .page = &renderer->pages[frag->atlasId],
            .sourceLeft = left,
            .sourceTop = top,
            .sourceRight = right,
            .sourceBottom = bottom,
            .textureX = textureX,
            .textureY = textureY,
            .textureWidth = textureWidth,
            .textureHeight = textureHeight,
        };
        return true;
    }
    return false;
}

static void GX_interpolateQuad(
    const WiiQuad* quad, float s, float t, float* x, float* y) {
    *x = (1.0f - s) * (1.0f - t) * quad->x1 + s * (1.0f - t) * quad->x2 +
        s * t * quad->x3 + (1.0f - s) * t * quad->x4;
    *y = (1.0f - s) * (1.0f - t) * quad->y1 + s * (1.0f - t) * quad->y2 +
        s * t * quad->y3 + (1.0f - s) * t * quad->y4;
}

static void GX_drawAtlasPart(WiiRenderer* renderer, int32_t tpagIndex, int32_t srcOffX,
    int32_t srcOffY, int32_t srcW, int32_t srcH, float x, float y, float xscale, float yscale,
    uint32_t color, float alpha) {
    if (!GX_isHostTarget(renderer)) return;
    GX_prewarmTpagSprites(renderer, tpagIndex);
    const WiiAtlasItem* item = GX_resolveAtlasItem(renderer, tpagIndex, NULL);
    if (item == NULL) return;
    uint32_t offset = 0u;
    WiiAtlasFragmentClip clip;
    while (GX_nextClippedAtlasFragment(
        renderer, item, &offset, srcOffX, srcOffY, srcW, srcH, &clip)) {
        float dstX = x + (float)(clip.sourceLeft - srcOffX) * xscale;
        float dstY = y + (float)(clip.sourceTop - srcOffY) * yscale;
        GX_drawCi8QuadGX(renderer, clip.page, clip.textureX, clip.textureY,
            clip.textureWidth, clip.textureHeight, dstX, dstY,
            (float)(clip.sourceRight - clip.sourceLeft) * xscale,
            (float)(clip.sourceBottom - clip.sourceTop) * yscale, xscale < 0.0f, yscale < 0.0f,
            color, alpha);
    }
}

static void GX_drawTransformedAtlasPart(WiiRenderer* renderer, int32_t tpagIndex,
    int32_t srcOffX, int32_t srcOffY, int32_t srcW, int32_t srcH, const Matrix4f* transform,
    float dstLeft, float dstTop, float dstRight, float dstBottom, uint32_t color, float alpha) {
    if (transform == NULL || !GX_isHostTarget(renderer)) return;
    if (srcW <= 0 || srcH <= 0 || dstRight == dstLeft || dstBottom == dstTop || alpha <= 0.0f)
        return;

    GX_prewarmTpagSprites(renderer, tpagIndex);
    const WiiAtlasItem* item = GX_resolveAtlasItem(renderer, tpagIndex, NULL);
    if (item == NULL) return;
    float dstW = dstRight - dstLeft;
    float dstH = dstBottom - dstTop;

    uint32_t offset = 0u;
    WiiAtlasFragmentClip clip;
    while (GX_nextClippedAtlasFragment(
        renderer, item, &offset, srcOffX, srcOffY, srcW, srcH, &clip)) {
        float localLeft =
            dstLeft + ((float)(clip.sourceLeft - srcOffX) / (float)srcW) * dstW;
        float localTop = dstTop + ((float)(clip.sourceTop - srcOffY) / (float)srcH) * dstH;
        float localRight =
            dstLeft + ((float)(clip.sourceRight - srcOffX) / (float)srcW) * dstW;
        float localBottom =
            dstTop + ((float)(clip.sourceBottom - srcOffY) / (float)srcH) * dstH;
        float x1, y1, x2, y2, x3, y3, x4, y4;
        Matrix4f_transformPoint(transform, localLeft, localTop, &x1, &y1);
        Matrix4f_transformPoint(transform, localRight, localTop, &x2, &y2);
        Matrix4f_transformPoint(transform, localRight, localBottom, &x3, &y3);
        Matrix4f_transformPoint(transform, localLeft, localBottom, &x4, &y4);
        GX_drawCi8Quad(renderer, clip.page, clip.textureX, clip.textureY,
            clip.textureWidth, clip.textureHeight, x1, y1, x2, y2, x3, y3, x4, y4, false, false,
            color, alpha);
    }
}

static void GX_drawAtlasPartQuad(WiiRenderer* renderer, int32_t tpagIndex, int32_t srcOffX,
    int32_t srcOffY, int32_t srcW, int32_t srcH, float quadX1, float quadY1, float quadX2,
    float quadY2, float quadX3, float quadY3, float quadX4, float quadY4, uint32_t color,
    float alpha) {
    if (!GX_isHostTarget(renderer)) return;
    if (srcW <= 0 || srcH <= 0 || alpha <= 0.0f) return;

    GX_prewarmTpagSprites(renderer, tpagIndex);
    const WiiAtlasItem* item = GX_resolveAtlasItem(renderer, tpagIndex, NULL);
    if (item == NULL) return;

    WiiQuad quad = {quadX1, quadY1, quadX2, quadY2, quadX3, quadY3, quadX4, quadY4};
    uint32_t offset = 0u;
    WiiAtlasFragmentClip clip;
    while (GX_nextClippedAtlasFragment(
        renderer, item, &offset, srcOffX, srcOffY, srcW, srcH, &clip)) {
        float s0 = (float)(clip.sourceLeft - srcOffX) / (float)srcW;
        float t0 = (float)(clip.sourceTop - srcOffY) / (float)srcH;
        float s1 = (float)(clip.sourceRight - srcOffX) / (float)srcW;
        float t1 = (float)(clip.sourceBottom - srcOffY) / (float)srcH;
        float x1, y1, x2, y2, x3, y3, x4, y4;
        GX_interpolateQuad(&quad, s0, t0, &x1, &y1);
        GX_interpolateQuad(&quad, s1, t0, &x2, &y2);
        GX_interpolateQuad(&quad, s1, t1, &x3, &y3);
        GX_interpolateQuad(&quad, s0, t1, &x4, &y4);
        GX_drawCi8Quad(renderer, clip.page, clip.textureX, clip.textureY,
            clip.textureWidth, clip.textureHeight, x1, y1, x2, y2, x3, y3, x4, y4, false, false,
            color, alpha);
    }
}

static void GX_init(Renderer* base, DataWin* dataWin) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    base->dataWin = dataWin;
    base->drawColor = 0xFFFFFFu;
    base->drawAlpha = 1.0f;
    base->drawFont = -1;
    base->circlePrecision = 24;
    renderer->originalSpriteCount = dataWin != NULL ? dataWin->sprt.count : 0u;
    renderer->originalTpagCount = dataWin != NULL ? dataWin->tpag.count : 0u;
    renderer->clearR = 0;
    renderer->clearG = 0;
    renderer->clearB = 0;
    GX_loadAtlas(renderer);
    if (!GX_loadTpagBindings(renderer, dataWin)) {
        logError("Wii renderer: missing embedded TPAG bindings; regenerate preprocessed assets.\n");
    }
    GX_buildTpagOwners(renderer, dataWin);
    GX_loadRoomManifest(renderer);
}

static void GX_destroy(Renderer* base) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    GX_syncSurfaceStorage(renderer);
    for (uint32_t i = 0u; i < renderer->surfaceCount; ++i)
        free(renderer->surfaces[i].pixels);
    GX_clearAtlas(renderer);
    GX_destroyTpagBindings(renderer);
    free(renderer->tpagOwnerSprite);
    free(renderer->warmedSprites);
    GX_destroyRoomManifest(renderer);
    free(renderer->surfaces);
    arrfree(renderer->targetStack);
    free(renderer->dataWinPath);
    free(renderer);
}

static void GX_clearScreen(Renderer* base, uint32_t color, float alpha) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    GX_flushBatch(renderer);
    WiiSurface* target = GX_getTarget(renderer);
    float width = target != NULL ? (float)target->width : (float)renderer->frameGameW;
    float height = target != NULL ? (float)target->height : (float)renderer->frameGameH;
    if (target != NULL)
        GX_drawSolidRectReplace(renderer, 0.0f, 0.0f, width, height, color, alpha);
    else
        GX_drawSolidRectGX(renderer, 0.0f, 0.0f, width, height, color, alpha);
}

static void GX_beginFrame(
    Renderer* base, int32_t gameW, int32_t gameH, int32_t windowW, int32_t windowH) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    renderer->frameCounter++;
    renderer->frameGameW = gameW > 0 ? gameW : WII_FRAMEBUFFER_WIDTH;
    renderer->frameGameH = gameH > 0 ? gameH : WII_FRAMEBUFFER_HEIGHT;
    renderer->frameBufferW = windowW > 0 ? windowW : WII_FRAMEBUFFER_WIDTH;
    renderer->frameBufferH = windowH > 0 ? windowH : WII_FRAMEBUFFER_HEIGHT;
    if (GX_surfaceIdIsLive(renderer, renderer->applicationSurfaceId)) {
        GX_bindSurfaceTarget(renderer, renderer->applicationSurfaceId);
    } else {
        GX_bindHostTarget(renderer);
    }
    GX_clearScreen(base,
        (uint32_t)renderer->clearR | ((uint32_t)renderer->clearG << 8u) |
            ((uint32_t)renderer->clearB << 16u),
        1.0f);
}

static void GX_endFrameNoop(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
}
static void GX_endFrameInit(Renderer* base) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t appSurfaceId = renderer->applicationSurfaceId;
    if (!GX_surfaceIdIsLive(renderer, appSurfaceId)) {
        GX_bindHostTarget(renderer);
        return;
    }
    if (base->runner != NULL && !base->runner->appSurfaceAutoDraw) {
        GX_bindHostTarget(renderer);
        return;
    }
    if (GX_targetIsSurface(renderer)) GX_finishSurfaceTarget(renderer);
    renderer->targetSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    GX_setEfbFormatForTarget(renderer, NULL);
    float fbW =
        renderer->frameBufferW > 0 ? (float)renderer->frameBufferW : (float)WII_FRAMEBUFFER_WIDTH;
    float fbH =
        renderer->frameBufferH > 0 ? (float)renderer->frameBufferH : (float)WII_FRAMEBUFFER_HEIGHT;
    float outputX = GX_outputX(renderer);
    float outputY = GX_outputY(renderer);
    float outputW = GX_outputW(renderer);
    float outputH = GX_outputH(renderer);
    GX_setGXProjection(renderer, 0.0f, 0.0f, fbW, fbH, 0.0f, 0.0f, fbW, fbH);
    GX_setViewProjection(renderer, 0.0f, 0.0f, fbW, fbH, 0.0f);
    GX_setActiveProjection(renderer, 0, 0, 1.0f, 1.0f, 0.0f, 0.0f);
    WiiSurface* appSurface = &renderer->surfaces[appSurfaceId];
    GX_drawSolidRectReplace(renderer, 0.0f, 0.0f, fbW, fbH, 0x000000u, 1.0f);
    bool filteredOutput = outputX > 0.0f || outputY > 0.0f || outputW < fbW || outputH < fbH;
    bool texFilter = renderer->base.texFilter;
    renderer->base.texFilter = filteredOutput;
    GX_drawSurfaceRect(renderer, appSurface, 0, 0, appSurface->width, appSurface->height,
        outputX, outputY, outputW, outputH, 0xFFFFFFu, 1.0f, false);
    GX_flushBatch(renderer);
    renderer->base.texFilter = texFilter;
}
static void GX_beginView(Renderer* base, int32_t viewX, int32_t viewY, int32_t viewW,
    int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (!GX_isHostTarget(renderer) || renderer->frameGameW <= 0 ||
        renderer->frameGameH <= 0)
        return;
    GX_flushBatch(renderer);
    (void)GX_currentViewCamera(renderer);
    WiiSurface* target = GX_getTarget(renderer);
    float targetX = target != NULL ? 0.0f : GX_outputX(renderer);
    float targetY = target != NULL ? 0.0f : GX_outputY(renderer);
    float targetW = target != NULL ? (float)target->width : GX_outputW(renderer);
    float targetH = target != NULL ? (float)target->height : GX_outputH(renderer);
    float scaleX = targetW / (float)renderer->frameGameW;
    float scaleY = targetH / (float)renderer->frameGameH;
    float portPixelX = targetX + (float)portX * scaleX;
    float portPixelY = targetY + (float)portY * scaleY;
    float portPixelW = (float)portW * scaleX;
    float portPixelH = (float)portH * scaleY;
    float viewportX = portPixelX;
    float viewportY = portPixelY;
    float viewportW = portPixelW;
    float viewportH = portPixelH;
    float viewScaleX = viewW > 0 ? viewportW / (float)viewW : 1.0f;
    float viewScaleY = viewH > 0 ? viewportH / (float)viewH : 1.0f;
    if (viewScaleX <= 0.0f) viewScaleX = 1.0f;
    if (viewScaleY <= 0.0f) viewScaleY = 1.0f;
    uint32_t scissorX = portPixelX > 0.0f ? (uint32_t)floorf(portPixelX) : 0u;
    uint32_t scissorY = portPixelY > 0.0f ? (uint32_t)floorf(portPixelY) : 0u;
    uint32_t scissorW = portPixelW > 0.0f ? (uint32_t)ceilf(portPixelW) : 1u;
    uint32_t scissorH = portPixelH > 0.0f ? (uint32_t)ceilf(portPixelH) : 1u;
    base->CPortX = (int32_t)viewportX;
    base->CPortY = (int32_t)viewportY;
    base->CPortW = (int32_t)viewportW;
    base->CPortH = (int32_t)viewportH;

    GX_setViewProjection(
        renderer, (float)viewX, (float)viewY, (float)viewW, (float)viewH, viewAngle);
    GX_setActiveProjection(
        renderer, viewX, viewY, viewScaleX, viewScaleY, viewportX, viewportY);
    if (!GX_setClipProjection(renderer,
        &base->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION], viewportX, viewportY, viewportW,
        viewportH)) {
        GX_setProjectionScissor(renderer, (float)viewX, (float)viewY,
            (float)(viewX + viewW), (float)(viewY + viewH), viewportX, viewportY, viewportW,
            viewportH, scissorX, scissorY, scissorW, scissorH);
        GX_setActiveProjection(
            renderer, viewX, viewY, viewScaleX, viewScaleY, viewportX, viewportY);
    }
}
static void GX_applyProjection(
    Renderer* base, const Matrix4f* viewMatrix, const Matrix4f* projectionMatrix) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (viewMatrix == NULL || projectionMatrix == NULL) return;
    GMLCamera* camera = GX_findProjectionCamera(renderer, viewMatrix, projectionMatrix);

    GX_flushBatch(renderer);
    float viewportX = (float)base->CPortX;
    float viewportY = (float)base->CPortY;
    float viewportW = base->CPortW > 0 ? (float)base->CPortW : GX_outputW(renderer);
    float viewportH = base->CPortH > 0 ? (float)base->CPortH : GX_outputH(renderer);
    if (viewportW <= 0.0f) viewportW = 1.0f;
    if (viewportH <= 0.0f) viewportH = 1.0f;

    Matrix4f worldToClip = {0};
    GX_updateGmlMatrixState(base, viewMatrix, projectionMatrix, &worldToClip);
    if (camera != NULL && camera->viewWidth > 0 && camera->viewHeight > 0 &&
        GX_cameraIsSimple(camera)) {
        GX_setActiveProjection(renderer, (int32_t)floorf(camera->viewX),
            (int32_t)floorf(camera->viewY), viewportW / (float)camera->viewWidth,
            viewportH / (float)camera->viewHeight, viewportX, viewportY);
    }
    if (GX_setClipProjection(
            renderer, &worldToClip, viewportX, viewportY, viewportW, viewportH))
        return;
    renderer->renderState.activeProjectionUsesMatrix = false;
    if (camera == NULL || camera->viewWidth <= 0 || camera->viewHeight <= 0) return;

    if (camera->viewWidth <= 0 || camera->viewHeight <= 0) return;
    GX_setGXProjection(renderer, camera->viewX, camera->viewY,
        camera->viewX + (float)camera->viewWidth, camera->viewY + (float)camera->viewHeight,
        viewportX, viewportY, viewportW, viewportH);
    GX_setViewProjection(renderer, camera->viewX, camera->viewY, (float)camera->viewWidth,
        (float)camera->viewHeight, camera->viewAngle);
    GX_setActiveProjection(renderer, (int32_t)floorf(camera->viewX),
        (int32_t)floorf(camera->viewY), viewportW / (float)camera->viewWidth,
        viewportH / (float)camera->viewHeight, viewportX, viewportY);
}
static void GX_beginGUI(Renderer* base, int32_t guiW, int32_t guiH,
    MAYBE_UNUSED int32_t portX, MAYBE_UNUSED int32_t portY, int32_t portW, int32_t portH,
    int32_t targetSurfaceId) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (targetSurfaceId >= 0) {
        if (!GX_bindSurfaceTarget(renderer, targetSurfaceId)) return;
    } else {
        if (!GX_bindHostTarget(renderer)) return;
    }
    if (!GX_isHostTarget(renderer) || guiW <= 0 || guiH <= 0 || portW <= 0 || portH <= 0)
        return;
    GX_flushBatch(renderer);
    WiiSurface* target = GX_getTarget(renderer);
    float viewportX = target != NULL ? 0.0f : GX_outputX(renderer);
    float viewportY = target != NULL ? 0.0f : GX_outputY(renderer);
    float viewportW = target != NULL ? (float)target->width : GX_outputW(renderer);
    float viewportH = target != NULL ? (float)target->height : GX_outputH(renderer);
    GX_setGXProjection(
        renderer, 0.0f, 0.0f, (float)guiW, (float)guiH, viewportX, viewportY, viewportW, viewportH);
    GX_setViewProjection(renderer, 0.0f, 0.0f, (float)guiW, (float)guiH, 0.0f);
    GX_setActiveProjection(renderer, 0, 0, guiW > 0 ? viewportW / (float)guiW : 1.0f,
        guiH > 0 ? viewportH / (float)guiH : 1.0f, viewportX, viewportY);
}
static void GX_setGuiProjection(Renderer* base, int32_t guiW, int32_t guiH, int32_t portW,
    int32_t portH, bool renderingToUserSurface) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t targetSurfaceId =
        renderingToUserSurface ? renderer->targetSurfaceId : RENDER_TARGET_HOST_FRAMEBUFFER;
    GX_beginGUI(base, guiW, guiH, 0, 0, portW, portH, targetSurfaceId);
}

static void GX_drawSprite(Renderer* base, int32_t tpagIndex, float x, float y,
    float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color,
    float alpha) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (base->dataWin == NULL || tpagIndex < 0 ||
        (uint32_t)tpagIndex >= base->dataWin->tpag.count)
        return;
    TexturePageItem* tpag = &base->dataWin->tpag.items[tpagIndex];
    if (tpag->sourceWidth == 0u || tpag->sourceHeight == 0u || xscale == 0.0f || yscale == 0.0f ||
        alpha <= 0.0f)
        return;

    float localLeft = (float)tpag->targetX - originX;
    float localTop = (float)tpag->targetY - originY;
    float localRight = localLeft + (float)tpag->sourceWidth;
    float localBottom = localTop + (float)tpag->sourceHeight;
    WiiSurface* surface = GX_surfaceForTpag(renderer, tpagIndex);
    if (surface != NULL) {
        Matrix4f transform;
        Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, -angleDeg * (float)M_PI / 180.0f);
        float x1, y1, x2, y2, x3, y3, x4, y4;
        Matrix4f_transformPoint(&transform, localLeft, localTop, &x1, &y1);
        Matrix4f_transformPoint(&transform, localRight, localTop, &x2, &y2);
        Matrix4f_transformPoint(&transform, localRight, localBottom, &x3, &y3);
        Matrix4f_transformPoint(&transform, localLeft, localBottom, &x4, &y4);
        GX_drawSurfaceQuad(renderer, surface, 0, 0, surface->width, surface->height, x1,
            y1, x2, y2, x3, y3, x4, y4, color, alpha, true);
        return;
    }

    if (GX_isHostTarget(renderer)) {
        Matrix4f transform;
        Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, -angleDeg * (float)M_PI / 180.0f);
        GX_drawTransformedAtlasPart(renderer, tpagIndex, 0, 0, tpag->sourceWidth,
            tpag->sourceHeight, &transform, localLeft, localTop, localRight, localBottom, color,
            alpha);
        return;
    }

    GX_drawAtlasPart(renderer, tpagIndex, 0, 0, tpag->sourceWidth, tpag->sourceHeight,
        x + localLeft * xscale, y + localTop * yscale, xscale, yscale, color, alpha);
}

static const WiiAtlasTileEntry* GX_findTilemapAtlasEntry(WiiRenderer* renderer,
    int32_t tpagIndex, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH);
static bool GX_drawAtlasTileQuad(WiiRenderer* renderer,
    const WiiAtlasTileEntry* entry, float quadX1, float quadY1, float quadX2, float quadY2,
    float quadX3, float quadY3, float quadX4, float quadY4, uint32_t color, float alpha);

static void GX_drawSpritePart(Renderer* base, int32_t tpagIndex, float srcOffX,
    float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale,
    float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (srcW <= 0 || srcH <= 0 || xscale == 0.0f || yscale == 0.0f || alpha <= 0.0f)
        return;
    float x1, y1, x2, y2, x3, y3, x4, y4;
    GX_makeSpritePartQuad(srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY, &x1,
        &y1, &x2, &y2, &x3, &y3, &x4, &y4);
    WiiSurface* surface = GX_surfaceForTpag(renderer, tpagIndex);
    if (surface != NULL) {
        TexturePageItem* tpag = base->dataWin != NULL && tpagIndex >= 0 &&
                                        (uint32_t)tpagIndex < base->dataWin->tpag.count
                                    ? &base->dataWin->tpag.items[tpagIndex]
                                    : NULL;
        int32_t logicalW =
            tpag != NULL && tpag->sourceWidth > 0u ? (int32_t)tpag->sourceWidth : surface->width;
        int32_t logicalH =
            tpag != NULL && tpag->sourceHeight > 0u ? (int32_t)tpag->sourceHeight : surface->height;
        if (srcOffX < 0) {
            srcW += srcOffX;
            srcOffX = 0;
        }
        if (srcOffY < 0) {
            srcH += srcOffY;
            srcOffY = 0;
        }
        if (srcOffX >= logicalW || srcOffY >= logicalH || srcW <= 0 || srcH <= 0) return;
        if (srcOffX + srcW > logicalW) srcW = logicalW - srcOffX;
        if (srcOffY + srcH > logicalH) srcH = logicalH - srcOffY;
        float texScaleX = logicalW > 0 ? (float)surface->width / (float)logicalW : 1.0f;
        float texScaleY = logicalH > 0 ? (float)surface->height / (float)logicalH : 1.0f;
        int32_t physX0 = (int32_t)floorf((float)srcOffX * texScaleX);
        int32_t physY0 = (int32_t)floorf((float)srcOffY * texScaleY);
        int32_t physX1 = (int32_t)ceilf((float)(srcOffX + srcW) * texScaleX);
        int32_t physY1 = (int32_t)ceilf((float)(srcOffY + srcH) * texScaleY);
        if (physX0 < 0) physX0 = 0;
        if (physY0 < 0) physY0 = 0;
        if (physX1 > surface->width) physX1 = surface->width;
        if (physY1 > surface->height) physY1 = surface->height;
        int32_t physW = physX1 - physX0;
        int32_t physH = physY1 - physY0;
        if (physW <= 0 || physH <= 0) return;
        GX_drawSurfaceQuad(renderer, surface, physX0, physY0, physW, physH, x1, y1, x2,
            y2, x3, y3, x4, y4, color, alpha, true);
        return;
    }

    if (GX_isHostTarget(renderer)) {
        const WiiAtlasTileEntry* tileEntry =
            GX_findTilemapAtlasEntry(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH);
        if (tileEntry != NULL &&
            GX_drawAtlasTileQuad(
                renderer, tileEntry, x1, y1, x2, y2, x3, y3, x4, y4, color, alpha))
            return;
        GX_drawAtlasPartQuad(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH, x1, y1, x2,
            y2, x3, y3, x4, y4, color, alpha);
        return;
    }

    GX_drawAtlasPart(renderer, tpagIndex, srcOffX, srcOffY, srcW, srcH,
        x - pivotX * xscale, y - pivotY * yscale, xscale, yscale, color, alpha);
}

static void GX_drawSpritePartColor(Renderer* base, int32_t tpagIndex, float srcOffX,
    float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale,
    float angleDeg, float pivotX, float pivotY, uint32_t c1, MAYBE_UNUSED uint32_t c2,
    MAYBE_UNUSED uint32_t c3, MAYBE_UNUSED uint32_t c4, float alpha) {
    assert(base != NULL);
    GX_drawSpritePart(base, tpagIndex, srcOffX, srcOffY, srcW, srcH, x, y, xscale, yscale,
        angleDeg, pivotX, pivotY, c1, alpha);
}

static void GX_drawSpritePos(Renderer* base, int32_t tpagIndex, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (base->dataWin == NULL || tpagIndex < 0 ||
        (uint32_t)tpagIndex >= base->dataWin->tpag.count)
        return;
    TexturePageItem* tpag = &base->dataWin->tpag.items[tpagIndex];
    if (tpag->sourceWidth == 0u || tpag->sourceHeight == 0u || alpha <= 0.0f) return;
    WiiSurface* surface = GX_surfaceForTpag(renderer, tpagIndex);
    if (surface != NULL) {
        GX_drawSurfaceQuad(renderer, surface, 0, 0, surface->width, surface->height, x1,
            y1, x2, y2, x3, y3, x4, y4, 0xFFFFFFu, alpha, true);
        return;
    }
    if (GX_isHostTarget(renderer)) {
        GX_drawAtlasPartQuad(renderer, tpagIndex, 0, 0, tpag->sourceWidth,
            tpag->sourceHeight, x1, y1, x2, y2, x3, y3, x4, y4, 0xFFFFFFu, alpha);
    }
}

static void GX_drawRectangleColor(Renderer* base, float x1, float y1, float x2, float y2,
    uint32_t c1, MAYBE_UNUSED uint32_t c2, MAYBE_UNUSED uint32_t c3, MAYBE_UNUSED uint32_t c4,
    float alpha, bool outline) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (!GX_isHostTarget(renderer)) return;
    if (outline) {
        GX_drawSolidRectGX(renderer, x1, y1, x2, y1 + 1.0f, c1, alpha);
        GX_drawSolidRectGX(renderer, x1, y2 - 1.0f, x2, y2, c1, alpha);
        GX_drawSolidRectGX(renderer, x1, y1, x1 + 1.0f, y2, c1, alpha);
        GX_drawSolidRectGX(renderer, x2 - 1.0f, y1, x2, y2, c1, alpha);
    } else {
        GX_drawSolidRectGX(renderer, x1, y1, x2, y2, c1, alpha);
    }
}

static void GX_drawRectangle(Renderer* renderer, float x1, float y1, float x2, float y2,
    uint32_t color, float alpha, bool outline) {
    assert(renderer != NULL);
    GX_drawRectangleColor(
        renderer, x1, y1, x2, y2, color, color, color, color, alpha, outline);
}

static void GX_drawLineColor(Renderer* base, float x1, float y1, float x2, float y2,
    float width, uint32_t color1, uint32_t color2, float alpha) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (!GX_isHostTarget(renderer) || alpha <= 0.0f) return;
    float dx = x2 - x1;
    float dy = y2 - y1;
    float lenSq = dx * dx + dy * dy;
    if (lenSq < 0.000001f) {
        float half = fmaxf(width, 1.0f) * 0.5f;
        GX_drawSolidRectGX(
            renderer, x1 - half, y1 - half, x1 + half, y1 + half, color1, alpha);
        return;
    }
    float len = sqrtf(lenSq);
    float half = fmaxf(width, 1.0f) * 0.5f;
    float nx = -dy / len * half;
    float ny = dx / len * half;
    uint8_t a = GX_alphaToU8(alpha);
    if (a == 0u) return;
    GX_enqueueSolidQuad(renderer, false, x1 - nx, y1 - ny, x2 - nx, y2 - ny, x2 + nx,
        y2 + ny, x1 + nx, y1 + ny, color1, color2, color2, color1, a);
}

static void GX_drawLine(Renderer* renderer, float x1, float y1, float x2, float y2,
    float width, uint32_t color, float alpha) {
    assert(renderer != NULL);
    GX_drawLineColor(renderer, x1, y1, x2, y2, width, color, color, alpha);
}

static void GX_drawTriangle(Renderer* base, float x1, float y1, float x2, float y2,
    float x3, float y3, uint32_t color1, uint32_t color2, uint32_t color3, float alpha,
    bool outline) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (!GX_isHostTarget(renderer)) return;
    if (outline) {
        base->vtable->drawLineColor(base, x1, y1, x2, y2, 1.0f, color1, color2, alpha);
        base->vtable->drawLineColor(base, x2, y2, x3, y3, 1.0f, color2, color3, alpha);
        base->vtable->drawLineColor(base, x3, y3, x1, y1, 1.0f, color3, color1, alpha);
        return;
    }
    GX_drawSolidTriangle(
        renderer, x1, y1, x2, y2, x3, y3, color1, color2, color3, alpha);
}

typedef struct {
    Font* font;
    TexturePageItem* fontTpag;
    Sprite* spriteFontSprite;
} WiiFontState;

static bool GX_resolveFontState(
    WiiRenderer* renderer, DataWin* dataWin, Font* font, WiiFontState* state) {
    if (dataWin == NULL || font == NULL || state == NULL) return false;
    state->font = font;
    state->fontTpag = NULL;
    state->spriteFontSprite = NULL;

    if (!font->isSpriteFont) {
        int32_t tpagIndex = font->tpagIndex;
        if (tpagIndex < 0 || (uint32_t)tpagIndex >= dataWin->tpag.count) return false;
        TexturePageItem* tpag = &dataWin->tpag.items[tpagIndex];
        if (tpag->texturePageId < 0 || (uint32_t)tpag->texturePageId >= renderer->pageCount)
            return false;
        state->fontTpag = tpag;
    } else if (font->spriteIndex >= 0 && (uint32_t)font->spriteIndex < dataWin->sprt.count) {
        state->spriteFontSprite = &dataWin->sprt.sprites[font->spriteIndex];
    } else {
        return false;
    }

    return true;
}

static bool GX_drawGlyph(WiiRenderer* renderer, DataWin* dataWin, WiiFontState* state,
    FontGlyph* glyph, float cursorX, float cursorY, float originX, float originY, float scaleX,
    float scaleY, const Matrix4f* transform, uint32_t color, float alpha) {
    if (dataWin == NULL || state == NULL || glyph == NULL || glyph->sourceWidth == 0u ||
        glyph->sourceHeight == 0u)
        return false;

    Font* font = state->font;
    int32_t tpagIndex = -1;
    int32_t srcX = 0;
    int32_t srcY = 0;
    int32_t srcW = (int32_t)glyph->sourceWidth;
    int32_t srcH = (int32_t)glyph->sourceHeight;
    float localX = cursorX + (float)glyph->offset;
    float localY = cursorY;

    if (font->isSpriteFont && state->spriteFontSprite != NULL) {
        Sprite* sprite = state->spriteFontSprite;
        int32_t glyphIndex = (int32_t)(glyph - font->glyphs);
        if (glyphIndex < 0 || glyphIndex >= (int32_t)sprite->textureCount) return false;
        tpagIndex = sprite->tpagIndices[glyphIndex];
        if (tpagIndex < 0 || (uint32_t)tpagIndex >= dataWin->tpag.count) return false;
        TexturePageItem* glyphTpag = &dataWin->tpag.items[tpagIndex];
        srcW = (int32_t)glyphTpag->sourceWidth;
        srcH = (int32_t)glyphTpag->sourceHeight;
        localY = cursorY + (float)(int32_t)glyphTpag->targetY - (float)font->spriteOriginYAdjust;
    } else if (state->fontTpag != NULL) {
        tpagIndex = font->tpagIndex;
        srcX = (int32_t)glyph->sourceX;
        srcY = (int32_t)glyph->sourceY;
    } else {
        return false;
    }

    if (transform != NULL && GX_isHostTarget(renderer)) {
        GX_drawTransformedAtlasPart(renderer, tpagIndex, srcX, srcY, srcW, srcH, transform,
            localX, localY, localX + (float)srcW, localY + (float)srcH, color, alpha);
    } else {
        GX_drawAtlasPart(renderer, tpagIndex, srcX, srcY, srcW, srcH,
            originX + localX * scaleX, originY + localY * scaleY, scaleX, scaleY, color, alpha);
    }
    return true;
}

static void GX_drawTextInternal(Renderer* base, const char* text, float x, float y,
    float xscale, float yscale, float angleDeg, uint32_t color, float alpha, float lineSeparation) {
    if (text == NULL || alpha <= 0.0f) return;
    WiiRenderer* renderer = (WiiRenderer*)base;
    DataWin* dataWin = base->dataWin;
    if (dataWin == NULL) return;

    int32_t fontIndex = base->drawFont;
    if (fontIndex < 0 || (uint32_t)fontIndex >= dataWin->font.count) return;
    Font* font = &dataWin->font.fonts[fontIndex];

    WiiFontState state;
    if (!GX_resolveFontState(renderer, dataWin, font, &state)) return;

    int32_t textLen = (int32_t)strlen(text);
    if (textLen == 0) return;

    int32_t lineCount = TextUtils_countLines(text, textLen);
    float lineStride = (lineSeparation < 0.0f)
        ? TextUtils_lineStride(font)
        : (lineSeparation / (font->scaleY != 0.0f ? font->scaleY : 1.0f));
    float totalHeight = (float)lineCount * lineStride;
    float valignOffset = 0.0f;
    if (base->drawValign == 1)
        valignOffset = -totalHeight * 0.5f;
    else if (base->drawValign == 2)
        valignOffset = -totalHeight;

    float scaleX = xscale * font->scaleX;
    float scaleY = yscale * font->scaleY;
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, scaleX, scaleY, -angleDeg * (float)M_PI / 180.0f);
    const Matrix4f* glyphTransform = GX_isHostTarget(renderer) ? &transform : NULL;
    float cursorY = valignOffset - (float)font->ascenderOffset;
    int32_t lineStart = 0;

    for (int32_t lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
        int32_t lineEnd = lineStart;
        while (lineEnd < textLen && !TextUtils_isNewlineChar(text[lineEnd]))
            lineEnd++;
        int32_t lineLen = lineEnd - lineStart;

        float lineWidth = (base->drawHalign == 1 || base->drawHalign == 2)
            ? TextUtils_measureLineWidth(font, text + lineStart, lineLen) : 0.0f;
        float halignOffset = 0.0f;
        if (base->drawHalign == 1)
            halignOffset = -lineWidth * 0.5f;
        else if (base->drawHalign == 2)
            halignOffset = -lineWidth;

        float cursorX = halignOffset;
        int32_t pos = 0;
        bool hasCh = pos < lineLen;
        uint16_t ch = hasCh ? TextUtils_decodeUtf8(text + lineStart, lineLen, &pos) : 0u;

        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            uint16_t nextCh = 0u;
            bool hasNext = pos < lineLen;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);

            if (glyph != NULL) {
                bool drew = GX_drawGlyph(renderer, dataWin, &state, glyph, cursorX,
                    cursorY, x, y, scaleX, scaleY, glyphTransform, color, alpha);
                cursorX += (float)glyph->shift;
                if (drew && hasNext) cursorX += TextUtils_getKerningOffset(glyph, nextCh);
            }

            ch = nextCh;
            hasCh = hasNext;
        }

        cursorY += lineStride;
        lineStart = lineEnd < textLen ? TextUtils_skipNewline(text, lineEnd, textLen) : lineEnd;
    }
}

static void GX_drawText(Renderer* renderer, const char* text, float x, float y,
    float xscale, float yscale, float angleDeg, float lineSeparation) {
    assert(renderer != NULL);
    GX_drawTextInternal(renderer, text, x, y, xscale, yscale, angleDeg,
        renderer->drawColor, renderer->drawAlpha, lineSeparation);
}

static void GX_drawTextColor(Renderer* renderer, const char* text, float x, float y,
    float xscale, float yscale, float angleDeg, int32_t c1, MAYBE_UNUSED int32_t c2,
    MAYBE_UNUSED int32_t c3, MAYBE_UNUSED int32_t c4, float alpha, float lineSeparation) {
    assert(renderer != NULL);
    GX_drawTextInternal(
        renderer, text, x, y, xscale, yscale, angleDeg, (uint32_t)c1, alpha, lineSeparation);
}
static void GX_flush(Renderer* renderer) {
    assert(renderer != NULL);
    GX_flushBatch((WiiRenderer*)renderer);
}

static int32_t GX_createSurface(Renderer* base, int32_t width, int32_t height) {
    assert(base != NULL);
    return GX_createSurfaceStorage((WiiRenderer*)base, width, height, GX_TF_RGBA8);
}

static bool GX_surfaceExists(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    return GX_surfaceIdIsLive(renderer, resolved);
}

static bool GX_setRenderTarget(
    Renderer* base, int32_t surfaceID, bool implicitApplicationSurface) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (!implicitApplicationSurface && !GX_surfaceExists(base, surfaceID)) return false;
    int32_t depth = 0;
    if (base->runner != NULL) {
        while (depth < MAX_SURFACES && base->runner->surfaceStack[depth] != -1) depth++;
    }
    if (depth < arrlen(renderer->targetStack)) {
        GX_restoreRenderState(renderer, &renderer->targetStack[depth]);
        arrsetlen(renderer->targetStack, depth);
        return true;
    }
    bool pushed = depth > arrlen(renderer->targetStack);
    if (pushed) {
        WiiRenderStateSnapshot state;
        GX_captureRenderState(renderer, &state);
        arrput(renderer->targetStack, state);
    }
    int32_t resolved =
        surfaceID == APPLICATION_SURFACE_ID ? renderer->applicationSurfaceId : surfaceID;
    bool bound = resolved == RENDER_TARGET_HOST_FRAMEBUFFER
        ? GX_bindHostTarget(renderer)
        : GX_bindSurfaceTarget(renderer, resolved);
    if (!bound && pushed) (void)arrpop(renderer->targetStack);
    return bound;
}

static int32_t GX_ensureAppSurface(Renderer* base, int32_t width, int32_t height) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (base->runner == NULL || !base->runner->usingAppSurface || width <= 0 || height <= 0) {
        if (GX_surfaceIdIsLive(renderer, renderer->applicationSurfaceId)) {
            if (renderer->targetSurfaceId == renderer->applicationSurfaceId)
                GX_bindHostTarget(renderer);
            GX_freeSurfaceStorage(
                renderer, &renderer->surfaces[renderer->applicationSurfaceId]);
        }
        renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
        return APPLICATION_SURFACE_ID;
    }
    if (GX_surfaceIdIsLive(renderer, renderer->applicationSurfaceId)) {
        WiiSurface* surface = &renderer->surfaces[renderer->applicationSurfaceId];
        if (surface->width == width && surface->height == height)
            return renderer->applicationSurfaceId;
        GX_surfaceResize(base, renderer->applicationSurfaceId, width, height);
        if (GX_surfaceIdIsLive(renderer, renderer->applicationSurfaceId))
            return renderer->applicationSurfaceId;
        renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
    }
    int32_t surfaceID = GX_createSurfaceStorage(renderer, width, height, GX_TF_RGBA8);
    renderer->applicationSurfaceId = surfaceID >= 0 ? surfaceID : APPLICATION_SURFACE_ID;
    return renderer->applicationSurfaceId;
}

static float GX_getSurfaceWidth(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    return GX_surfaceIdIsLive(renderer, resolved)
        ? (float)renderer->surfaces[resolved].width
        : 0.0f;
}

static float GX_getSurfaceHeight(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    return GX_surfaceIdIsLive(renderer, resolved)
        ? (float)renderer->surfaces[resolved].height
        : 0.0f;
}

static void GX_drawSurface(Renderer* base, int32_t surfaceID, int32_t srcLeft,
    int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale,
    float yscale, float angleDeg, uint32_t color, float alpha) {
    assert(base != NULL);
    GX_drawSurfaceColor(base, surfaceID, srcLeft, srcTop, srcWidth, srcHeight, x, y,
        xscale, yscale, angleDeg, color, color, color, color, alpha);
}

static void GX_drawSurfaceColor(Renderer* base, int32_t surfaceID, int32_t srcLeft,
    int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale,
    float yscale, MAYBE_UNUSED float angleDeg, uint32_t c1, MAYBE_UNUSED uint32_t c2,
    MAYBE_UNUSED uint32_t c3, MAYBE_UNUSED uint32_t c4, float alpha) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    if (!GX_surfaceIdIsLive(renderer, resolved)) return;
    WiiSurface* source = &renderer->surfaces[resolved];
    if (renderer->targetSurfaceId == resolved) GX_finishSurfaceTarget(renderer);
    if (srcLeft < 0) srcLeft = 0;
    if (srcTop < 0) srcTop = 0;
    if (srcWidth < 0) srcWidth = source->width - srcLeft;
    if (srcHeight < 0) srcHeight = source->height - srcTop;
    if (srcLeft >= source->width || srcTop >= source->height || srcWidth <= 0 || srcHeight <= 0)
        return;
    if (srcLeft + srcWidth > source->width) srcWidth = source->width - srcLeft;
    if (srcTop + srcHeight > source->height) srcHeight = source->height - srcTop;
    if (srcWidth <= 0 || srcHeight <= 0) return;
    if (GX_isHostTarget(renderer)) {
        if (fabsf(angleDeg) > 0.0001f) {
            Matrix4f transform;
            Matrix4f_setTransform2D(
                &transform, x, y, xscale, yscale, -angleDeg * (float)M_PI / 180.0f);
            float x1, y1, x2, y2, x3, y3, x4, y4;
            Matrix4f_transformPoint(&transform, 0.0f, 0.0f, &x1, &y1);
            Matrix4f_transformPoint(&transform, (float)srcWidth, 0.0f, &x2, &y2);
            Matrix4f_transformPoint(&transform, (float)srcWidth, (float)srcHeight, &x3, &y3);
            Matrix4f_transformPoint(&transform, 0.0f, (float)srcHeight, &x4, &y4);
            GX_drawSurfaceQuad(renderer, source, srcLeft, srcTop, srcWidth, srcHeight,
                x1, y1, x2, y2, x3, y3, x4, y4, c1, alpha, true);
        } else {
            GX_drawSurfaceRect(renderer, source, srcLeft, srcTop, srcWidth, srcHeight, x,
                y, (float)srcWidth * xscale, (float)srcHeight * yscale, c1, alpha, true);
        }
    }
}

static void GX_drawSurfaceTiled(MAYBE_UNUSED Renderer* renderer,
    MAYBE_UNUSED int32_t surfaceID, MAYBE_UNUSED float x, MAYBE_UNUSED float y,
    MAYBE_UNUSED float xscale, MAYBE_UNUSED float yscale, MAYBE_UNUSED float roomW,
    MAYBE_UNUSED float roomH, MAYBE_UNUSED uint32_t color, MAYBE_UNUSED float alpha) {
    assert(renderer != NULL);
}

static void GX_surfaceResize(
    Renderer* base, int32_t surfaceID, int32_t width, int32_t height) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    if (!GX_surfaceIdIsLive(renderer, resolved) || width <= 0 || height <= 0) return;
    if (renderer->targetSurfaceId == resolved) GX_bindHostTarget(renderer);
    WiiSurface* surface = &renderer->surfaces[resolved];
    GX_allocSurfacePixels(renderer, surface, width, height, GX_TF_RGBA8);
}

static void GX_surfaceFree(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    if (!GX_surfaceIdIsLive(renderer, resolved)) return;
    if (renderer->targetSurfaceId == resolved) GX_bindHostTarget(renderer);
    if (renderer->applicationSurfaceId == resolved)
        renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
    GX_freeSurfaceStorage(renderer, &renderer->surfaces[resolved]);
}

static void GX_surfaceCopy(Renderer* base, int32_t destSurfaceID, int32_t destX,
    int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH,
    MAYBE_UNUSED bool part) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t destResolved = destSurfaceID;
    int32_t srcResolved = srcSurfaceID;
    if (!GX_surfaceIdIsLive(renderer, destResolved) ||
        !GX_surfaceIdIsLive(renderer, srcResolved))
        return;
    WiiRenderStateSnapshot savedState;
    GX_captureRenderState(renderer, &savedState);
    if (renderer->targetSurfaceId == srcResolved) GX_finishSurfaceTarget(renderer);
    if (!GX_bindSurfaceTarget(renderer, destResolved)) return;
    WiiSurface* source = &renderer->surfaces[srcResolved];
    GX_drawSurfaceRect(renderer, source, srcX, srcY, srcW, srcH, (float)destX,
        (float)destY, (float)srcW, (float)srcH, 0xFFFFFFu, 1.0f, true);
    GX_finishSurfaceTarget(renderer);
    GX_restoreRenderState(renderer, &savedState);
}

static bool GX_surfaceGetPixels(Renderer* base, int32_t surfaceID, uint8_t* outRGBA) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    int32_t resolved = surfaceID;
    if (!GX_surfaceIdIsLive(renderer, resolved) || outRGBA == NULL) return false;
    if (renderer->targetSurfaceId == resolved) GX_finishSurfaceTarget(renderer);
    WiiSurface* surface = &renderer->surfaces[resolved];
    if (!surface->dirty) DCInvalidateRange(surface->pixels, surface->textureSize);
    for (int32_t y = 0; y < surface->height; ++y) {
        for (int32_t x = 0; x < surface->width; ++x) {
            uint8_t r, g, b, a;
            GX_readSurfaceRGBA(surface, (uint32_t)x, (uint32_t)y, &r, &g, &b, &a);
            uint8_t* dst = outRGBA + ((size_t)y * (size_t)surface->width + (size_t)x) * 4u;
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = a;
        }
    }
    return true;
}

static int32_t GX_createSpriteFromSurface(Renderer* base, int32_t surfaceID, int32_t x,
    int32_t y, int32_t w, int32_t h, MAYBE_UNUSED bool removeback, bool smooth, int32_t xorig,
    int32_t yorig) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    DataWin* dataWin = base->dataWin;
    if (dataWin == NULL || w <= 0 || h <= 0) return -1;

    int32_t resolved = surfaceID;
    if (!GX_surfaceIdIsLive(renderer, resolved)) return -1;
    WiiSurface* source = &renderer->surfaces[resolved];
    if (renderer->targetSurfaceId == resolved) GX_finishSurfaceTarget(renderer);

    int32_t requestedW = w;
    int32_t requestedH = h;
    bool appSurfaceSource =
        surfaceID == APPLICATION_SURFACE_ID || resolved == renderer->applicationSurfaceId;
    bool useActiveProjection =
        appSurfaceSource && renderer->renderState.activeProjectionTargetSurfaceId == resolved;
    int32_t srcRectX;
    int32_t srcRectY;
    int32_t srcRectRight;
    int32_t srcRectBottom;
    if (useActiveProjection && renderer->renderState.activeProjectionUsesMatrix) {
        float x0, y0, x1, y1, x2, y2, x3, y3;
        Matrix4f_transformPoint(
            &renderer->renderState.activeProjectionWorldToPixel, (float)x, (float)y, &x0, &y0);
        Matrix4f_transformPoint(&renderer->renderState.activeProjectionWorldToPixel,
            (float)(x + requestedW), (float)y, &x1, &y1);
        Matrix4f_transformPoint(&renderer->renderState.activeProjectionWorldToPixel,
            (float)(x + requestedW), (float)(y + requestedH), &x2, &y2);
        Matrix4f_transformPoint(&renderer->renderState.activeProjectionWorldToPixel, (float)x,
            (float)(y + requestedH), &x3, &y3);
        float minX = fminf(fminf(x0, x1), fminf(x2, x3));
        float minY = fminf(fminf(y0, y1), fminf(y2, y3));
        float maxX = fmaxf(fmaxf(x0, x1), fmaxf(x2, x3));
        float maxY = fmaxf(fmaxf(y0, y1), fmaxf(y2, y3));
        srcRectX = (int32_t)floorf(minX);
        srcRectY = (int32_t)floorf(minY);
        srcRectRight = (int32_t)ceilf(maxX);
        srcRectBottom = (int32_t)ceilf(maxY);
    } else {
        float scaleX = useActiveProjection ? renderer->renderState.activeViewScaleX : 1.0f;
        float scaleY = useActiveProjection ? renderer->renderState.activeViewScaleY : 1.0f;
        float srcXf = useActiveProjection
            ? ((float)x - (float)renderer->renderState.activeViewX) * scaleX +
                renderer->renderState.activeViewOffsetX
            : (float)x;
        float srcYf = useActiveProjection
            ? ((float)y - (float)renderer->renderState.activeViewY) * scaleY +
                renderer->renderState.activeViewOffsetY
            : (float)y;
        float srcWf = (float)requestedW * scaleX;
        float srcHf = (float)requestedH * scaleY;
        if (srcWf < 0.0f) {
            srcXf += srcWf;
            srcWf = -srcWf;
        }
        if (srcHf < 0.0f) {
            srcYf += srcHf;
            srcHf = -srcHf;
        }
        srcRectX = (int32_t)floorf(srcXf);
        srcRectY = (int32_t)floorf(srcYf);
        srcRectRight = (int32_t)ceilf(srcXf + srcWf);
        srcRectBottom = (int32_t)ceilf(srcYf + srcHf);
    }
    int32_t snapshotW = srcRectRight - srcRectX;
    int32_t snapshotH = srcRectBottom - srcRectY;
    if (snapshotW <= 0 || snapshotH <= 0) return -1;

    int32_t copySrcX = srcRectX;
    int32_t copySrcY = srcRectY;
    int32_t copySrcRight = srcRectRight;
    int32_t copySrcBottom = srcRectBottom;
    if (copySrcX < 0) copySrcX = 0;
    if (copySrcY < 0) copySrcY = 0;
    if (copySrcRight > source->width) copySrcRight = source->width;
    if (copySrcBottom > source->height) copySrcBottom = source->height;
    int32_t copyW = copySrcRight - copySrcX;
    int32_t copyH = copySrcBottom - copySrcY;
    if (copyW <= 0 || copyH <= 0) return -1;
    int32_t destX = copySrcX - srcRectX;
    int32_t destY = copySrcY - srcRectY;

    int32_t snapshotSurfaceId =
        GX_createSurfaceStorage(renderer, snapshotW, snapshotH, GX_TF_RGBA8);
    if (snapshotSurfaceId < 0) return -1;
    source = &renderer->surfaces[resolved];
    WiiSurface* snapshot = &renderer->surfaces[snapshotSurfaceId];
    WiiRenderStateSnapshot savedState;
    GX_captureRenderState(renderer, &savedState);
    if (!GX_bindSurfaceTarget(renderer, snapshotSurfaceId)) {
        GX_freeSurfaceStorage(renderer, snapshot);
        return -1;
    }
    GX_drawSolidRectReplace(
        renderer, 0.0f, 0.0f, (float)snapshotW, (float)snapshotH, 0x000000u, 0.0f);
    GX_drawSurfaceRect(renderer, source, copySrcX, copySrcY, copyW, copyH, (float)destX,
        (float)destY, (float)copyW, (float)copyH, 0xFFFFFFu, 1.0f, false);
    GX_finishSurfaceTarget(renderer);
    GX_restoreRenderState(renderer, &savedState);

    uint32_t tpagIndex = GX_findOrAllocTpagSlot(renderer, dataWin);
    if (tpagIndex == UINT32_MAX ||
        !GX_ensureTpagBindingCount(renderer, dataWin->tpag.count)) {
        GX_freeSurfaceStorage(renderer, snapshot);
        return -1;
    }

    TexturePageItem* tpag = &dataWin->tpag.items[tpagIndex];
    memset(tpag, 0, sizeof(*tpag));
    tpag->present = true;
    tpag->sourceX = 0;
    tpag->sourceY = 0;
    tpag->sourceWidth = (uint16_t)requestedW;
    tpag->sourceHeight = (uint16_t)requestedH;
    tpag->targetX = 0;
    tpag->targetY = 0;
    tpag->targetWidth = (uint16_t)requestedW;
    tpag->targetHeight = (uint16_t)requestedH;
    tpag->boundingWidth = (uint16_t)requestedW;
    tpag->boundingHeight = (uint16_t)requestedH;
    tpag->texturePageId = 0;

    renderer->tpagBindings[tpagIndex] = (WiiTpagBinding){
        .source = WII_TPAG_SOURCE_SURFACE,
        .flags = 0u,
        .subtexIndex = (uint16_t)snapshotSurfaceId,
        .resizeDivisor = 1u,
        .atlasItemIndex = UINT32_MAX,
    };

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dataWin, renderer->originalSpriteCount);
    Sprite* sprite = &dataWin->sprt.sprites[spriteIndex];
    sprite->present = true;
    sprite->width = (uint32_t)requestedW;
    sprite->height = (uint32_t)requestedH;
    sprite->marginLeft = 0;
    sprite->marginTop = 0;
    sprite->marginRight = requestedW - 1;
    sprite->marginBottom = requestedH - 1;
    sprite->transparent = true;
    sprite->smooth = smooth;
    sprite->preload = true;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1u;
    sprite->tpagIndices = safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t)tpagIndex;
    sprite->maskCount = 0u;
    sprite->masks = NULL;
    sprite->maskWidth = (uint32_t)requestedW;
    sprite->maskHeight = (uint32_t)requestedH;
    sprite->maskOffsetX = 0;
    sprite->maskOffsetY = 0;
    return (int32_t)spriteIndex;
}

static void GX_deleteSprite(Renderer* base, int32_t spriteIndex) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    DataWin* dataWin = base->dataWin;
    if (dataWin == NULL || spriteIndex < 0 || (uint32_t)spriteIndex >= dataWin->sprt.count)
        return;
    if ((uint32_t)spriteIndex < renderer->originalSpriteCount) return;

    Sprite* sprite = &dataWin->sprt.sprites[spriteIndex];
    if (sprite->textureCount == 0u) return;

    for (uint32_t i = 0u; i < sprite->textureCount; ++i) {
        int32_t tpagIndex = sprite->tpagIndices != NULL ? sprite->tpagIndices[i] : -1;
        if (tpagIndex < 0 || (uint32_t)tpagIndex < renderer->originalTpagCount ||
            (uint32_t)tpagIndex >= dataWin->tpag.count)
            continue;
        if ((uint32_t)tpagIndex < renderer->tpagBindingCount &&
            renderer->tpagBindings[tpagIndex].source == WII_TPAG_SOURCE_SURFACE) {
            uint16_t surfaceId = renderer->tpagBindings[tpagIndex].subtexIndex;
            if (surfaceId < renderer->surfaceCount && renderer->surfaces[surfaceId].used)
                GX_surfaceFree(base, surfaceId);
            renderer->tpagBindings[tpagIndex] = (WiiTpagBinding){
                .source = WII_TPAG_SOURCE_NONE,
                .flags = 0u,
                .subtexIndex = 0u,
                .resizeDivisor = 1u,
                .atlasItemIndex = UINT32_MAX,
            };
        }
        dataWin->tpag.items[tpagIndex].present = false;
        dataWin->tpag.items[tpagIndex].texturePageId = -1;
    }

    free(sprite->tpagIndices);
    const char* keepName = sprite->name;
    memset(sprite, 0, sizeof(Sprite));
    sprite->name = keepName;
}
static BlendFactors GX_gpuGetBlendFactors(Renderer* renderer) {
    assert(renderer != NULL);
    return renderer->blendFactors;
}

static int32_t GX_gpuGetBlendMode(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
    return bm_normal;
}

static void GX_gpuSetBlendMode(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t mode) {
    assert(renderer != NULL);
}

static void GX_gpuSetBlendModeExt(Renderer* renderer, int32_t sfactor, int32_t dfactor,
    int32_t sfactorAlpha, int32_t dfactorAlpha) {
    assert(renderer != NULL);
    renderer->blendFactors = (BlendFactors){
        sfactor,
        dfactor,
        sfactorAlpha,
        dfactorAlpha,
    };
}

static void GX_gpuSetBlendEnable(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED bool enable) {
    assert(renderer != NULL);
}

static void GX_gpuSetTexFilter(Renderer* base, bool enable) {
    assert(base != NULL);
    if (base->texFilter == enable) return;
    GX_flushBatch((WiiRenderer*)base);
    base->texFilter = enable;
}
static void GX_gpuSetAlphaTestEnable(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED bool enable) {
    assert(renderer != NULL);
}

static bool GX_gpuGetAlphaTestEnable(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
    return false;
}

static void GX_gpuSetAlphaTestRef(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint8_t ref) {
    assert(renderer != NULL);
}

static void GX_gpuSetColorWriteEnable(MAYBE_UNUSED Renderer* renderer,
    MAYBE_UNUSED bool red, MAYBE_UNUSED bool green, MAYBE_UNUSED bool blue,
    MAYBE_UNUSED bool alpha) {
    assert(renderer != NULL);
}

static void GX_gpuGetColorWriteEnable(
    MAYBE_UNUSED Renderer* renderer, bool* red, bool* green, bool* blue, bool* alpha) {
    assert(renderer != NULL);
    if (red != NULL) *red = true;
    if (green != NULL) *green = true;
    if (blue != NULL) *blue = true;
    if (alpha != NULL) *alpha = true;
}

static bool GX_gpuGetBlendEnable(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
    return true;
}

static void GX_gpuSetFog(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED bool enable, MAYBE_UNUSED uint32_t color,
    MAYBE_UNUSED float start, MAYBE_UNUSED float end) {
    assert(renderer != NULL);
}
static const WiiAtlasTileEntry* GX_findAtlasTileByKey(WiiRenderer* renderer,
    int16_t bgDef, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH) {
    WiiAtlasTileKey key = GX_atlasTileKey(bgDef, srcX, srcY, srcW, srcH);
    ptrdiff_t index = hmgeti(renderer->tileLookup, key);
    if (index < 0) return NULL;
    uint32_t entryIndex = renderer->tileLookup[index].value;
    return entryIndex < renderer->tileEntryCount ? &renderer->tileEntries[entryIndex] : NULL;
}

static const WiiAtlasTileEntry* GX_findAtlasTileEntry(
    WiiRenderer* renderer, const RoomTile* tile) {
    if (tile == NULL || tile->useSpriteDefinition) return NULL;
    if (tile->backgroundDefinition < INT16_MIN || tile->backgroundDefinition > INT16_MAX ||
        tile->sourceX < 0 || tile->sourceY < 0 || tile->width > UINT16_MAX ||
        tile->height > UINT16_MAX) {
        return NULL;
    }
    return GX_findAtlasTileByKey(renderer, (int16_t)tile->backgroundDefinition,
        (uint16_t)tile->sourceX, (uint16_t)tile->sourceY, (uint16_t)tile->width,
        (uint16_t)tile->height);
}

static const WiiAtlasTileEntry* GX_findTilemapAtlasEntry(WiiRenderer* renderer,
    int32_t tpagIndex, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH) {
    if (renderer->base.dataWin == NULL || tpagIndex < 0) return NULL;
    if (srcX < 0 || srcY < 0 || srcW <= 0 || srcH <= 0 || srcX > UINT16_MAX || srcY > UINT16_MAX ||
        srcW > UINT16_MAX || srcH > UINT16_MAX)
        return NULL;
    DataWin* dataWin = renderer->base.dataWin;
    TexturePageItem* tpag =
        (uint32_t)tpagIndex < dataWin->tpag.count ? &dataWin->tpag.items[tpagIndex] : NULL;
    int32_t bgSrcX = tpag != NULL ? srcX + tpag->targetX : srcX;
    int32_t bgSrcY = tpag != NULL ? srcY + tpag->targetY : srcY;
    repeat(dataWin->bgnd.count, i) {
        Background* bg = &dataWin->bgnd.backgrounds[i];
        if (bg->tpagIndex != tpagIndex || bg->gms2TileWidth != (uint32_t)srcW ||
            bg->gms2TileHeight != (uint32_t)srcH || bg->gms2TileColumns == 0) {
            continue;
        }
        if (i > INT16_MAX) continue;
        const WiiAtlasTileEntry* entry = GX_findAtlasTileByKey(
            renderer, (int16_t)i, (uint16_t)srcX, (uint16_t)srcY, (uint16_t)srcW, (uint16_t)srcH);
        if (entry != NULL) return entry;
        if (bgSrcX >= 0 && bgSrcY >= 0 && bgSrcX <= UINT16_MAX && bgSrcY <= UINT16_MAX &&
            (bgSrcX != srcX || bgSrcY != srcY)) {
            entry = GX_findAtlasTileByKey(renderer, (int16_t)i, (uint16_t)bgSrcX,
                (uint16_t)bgSrcY, (uint16_t)srcW, (uint16_t)srcH);
            if (entry != NULL) return entry;
        }
    }
    return NULL;
}

static bool GX_drawAtlasTileQuad(WiiRenderer* renderer,
    const WiiAtlasTileEntry* entry, float quadX1, float quadY1, float quadX2, float quadY2,
    float quadX3, float quadY3, float quadX4, float quadY4, uint32_t color, float alpha) {
    if (entry == NULL || entry->srcW == 0u || entry->srcH == 0u || alpha <= 0.0f)
        return false;
    if (entry->atlasId != UINT16_MAX) {
        if (entry->atlasId >= renderer->pageCount ||
            !GX_loadRoomAtlasPage(renderer, entry->atlasId))
            return false;
        GX_drawCi8Quad(renderer, &renderer->pages[entry->atlasId], entry->x,
            entry->y, entry->width, entry->height, quadX1, quadY1, quadX2, quadY2, quadX3, quadY3,
            quadX4, quadY4, false, false, color, alpha);
        return true;
    }

    if (entry->fragmentCount == 0u || entry->fragmentStart >= renderer->fragmentCount) return false;
    WiiQuad quad = {quadX1, quadY1, quadX2, quadY2, quadX3, quadY3, quadX4, quadY4};
    for (uint32_t i = 0u; i < entry->fragmentCount; ++i) {
        uint32_t fragmentIndex = entry->fragmentStart + i;
        if (fragmentIndex >= renderer->fragmentCount) break;
        const WiiAtlasFragment* frag = &renderer->fragments[fragmentIndex];
        if (frag->atlasId >= renderer->pageCount ||
            !GX_loadRoomAtlasPage(renderer, frag->atlasId))
            continue;
        float s0 = (float)frag->sourceX / (float)entry->srcW;
        float t0 = (float)frag->sourceY / (float)entry->srcH;
        float s1 = (float)(frag->sourceX + frag->width) / (float)entry->srcW;
        float t1 = (float)(frag->sourceY + frag->height) / (float)entry->srcH;
        float x1, y1, x2, y2, x3, y3, x4, y4;
        GX_interpolateQuad(&quad, s0, t0, &x1, &y1);
        GX_interpolateQuad(&quad, s1, t0, &x2, &y2);
        GX_interpolateQuad(&quad, s1, t1, &x3, &y3);
        GX_interpolateQuad(&quad, s0, t1, &x4, &y4);
        GX_drawCi8Quad(renderer, &renderer->pages[frag->atlasId], frag->x, frag->y,
            frag->width, frag->height, x1, y1, x2, y2, x3, y3, x4, y4, false, false, color, alpha);
    }
    return true;
}

static bool GX_drawAtlasTileEntry(WiiRenderer* renderer, const WiiAtlasTileEntry* entry,
    const RoomTile* tile, float offsetX, float offsetY) {
    if (entry == NULL || tile == NULL || entry->srcW == 0u || entry->srcH == 0u)
        return false;
    uint32_t color = tile->color & 0x00FFFFFFu;
    float drawX = (float)tile->x + offsetX;
    float drawY = (float)tile->y + offsetY;

    if (entry->atlasId != UINT16_MAX) {
        if (entry->atlasId >= renderer->pageCount ||
            !GX_loadRoomAtlasPage(renderer, entry->atlasId))
            return false;
        if (!GX_isHostTarget(renderer)) return false;
        GX_drawCi8QuadGX(renderer, &renderer->pages[entry->atlasId], entry->x, entry->y,
            entry->width, entry->height, drawX, drawY, (float)entry->width * tile->scaleX,
            (float)entry->height * tile->scaleY, tile->scaleX < 0.0f, tile->scaleY < 0.0f, color,
            tile->alpha);
        return true;
    }

    if (entry->fragmentCount == 0u || entry->fragmentStart >= renderer->fragmentCount) return false;
    for (uint32_t i = 0u; i < entry->fragmentCount; ++i) {
        uint32_t fragmentIndex = entry->fragmentStart + i;
        if (fragmentIndex >= renderer->fragmentCount) break;
        const WiiAtlasFragment* frag = &renderer->fragments[fragmentIndex];
        if (frag->atlasId >= renderer->pageCount ||
            !GX_loadRoomAtlasPage(renderer, frag->atlasId))
            continue;
        if (!GX_isHostTarget(renderer)) continue;
        GX_drawCi8QuadGX(renderer, &renderer->pages[frag->atlasId], frag->x, frag->y,
            frag->width, frag->height, drawX + (float)frag->sourceX * tile->scaleX,
            drawY + (float)frag->sourceY * tile->scaleY, (float)frag->width * tile->scaleX,
            (float)frag->height * tile->scaleY, tile->scaleX < 0.0f, tile->scaleY < 0.0f, color,
            tile->alpha);
    }
    return true;
}

static void GX_drawTileFallback(
    Renderer* base, RoomTile* tile, float offsetX, float offsetY) {
    if (base->dataWin == NULL || tile == NULL) return;
    int32_t tpagIndex = Renderer_resolveObjectTPAGIndex(base->dataWin, tile);
    if (tpagIndex < 0 || (uint32_t)tpagIndex >= base->dataWin->tpag.count) return;

    TexturePageItem* tpag = &base->dataWin->tpag.items[tpagIndex];
    int32_t srcX = tile->sourceX;
    int32_t srcY = tile->sourceY;
    int32_t srcW = (int32_t)tile->width;
    int32_t srcH = (int32_t)tile->height;
    float drawX = (float)tile->x + offsetX;
    float drawY = (float)tile->y + offsetY;

    int32_t contentLeft = tpag->targetX;
    int32_t contentTop = tpag->targetY;
    if (contentLeft > srcX) {
        int32_t clip = contentLeft - srcX;
        drawX += (float)clip * tile->scaleX;
        srcW -= clip;
        srcX = contentLeft;
    }
    if (contentTop > srcY) {
        int32_t clip = contentTop - srcY;
        drawY += (float)clip * tile->scaleY;
        srcH -= clip;
        srcY = contentTop;
    }

    int32_t contentRight = tpag->targetX + tpag->sourceWidth;
    int32_t contentBottom = tpag->targetY + tpag->sourceHeight;
    if (srcX + srcW > contentRight) srcW = contentRight - srcX;
    if (srcY + srcH > contentBottom) srcH = contentBottom - srcY;
    if (srcW <= 0 || srcH <= 0) return;

    GX_drawSpritePart(base, tpagIndex, srcX - tpag->targetX, srcY - tpag->targetY, srcW,
        srcH, drawX, drawY, tile->scaleX, tile->scaleY, 0.0f, 0.0f, 0.0f, tile->color & 0x00FFFFFFu,
        tile->alpha);
}

static void GX_drawTile(Renderer* base, RoomTile* tile, float offsetX, float offsetY) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    const WiiAtlasTileEntry* entry = GX_findAtlasTileEntry(renderer, tile);
    if (entry != NULL && GX_drawAtlasTileEntry(renderer, entry, tile, offsetX, offsetY))
        return;
    GX_drawTileFallback(base, tile, offsetX, offsetY);
}

static void GX_drawTiledPart(Renderer* base, int32_t tpagIndex, int32_t srcX, int32_t srcY,
    int32_t srcW, int32_t srcH, float dstX, float dstY, float dstW, float dstH, uint32_t color,
    float alpha) {
    assert(base != NULL);
    if (srcW <= 0 || srcH <= 0 || dstW <= 0.0f || dstH <= 0.0f) return;
    float cursorY = 0.0f;
    while (cursorY < dstH) {
        float drawH = fminf((float)srcH, dstH - cursorY);
        float cursorX = 0.0f;
        while (cursorX < dstW) {
            float drawW = fminf((float)srcW, dstW - cursorX);
            base->vtable->drawSpritePart(base, tpagIndex, srcX, srcY, (int32_t)drawW,
                (int32_t)drawH, dstX + cursorX, dstY + cursorY, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, color,
                alpha);
            cursorX += drawW;
        }
        cursorY += drawH;
    }
}

static void GX_drawSpriteTiled(Renderer* base, int32_t tpagIndex, float originX,
    float originY, float x, float y, float xscale, float yscale, bool tileX, bool tileY,
    float roomW, float roomH, uint32_t color, float alpha) {
    assert(base != NULL);
    if (base->dataWin == NULL || tpagIndex < 0 ||
        (uint32_t)tpagIndex >= base->dataWin->tpag.count)
        return;
    TexturePageItem* tpag = &base->dataWin->tpag.items[tpagIndex];
    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float stepX = (float)tpag->boundingWidth * axScale;
    float stepY = (float)tpag->boundingHeight * ayScale;
    if (stepX <= 0.0f || stepY <= 0.0f) return;
    float startX = tileX ? fmodf(x - originX * axScale, stepX) : x - originX * axScale;
    float startY = tileY ? fmodf(y - originY * ayScale, stepY) : y - originY * ayScale;
    if (tileX && startX > 0.0f) startX -= stepX;
    if (tileY && startY > 0.0f) startY -= stepY;
    float endX = tileX ? roomW : startX + stepX;
    float endY = tileY ? roomH : startY + stepY;
    if (startX >= endX || startY >= endY) return;
    int32_t tilesX = tileX ? (int32_t)((endX - startX) / stepX) + 1 : 1;
    int32_t tilesY = tileY ? (int32_t)((endY - startY) / stepY) + 1 : 1;
    if (tilesX <= 0 || tilesY <= 0) return;

    repeat(tilesY, iy) {
        float py = startY + (float)iy * stepY;
        if (py >= endY) break;
        repeat(tilesX, ix) {
            float px = startX + (float)ix * stepX;
            if (px >= endX) break;
            base->vtable->drawSprite(base, tpagIndex, px + originX * axScale,
                py + originY * ayScale, originX, originY, xscale, yscale, 0.0f, color, alpha);
        }
    }
}
static void GX_gpuSetShader(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex) {
    assert(renderer != NULL);
}
static void GX_gpuResetShader(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
}

static int32_t GX_shaderGetUniform(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) {
    assert(renderer != NULL);
    return -1;
}

static int32_t GX_shaderGetSamplerIndex(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) {
    assert(renderer != NULL);
    return -1;
}

static void GX_shaderSetUniformF(MAYBE_UNUSED Renderer* renderer,
    MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED float value1,
    MAYBE_UNUSED float value2, MAYBE_UNUSED float value3, MAYBE_UNUSED float value4) {
    assert(renderer != NULL);
}

static void GX_shaderSetUniformFArray(MAYBE_UNUSED Renderer* renderer,
    MAYBE_UNUSED int32_t handle, MAYBE_UNUSED float* values, MAYBE_UNUSED uint32_t count) {
    assert(renderer != NULL);
}

static void GX_shaderSetUniformI(MAYBE_UNUSED Renderer* renderer,
    MAYBE_UNUSED int32_t handle, MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t value1,
    MAYBE_UNUSED int32_t value2, MAYBE_UNUSED int32_t value3, MAYBE_UNUSED int32_t value4) {
    assert(renderer != NULL);
}

static uint32_t GX_spriteGetTexture(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t tpagIndex) {
    assert(renderer != NULL);
    return 0u;
}

static uint32_t GX_surfaceGetTexture(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t surfaceID) {
    assert(renderer != NULL);
    return 0u;
}

static float GX_textureGetTexelWidth(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) {
    assert(renderer != NULL);
    return 1.0f;
}

static float GX_textureGetTexelHeight(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID) {
    assert(renderer != NULL);
    return 1.0f;
}

static bool GX_textureGetUVs(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED uint32_t texID, float* outUVs) {
    assert(renderer != NULL);
    if (outUVs == NULL) return false;
    outUVs[0] = 0.0f;
    outUVs[1] = 0.0f;
    outUVs[2] = 1.0f;
    outUVs[3] = 1.0f;
    return true;
}

static void GX_textureSetStage(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texID) {
    assert(renderer != NULL);
}

static bool GX_shaderIsCompiled(
    MAYBE_UNUSED Renderer* renderer, MAYBE_UNUSED int32_t shader) {
    assert(renderer != NULL);
    return false;
}

static bool GX_shadersSupported(void) {
    return false;
}
static void GX_setMatrix(Renderer* base, int32_t matrixType, Matrix4f matrix) {
    assert(base != NULL);
    WiiRenderer* renderer = (WiiRenderer*)base;
    if (matrixType < 0 || matrixType >= MATRICES_MAX) return;
    if (memcmp(&base->gmlMatrices[matrixType], &matrix, sizeof(Matrix4f)) == 0) return;

    GX_flushBatch(renderer);
    base->gmlMatrices[matrixType] = matrix;

    Matrix4f world = base->gmlMatrices[MATRIX_WORLD];
    Matrix4f view = base->gmlMatrices[MATRIX_VIEW];
    Matrix4f projection = base->gmlMatrices[MATRIX_PROJECTION];
    Matrix4f worldView;
    Matrix4f worldToClip = {0};
    Matrix4f_multiply(&worldView, &view, &world);
    Matrix4f_multiply(&worldToClip, &projection, &worldView);
    base->gmlMatrices[MATRIX_WORLD_VIEW] = worldView;
    base->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = worldToClip;

    float viewportX = (float)base->CPortX;
    float viewportY = (float)base->CPortY;
    float viewportW = base->CPortW > 0 ? (float)base->CPortW : GX_outputW(renderer);
    float viewportH = base->CPortH > 0 ? (float)base->CPortH : GX_outputH(renderer);
    if (viewportW <= 0.0f) viewportW = 1.0f;
    if (viewportH <= 0.0f) viewportH = 1.0f;
    if (!GX_setClipProjection(
            renderer, &worldToClip, viewportX, viewportY, viewportW, viewportH)) {
        renderer->renderState.activeProjectionUsesMatrix = false;
    }
}

static RendererVtable WiiRenderer_vtable = {
    .init = GX_init,
    .destroy = GX_destroy,
    .beginFrame = GX_beginFrame,
    .endFrameInit = GX_endFrameInit,
    .endFrameEnd = GX_endFrameNoop,
    .beginView = GX_beginView,
    .endView = GX_endFrameNoop,
    .applyProjection = GX_applyProjection,
    .beginGUI = GX_beginGUI,
    .setGuiProjection = GX_setGuiProjection,
    .endGUI = GX_endFrameNoop,
    .drawSprite = GX_drawSprite,
    .drawSpritePart = GX_drawSpritePart,
    .drawSpritePartColor = GX_drawSpritePartColor,
    .drawSpritePos = GX_drawSpritePos,
    .drawRectangle = GX_drawRectangle,
    .drawRectangleColor = GX_drawRectangleColor,
    .drawLine = GX_drawLine,
    .drawTriangle = GX_drawTriangle,
    .drawLineColor = GX_drawLineColor,
    .drawText = GX_drawText,
    .drawTextColor = GX_drawTextColor,
    .flush = GX_flush,
    .clearScreen = GX_clearScreen,
    .createSpriteFromSurface = GX_createSpriteFromSurface,
    .deleteSprite = GX_deleteSprite,
    .gpuGetBlendFactors = GX_gpuGetBlendFactors,
    .gpuGetBlendMode = GX_gpuGetBlendMode,
    .gpuSetBlendMode = GX_gpuSetBlendMode,
    .gpuSetBlendModeExt = GX_gpuSetBlendModeExt,
    .gpuSetBlendEnable = GX_gpuSetBlendEnable,
    .gpuSetTexFilter = GX_gpuSetTexFilter,
    .gpuSetAlphaTestEnable = GX_gpuSetAlphaTestEnable,
    .gpuGetAlphaTestEnable = GX_gpuGetAlphaTestEnable,
    .gpuSetAlphaTestRef = GX_gpuSetAlphaTestRef,
    .gpuSetColorWriteEnable = GX_gpuSetColorWriteEnable,
    .gpuGetColorWriteEnable = GX_gpuGetColorWriteEnable,
    .gpuGetBlendEnable = GX_gpuGetBlendEnable,
    .gpuSetFog = GX_gpuSetFog,
    .drawTile = GX_drawTile,
    .drawSpriteTiled = GX_drawSpriteTiled,
    .createSurface = GX_createSurface,
    .surfaceExists = GX_surfaceExists,
    .setRenderTarget = GX_setRenderTarget,
    .ensureApplicationSurface = GX_ensureAppSurface,
    .getSurfaceWidth = GX_getSurfaceWidth,
    .getSurfaceHeight = GX_getSurfaceHeight,
    .drawSurface = GX_drawSurface,
    .drawSurfaceColor = GX_drawSurfaceColor,
    .drawSurfaceTiled = GX_drawSurfaceTiled,
    .surfaceResize = GX_surfaceResize,
    .surfaceFree = GX_surfaceFree,
    .surfaceCopy = GX_surfaceCopy,
    .surfaceGetPixels = GX_surfaceGetPixels,
    .drawTiledPart = GX_drawTiledPart,
    .gpuSetShader = GX_gpuSetShader,
    .gpuResetShader = GX_gpuResetShader,
    .shaderGetUniform = GX_shaderGetUniform,
    .shaderGetSamplerIndex = GX_shaderGetSamplerIndex,
    .shaderSetUniformF = GX_shaderSetUniformF,
    .shaderSetUniformFArray = GX_shaderSetUniformFArray,
    .shaderSetUniformI = GX_shaderSetUniformI,
    .spriteGetTexture = GX_spriteGetTexture,
    .surfaceGetTexture = GX_surfaceGetTexture,
    .textureGetTexelWidth = GX_textureGetTexelWidth,
    .textureGetTexelHeight = GX_textureGetTexelHeight,
    .textureGetUVs = GX_textureGetUVs,
    .textureSetStage = GX_textureSetStage,
    .shaderIsCompiled = GX_shaderIsCompiled,
    .shadersSupported = GX_shadersSupported,
    .setMatrix = GX_setMatrix,
};

Renderer* GX_create(
    const char* dataWinPath, bool cacheAtlas, uint32_t atlasTextureCacheBytes) {
    WiiRenderer* renderer = safeCalloc(1, sizeof(WiiRenderer));
    renderer->base.vtable = &WiiRenderer_vtable;
    renderer->dataWinPath =
        safeStrdup(dataWinPath != NULL ? dataWinPath : "data.win");
    renderer->cacheAtlas = cacheAtlas;
    renderer->atlasTextureCacheBytes = atlasTextureCacheBytes;
    renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
    renderer->targetSurfaceId = APPLICATION_SURFACE_ID;
    renderer->renderState.activeViewScaleX = 1.0f;
    renderer->renderState.activeViewScaleY = 1.0f;
    renderer->renderState.activeProjectionTargetSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    renderer->activeRoomIndex = -1;
    renderer->outputW = WII_FRAMEBUFFER_WIDTH;
    renderer->outputH = WII_FRAMEBUFFER_HEIGHT;
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_WORLD]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_VIEW]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_PROJECTION]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_WORLD_VIEW]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION]);
    return &renderer->base;
}

void GX_setOutputSize(WiiRenderer* renderer, int32_t width, int32_t height) {
    assert(renderer != NULL);
    renderer->frameBufferW = width > 0 ? width : WII_FRAMEBUFFER_WIDTH;
    renderer->frameBufferH = height > 0 ? height : WII_FRAMEBUFFER_HEIGHT;
    renderer->outputX = 0;
    renderer->outputY = 0;
    renderer->outputW = renderer->frameBufferW;
    renderer->outputH = renderer->frameBufferH;
}

void GX_setOutputRect(
    WiiRenderer* renderer, int32_t x, int32_t y, int32_t width, int32_t height) {
    assert(renderer != NULL);
    int32_t framebufferW =
        renderer->frameBufferW > 0 ? renderer->frameBufferW : WII_FRAMEBUFFER_WIDTH;
    int32_t framebufferH =
        renderer->frameBufferH > 0 ? renderer->frameBufferH : WII_FRAMEBUFFER_HEIGHT;
    if (width <= 0) width = framebufferW;
    if (height <= 0) height = framebufferH;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > framebufferW) x = framebufferW;
    if (y > framebufferH) y = framebufferH;
    if (width > framebufferW - x) width = framebufferW - x;
    if (height > framebufferH - y) height = framebufferH - y;
    renderer->outputX = x;
    renderer->outputY = y;
    renderer->outputW = width;
    renderer->outputH = height;
}

uint32_t GX_getAtlasBytes(const WiiRenderer* renderer) {
    assert(renderer != NULL);
    return renderer->residentAtlasBytes;
}

uint32_t GX_getLoadedAtlasPages(const WiiRenderer* renderer) {
    assert(renderer != NULL);
    if (renderer->pages == NULL) return 0u;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < renderer->pageCount; ++i) {
        if (renderer->pages[i].gxReady) count++;
    }
    return count;
}

uint32_t GX_getAtlasPages(const WiiRenderer* renderer) {
    assert(renderer != NULL);
    return renderer->pageCount;
}

uint32_t GX_getSurfaceCount(const WiiRenderer* renderer) {
    assert(renderer != NULL);
    if (renderer->surfaces == NULL) return 0u;
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < renderer->surfaceCount; ++i) {
        if (renderer->surfaces[i].used) count++;
    }
    return count;
}

void GX_clearOutput(WiiRenderer* renderer) {
    assert(renderer != NULL);
    GX_flushBatch(renderer);
    if (GX_targetIsSurface(renderer)) {
        GX_finishSurfaceTarget(renderer);
        renderer->targetSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    }
    GX_setEfbFormatForTarget(renderer, NULL);
    float fbW =
        renderer->frameBufferW > 0 ? (float)renderer->frameBufferW : (float)WII_FRAMEBUFFER_WIDTH;
    float fbH =
        renderer->frameBufferH > 0 ? (float)renderer->frameBufferH : (float)WII_FRAMEBUFFER_HEIGHT;
    GX_setGXProjection(renderer, 0.0f, 0.0f, fbW, fbH, 0.0f, 0.0f, fbW, fbH);
    GX_setViewProjection(renderer, 0.0f, 0.0f, fbW, fbH, 0.0f);
    GX_setActiveProjection(renderer, 0, 0, 1.0f, 1.0f, 0.0f, 0.0f);
    GX_drawSolidRectReplace(renderer, 0.0f, 0.0f, fbW, fbH, 0x000000u, 1.0f);
    GX_flushBatch(renderer);
}

void GX_present(
    WiiRenderer* renderer, void* xfb, uint32_t fbWidth, uint32_t efbHeight, uint32_t xfbHeight) {
    assert(renderer != NULL);
    if (xfb == NULL) return;
    GX_flushBatch(renderer);
    if (GX_targetIsSurface(renderer)) {
        GX_finishSurfaceTarget(renderer);
        renderer->targetSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    }
    if (fbWidth == 0u) fbWidth = WII_FRAMEBUFFER_WIDTH;
    if (efbHeight == 0u) efbHeight = WII_FRAMEBUFFER_HEIGHT;
    if (xfbHeight == 0u) xfbHeight = efbHeight;
    GX_setEfbFormatForTarget(renderer, NULL);
    GX_SetDispCopySrc(0, 0, fbWidth, efbHeight);
    GX_SetDispCopyDst(fbWidth, xfbHeight);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_TRUE);
    GX_CopyDisp(xfb, GX_TRUE);
    GX_DrawDone();
}
