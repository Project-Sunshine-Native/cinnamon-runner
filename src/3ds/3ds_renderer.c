#include "3ds_renderer.h"
#include <citro2d.h>
#include "3ds_file_system.h"
#include "3ds_log.h"
#include "3ds_native_texture.h"
#include "3ds_platform_config.h"
#include "../log.h"
#include "../runner.h"
#include "../text_utils.h"
#include "../utils.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds/allocator/vram.h>
#include <stb/ds/stb_ds.h>
#define N3DS_TOP_WIDTH 400
#define N3DS_TOP_HEIGHT 240
#define N3DS_BOTTOM_WIDTH 320
#define N3DS_BOTTOM_HEIGHT 240
#define N3DS_MAX_TEXTURE_SIZE 1024
#define N3DS_MAX_APPLICATION_SURFACE_WIDTH 640
#define N3DS_MAX_APPLICATION_SURFACE_HEIGHT 480
#define N3DS_TRANSFER_FLAGS                                                                        \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |               \
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |                                             \
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
#define N3DS_ATLAS_MAGIC 0x5441334Eu
#define N3DS_ATLAS_VERSION 2u
#define N3DS_ATLAS_DIRECTORY_MAGIC 0x4441334Eu
#define N3DS_ATLAS_DIRECTORY_VERSION 2u
#define N3DS_ATLAS_BLOCK_TPAG_BINDINGS 0x47415054u
#define N3DS_ATLAS_BLOCK_ROOM_MANIFEST 0x4D4F4F52u
#define N3DS_ROOM_MANIFEST_MAGIC 0x4D52334Eu
#define N3DS_ROOM_MANIFEST_VERSION 2u
#define N3DS_TPAG_BINDINGS_VERSION 2u
#define N3DS_ATLAS_PAGE_FORMAT_ETC1A4 1u
#define N3DS_ATLAS_PAGE_FORMAT_L4 3u
#define N3DS_ATLAS_PAGE_FORMAT_LA4 4u
#define N3DS_ATLAS_PAGE_FORMAT_RGBA4444 6u
#define N3DS_ATLAS_PAGE_FORMAT_ETC1 11u
#define N3DS_TPAG_BINDING_FLAG_BORROWED_SURFACE 0x08u
#define N3DS_SURFACE_SPRITE_POOL_MAX 4u
#define N3DS_USER_SURFACE_FORMAT GPU_RGBA4
#define N3DS_TILE_LOOKUP_CACHE_SIZE 64u
#define N3DS_LINE_CLIP_EPSILON 0.000001f
#define N3DS_LINE_LENGTH_EPSILON 0.0001f

static uint32_t C3D_addBytes(uint32_t a, uint32_t b) {
    uint64_t sum = (uint64_t)a + b;
    return sum > UINT32_MAX ? UINT32_MAX : (uint32_t)sum;
}

static bool C3D_fcramOverBudget(uint32_t residentFcramBytes, uint32_t incomingBytes) {
    uint32_t reserveBytes = n3ds_getTextureReserveBytes();
    uint32_t projectedBytes = C3D_addBytes(residentFcramBytes, incomingBytes);
    uint32_t projectedReserve = C3D_addBytes(reserveBytes, incomingBytes);
    if (projectedBytes > n3ds_getTextureCacheBytes()) return true;
    if (n3ds_getFreeLinearBytes() < projectedReserve) return true;
    return n3ds_getFreeHeapBudgetBytes() < projectedReserve;
}

typedef enum {
    N3DS_TPAG_SOURCE_NONE = 0,
    N3DS_TPAG_SOURCE_ATLAS = 2,
    N3DS_TPAG_SOURCE_SURFACE = 3,
} N3DSTpagSource;

typedef struct {
    float left;
    float top;
    float right;
    float bottom;
    uint16_t width;
    uint16_t height;
} N3DSUVRect;

typedef struct {
    N3DSTpagSource source;
    uint32_t assetIndex;
    uint32_t surfaceGeneration;
    uint16_t subtexIndex;
    uint16_t resizeDivisor;
    uint32_t atlasItemIndex;
    uint8_t flags;
    bool hasTilemapEntries;
} N3DSTpagBinding;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t pageFormat;
    uint32_t dataOffset;
    uint32_t dataSize;
} N3DSAtlasPageDesc;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t fragmentStart;
    uint16_t fragmentCount;
    uint16_t resizeDivisor;
} N3DSAtlasItem;

typedef struct {
    uint16_t pageIndex;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t sourceX;
    uint16_t sourceY;
} N3DSAtlasFragment;

typedef struct {
    int16_t bgDef;
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
    uint16_t pageIndex;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint32_t fragmentStart;
    uint16_t fragmentCount;
} N3DSAtlasTileEntry;

typedef struct {
    int16_t bgDef;
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
} N3DSAtlasTileKey;

typedef struct {
    N3DSAtlasTileKey key;
    uint32_t value;
} N3DSAtlasTileLookupEntry;

typedef struct {
    int32_t tpagIndex;
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
} N3DSAtlasTilemapKey;

typedef struct {
    N3DSAtlasTilemapKey key;
    uint32_t value;
} N3DSAtlasTilemapLookupEntry;

typedef struct {
    bool valid;
    N3DSAtlasTileKey key;
    uint32_t value;
} N3DSAtlasTileCacheEntry;

typedef struct {
    bool active;
    bool loadFailed;
    uint32_t lastLoadAttemptFrame;
    bool inVram;
    C3D_Tex tex;
    N3DSUVRect region;
    float uPerPixel;
    float vPerPixel;
    uint32_t sizeBytes;
    uint64_t lastUse;
} N3DSAtlasPage;

typedef struct {
    uint32_t roomIndex;
    uint32_t pageStart;
    uint32_t pageCount;
    uint32_t tileStart;
    uint32_t tileCount;
    uint32_t reserved2;
    uint32_t reserved3;
    uint32_t reserved4;
    uint32_t reserved5;
} N3DSRoomManifestEntry;

typedef struct {
    uint16_t pageIndex;
    uint8_t flags;
    uint8_t reserved;
} N3DSRoomManifestPageRef;

typedef struct {
    uint32_t tileEntryIndex;
    int32_t tpagIndex;
} N3DSRoomManifestTileRef;

typedef struct {
    bool loaded;
    N3DSRoomManifestEntry* entries;
    uint32_t entryCount;
    N3DSRoomManifestTileRef* tileRefs;
    uint32_t tileRefCount;
    int32_t* entryByRoomIndex;
    uint32_t entryByRoomIndexCount;
} N3DSRoomManifest;

typedef struct {
    int32_t srcX;
    int32_t srcY;
    int32_t copyW;
    int32_t copyH;
    int32_t destX;
    int32_t destY;
    int32_t snapshotW;
    int32_t snapshotH;
} N3DSSurfaceSnapshotRect;

typedef struct {
    const RoomTile* tiles;
    uint32_t count;
    uint32_t tileOffset;
} N3DSRoomTileRange;

typedef struct {
    int32_t backgroundDefinition;
    int32_t sourceX;
    int32_t sourceY;
    uint32_t width;
    uint32_t height;
    uint32_t useSpriteDefinition;
} N3DSRoomTileLookupKey;

typedef struct {
    N3DSRoomTileLookupKey key;
    uint32_t value;
} N3DSRoomTileLookupEntry;

typedef struct {
    bool loaded;
    char atlasPath[256];
    FILE* fp;
    N3DSAtlasPageDesc* pageDescs;
    uint32_t pageCount;
    N3DSAtlasItem* items;
    uint32_t itemCount;
    N3DSAtlasFragment* fragments;
    uint32_t fragmentCount;
    N3DSAtlasTileEntry* tileEntries;
    N3DSAtlasTileLookupEntry* tileLookup;
    N3DSAtlasTilemapLookupEntry* tilemapLookup;
    N3DSAtlasTileCacheEntry tileCache[N3DS_TILE_LOOKUP_CACHE_SIZE];
    uint32_t tileEntryCount;
    N3DSAtlasPage* pages;
    bool* roomWantedPages;
    uint8_t* data;
    uint32_t dataSize;
    uint32_t fileSize;
    uint32_t embeddedTpagBindingOffset;
    uint32_t embeddedTpagBindingSize;
    uint32_t embeddedRoomManifestOffset;
    uint32_t embeddedRoomManifestSize;
    uint8_t* ioBuffer;
    uint32_t ioBufferSize;
    uint32_t residentBytes;
} N3DSAtlas;

typedef struct {
    bool used;
    bool pendingFree;
    bool samplingReady;
    bool topAligned;
    uint32_t generation;
    int32_t width;
    int32_t height;
    int32_t logicalWidth;
    int32_t logicalHeight;
    int32_t texWidth;
    int32_t texHeight;
    GPU_TEXCOLOR texFormat;
    GX_TRANSFER_FORMAT transferFormat;
    C3D_Tex tex;
    C3D_RenderTarget* target;
} N3DSSurface;

typedef enum {
    N3DS_MAP_AFFINE = 0,
    N3DS_MAP_AXIS,
    N3DS_MAP_BILERP,
} N3DSMapKind;

typedef struct {
    N3DSMapKind kind;
    Matrix4f transform;
    float axisX;
    float axisY;
    float axisScaleX;
    float axisScaleY;
    float srcLeft;
    float srcTop;
    float srcWidth;
    float srcHeight;
    float x1;
    float y1;
    float x2;
    float y2;
    float x3;
    float y3;
    float x4;
    float y4;
} N3DSDrawMapper;

typedef struct {
    Font* font;
    Sprite* spriteFontSprite;
    N3DSAtlasItem* atlasItem;
} N3DSFontState;

typedef struct {
    bool valid;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} N3DSViewportCache;

typedef struct {
    bool blendEnabled;
    int32_t blendSrc;
    int32_t blendDst;
    int32_t blendSrcAlpha;
    int32_t blendDstAlpha;
    GPU_BLENDEQUATION blendEq;
    bool alphaTestEnabled;
    uint8_t alphaTestRef;
    bool colorWriteR;
    bool colorWriteG;
    bool colorWriteB;
    bool colorWriteA;
} N3DSRenderState;

typedef struct N3DSRenderStateSnapshot N3DSRenderStateSnapshot;

typedef struct {
    Renderer base;
    N3DSC3DDrawContext drawContext;
    N3DSViewportCache viewportCache;
    bool drawContextReady;
    bool ready;
    bool cacheAtlas;
    bool halfResApplicationSurface;
    uint32_t atlasTextureCacheBytes;
    char startupError[192];
    C3D_RenderTarget* topTarget;
    C3D_RenderTarget* bottomTarget;
    bool bottomScreenUsedThisFrame;
    bool gameOnBottomScreen;
    int32_t currentTargetSurfaceId;
    bool currentHostTargetIsBottomScreen;
    int32_t applicationSurfaceId;
    int32_t bottomSurfaceId;
    int32_t frameGameW;
    int32_t frameGameH;
    N3DSTpagBinding* tpagBindings;
    uint32_t tpagBindingCount;
    N3DSAtlas atlas;
    N3DSRoomManifest roomManifest;
    N3DSRoomTileRange* roomTileRanges;
    uint32_t roomTileRangeCount;
    N3DSRoomTileLookupEntry* roomTileLookup;
    int32_t activeRoomIndex;
    uint64_t usageCounter;
    uint32_t frameCounter;
    N3DSSurface* surfaces;
    uint32_t surfaceCount;
    uint32_t nextSurfaceGeneration;
    uint32_t runtimeTpagStart;
    uint32_t* freeRuntimeTpagIndices;
    int32_t* freeSurfaceSpriteSurfaces;
    int32_t* pendingSurfaceFrees;
    int32_t hostCaptureSurfaceId;
    int32_t failedApplicationSurfaceW;
    int32_t failedApplicationSurfaceH;
    N3DSRenderState renderState;
    int32_t currentViewX;
    int32_t currentViewY;
    int32_t currentViewW;
    int32_t currentViewH;
    float currentViewAngle;
    int32_t currentLogicalPortX;
    int32_t currentLogicalPortY;
    int32_t currentLogicalPortW;
    int32_t currentLogicalPortH;
    bool customCameraTransformActive;
    bool viewCullActive;
    Matrix4f screenTransform;
    bool screenTransformActive;
    bool hostViewProjectionActive;
    N3DSRenderStateSnapshot* targetStack;
} N3DSRenderer;

struct N3DSRenderStateSnapshot {
    int32_t targetSurfaceId;
    bool hostBottomScreen;
    int32_t portX;
    int32_t portY;
    int32_t portW;
    int32_t portH;
    int32_t logicalPortX;
    int32_t logicalPortY;
    int32_t logicalPortW;
    int32_t logicalPortH;
    N3DSViewportCache viewportCache;
    C3D_Mtx projection;
    Matrix4f screenTransform;
    bool screenTransformActive;
    bool customCameraTransformActive;
    bool hostViewProjectionActive;
    bool viewCullActive;
    int32_t viewX;
    int32_t viewY;
    int32_t viewW;
    int32_t viewH;
    float viewAngle;
    int32_t cameraCurrent;
    Matrix4f gmlMatrices[MATRICES_MAX];
    N3DSRenderState renderState;
};

static void C3D_surfaceResize(
    Renderer* base, int32_t surfaceID, int32_t width, int32_t height);
static void C3D_getHostViewport(const N3DSRenderer* renderer, int32_t portX,
    int32_t portY, int32_t portW, int32_t portH, int32_t* outX, int32_t* outY, int32_t* outW,
    int32_t* outH);
static void C3D_applyBlendState(N3DSRenderer* renderer);
static void C3D_bindTarget(N3DSRenderer* renderer, int32_t surfaceId, bool bottomScreen);
static void C3D_bindTopScreenTarget(N3DSRenderer* renderer);
static void C3D_updateHostProjection(N3DSRenderer* renderer);
static void C3D_setViewportAndScissor(
    N3DSRenderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h);
static void C3D_setScreenRect(N3DSRenderer* renderer, float srcLeft,
    float srcTop, float srcRight, float srcBottom, float angleDeg, int32_t portX, int32_t portY,
    int32_t portW, int32_t portH);
static bool C3D_setScreenTransform(N3DSRenderer* renderer,
    const Matrix4f* worldToClip, int32_t portX, int32_t portY, int32_t portW, int32_t portH);
static bool C3D_surfaceIdIsLive(const N3DSRenderer* renderer, int32_t surfaceID);
static bool C3D_surfaceBindingMatches(
    const N3DSRenderer* renderer, const N3DSTpagBinding* binding);
static void C3D_destroySurfaceSlot(N3DSRenderer* renderer, int32_t surfaceID);
static void C3D_destroySurfaceSlotNow(N3DSRenderer* renderer, int32_t surfaceID);
static void C3D_queueSurfaceFree(N3DSRenderer* renderer, int32_t surfaceID);
static bool C3D_finishSurfaceUpload(N3DSRenderer* renderer, int32_t surfaceID);
static void C3D_getSurfaceRegion(const N3DSSurface* surface, N3DSUVRect* outRegion);
static float C3D_surfacePhysicalScaleX(const N3DSSurface* surface);
static float C3D_surfacePhysicalScaleY(const N3DSSurface* surface);
static bool C3D_surfaceIsScaled(const N3DSSurface* surface);
static bool C3D_surfaceGetPixels(Renderer* base, int32_t surfaceID, uint8_t* outRGBA);
static int32_t C3D_resolveAppSurface(Renderer* base);
static int32_t C3D_resolveSampleSurface(
    Renderer* base, int32_t surfaceID, bool snapshotApplicationSurface, bool snapshotActiveTarget);
static int32_t C3D_snapshotSurface(Renderer* base, int32_t sourceID);
static bool C3D_getAtlasBinding(
    N3DSRenderer* renderer, int32_t tpagIndex, TexturePageItem** outTpag, N3DSAtlasItem** outItem);
static bool C3D_atlasRangeInRam(
    const N3DSRenderer* renderer, uint32_t offset, uint32_t size, const uint8_t** outBytes);
static bool C3D_loadAtlasPage(
    N3DSRenderer* renderer, uint32_t pageIndex, C3D_Tex** outTex);
static bool C3D_loadRoomWantedAtlasPage(
    N3DSRenderer* renderer, uint32_t pageIndex, C3D_Tex** outTex);
static void C3D_destroyAtlas(N3DSRenderer* renderer);
static void C3D_markAtlasPageUsed(N3DSRenderer* renderer, N3DSAtlasPage* page);
static void C3D_evictAtlasPages(
    N3DSRenderer* renderer, uint32_t neededBytes, uint32_t protectedPage);
static bool C3D_atlasItemIsDrawable(const N3DSRenderer* renderer, uint32_t itemIndex);
static void C3D_setTextureFiltering(C3D_Tex* tex, bool linear);
static void C3D_drawTextInternal(Renderer* base, const char* text, float x, float y,
    float xscale, float yscale, float angleDeg, uint32_t color, float alpha, float lineSeparation);

static void C3D_safeFrameSplit(N3DSRenderer* renderer) {
    if (renderer->currentTargetSurfaceId == INT32_MIN) return;
    n3ds_c3dFlush(&renderer->drawContext);
    n3ds_c3dFrameSplit(&renderer->drawContext);
    C3D_applyBlendState(renderer);
}

static void C3D_markSurfaceReady(N3DSRenderer* renderer, int32_t surfaceID) {
    if (surfaceID < 0 || (uint32_t)surfaceID >= renderer->surfaceCount ||
        !renderer->surfaces[surfaceID].used)
        return;
    C3D_TexFlush(&renderer->surfaces[surfaceID].tex);
    renderer->surfaces[surfaceID].samplingReady = true;
}

static void C3D_invalidateViewportCache(N3DSRenderer* renderer) {
    renderer->viewportCache.valid = false;
}

static void C3D_setViewportAndScissor(
    N3DSRenderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    N3DSViewportCache* cache = &renderer->viewportCache;
    if (cache->valid && cache->x == x && cache->y == y && cache->width == w &&
        cache->height == h) {
        return;
    }
    n3ds_c3dFlush(&renderer->drawContext);
    C3D_SetViewport((u32)x, (u32)y, (u32)w, (u32)h);
    C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)x, (u32)y, (u32)(x + w), (u32)(y + h));
    cache->valid = true;
    cache->x = x;
    cache->y = y;
    cache->width = w;
    cache->height = h;
}

static void C3D_setViewportClamped(
    N3DSRenderer* renderer, int32_t x, int32_t y, int32_t w, int32_t h, int32_t maxW,
    int32_t maxH) {
    if (maxW < 1) maxW = 1;
    if (maxH < 1) maxH = 1;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x >= maxW) x = maxW - 1;
    if (y >= maxH) y = maxH - 1;
    if (x + w > maxW) w = maxW - x;
    if (y + h > maxH) h = maxH - y;
    C3D_setViewportAndScissor(renderer, x, y, w, h);
}

static void C3D_setHostViewport(N3DSRenderer* renderer) {
    C3D_RenderTarget* target =
        renderer->currentHostTargetIsBottomScreen ? renderer->bottomTarget : renderer->topTarget;
    if (target == NULL) return;
    C3D_setViewportAndScissor(
        renderer, 0, 0, target->frameBuf.width, target->frameBuf.height);
}

static void C3D_restoreTargetViewport(N3DSRenderer* renderer, int32_t targetSurfaceId,
    bool hostBottomScreen, int32_t portX, int32_t portY, int32_t portW, int32_t portH) {
    if (targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER) {
        renderer->currentHostTargetIsBottomScreen = hostBottomScreen;
        C3D_setHostViewport(renderer);
        return;
    }
    if (targetSurfaceId >= 0 && (uint32_t)targetSurfaceId < renderer->surfaceCount &&
        renderer->surfaces[targetSurfaceId].used &&
        renderer->surfaces[targetSurfaceId].target != NULL) {
        N3DSSurface* surface = &renderer->surfaces[targetSurfaceId];
        float scaleX = C3D_surfacePhysicalScaleX(surface);
        float scaleY = C3D_surfacePhysicalScaleY(surface);
        int32_t physX = (int32_t)floorf((float)portX * scaleX + 0.0001f);
        int32_t physY = (int32_t)floorf((float)portY * scaleY + 0.0001f);
        int32_t physW = (int32_t)ceilf((float)(portX + portW) * scaleX - (float)physX);
        int32_t physH = (int32_t)ceilf((float)(portY + portH) * scaleY - (float)physY);
        int32_t viewportY = surface->target->frameBuf.height - physY - physH;
        C3D_setViewportClamped(renderer, physX, viewportY, physW, physH,
            surface->target->frameBuf.width, surface->target->frameBuf.height);
        return;
    }
    C3D_setViewportAndScissor(renderer, portX, portY, portW, portH);
}

static void C3D_saveRenderState(N3DSRenderer* renderer, N3DSRenderStateSnapshot* out) {
    if (out == NULL) return;
    out->targetSurfaceId = renderer->currentTargetSurfaceId;
    out->hostBottomScreen = renderer->currentHostTargetIsBottomScreen;
    out->portX = renderer->base.CPortX;
    out->portY = renderer->base.CPortY;
    out->portW = renderer->base.CPortW;
    out->portH = renderer->base.CPortH;
    out->logicalPortX = renderer->currentLogicalPortX;
    out->logicalPortY = renderer->currentLogicalPortY;
    out->logicalPortW = renderer->currentLogicalPortW;
    out->logicalPortH = renderer->currentLogicalPortH;
    out->viewportCache = renderer->viewportCache;
    out->projection = renderer->drawContext.projection;
    out->screenTransform = renderer->screenTransform;
    out->screenTransformActive = renderer->screenTransformActive;
    out->customCameraTransformActive = renderer->customCameraTransformActive;
    out->hostViewProjectionActive = renderer->hostViewProjectionActive;
    out->viewCullActive = renderer->viewCullActive;
    out->viewX = renderer->currentViewX;
    out->viewY = renderer->currentViewY;
    out->viewW = renderer->currentViewW;
    out->viewH = renderer->currentViewH;
    out->viewAngle = renderer->currentViewAngle;
    out->cameraCurrent = renderer->base.cameraCurrent;
    memcpy(out->gmlMatrices, renderer->base.gmlMatrices, sizeof(out->gmlMatrices));
    out->renderState = renderer->renderState;
}

static void C3D_beginSurfaceCopyState(N3DSRenderer* renderer) {
    renderer->renderState.blendEnabled = false;
    renderer->renderState.alphaTestEnabled = false;
    renderer->renderState.colorWriteR = true;
    renderer->renderState.colorWriteG = true;
    renderer->renderState.colorWriteB = true;
    renderer->renderState.colorWriteA = true;
    C3D_applyBlendState(renderer);
}

static void C3D_restoreBlendState(
    N3DSRenderer* renderer, const N3DSRenderState* state) {
    if (state == NULL) return;
    renderer->renderState = *state;
    C3D_applyBlendState(renderer);
}

static void C3D_restoreViewState(
    N3DSRenderer* renderer, const N3DSRenderStateSnapshot* state, int32_t readySurfaceId) {
    if (state == NULL) return;
    if (state->targetSurfaceId != INT32_MIN)
        C3D_bindTarget(renderer, state->targetSurfaceId, state->hostBottomScreen);
    else {
        n3ds_c3dFrameSplit(&renderer->drawContext);
        if (readySurfaceId >= 0) C3D_markSurfaceReady(renderer, readySurfaceId);
        renderer->currentTargetSurfaceId = INT32_MIN;
        renderer->currentHostTargetIsBottomScreen = state->hostBottomScreen;
    }
    renderer->base.CPortX = state->portX;
    renderer->base.CPortY = state->portY;
    renderer->base.CPortW = state->portW;
    renderer->base.CPortH = state->portH;
    renderer->currentLogicalPortX = state->logicalPortX;
    renderer->currentLogicalPortY = state->logicalPortY;
    renderer->currentLogicalPortW = state->logicalPortW;
    renderer->currentLogicalPortH = state->logicalPortH;
    if (state->viewportCache.valid) {
        C3D_setViewportAndScissor(renderer, state->viewportCache.x,
            state->viewportCache.y, state->viewportCache.width, state->viewportCache.height);
    } else {
        C3D_restoreTargetViewport(renderer, state->targetSurfaceId,
            state->hostBottomScreen, state->logicalPortX, state->logicalPortY,
            state->logicalPortW, state->logicalPortH);
    }
    n3ds_c3dSetProjectionC3D(&renderer->drawContext, &state->projection);
    renderer->screenTransform = state->screenTransform;
    renderer->screenTransformActive = state->screenTransformActive;
    renderer->customCameraTransformActive = state->customCameraTransformActive;
    renderer->hostViewProjectionActive = state->hostViewProjectionActive;
    renderer->viewCullActive = state->viewCullActive;
    renderer->currentViewX = state->viewX;
    renderer->currentViewY = state->viewY;
    renderer->currentViewW = state->viewW;
    renderer->currentViewH = state->viewH;
    renderer->currentViewAngle = state->viewAngle;
    renderer->base.cameraCurrent = state->cameraCurrent;
    memcpy(renderer->base.gmlMatrices, state->gmlMatrices, sizeof(state->gmlMatrices));
}

static void C3D_restoreRenderState(
    N3DSRenderer* renderer, const N3DSRenderStateSnapshot* state, int32_t readySurfaceId) {
    if (state == NULL) return;
    C3D_restoreViewState(renderer, state, readySurfaceId);
    C3D_restoreBlendState(renderer, &state->renderState);
}

static uint32_t C3D_hashU16(uint32_t hash, uint16_t value) {
    hash ^= (uint8_t)(value & 0xFFu);
    hash *= 16777619u;
    hash ^= (uint8_t)((value >> 8) & 0xFFu);
    hash *= 16777619u;
    return hash;
}

static uint32_t C3D_hashI16(uint32_t hash, int16_t value) {
    return C3D_hashU16(hash, (uint16_t)value);
}

static uint32_t C3D_computeTpagHash(const DataWin* dataWin) {
    uint32_t hash = 2166136261u;
    if (dataWin == NULL) return hash;
    hash ^= dataWin->tpag.count;
    hash *= 16777619u;
    for (uint32_t i = 0; i < dataWin->tpag.count; ++i) {
        const TexturePageItem* item = &dataWin->tpag.items[i];
        hash = C3D_hashU16(hash, item->sourceX);
        hash = C3D_hashU16(hash, item->sourceY);
        hash = C3D_hashU16(hash, item->sourceWidth);
        hash = C3D_hashU16(hash, item->sourceHeight);
        hash = C3D_hashU16(hash, item->targetX);
        hash = C3D_hashU16(hash, item->targetY);
        hash = C3D_hashU16(hash, item->targetWidth);
        hash = C3D_hashU16(hash, item->targetHeight);
        hash = C3D_hashU16(hash, item->boundingWidth);
        hash = C3D_hashU16(hash, item->boundingHeight);
        hash = C3D_hashI16(hash, item->texturePageId);
    }
    return hash != 0u ? hash : 1u;
}

static void C3D_setTextureFiltering(C3D_Tex* tex, bool linear) {
    if (tex == NULL) return;
    C3D_TexSetFilter(tex, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
}

static uint32_t C3D_makeColor(uint32_t bgr, float alpha) {
    uint8_t r = (uint8_t)BGR_R(bgr);
    uint8_t g = (uint8_t)BGR_G(bgr);
    uint8_t b = (uint8_t)BGR_B(bgr);
    uint8_t a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f + 0.5f);
    return C2D_Color32(r, g, b, a);
}

static uint32_t C3D_makeClearColor(uint32_t bgr, float alpha) {
    uint8_t r = (uint8_t)BGR_R(bgr);
    uint8_t g = (uint8_t)BGR_G(bgr);
    uint8_t b = (uint8_t)BGR_B(bgr);
    uint8_t a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f + 0.5f);
    return n3ds_c3dClearColor(r, g, b, a);
}

static bool C3D_isPow2U16(uint16_t value) {
    return value != 0u && (value & (uint16_t)(value - 1u)) == 0u;
}

static bool C3D_validAtlasPageSize(uint16_t width, uint16_t height) {
    return width >= 16u && width <= N3DS_MAX_TEXTURE_SIZE && height >= 16u &&
        height <= N3DS_MAX_TEXTURE_SIZE &&
        C3D_isPow2U16(width) && C3D_isPow2U16(height);
}

static bool C3D_validAtlasPageFormat(uint32_t pageFormat) {
    switch (pageFormat) {
    case N3DS_ATLAS_PAGE_FORMAT_ETC1A4:
    case N3DS_ATLAS_PAGE_FORMAT_L4:
    case N3DS_ATLAS_PAGE_FORMAT_LA4:
    case N3DS_ATLAS_PAGE_FORMAT_RGBA4444:
    case N3DS_ATLAS_PAGE_FORMAT_ETC1:
        return true;
    default:
        return false;
    }
}

static uint32_t C3D_atlasNativeFormat(uint32_t pageFormat) {
    switch (pageFormat) {
    case N3DS_ATLAS_PAGE_FORMAT_ETC1A4:
        return N3DS_NATIVE_TEXFMT_ETC1A4;
    case N3DS_ATLAS_PAGE_FORMAT_L4:
        return N3DS_NATIVE_TEXFMT_L4;
    case N3DS_ATLAS_PAGE_FORMAT_LA4:
        return N3DS_NATIVE_TEXFMT_LA4;
    case N3DS_ATLAS_PAGE_FORMAT_RGBA4444:
        return N3DS_NATIVE_TEXFMT_RGBA4444;
    case N3DS_ATLAS_PAGE_FORMAT_ETC1:
        return N3DS_NATIVE_TEXFMT_ETC1;
    default:
        return UINT32_MAX;
    }
}

static void C3D_letterboxRect(int32_t srcW, int32_t srcH, int32_t dstW, int32_t dstH,
    float* outLeft, float* outTop, float* outRight, float* outBottom) {
    if (outLeft == NULL || outTop == NULL || outRight == NULL || outBottom == NULL || srcW <= 0 ||
        srcH <= 0 || dstW <= 0 || dstH <= 0)
        return;
    int32_t scaledW = dstW;
    int32_t scaledH = dstH;
    if (((int64_t)srcW * (int64_t)dstH) < ((int64_t)dstW * (int64_t)srcH))
        scaledW = (int32_t)(((int64_t)srcW * (int64_t)dstH) / (int64_t)srcH);
    else
        scaledH = (int32_t)(((int64_t)srcH * (int64_t)dstW) / (int64_t)srcW);
    *outLeft = (float)((dstW - scaledW) / 2);
    *outTop = (float)((dstH - scaledH) / 2);
    *outRight = *outLeft + (float)scaledW;
    *outBottom = *outTop + (float)scaledH;
}

static uint32_t C3D_readLe32(const uint8_t* src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 24);
}

static uint16_t C3D_readLe16(const uint8_t* src) {
    return (uint16_t)src[0] | (uint16_t)((uint16_t)src[1] << 8);
}

static GPU_BLENDFACTOR C3D_blendFactorFromGml(int32_t factor) {
    switch (factor) {
    case bm_zero:
        return GPU_ZERO;
    case bm_one:
        return GPU_ONE;
    case bm_src_color:
        return GPU_SRC_COLOR;
    case bm_inv_src_color:
        return GPU_ONE_MINUS_SRC_COLOR;
    case bm_src_alpha:
        return GPU_SRC_ALPHA;
    case bm_inv_src_alpha:
        return GPU_ONE_MINUS_SRC_ALPHA;
    case bm_dest_alpha:
        return GPU_DST_ALPHA;
    case bm_inv_dest_alpha:
        return GPU_ONE_MINUS_DST_ALPHA;
    case bm_dest_color:
        return GPU_DST_COLOR;
    case bm_inv_dest_color:
        return GPU_ONE_MINUS_DST_COLOR;
    case bm_src_alpha_sat:
        return GPU_SRC_ALPHA_SATURATE;
    default:
        return GPU_ONE;
    }
}

static void C3D_setError(N3DSRenderer* renderer, const char* text) {
    snprintf(renderer->startupError, sizeof(renderer->startupError), "%s",
        text != NULL ? text : "3ds renderer error");
    renderer->ready = false;
}

static uint32_t C3D_makeTextureClearColor(uint32_t bgr, float alpha, GPU_TEXCOLOR format) {
    uint8_t r = (uint8_t)BGR_R(bgr);
    uint8_t g = (uint8_t)BGR_G(bgr);
    uint8_t b = (uint8_t)BGR_B(bgr);
    uint8_t a = (uint8_t)(fmaxf(0.0f, fminf(alpha, 1.0f)) * 255.0f + 0.5f);
    switch (format) {
    case GPU_RGBA4: {
        uint16_t packed = (uint16_t)((((uint16_t)(r >> 4)) << 12) | (((uint16_t)(g >> 4)) << 8) |
            (((uint16_t)(b >> 4)) << 4) | (uint16_t)(a >> 4));
        return ((uint32_t)packed << 16) | (uint32_t)packed;
    }
    case GPU_RGB8:
        return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8);
    case GPU_RGBA8:
    default:
        return C3D_makeClearColor(bgr, alpha);
    }
}

static GX_TRANSFER_FORMAT C3D_transferFormatForTex(GPU_TEXCOLOR format) {
    switch (format) {
    case GPU_RGB8:
        return GX_TRANSFER_FMT_RGB8;
    case GPU_RGBA4:
        return GX_TRANSFER_FMT_RGBA4;
    case GPU_RGBA8:
    default:
        return GX_TRANSFER_FMT_RGBA8;
    }
}

static void C3D_uvFromNative(
    N3DSUVRect* rect, uint16_t width, uint16_t height, uint16_t texWidth, uint16_t texHeight) {
    if (rect == NULL) return;
    rect->width = width;
    rect->height = height;
    rect->left = 0.0f;
    rect->right = texWidth > 0 ? (float)width / (float)texWidth : 1.0f;
    rect->top = 1.0f;
    rect->bottom = texHeight > 0 ? 1.0f - (float)height / (float)texHeight : 0.0f;
}

static uint16_t C3D_scaleDownHalf16(uint16_t value) {
    return value > 1u ? (uint16_t)((value + 1u) / 2u) : 1u;
}

static void C3D_uvClip(const N3DSUVRect* rect, float srcX0, float srcY0, float srcX1,
    float srcY1, float* u1, float* v1, float* u2, float* v2, float* u3, float* v3, float* u4,
    float* v4) {
    if (rect == NULL || u1 == NULL || v1 == NULL || u2 == NULL || v2 == NULL || u3 == NULL ||
        v3 == NULL || u4 == NULL || v4 == NULL)
        return;
    float tx0 = rect->width > 0 ? srcX0 / (float)rect->width : 0.0f;
    float ty0 = rect->height > 0 ? srcY0 / (float)rect->height : 0.0f;
    float tx1 = rect->width > 0 ? srcX1 / (float)rect->width : 0.0f;
    float ty1 = rect->height > 0 ? srcY1 / (float)rect->height : 0.0f;
    float du = rect->right - rect->left;
    float dv = rect->bottom - rect->top;
    *u1 = rect->left + du * tx0;
    *v1 = rect->top + dv * ty0;
    *u2 = rect->left + du * tx1;
    *v2 = rect->top + dv * ty0;
    *u3 = rect->left + du * tx0;
    *v3 = rect->top + dv * ty1;
    *u4 = rect->left + du * tx1;
    *v4 = rect->top + dv * ty1;
}

static void C3D_uvSubRect(N3DSUVRect* outRect, const N3DSUVRect* rect, float srcX0,
    float srcY0, float srcX1, float srcY1) {
    if (outRect == NULL || rect == NULL) return;
    float u1, v1, u2, v2, u3, v3, u4, v4;
    C3D_uvClip(rect, srcX0, srcY0, srcX1, srcY1, &u1, &v1, &u2, &v2, &u3, &v3, &u4, &v4);
    outRect->left = u1;
    outRect->top = v1;
    outRect->right = u4;
    outRect->bottom = v4;
    outRect->width = (uint16_t)fmaxf(0.0f, srcX1 - srcX0);
    outRect->height = (uint16_t)fmaxf(0.0f, srcY1 - srcY0);
}

static void C3D_mapInitAffine(N3DSDrawMapper* mapper, const Matrix4f* transform) {
    if (mapper == NULL || transform == NULL) return;
    mapper->kind = N3DS_MAP_AFFINE;
    mapper->transform = *transform;
}

static void C3D_mapInitAxis(
    N3DSDrawMapper* mapper, float x, float y, float xscale, float yscale) {
    if (mapper == NULL) return;
    mapper->kind = N3DS_MAP_AXIS;
    mapper->axisX = x;
    mapper->axisY = y;
    mapper->axisScaleX = xscale;
    mapper->axisScaleY = yscale;
}

static void C3D_mapInitBilerp(N3DSDrawMapper* mapper, float srcLeft, float srcTop,
    float srcRight, float srcBottom, float x1, float y1, float x2, float y2, float x3, float y3,
    float x4, float y4) {
    if (mapper == NULL) return;
    mapper->kind = N3DS_MAP_BILERP;
    mapper->srcLeft = srcLeft;
    mapper->srcTop = srcTop;
    mapper->srcWidth = srcRight - srcLeft;
    mapper->srcHeight = srcBottom - srcTop;
    mapper->x1 = x1;
    mapper->y1 = y1;
    mapper->x2 = x2;
    mapper->y2 = y2;
    mapper->x3 = x3;
    mapper->y3 = y3;
    mapper->x4 = x4;
    mapper->y4 = y4;
}

static void C3D_mapInitSprite(
    N3DSDrawMapper* mapper, float x, float y, float xscale, float yscale, float angleDeg) {
    if (fabsf(angleDeg) <= 0.0001f) {
        C3D_mapInitAxis(mapper, x, y, xscale, yscale);
        return;
    }
    Matrix4f transform;
    Matrix4f_setTransform2D(&transform, x, y, xscale, yscale, -angleDeg * (float)M_PI / 180.0f);
    C3D_mapInitAffine(mapper, &transform);
}

static void C3D_mapInitPart(N3DSDrawMapper* mapper, float srcW, float srcH, float x,
    float y, float xscale, float yscale, float angleDeg, float pivotX, float pivotY) {
    if (fabsf(angleDeg) <= 0.0001f) {
        C3D_mapInitAxis(mapper, x, y, xscale, yscale);
        return;
    }

    float qx1 = x;
    float qy1 = y;
    float qx2 = x + srcW * xscale;
    float qy2 = y;
    float qx3 = qx2;
    float qy3 = y + srcH * yscale;
    float qx4 = x;
    float qy4 = qy3;
    float angleRad = -angleDeg * (float)M_PI / 180.0f;
    float cosA = cosf(angleRad);
    float sinA = sinf(angleRad);
    float dx = qx1 - pivotX;
    float dy = qy1 - pivotY;
    float x1 = cosA * dx - sinA * dy + pivotX;
    float y1 = sinA * dx + cosA * dy + pivotY;
    dx = qx2 - pivotX;
    dy = qy2 - pivotY;
    float x2 = cosA * dx - sinA * dy + pivotX;
    float y2 = sinA * dx + cosA * dy + pivotY;
    dx = qx3 - pivotX;
    dy = qy3 - pivotY;
    float x3 = cosA * dx - sinA * dy + pivotX;
    float y3 = sinA * dx + cosA * dy + pivotY;
    dx = qx4 - pivotX;
    dy = qy4 - pivotY;
    float x4 = cosA * dx - sinA * dy + pivotX;
    float y4 = sinA * dx + cosA * dy + pivotY;
    C3D_mapInitBilerp(
        mapper, 0.0f, 0.0f, (float)srcW, (float)srcH, x1, y1, x2, y2, x4, y4, x3, y3);
}

static void C3D_lerpQuadPoint(float x1, float y1, float x2, float y2, float x3, float y3,
    float x4, float y4, float u, float v, float* outX, float* outY) {
    float topX = x1 + (x2 - x1) * u;
    float topY = y1 + (y2 - y1) * u;
    float bottomX = x3 + (x4 - x3) * u;
    float bottomY = y3 + (y4 - y3) * u;
    *outX = topX + (bottomX - topX) * v;
    *outY = topY + (bottomY - topY) * v;
}

static void C3D_transformPoint(N3DSRenderer* renderer, float* x, float* y) {
    if (x == NULL || y == NULL || !renderer->screenTransformActive) return;
    float tx = 0.0f;
    float ty = 0.0f;
    Matrix4f_transformPoint(&renderer->screenTransform, *x, *y, &tx, &ty);
    *x = tx;
    *y = ty;
}

static void C3D_makeSurfaceProjection(
    C3D_Mtx* out, float viewX, float viewY, float viewW, float viewH, float viewAngle) {
    if (out == NULL) return;
    Mtx_Identity(out);
    Mtx_Ortho(out, viewX, viewX + viewW, viewY + viewH, viewY, -1.0f, 1.0f, true);
    if (viewAngle != 0.0f) {
        float cx = viewX + viewW * 0.5f;
        float cy = viewY + viewH * 0.5f;
        C3D_Mtx rot;
        C3D_Mtx res;
        Mtx_Identity(&rot);
        Mtx_Translate(&rot, cx, cy, 0.0f, true);
        Mtx_RotateZ(&rot, -viewAngle * (float)M_PI / 180.0f, true);
        Mtx_Translate(&rot, -cx, -cy, 0.0f, true);
        Mtx_Multiply(&res, out, &rot);
        *out = res;
    }
}

static void C3D_makeSurfaceGuiProj(C3D_Mtx* out, int32_t guiW, int32_t guiH) {
    if (out == NULL) return;
    Mtx_Identity(out);
    Mtx_Ortho(out, 0.0f, (float)guiW, (float)guiH, 0.0f, -1.0f, 1.0f, true);
}

static void C3D_setScreenRect(N3DSRenderer* renderer, float srcLeft,
    float srcTop, float srcRight, float srcBottom, float angleDeg, int32_t portX, int32_t portY,
    int32_t portW, int32_t portH) {
    float srcW = srcRight - srcLeft;
    float srcH = srcBottom - srcTop;
    if (fabsf(srcW) < 0.0001f || fabsf(srcH) < 0.0001f) {
        renderer->screenTransformActive = false;
        return;
    }
    float scaleX = (float)portW / srcW;
    float scaleY = (float)portH / srcH;
    float srcCenterX = srcLeft + srcW * 0.5f;
    float srcCenterY = srcTop + srcH * 0.5f;
    float dstCenterX = (float)portX + (float)portW * 0.5f;
    float dstCenterY = (float)portY + (float)portH * 0.5f;
    Matrix4f_identity(&renderer->screenTransform);
    Matrix4f_translate(&renderer->screenTransform, dstCenterX, dstCenterY, 0.0f);
    Matrix4f_scale(&renderer->screenTransform, scaleX, scaleY, 1.0f);
    if (angleDeg != 0.0f)
        Matrix4f_rotateZ(&renderer->screenTransform, -angleDeg * (float)M_PI / 180.0f);
    Matrix4f_translate(&renderer->screenTransform, -srcCenterX, -srcCenterY, 0.0f);
    renderer->screenTransformActive = true;
}

static void C3D_computeGuiSourceRect(int32_t guiW, int32_t guiH, int32_t portW,
    int32_t portH, float* outLeft, float* outTop, float* outRight, float* outBottom) {
    if (outLeft == NULL || outTop == NULL || outRight == NULL || outBottom == NULL) return;
    float left = 0.0f;
    float right = (float)guiW;
    float top = 0.0f;
    float bottom = (float)guiH;
    if (guiW > 0 && guiH > 0 && portW > 0 && portH > 0) {
        float viewAspect = (float)portW / (float)portH;
        float guiAspect = (float)guiW / (float)guiH;
        if (viewAspect > guiAspect) {
            float margin = ((float)guiH * viewAspect - (float)guiW) * 0.5f;
            left = -margin;
            right = (float)guiW + margin;
        } else if (viewAspect < guiAspect) {
            float margin = ((float)guiW / viewAspect - (float)guiH) * 0.5f;
            top = -margin;
            bottom = (float)guiH + margin;
        }
    }
    *outLeft = left;
    *outTop = top;
    *outRight = right;
    *outBottom = bottom;
}

static void C3D_mapPoint(
    const N3DSDrawMapper* mapper, float x, float y, float* outX, float* outY) {
    if (mapper == NULL || outX == NULL || outY == NULL) return;
    if (mapper->kind == N3DS_MAP_AFFINE) {
        Matrix4f_transformPoint(&mapper->transform, x, y, outX, outY);
        return;
    }
    if (mapper->kind == N3DS_MAP_AXIS) {
        *outX = mapper->axisX + x * mapper->axisScaleX;
        *outY = mapper->axisY + y * mapper->axisScaleY;
        return;
    }
    float u = mapper->srcWidth != 0.0f ? (x - mapper->srcLeft) / mapper->srcWidth : 0.0f;
    float v = mapper->srcHeight != 0.0f ? (y - mapper->srcTop) / mapper->srcHeight : 0.0f;
    C3D_lerpQuadPoint(mapper->x1, mapper->y1, mapper->x2, mapper->y2, mapper->x3,
        mapper->y3, mapper->x4, mapper->y4, u, v, outX, outY);
}

static void C3D_mapRect(N3DSRenderer* renderer, const N3DSDrawMapper* mapper, float left,
    float top, float right, float bottom, float* x1, float* y1, float* x2, float* y2, float* x3,
    float* y3, float* x4, float* y4) {
    if (mapper != NULL && mapper->kind == N3DS_MAP_AXIS) {
        *x1 = mapper->axisX + left * mapper->axisScaleX;
        *y1 = mapper->axisY + top * mapper->axisScaleY;
        *x2 = mapper->axisX + right * mapper->axisScaleX;
        *y2 = *y1;
        *x3 = *x1;
        *y3 = mapper->axisY + bottom * mapper->axisScaleY;
        *x4 = *x2;
        *y4 = *y3;
        if (renderer->screenTransformActive) {
            const Matrix4f* m = &renderer->screenTransform;
            float ox1 = *x1, oy1 = *y1;
            float ox2 = *x2, oy2 = *y2;
            float ox3 = *x3, oy3 = *y3;
            float ox4 = *x4, oy4 = *y4;
            *x1 = m->m[0] * ox1 + m->m[4] * oy1 + m->m[12];
            *y1 = m->m[1] * ox1 + m->m[5] * oy1 + m->m[13];
            *x2 = m->m[0] * ox2 + m->m[4] * oy2 + m->m[12];
            *y2 = m->m[1] * ox2 + m->m[5] * oy2 + m->m[13];
            *x3 = m->m[0] * ox3 + m->m[4] * oy3 + m->m[12];
            *y3 = m->m[1] * ox3 + m->m[5] * oy3 + m->m[13];
            *x4 = m->m[0] * ox4 + m->m[4] * oy4 + m->m[12];
            *y4 = m->m[1] * ox4 + m->m[5] * oy4 + m->m[13];
        }
        return;
    }
    C3D_mapPoint(mapper, left, top, x1, y1);
    C3D_mapPoint(mapper, right, top, x2, y2);
    C3D_mapPoint(mapper, left, bottom, x3, y3);
    C3D_mapPoint(mapper, right, bottom, x4, y4);
    if (renderer->screenTransformActive) {
        const Matrix4f* m = &renderer->screenTransform;
        float ox1 = *x1, oy1 = *y1;
        float ox2 = *x2, oy2 = *y2;
        float ox3 = *x3, oy3 = *y3;
        float ox4 = *x4, oy4 = *y4;
        *x1 = m->m[0] * ox1 + m->m[4] * oy1 + m->m[12];
        *y1 = m->m[1] * ox1 + m->m[5] * oy1 + m->m[13];
        *x2 = m->m[0] * ox2 + m->m[4] * oy2 + m->m[12];
        *y2 = m->m[1] * ox2 + m->m[5] * oy2 + m->m[13];
        *x3 = m->m[0] * ox3 + m->m[4] * oy3 + m->m[12];
        *y3 = m->m[1] * ox3 + m->m[5] * oy3 + m->m[13];
        *x4 = m->m[0] * ox4 + m->m[4] * oy4 + m->m[12];
        *y4 = m->m[1] * ox4 + m->m[5] * oy4 + m->m[13];
    }
}

static bool C3D_quadVisible(const N3DSRenderer* renderer, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4) {
    if (!renderer->viewCullActive) return true;
    if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2) || !isfinite(x3) ||
        !isfinite(y3) || !isfinite(x4) || !isfinite(y4))
        return true;
    float minX = x1;
    float maxX = x1;
    float minY = y1;
    float maxY = y1;
    if (x2 < minX) minX = x2;
    if (x3 < minX) minX = x3;
    if (x4 < minX) minX = x4;
    if (x2 > maxX) maxX = x2;
    if (x3 > maxX) maxX = x3;
    if (x4 > maxX) maxX = x4;
    if (y2 < minY) minY = y2;
    if (y3 < minY) minY = y3;
    if (y4 < minY) minY = y4;
    if (y2 > maxY) maxY = y2;
    if (y3 > maxY) maxY = y3;
    if (y4 > maxY) maxY = y4;
    float clipLeft = 0.0f;
    float clipTop = 0.0f;
    float clipRight = 0.0f;
    float clipBottom = 0.0f;
    if (renderer->screenTransformActive) {
        clipLeft = (float)renderer->base.CPortX;
        clipTop = (float)renderer->base.CPortY;
        clipRight = clipLeft + (float)renderer->base.CPortW;
        clipBottom = clipTop + (float)renderer->base.CPortH;
    } else {
        if (fabsf(renderer->currentViewAngle) > 0.0001f) return true;
        clipLeft = (float)renderer->currentViewX;
        clipTop = (float)renderer->currentViewY;
        clipRight = clipLeft + (float)renderer->currentViewW;
        clipBottom = clipTop + (float)renderer->currentViewH;
    }
    if (clipRight < clipLeft) {
        float t = clipLeft;
        clipLeft = clipRight;
        clipRight = t;
    }
    if (clipBottom < clipTop) {
        float t = clipTop;
        clipTop = clipBottom;
        clipBottom = t;
    }
    if (clipRight <= clipLeft || clipBottom <= clipTop) return true;
    return maxX > clipLeft && minX < clipRight && maxY > clipTop && minY < clipBottom;
}

static bool C3D_axisRectVisible(
    const N3DSRenderer* renderer, float minX, float minY, float maxX, float maxY) {
    if (!renderer->viewCullActive) return true;
    if (!isfinite(minX) || !isfinite(minY) || !isfinite(maxX) || !isfinite(maxY)) return true;
    float clipLeft = 0.0f;
    float clipTop = 0.0f;
    float clipRight = 0.0f;
    float clipBottom = 0.0f;
    if (fabsf(renderer->currentViewAngle) > 0.0001f) return true;
    clipLeft = (float)renderer->currentViewX;
    clipTop = (float)renderer->currentViewY;
    clipRight = clipLeft + (float)renderer->currentViewW;
    clipBottom = clipTop + (float)renderer->currentViewH;
    if (clipRight < clipLeft) {
        float t = clipLeft;
        clipLeft = clipRight;
        clipRight = t;
    }
    if (clipBottom < clipTop) {
        float t = clipTop;
        clipTop = clipBottom;
        clipBottom = t;
    }
    if (clipRight <= clipLeft || clipBottom <= clipTop) return true;
    return maxX > clipLeft && minX < clipRight && maxY > clipTop && minY < clipBottom;
}

bool C3D_getTileCullRect(const Renderer* base, float rect[4]) {
    const N3DSRenderer* renderer = (const N3DSRenderer*)base;
    if (!renderer->viewCullActive || renderer->screenTransformActive ||
        renderer->currentViewAngle != 0.0f || renderer->currentViewW <= 0 ||
        renderer->currentViewH <= 0) return false;

    rect[0] = (float)renderer->currentViewX;
    rect[1] = (float)renderer->currentViewY;
    rect[2] = rect[0] + (float)renderer->currentViewW;
    rect[3] = rect[1] + (float)renderer->currentViewH;
    return true;
}

static void C3D_emitMappedQuad(N3DSRenderer* renderer, C3D_Tex* tex,
    const N3DSUVRect* rect, const N3DSDrawMapper* mapper, float srcLeft, float srcTop,
    float srcRight, float srcBottom, float dstLeft, float dstTop, float dstRight, float dstBottom,
    uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4) {
    if (tex == NULL || rect == NULL || mapper == NULL) return;
    float x1, y1, x2, y2, x3, y3, x4, y4;
    float u1, v1, u2, v2, u3, v3, u4, v4;
    C3D_mapRect(renderer, mapper, dstLeft, dstTop, dstRight, dstBottom, &x1, &y1, &x2, &y2,
        &x3, &y3, &x4, &y4);
    if (!C3D_quadVisible(renderer, x1, y1, x2, y2, x3, y3, x4, y4)) return;
    C3D_uvClip(
        rect, srcLeft, srcTop, srcRight, srcBottom, &u1, &v1, &u2, &v2, &u3, &v3, &u4, &v4);
    const N3DSVertex vertices[4] = {
        {{x1, y1}, {u1, v1}},
        {{x2, y2}, {u2, v2}},
        {{x3, y3}, {u3, v3}},
        {{x4, y4}, {u4, v4}},
    };
    const uint32_t colors[4] = {c1, c2, c3, c4};
    n3ds_c3dQuadGradientUV(&renderer->drawContext, tex, vertices, colors);
}

static bool C3D_mapVisibleSpriteQuad(N3DSRenderer* renderer, const N3DSDrawMapper* mapper,
    float dstLeft, float dstTop, float dstRight, float dstBottom, float* x1, float* y1, float* x2,
    float* y2, float* x3, float* y3, float* x4, float* y4) {
    if (mapper == NULL || x1 == NULL || y1 == NULL || x2 == NULL ||
        y2 == NULL || x3 == NULL || y3 == NULL || x4 == NULL || y4 == NULL)
        return false;
    C3D_mapRect(
        renderer, mapper, dstLeft, dstTop, dstRight, dstBottom, x1, y1, x2, y2, x3, y3, x4, y4);
    return C3D_quadVisible(renderer, *x1, *y1, *x2, *y2, *x3, *y3, *x4, *y4);
}

static bool C3D_mappedRectVisible(N3DSRenderer* renderer,
    const N3DSDrawMapper* mapper, float dstLeft, float dstTop, float dstRight, float dstBottom) {
    if (mapper == NULL) return false;
    if (!renderer->viewCullActive) return true;
    if (mapper->kind == N3DS_MAP_AXIS && !renderer->screenTransformActive) {
        float left = mapper->axisX + dstLeft * mapper->axisScaleX;
        float top = mapper->axisY + dstTop * mapper->axisScaleY;
        float right = mapper->axisX + dstRight * mapper->axisScaleX;
        float bottom = mapper->axisY + dstBottom * mapper->axisScaleY;
        return C3D_axisRectVisible(renderer, left < right ? left : right,
            top < bottom ? top : bottom, left > right ? left : right,
            top > bottom ? top : bottom);
    }
    float x1, y1, x2, y2, x3, y3, x4, y4;
    return C3D_mapVisibleSpriteQuad(renderer, mapper, dstLeft, dstTop, dstRight, dstBottom,
        &x1, &y1, &x2, &y2, &x3, &y3, &x4, &y4);
}

static void C3D_emitMappedQuadSprite(N3DSRenderer* renderer, C3D_Tex* tex,
    const N3DSUVRect* rect, const N3DSDrawMapper* mapper, float srcLeft, float srcTop,
    float srcRight, float srcBottom, float dstLeft, float dstTop, float dstRight, float dstBottom,
    uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4) {
    if (tex == NULL || rect == NULL || mapper == NULL) return;
    float x1, y1, x2, y2, x3, y3, x4, y4;
    float u1, v1, u2, v2, u3, v3, u4, v4;
    if (mapper->kind == N3DS_MAP_AXIS && !renderer->screenTransformActive) {
        x1 = mapper->axisX + dstLeft * mapper->axisScaleX;
        y1 = mapper->axisY + dstTop * mapper->axisScaleY;
        x2 = mapper->axisX + dstRight * mapper->axisScaleX;
        y2 = y1;
        x3 = x1;
        y3 = mapper->axisY + dstBottom * mapper->axisScaleY;
        x4 = x2;
        y4 = y3;
        float minX = x1 < x2 ? x1 : x2;
        float maxX = x1 > x2 ? x1 : x2;
        float minY = y1 < y3 ? y1 : y3;
        float maxY = y1 > y3 ? y1 : y3;
        if (!C3D_axisRectVisible(renderer, minX, minY, maxX, maxY))
            return;
        if (srcLeft == 0.0f && srcTop == 0.0f && srcRight == (float)rect->width &&
            srcBottom == (float)rect->height) {
            u1 = rect->left;
            v1 = rect->top;
            u2 = rect->right;
            v2 = rect->top;
            u3 = rect->left;
            v3 = rect->bottom;
            u4 = rect->right;
            v4 = rect->bottom;
        } else {
            C3D_uvClip(
                rect, srcLeft, srcTop, srcRight, srcBottom, &u1, &v1, &u2, &v2, &u3, &v3, &u4, &v4);
        }
        const N3DSVertex vertices[4] = {
            {{x1, y1}, {u1, v1}},
            {{x2, y2}, {u2, v2}},
            {{x3, y3}, {u3, v3}},
            {{x4, y4}, {u4, v4}},
        };
        const uint32_t colors[4] = {c1, c2, c3, c4};
        n3ds_c3dQuadGradientUV(&renderer->drawContext, tex, vertices, colors);
        return;
    }
    if (!C3D_mapVisibleSpriteQuad(renderer, mapper, dstLeft, dstTop, dstRight, dstBottom,
            &x1, &y1, &x2, &y2, &x3, &y3, &x4, &y4))
        return;
    C3D_uvClip(
        rect, srcLeft, srcTop, srcRight, srcBottom, &u1, &v1, &u2, &v2, &u3, &v3, &u4, &v4);
    const N3DSVertex vertices[4] = {
        {{x1, y1}, {u1, v1}},
        {{x2, y2}, {u2, v2}},
        {{x3, y3}, {u3, v3}},
        {{x4, y4}, {u4, v4}},
    };
    const uint32_t colors[4] = {c1, c2, c3, c4};
    n3ds_c3dQuadGradientUV(&renderer->drawContext, tex, vertices, colors);
}

static float C3D_textLineStride(Font* font, float lineSeparation) {
    if (lineSeparation < 0.0f) return TextUtils_lineStride(font);
    float scaleY = font != NULL && font->scaleY != 0.0f ? font->scaleY : 1.0f;
    return lineSeparation / scaleY;
}

static void C3D_emitMappedSolidQuad(N3DSRenderer* renderer,
    const N3DSDrawMapper* mapper, float dstLeft, float dstTop, float dstRight, float dstBottom,
    uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4) {
    if (mapper == NULL) return;
    float x1, y1, x2, y2, x3, y3, x4, y4;
    C3D_mapRect(renderer, mapper, dstLeft, dstTop, dstRight, dstBottom, &x1, &y1, &x2, &y2,
        &x3, &y3, &x4, &y4);
    if (!C3D_quadVisible(renderer, x1, y1, x2, y2, x3, y3, x4, y4)) return;
    const N3DSVertex vertices[4] = {
        {{x1, y1}, {0.0f, 1.0f}},
        {{x2, y2}, {1.0f, 1.0f}},
        {{x3, y3}, {0.0f, 0.0f}},
        {{x4, y4}, {1.0f, 0.0f}},
    };
    const uint32_t colors[4] = {c1, c2, c3, c4};
    n3ds_c3dQuadGradientUV(
        &renderer->drawContext, &renderer->drawContext.whiteTex, vertices, colors);
}

static void C3D_freeAtlasPageResources(N3DSAtlasPage* page) {
    if (page == NULL) return;
    if (page->tex.data != NULL) C3D_TexDelete(&page->tex);
    page->active = false;
    page->inVram = false;
    page->sizeBytes = 0u;
    page->uPerPixel = 0.0f;
    page->vPerPixel = 0.0f;
    memset(&page->tex, 0, sizeof(page->tex));
    memset(&page->region, 0, sizeof(page->region));
}

static void C3D_unloadAtlasPage(N3DSRenderer* renderer, uint32_t pageIndex) {
    if (renderer->atlas.pages == NULL || pageIndex >= renderer->atlas.pageCount)
        return;
    N3DSAtlasPage* page = &renderer->atlas.pages[pageIndex];
    if (!page->active) return;
    n3ds_c3dInvalidateTexture(&renderer->drawContext, &page->tex);
    renderer->atlas.residentBytes = renderer->atlas.residentBytes > page->sizeBytes
                                        ? renderer->atlas.residentBytes - page->sizeBytes
                                        : 0u;
    C3D_freeAtlasPageResources(page);
}

static const char* C3D_nativeFormatName(uint32_t format) {
    switch (format) {
    case N3DS_NATIVE_TEXFMT_RGBA4444:
        return "rgba4444";
    case N3DS_NATIVE_TEXFMT_L4:
        return "l4";
    case N3DS_NATIVE_TEXFMT_LA4:
        return "la4";
    case N3DS_NATIVE_TEXFMT_ETC1A4:
        return "etc1a4";
    default:
        return "unknown";
    }
}

static void C3D_markAtlasPageUsed(N3DSRenderer* renderer, N3DSAtlasPage* page) {
    if (page == NULL) return;
    page->lastUse = ++renderer->usageCounter;
}

static void C3D_markRoomPageWanted(N3DSRenderer* renderer, uint32_t pageIndex) {
    if (pageIndex >= renderer->atlas.pageCount ||
        renderer->atlas.roomWantedPages == NULL)
        return;
    renderer->atlas.roomWantedPages[pageIndex] = true;
}

static uint32_t C3D_countAtlasPages(const N3DSRenderer* renderer) {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < renderer->atlas.pageCount; ++i) {
        const N3DSAtlasPage* page = &renderer->atlas.pages[i];
        if (page->active) count++;
    }
    return count;
}

static uint32_t C3D_pickEvictableAtlasPage(
    N3DSRenderer* renderer, uint32_t skipAtlasPage, bool allowRoomWanted) {
    uint32_t candidate = UINT32_MAX;
    uint64_t oldest = UINT64_MAX;
    for (uint32_t i = 0; i < renderer->atlas.pageCount; ++i) {
        if (i == skipAtlasPage) continue;
        N3DSAtlasPage* page = &renderer->atlas.pages[i];
        if (!page->active) continue;
        if (!allowRoomWanted && renderer->atlas.roomWantedPages != NULL &&
            renderer->atlas.roomWantedPages[i]) {
            continue;
        }
        if (page->lastUse >= oldest) continue;
        oldest = page->lastUse;
        candidate = i;
    }
    return candidate;
}

static bool C3D_suspendDrawFrame(N3DSRenderer* renderer) {
    if (!renderer->drawContext.frameActive) return false;
    bool topUsed = renderer->topTarget != NULL && renderer->topTarget->used;
    bool bottomUsed = renderer->bottomTarget != NULL && renderer->bottomTarget->used;
    n3ds_c3dFlush(&renderer->drawContext);
    if (renderer->topTarget != NULL) renderer->topTarget->used = false;
    if (renderer->bottomTarget != NULL) renderer->bottomTarget->used = false;
    n3ds_c3dEndFrame(&renderer->drawContext);
    if (renderer->topTarget != NULL) renderer->topTarget->used = topUsed;
    if (renderer->bottomTarget != NULL) renderer->bottomTarget->used = bottomUsed;
    return true;
}

static void C3D_resumeDrawFrame(N3DSRenderer* renderer) {
    C3D_FrameBegin(0);
    renderer->drawContext.frameActive = true;
    renderer->drawContext.bufferDirty = true;
    renderer->drawContext.stateDirty = true;
    renderer->drawContext.projectionDirty = true;
    renderer->drawContext.boundTexture = NULL;
}

static void C3D_finishAtlasDraws(N3DSRenderer* renderer) {
    bool resumeFrame = C3D_suspendDrawFrame(renderer);
    if (resumeFrame) {
        C3D_resumeDrawFrame(renderer);
    } else {
        C3D_FrameBegin(0);
        C3D_FrameEnd(0);
    }
}

static void C3D_finishVertexStream(void* user) {
    C3D_finishAtlasDraws((N3DSRenderer*)user);
}

static bool C3D_evictOldestAtlasPage(N3DSRenderer* renderer, uint32_t protectedPage) {
    uint32_t candidate = C3D_pickEvictableAtlasPage(renderer, protectedPage, false);
    if (candidate == UINT32_MAX) {
        candidate = C3D_pickEvictableAtlasPage(renderer, protectedPage, true);
    }
    if (candidate == UINT32_MAX) return false;
    C3D_finishAtlasDraws(renderer);
    C3D_unloadAtlasPage(renderer, candidate);
    return true;
}

static void C3D_evictAtlasPages(
    N3DSRenderer* renderer, uint32_t neededBytes, uint32_t protectedPage) {
    if (renderer->atlas.pageCount == 0u) return;
    uint32_t budget = renderer->atlasTextureCacheBytes != 0u
        ? renderer->atlasTextureCacheBytes
        : n3ds_getTextureCacheBytes();
    if (neededBytes > budget) neededBytes = budget;
    bool synchronized = false;

    while (renderer->atlas.residentBytes > budget - neededBytes ||
        C3D_fcramOverBudget(renderer->atlas.residentBytes, neededBytes)) {
        uint32_t candidate = C3D_pickEvictableAtlasPage(renderer, protectedPage, false);
        if (candidate == UINT32_MAX) {
            candidate = C3D_pickEvictableAtlasPage(renderer, protectedPage, true);
        }
        if (candidate == UINT32_MAX) break;

        if (!synchronized) {
            C3D_finishAtlasDraws(renderer);
            synchronized = true;
        }
        C3D_unloadAtlasPage(renderer, candidate);
    }
}

static uint8_t* C3D_getAtlasIoBuffer(N3DSRenderer* renderer, uint32_t size) {
    if (size == 0u) return NULL;
    if (renderer->atlas.ioBuffer != NULL && renderer->atlas.ioBufferSize >= size)
        return renderer->atlas.ioBuffer;
    uint8_t* buffer = realloc(renderer->atlas.ioBuffer, size);
    while (buffer == NULL && C3D_evictOldestAtlasPage(renderer, UINT32_MAX)) {
        buffer = realloc(renderer->atlas.ioBuffer, size);
    }
    if (buffer == NULL) return NULL;
    renderer->atlas.ioBuffer = buffer;
    renderer->atlas.ioBufferSize = size;
    return renderer->atlas.ioBuffer;
}

static bool C3D_loadAtlasPage(
    N3DSRenderer* renderer, uint32_t pageIndex, C3D_Tex** outTex) {
    if (!renderer->atlas.loaded || pageIndex >= renderer->atlas.pageCount)
        return false;
    N3DSAtlasPage* page = &renderer->atlas.pages[pageIndex];
    if (page->active) {
        C3D_markAtlasPageUsed(renderer, page);
        if (outTex != NULL) *outTex = &page->tex;
        return true;
    }
    if (page->loadFailed && page->lastLoadAttemptFrame == renderer->frameCounter) return false;
    bool reportFailure = !page->loadFailed;
    page->lastLoadAttemptFrame = renderer->frameCounter;

    N3DSAtlasPageDesc* desc = &renderer->atlas.pageDescs[pageIndex];

    const uint8_t* packedBytes = NULL;
    bool loaded = false;
    N3DSNativeTextureInfo nativeInfo;
    memset(&nativeInfo, 0, sizeof(nativeInfo));

    if (desc->dataSize > 0u && desc->dataOffset > 0u) {
        if (C3D_atlasRangeInRam(
                renderer, desc->dataOffset, desc->dataSize, &packedBytes)) {
        } else {
            FILE* atlasFp = renderer->atlas.fp != NULL
                                ? renderer->atlas.fp
                                : n3ds_fsOpenReadPath(renderer->atlas.atlasPath);
            if (atlasFp != NULL && fseek(atlasFp, (long)desc->dataOffset, SEEK_SET) == 0) {
                uint8_t* ioBytes = C3D_getAtlasIoBuffer(renderer, desc->dataSize);
                if (ioBytes != NULL &&
                    n3ds_fsReadFileBytes(atlasFp, ioBytes, desc->dataSize) ==
                        desc->dataSize) {
                    packedBytes = ioBytes;
                }
            }
            if (atlasFp != NULL && atlasFp != renderer->atlas.fp) fclose(atlasFp);
        }
        if (packedBytes != NULL) {
            bool parsedNative = n3ds_texParse(packedBytes, desc->dataSize, &nativeInfo);
            uint32_t expectedNativeFormat = C3D_atlasNativeFormat(desc->pageFormat);
            size_t expectedNativeSize = n3ds_texUploadSize(
                &nativeInfo, n3ds_nextPow2(nativeInfo.width), n3ds_nextPow2(nativeInfo.height));
            if (parsedNative && nativeInfo.width == desc->width &&
                nativeInfo.height == desc->height &&
                nativeInfo.nativeFormat == expectedNativeFormat && expectedNativeSize != 0u &&
                nativeInfo.uncompressedDataSize == expectedNativeSize) {
                uint32_t incomingBytes = (uint32_t)n3ds_texUploadSize(
                    &nativeInfo, n3ds_nextPow2(nativeInfo.width), n3ds_nextPow2(nativeInfo.height));
                C3D_evictAtlasPages(renderer, incomingBytes, pageIndex);
                bool outOfMemory = false;
                loaded = n3ds_texLoad(
                    packedBytes, desc->dataSize, &page->tex, false, &page->inVram, &outOfMemory);
                while (!loaded && outOfMemory &&
                    C3D_evictOldestAtlasPage(renderer, pageIndex)) {
                    loaded = n3ds_texLoad(packedBytes, desc->dataSize,
                        &page->tex, false, &page->inVram, &outOfMemory);
                }

                if (loaded) {
                    C3D_uvFromNative(&page->region, desc->width, desc->height,
                        (uint16_t)page->tex.width, (uint16_t)page->tex.height);
                    page->uPerPixel = desc->width > 0u
                        ? (page->region.right - page->region.left) / (float)desc->width
                        : 0.0f;
                    page->vPerPixel = desc->height > 0u
                        ? (page->region.bottom - page->region.top) / (float)desc->height
                        : 0.0f;
                }
            }
        }
    }

    if (!loaded) {
        C3D_freeAtlasPageResources(page);
        page->loadFailed = true;
        if (reportFailure)
            logEvent("renderer",
                "atlas page load failed page=%lu %ux%u fmt=%lu dataOff=%lu dataSize=%lu "
                "linFree=%lu appFree=%lu",
                (unsigned long)pageIndex, (unsigned int)desc->width, (unsigned int)desc->height,
                (unsigned long)desc->pageFormat, (unsigned long)desc->dataOffset,
                (unsigned long)desc->dataSize,
                (unsigned long)n3ds_getFreeLinearBytes(),
                (unsigned long)n3ds_getFreeHeapBudgetBytes());
        return false;
    }

    C3D_setTextureFiltering(&page->tex, renderer->base.texFilter);
    C3D_TexSetWrap(&page->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    page->sizeBytes = page->tex.size;
    C3D_markAtlasPageUsed(renderer, page);
    page->active = true;
    page->loadFailed = false;
    renderer->atlas.residentBytes += page->sizeBytes;
    C3D_evictAtlasPages(renderer, 0u, pageIndex);
    if (outTex != NULL) *outTex = &page->tex;
    return true;
}

static bool C3D_loadRoomWantedAtlasPage(
    N3DSRenderer* renderer, uint32_t pageIndex, C3D_Tex** outTex) {
    C3D_markRoomPageWanted(renderer, pageIndex);
    return C3D_loadAtlasPage(renderer, pageIndex, outTex);
}

static void C3D_buildRoomManifestIndex(N3DSRenderer* renderer) {
    if (!renderer->roomManifest.loaded || renderer->roomManifest.entries == NULL)
        return;
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

static bool C3D_loadRoomManifestFile(
    N3DSRenderer* renderer, FILE* fp, uint32_t blockSize) {
    if (fp == NULL) return false;
    uint32_t header[8] = {0};
    bool ok = fread(header, sizeof(uint32_t), 8u, fp) == 8u;
    if (!ok || header[0] != N3DS_ROOM_MANIFEST_MAGIC || header[1] != N3DS_ROOM_MANIFEST_VERSION) {
        return false;
    }

    uint32_t entryCount = header[2];
    uint32_t pageRefCount = header[3];
    uint32_t tileRefCount = header[5];
    uint32_t tileRefRowSize = header[6];
    uint64_t requiredSize = 32u + (uint64_t)entryCount * sizeof(N3DSRoomManifestEntry) +
                            (uint64_t)pageRefCount * sizeof(N3DSRoomManifestPageRef) +
                            (uint64_t)tileRefCount * sizeof(N3DSRoomManifestTileRef);
    if ((tileRefCount > 0u && tileRefRowSize != sizeof(N3DSRoomManifestTileRef)) ||
        requiredSize > blockSize) {
        return false;
    }
    N3DSRoomManifestEntry* entries =
        safeCalloc(entryCount > 0u ? entryCount : 1u, sizeof(N3DSRoomManifestEntry));
    N3DSRoomManifestTileRef* tileRefs =
        safeCalloc(tileRefCount > 0u ? tileRefCount : 1u, sizeof(N3DSRoomManifestTileRef));
    if (entryCount > 0u) {
        if (fread(entries, sizeof(N3DSRoomManifestEntry), entryCount, fp) != entryCount) ok = false;
    }
    uint64_t pageBytes = (uint64_t)pageRefCount * sizeof(N3DSRoomManifestPageRef);
    uint8_t discard[256];
    while (ok && pageBytes > 0u) {
        size_t count = pageBytes > sizeof(discard) ? sizeof(discard) : (size_t)pageBytes;
        if (fread(discard, 1, count, fp) != count) ok = false;
        pageBytes -= count;
    }
    if (ok && tileRefCount > 0u &&
        fread(tileRefs, sizeof(N3DSRoomManifestTileRef), tileRefCount, fp) != tileRefCount)
        ok = false;
    for (uint32_t i = 0u; ok && i < entryCount; ++i) {
        N3DSRoomManifestEntry* entry = &entries[i];
        if (entry->pageStart > pageRefCount || entry->pageCount > pageRefCount - entry->pageStart ||
            entry->tileStart > tileRefCount || entry->tileCount > tileRefCount - entry->tileStart) {
            ok = false;
        }
    }
    if (!ok) {
        free(entries);
        free(tileRefs);
        return false;
    }
    renderer->roomManifest.entryCount = entryCount;
    renderer->roomManifest.tileRefCount = tileRefCount;
    renderer->roomManifest.entries = entries;
    renderer->roomManifest.tileRefs = tileRefs;
    renderer->roomManifest.loaded = true;
    C3D_buildRoomManifestIndex(renderer);
    return true;
}

static bool C3D_loadEmbedRoomManifest(N3DSRenderer* renderer) {
    if (renderer->atlas.embeddedRoomManifestOffset == 0u ||
        renderer->atlas.embeddedRoomManifestSize < 32u || renderer->atlas.atlasPath[0] == '\0') {
        return true;
    }
    FILE* fp = n3ds_fsOpenReadPath(renderer->atlas.atlasPath);
    if (fp == NULL) return false;
    bool ok = fseek(fp, (long)renderer->atlas.embeddedRoomManifestOffset, SEEK_SET) == 0 &&
        C3D_loadRoomManifestFile(renderer, fp, renderer->atlas.embeddedRoomManifestSize);
    fclose(fp);
    return ok;
}

static void C3D_destroyRoomManifest(N3DSRenderer* renderer) {
    free(renderer->roomManifest.entries);
    free(renderer->roomManifest.tileRefs);
    free(renderer->roomManifest.entryByRoomIndex);
    memset(&renderer->roomManifest, 0, sizeof(renderer->roomManifest));
}

static bool C3D_atlasRangeInRam(
    const N3DSRenderer* renderer, uint32_t offset, uint32_t size, const uint8_t** outBytes) {
    if (outBytes != NULL) *outBytes = NULL;
    if (renderer->atlas.data == NULL || size == 0u) return false;
    if (offset > renderer->atlas.dataSize || size > renderer->atlas.dataSize - offset) return false;
    if (outBytes != NULL) *outBytes = renderer->atlas.data + offset;
    return true;
}

static void C3D_tryCacheAtlasFile(N3DSRenderer* renderer, FILE* fp) {
    if (fp == NULL || renderer->atlas.data != NULL) return;
    if (!renderer->cacheAtlas) return;
    if (fseek(fp, 0, SEEK_END) != 0) {
        renderer->cacheAtlas = false;
        return;
    }
    long atlasSizeLong = ftell(fp);
    if (atlasSizeLong <= 0) {
        renderer->cacheAtlas = false;
        return;
    }
    unsigned long atlasSizeUnsigned = (unsigned long)atlasSizeLong;
    if (atlasSizeUnsigned > UINT32_MAX) atlasSizeUnsigned = UINT32_MAX;
    renderer->atlas.fileSize = (uint32_t)atlasSizeUnsigned;
    if (fseek(fp, 0, SEEK_SET) != 0) {
        renderer->cacheAtlas = false;
        return;
    }

    size_t atlasSize = (size_t)renderer->atlas.fileSize;
    if (atlasSize == 0u) {
        renderer->cacheAtlas = false;
        return;
    }

    uint8_t* atlasData = malloc(atlasSize);
    if (atlasData == NULL) {
        renderer->cacheAtlas = false;
        logEvent("atlas", "file cache allocation failed size=%lu", (unsigned long)atlasSize);
        return;
    }
    size_t readBytes = n3ds_fsReadFileBytes(fp, atlasData, atlasSize);
    if (readBytes != atlasSize) {
        free(atlasData);
        renderer->cacheAtlas = false;
        logEvent("atlas", "file cache read failed size=%lu got=%lu", (unsigned long)atlasSize,
            (unsigned long)readBytes);
        return;
    }

    renderer->atlas.data = atlasData;
    renderer->atlas.dataSize = (uint32_t)readBytes;
    logEvent("atlas", "file cached in ram size=%lu", (unsigned long)renderer->atlas.dataSize);
}

static void C3D_loadAtlasDirectory(
    N3DSRenderer* renderer, FILE* fp, uint32_t pageDataEnd, uint32_t fileSize) {
    if (fp == NULL || pageDataEnd >= fileSize || fileSize < 16u) return;
    uint32_t footerOffset = fileSize - 16u;
    uint8_t footer[16];
    if (fseek(fp, (long)footerOffset, SEEK_SET) != 0 ||
        fread(footer, 1, sizeof(footer), fp) != sizeof(footer))
        return;
    if (C3D_readLe32(footer + 0u) != N3DS_ATLAS_DIRECTORY_MAGIC ||
        C3D_readLe32(footer + 4u) != N3DS_ATLAS_DIRECTORY_VERSION) {
        return;
    }

    uint32_t blockCount = C3D_readLe32(footer + 8u);
    uint32_t directoryOffset = C3D_readLe32(footer + 12u);
    uint64_t directorySize = (uint64_t)blockCount * 16u;
    if (blockCount == 0u || blockCount > 32u || directoryOffset < pageDataEnd ||
        directoryOffset > footerOffset ||
        directorySize > (uint64_t)(footerOffset - directoryOffset)) {
        return;
    }

    if (fseek(fp, (long)directoryOffset, SEEK_SET) != 0) return;
    for (uint32_t i = 0u; i < blockCount; ++i) {
        uint8_t row[16];
        if (fread(row, 1, sizeof(row), fp) != sizeof(row)) return;
        uint32_t type = C3D_readLe32(row + 0u);
        uint32_t version = C3D_readLe32(row + 4u);
        uint32_t offset = C3D_readLe32(row + 8u);
        uint32_t size = C3D_readLe32(row + 12u);
        if (size == 0u || offset < pageDataEnd || offset > directoryOffset ||
            size > directoryOffset - offset)
            return;

        if (type == N3DS_ATLAS_BLOCK_TPAG_BINDINGS && version == N3DS_TPAG_BINDINGS_VERSION) {
            renderer->atlas.embeddedTpagBindingOffset = offset;
            renderer->atlas.embeddedTpagBindingSize = size;
        } else if (type == N3DS_ATLAS_BLOCK_ROOM_MANIFEST &&
            version == N3DS_ROOM_MANIFEST_VERSION) {
            renderer->atlas.embeddedRoomManifestOffset = offset;
            renderer->atlas.embeddedRoomManifestSize = size;
        }
    }
}

typedef struct {
    uint32_t fileSize;
    uint32_t pageDataBase;
    uint32_t pageDataEnd;
    uint32_t textureFormat;
    uint32_t tpagHash;
} N3DSAtlasLoadInfo;

static const char* C3D_readAtlasHeader(
    N3DSRenderer* renderer, FILE* fp, N3DSAtlasLoadInfo* info) {
    if (fseek(fp, 0, SEEK_END) != 0) return "Failed to measure 3DS atlas metadata";
    long fileSize = ftell(fp);
    if (fileSize < 28 || fileSize > INT32_MAX) return "Invalid 3DS atlas file size";
    rewind(fp);

    uint8_t header[28];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header) ||
        C3D_readLe32(header + 0u) != N3DS_ATLAS_MAGIC ||
        C3D_readLe16(header + 4u) != N3DS_ATLAS_VERSION) {
        return "Unsupported 3DS atlas format";
    }

    info->fileSize = (uint32_t)fileSize;
    info->textureFormat = C3D_readLe32(header + 20u);
    info->tpagHash = C3D_readLe32(header + 24u);
    if (info->tpagHash != C3D_computeTpagHash(renderer->base.dataWin)) {
        return "3DS atlas does not match data.win. Re-run cinnamon-preprocessor.";
    }

    renderer->atlas.pageCount = C3D_readLe16(header + 6u);
    renderer->atlas.itemCount = C3D_readLe32(header + 8u);
    renderer->atlas.fragmentCount = C3D_readLe32(header + 12u);
    renderer->atlas.tileEntryCount = C3D_readLe32(header + 16u);
    size_t pageDataBase = 28u + (size_t)renderer->atlas.pageCount * 16u +
        (size_t)renderer->atlas.itemCount * 12u +
        (size_t)renderer->atlas.fragmentCount * 14u +
        (size_t)renderer->atlas.tileEntryCount * 26u;
    if (pageDataBase > (size_t)fileSize) return "Invalid 3DS atlas metadata tables";
    info->pageDataBase = (uint32_t)pageDataBase;
    info->pageDataEnd = info->pageDataBase;
    return NULL;
}

static void C3D_allocateAtlasTables(N3DSRenderer* renderer) {
    renderer->atlas.pageDescs = safeCalloc(
        renderer->atlas.pageCount > 0u ? renderer->atlas.pageCount : 1u,
        sizeof(N3DSAtlasPageDesc));
    renderer->atlas.items = safeCalloc(
        renderer->atlas.itemCount > 0u ? renderer->atlas.itemCount : 1u,
        sizeof(N3DSAtlasItem));
    renderer->atlas.fragments = safeCalloc(
        renderer->atlas.fragmentCount > 0u ? renderer->atlas.fragmentCount : 1u,
        sizeof(N3DSAtlasFragment));
    renderer->atlas.tileEntries = safeCalloc(
        renderer->atlas.tileEntryCount > 0u ? renderer->atlas.tileEntryCount : 1u,
        sizeof(N3DSAtlasTileEntry));
    renderer->atlas.pages = safeCalloc(
        renderer->atlas.pageCount > 0u ? renderer->atlas.pageCount : 1u,
        sizeof(N3DSAtlasPage));
    renderer->atlas.roomWantedPages = safeCalloc(
        renderer->atlas.pageCount > 0u ? renderer->atlas.pageCount : 1u, sizeof(bool));
}

static const char* C3D_readAtlasPages(
    N3DSRenderer* renderer, FILE* fp, N3DSAtlasLoadInfo* info) {
    for (uint32_t i = 0; i < renderer->atlas.pageCount; ++i) {
        uint8_t row[16];
        N3DSAtlasPageDesc* page = &renderer->atlas.pageDescs[i];
        if (fread(row, 1, sizeof(row), fp) != sizeof(row))
            return "Failed to read 3DS atlas page table";
        page->width = C3D_readLe16(row + 0u);
        page->height = C3D_readLe16(row + 2u);
        page->pageFormat = C3D_readLe32(row + 4u);
        page->dataOffset = C3D_readLe32(row + 8u);
        page->dataSize = C3D_readLe32(row + 12u);
        if (!C3D_validAtlasPageSize(page->width, page->height)) {
            return "Invalid 3DS atlas page dimensions. Re-run cinnamon-preprocessor.";
        }
        if (!C3D_validAtlasPageFormat(page->pageFormat)) {
            return "Unsupported 3DS atlas page format. Re-run cinnamon-preprocessor for the 3DS "
                   "target.";
        }
        if (page->dataOffset < info->pageDataBase || page->dataOffset > info->fileSize ||
            page->dataSize > info->fileSize - page->dataOffset) {
            return "Invalid 3DS atlas page data range. Re-run cinnamon-preprocessor.";
        }
        uint32_t pageEnd = page->dataOffset + page->dataSize;
        if (pageEnd > info->pageDataEnd) info->pageDataEnd = pageEnd;
    }
    return NULL;
}

static const char* C3D_readAtlasItems(N3DSRenderer* renderer, FILE* fp) {
    for (uint32_t i = 0; i < renderer->atlas.itemCount; ++i) {
        uint8_t row[12];
        N3DSAtlasItem* item = &renderer->atlas.items[i];
        if (fread(row, 1, sizeof(row), fp) != sizeof(row))
            return "Failed to read 3DS atlas item table";
        item->width = C3D_readLe16(row + 0u);
        item->height = C3D_readLe16(row + 2u);
        item->fragmentStart = C3D_readLe32(row + 4u);
        item->fragmentCount = C3D_readLe16(row + 8u);
        item->resizeDivisor = C3D_readLe16(row + 10u);
        if (item->resizeDivisor == 0u) item->resizeDivisor = 1u;
    }
    return NULL;
}

static const char* C3D_readAtlasFragments(N3DSRenderer* renderer, FILE* fp) {
    for (uint32_t i = 0; i < renderer->atlas.fragmentCount; ++i) {
        uint8_t row[14];
        N3DSAtlasFragment* fragment = &renderer->atlas.fragments[i];
        if (fread(row, 1, sizeof(row), fp) != sizeof(row))
            return "Failed to read 3DS atlas fragment table";
        fragment->pageIndex = C3D_readLe16(row + 0u);
        fragment->x = C3D_readLe16(row + 2u);
        fragment->y = C3D_readLe16(row + 4u);
        fragment->width = C3D_readLe16(row + 6u);
        fragment->height = C3D_readLe16(row + 8u);
        fragment->sourceX = C3D_readLe16(row + 10u);
        fragment->sourceY = C3D_readLe16(row + 12u);
    }
    return NULL;
}

static const char* C3D_readAtlasTiles(N3DSRenderer* renderer, FILE* fp) {
    for (uint32_t i = 0; i < renderer->atlas.tileEntryCount; ++i) {
        uint8_t row[26];
        N3DSAtlasTileEntry* entry = &renderer->atlas.tileEntries[i];
        if (fread(row, 1, sizeof(row), fp) != sizeof(row))
            return "Failed to read 3DS atlas tile metadata";
        entry->bgDef = (int16_t)C3D_readLe16(row + 0u);
        entry->srcX = C3D_readLe16(row + 2u);
        entry->srcY = C3D_readLe16(row + 4u);
        entry->srcW = C3D_readLe16(row + 6u);
        entry->srcH = C3D_readLe16(row + 8u);
        entry->pageIndex = C3D_readLe16(row + 10u);
        entry->x = C3D_readLe16(row + 12u);
        entry->y = C3D_readLe16(row + 14u);
        entry->width = C3D_readLe16(row + 16u);
        entry->height = C3D_readLe16(row + 18u);
        entry->fragmentStart = C3D_readLe32(row + 20u);
        entry->fragmentCount = C3D_readLe16(row + 24u);
        N3DSAtlasTileKey key = {
            .bgDef = entry->bgDef,
            .srcX = entry->srcX,
            .srcY = entry->srcY,
            .srcW = entry->srcW,
            .srcH = entry->srcH,
        };
        hmput(renderer->atlas.tileLookup, key, i);
        if (entry->bgDef < 0 || (uint32_t)entry->bgDef >= renderer->base.dataWin->bgnd.count)
            continue;
        Background* bg = &renderer->base.dataWin->bgnd.backgrounds[entry->bgDef];
        if (bg->tpagIndex < 0) continue;
        N3DSAtlasTilemapKey tilemapKey = {
            .tpagIndex = bg->tpagIndex,
            .srcX = entry->srcX,
            .srcY = entry->srcY,
            .srcW = entry->srcW,
            .srcH = entry->srcH,
        };
        hmput(renderer->atlas.tilemapLookup, tilemapKey, i);
    }
    return NULL;
}

static bool C3D_loadAtlas(N3DSRenderer* renderer) {
    char atlasPath[512];
    FILE* fp = n3ds_fsOpenGfxAsset("atlas.bin", atlasPath, sizeof(atlasPath));
    if (fp == NULL && atlasPath[0] == '\0') {
        C3D_setError(renderer, "Missing 3DS gfx/atlas.bin. Re-run cinnamon-preprocessor.");
        return false;
    }
    if (fp == NULL) {
        C3D_setError(renderer, "Failed to open 3DS atlas metadata");
        return false;
    }

    N3DSAtlasLoadInfo info = {0};
    const char* loadError = C3D_readAtlasHeader(renderer, fp, &info);
    if (loadError != NULL) goto fail;
    C3D_allocateAtlasTables(renderer);
    if ((loadError = C3D_readAtlasPages(renderer, fp, &info)) != NULL ||
        (loadError = C3D_readAtlasItems(renderer, fp)) != NULL ||
        (loadError = C3D_readAtlasFragments(renderer, fp)) != NULL ||
        (loadError = C3D_readAtlasTiles(renderer, fp)) != NULL) {
        goto fail;
    }

    C3D_loadAtlasDirectory(renderer, fp, info.pageDataEnd, info.fileSize);

    snprintf(renderer->atlas.atlasPath, sizeof(renderer->atlas.atlasPath), "%s", atlasPath);
    C3D_tryCacheAtlasFile(renderer, fp);
    if (renderer->atlas.data != NULL && renderer->atlas.fileSize > 0u &&
        renderer->atlas.dataSize >= renderer->atlas.fileSize) {
        fclose(fp);
    } else {
        renderer->atlas.fp = fp;
        fp = NULL;
    }
    logEvent("atlas", "path=%s pages=%lu globalFmt=%s(%lu) hash=%08lx cached=%lu full=%lu",
        renderer->atlas.atlasPath, (unsigned long)renderer->atlas.pageCount,
        C3D_nativeFormatName(info.textureFormat), (unsigned long)info.textureFormat,
        (unsigned long)info.tpagHash, (unsigned long)renderer->atlas.dataSize,
        (unsigned long)renderer->atlas.fileSize);
    renderer->atlas.loaded = true;
    return true;

fail:
    if (fp != NULL) fclose(fp);
    C3D_destroyAtlas(renderer);
    C3D_setError(
        renderer, loadError != NULL ? loadError : "Failed to load 3DS atlas metadata");
    return false;
}

static void C3D_destroyAtlas(N3DSRenderer* renderer) {
    if (renderer->atlas.pages != NULL) {
        for (uint32_t i = 0; i < renderer->atlas.pageCount; ++i) {
            if (renderer->atlas.pages[i].active)
                C3D_freeAtlasPageResources(&renderer->atlas.pages[i]);
        }
    }
    free(renderer->atlas.pageDescs);
    free(renderer->atlas.items);
    free(renderer->atlas.fragments);
    free(renderer->atlas.tileEntries);
    hmfree(renderer->atlas.tileLookup);
    hmfree(renderer->atlas.tilemapLookup);
    free(renderer->atlas.pages);
    free(renderer->atlas.roomWantedPages);
    free(renderer->atlas.data);
    free(renderer->atlas.ioBuffer);
    if (renderer->atlas.fp != NULL) fclose(renderer->atlas.fp);
    memset(&renderer->atlas, 0, sizeof(renderer->atlas));
}

static bool C3D_getAtlasBinding(
    N3DSRenderer* renderer, int32_t tpagIndex, TexturePageItem** outTpag, N3DSAtlasItem** outItem) {
    if (!renderer->atlas.loaded || renderer->base.dataWin == NULL ||
        tpagIndex < 0 || (uint32_t)tpagIndex >= renderer->tpagBindingCount)
        return false;
    N3DSTpagBinding* binding = &renderer->tpagBindings[tpagIndex];
    if (binding->source != N3DS_TPAG_SOURCE_ATLAS ||
        binding->atlasItemIndex >= renderer->atlas.itemCount)
        return false;
    if (outTpag != NULL) *outTpag = &renderer->base.dataWin->tpag.items[tpagIndex];
    if (outItem != NULL) *outItem = &renderer->atlas.items[binding->atlasItemIndex];
    return true;
}

static bool C3D_clipLineToRect(
    float* x1, float* y1, float* x2, float* y2, float left, float top, float right, float bottom) {
    if (x1 == NULL || y1 == NULL || x2 == NULL || y2 == NULL) return false;
    if (right < left) {
        float t = left;
        left = right;
        right = t;
    }
    if (bottom < top) {
        float t = top;
        top = bottom;
        bottom = t;
    }
    if (right <= left || bottom <= top) return false;

    float dx = *x2 - *x1;
    float dy = *y2 - *y1;
    float p[4] = {-dx, dx, -dy, dy};
    float q[4] = {*x1 - left, right - *x1, *y1 - top, bottom - *y1};
    float u1 = 0.0f;
    float u2 = 1.0f;
    for (int i = 0; i < 4; ++i) {
        if (fabsf(p[i]) <= N3DS_LINE_CLIP_EPSILON) {
            if (q[i] < 0.0f) return false;
            continue;
        }
        float r = q[i] / p[i];
        if (p[i] < 0.0f) {
            if (r > u2) return false;
            if (r > u1) u1 = r;
        } else {
            if (r < u1) return false;
            if (r < u2) u2 = r;
        }
    }
    float ox1 = *x1;
    float oy1 = *y1;
    *x1 = ox1 + u1 * dx;
    *y1 = oy1 + u1 * dy;
    *x2 = ox1 + u2 * dx;
    *y2 = oy1 + u2 * dy;
    return true;
}

static bool C3D_getLineClipRect(
    const N3DSRenderer* renderer, float* left, float* top, float* right, float* bottom) {
    if (left == NULL || top == NULL || right == NULL || bottom == NULL)
        return false;
    if (renderer->screenTransformActive || renderer->hostViewProjectionActive) {
        *left = (float)renderer->base.CPortX;
        *top = (float)renderer->base.CPortY;
        *right = *left + (float)renderer->base.CPortW;
        *bottom = *top + (float)renderer->base.CPortH;
        return true;
    }
    if (renderer->currentTargetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER) {
        *left = 0.0f;
        *top = 0.0f;
        *right =
            (float)(renderer->currentHostTargetIsBottomScreen ? N3DS_BOTTOM_WIDTH : N3DS_TOP_WIDTH);
        *bottom = (float)(renderer->currentHostTargetIsBottomScreen
                ? N3DS_BOTTOM_HEIGHT
                : N3DS_TOP_HEIGHT);
        return true;
    }
    if (renderer->currentTargetSurfaceId >= 0 &&
        (uint32_t)renderer->currentTargetSurfaceId < renderer->surfaceCount) {
        const N3DSSurface* surface = &renderer->surfaces[renderer->currentTargetSurfaceId];
        if (!surface->used) return false;
        *left = 0.0f;
        *top = 0.0f;
        *right = (float)surface->width;
        *bottom = (float)surface->height;
        return true;
    }
    return false;
}

static void C3D_getSurfaceContentRegion(N3DSUVRect* outRegion,
    const N3DSUVRect* region, const TexturePageItem* tpag, const N3DSSurface* surface) {
    if (outRegion == NULL || region == NULL || tpag == NULL || surface == NULL) return;
    float scaleX = C3D_surfacePhysicalScaleX(surface);
    float scaleY = C3D_surfacePhysicalScaleY(surface);
    C3D_uvSubRect(outRegion, region, (float)tpag->sourceX * scaleX,
        (float)tpag->sourceY * scaleY, (float)(tpag->sourceX + tpag->sourceWidth) * scaleX,
        (float)(tpag->sourceY + tpag->sourceHeight) * scaleY);
    outRegion->width = tpag->sourceWidth;
    outRegion->height = tpag->sourceHeight;
}

static bool C3D_resolveTextureRegion(
    N3DSRenderer* renderer, int32_t tpagIndex, C3D_Tex** outTex, N3DSUVRect** outRegion) {
    if (tpagIndex < 0 || (uint32_t)tpagIndex >= renderer->tpagBindingCount)
        return false;
    N3DSTpagBinding* binding = &renderer->tpagBindings[tpagIndex];
    if (binding->source == N3DS_TPAG_SOURCE_SURFACE) {
        uint32_t surfaceID = binding->assetIndex;
        if (!C3D_surfaceBindingMatches(renderer, binding) ||
            !C3D_finishSurfaceUpload(renderer, (int32_t)surfaceID))
            return false;
        if (outTex != NULL) *outTex = &renderer->surfaces[surfaceID].tex;
        if (outRegion != NULL) {
            static N3DSUVRect scratch;
            C3D_getSurfaceRegion(&renderer->surfaces[surfaceID], &scratch);
            *outRegion = &scratch;
        }
        return true;
    }
    if (binding->source == N3DS_TPAG_SOURCE_ATLAS) {
        TexturePageItem* tpag = NULL;
        N3DSAtlasItem* item = NULL;
        if (!C3D_getAtlasBinding(renderer, tpagIndex, &tpag, &item) ||
            item->fragmentCount == 0u)
            return false;
        N3DSAtlasFragment* fragment = &renderer->atlas.fragments[item->fragmentStart];
        if ((uint32_t)fragment->pageIndex >= renderer->atlas.pageCount || fragment->sourceX != 0u ||
            fragment->sourceY != 0u || fragment->width != item->width ||
            fragment->height != item->height)
            return false;
        if (!C3D_loadRoomWantedAtlasPage(renderer, fragment->pageIndex, outTex))
            return false;
        if (outRegion != NULL) {
            static N3DSUVRect scratch;
            N3DSAtlasPage* page = &renderer->atlas.pages[fragment->pageIndex];
            uint16_t divisor = item->resizeDivisor > 1u ? item->resizeDivisor : 1u;
            uint16_t physicalWidth =
                divisor == 2u ? C3D_scaleDownHalf16(fragment->width) : fragment->width;
            uint16_t physicalHeight =
                divisor == 2u ? C3D_scaleDownHalf16(fragment->height) : fragment->height;
            C3D_uvSubRect(&scratch, &page->region, (float)fragment->x, (float)fragment->y,
                (float)(fragment->x + physicalWidth), (float)(fragment->y + physicalHeight));
            scratch.width = item->width;
            scratch.height = item->height;
            *outRegion = &scratch;
        }
        return true;
    }
    return false;
}

static void C3D_bindTarget(N3DSRenderer* renderer, int32_t surfaceId, bool bottomScreen) {
#if !N3DS_HAS_BOTTOM_TARGET
    bottomScreen = false;
#endif
    if (surfaceId >= 0 && (uint32_t)surfaceId < renderer->surfaceCount &&
        renderer->surfaces[surfaceId].used) {
        N3DSSurface* surface = &renderer->surfaces[surfaceId];
        C3D_RenderTarget* target = renderer->surfaces[surfaceId].target;
        if (target == NULL) {
            return;
        }
        bool sameTarget = renderer->currentTargetSurfaceId == surfaceId;
        n3ds_c3dFlush(&renderer->drawContext);
        renderer->hostViewProjectionActive = false;
        if (!sameTarget && renderer->currentTargetSurfaceId != INT32_MIN) {
        }
        if (!sameTarget) {
            C3D_FrameDrawOn(target);
            C3D_invalidateViewportCache(renderer);
        }
        C3D_setViewportAndScissor(renderer, 0,
            target->frameBuf.height - surface->height, surface->width, surface->height);
        renderer->currentTargetSurfaceId = surfaceId;
        renderer->currentHostTargetIsBottomScreen = false;
        if (!sameTarget) surface->samplingReady = false;
        C3D_applyBlendState(renderer);
        return;
    }

    C3D_RenderTarget* target = bottomScreen ? renderer->bottomTarget : renderer->topTarget;
    if (target == NULL) return;
    bool sameTarget = renderer->currentTargetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER &&
        renderer->currentHostTargetIsBottomScreen == bottomScreen;
    n3ds_c3dFlush(&renderer->drawContext);
    renderer->hostViewProjectionActive = false;
    if (!sameTarget && renderer->currentTargetSurfaceId != INT32_MIN &&
        renderer->currentTargetSurfaceId != RENDER_TARGET_HOST_FRAMEBUFFER) {
    }
    if (!sameTarget) {
        C3D_FrameDrawOn(target);
        C3D_invalidateViewportCache(renderer);
    }
    renderer->currentTargetSurfaceId = RENDER_TARGET_HOST_FRAMEBUFFER;
    renderer->currentHostTargetIsBottomScreen = bottomScreen;
    C3D_setHostViewport(renderer);
    C3D_applyBlendState(renderer);
}

static void C3D_getHostViewport(const N3DSRenderer* renderer, int32_t portX,
    int32_t portY, int32_t portW, int32_t portH, int32_t* outX, int32_t* outY, int32_t* outW,
    int32_t* outH) {
    if (outX == NULL || outY == NULL || outW == NULL || outH == NULL) return;
    int32_t hostW = renderer->currentHostTargetIsBottomScreen
                        ? N3DS_BOTTOM_WIDTH
                        : N3DS_TOP_WIDTH;
    int32_t hostH = renderer->currentHostTargetIsBottomScreen
                        ? N3DS_BOTTOM_HEIGHT
                        : N3DS_TOP_HEIGHT;
    if (renderer->frameGameW <= 0 || renderer->frameGameH <= 0) {
        *outX = portX;
        *outY = portY;
        *outW = portW;
        *outH = portH;
        return;
    }
    float left = 0.0f;
    float top = 0.0f;
    float right = (float)hostW;
    float bottom = (float)hostH;
    C3D_letterboxRect(
        renderer->frameGameW, renderer->frameGameH, hostW, hostH, &left, &top, &right, &bottom);
    float scaleX = renderer->frameGameW > 0 ? (right - left) / (float)renderer->frameGameW : 1.0f;
    float scaleY = renderer->frameGameH > 0 ? (bottom - top) / (float)renderer->frameGameH : 1.0f;
    *outX = (int32_t)floorf(left + (float)portX * scaleX + 0.5f);
    *outY = (int32_t)floorf(top + (float)portY * scaleY + 0.5f);
    *outW = (int32_t)floorf((float)portW * scaleX + 0.5f);
    *outH = (int32_t)floorf((float)portH * scaleY + 0.5f);
}

static void C3D_getHostGuiViewport(int32_t guiW, int32_t guiH, int32_t portX,
    int32_t portY, int32_t portW, int32_t portH, int32_t* outX, int32_t* outY, int32_t* outW,
    int32_t* outH) {
    if (outX == NULL || outY == NULL || outW == NULL || outH == NULL) return;
    if (guiW <= 0 || guiH <= 0 || portW <= 0 || portH <= 0) {
        *outX = portX;
        *outY = portY;
        *outW = portW;
        *outH = portH;
        return;
    }

    float left = 0.0f;
    float top = 0.0f;
    float right = (float)portW;
    float bottom = (float)portH;
    C3D_letterboxRect(guiW, guiH, portW, portH, &left, &top, &right, &bottom);
    *outX = portX + (int32_t)floorf(left + 0.5f);
    *outY = portY + (int32_t)floorf(top + 0.5f);
    *outW = (int32_t)floorf((right - left) + 0.5f);
    *outH = (int32_t)floorf((bottom - top) + 0.5f);
}

static void C3D_applyBlendState(N3DSRenderer* renderer) {
    N3DSRenderState* state = &renderer->renderState;
    GPU_BLENDFACTOR srcColor =
        state->blendEnabled ? C3D_blendFactorFromGml(state->blendSrc) : GPU_ONE;
    GPU_BLENDFACTOR dstColor =
        state->blendEnabled ? C3D_blendFactorFromGml(state->blendDst) : GPU_ZERO;
    GPU_BLENDFACTOR srcAlpha =
        state->blendEnabled ? C3D_blendFactorFromGml(state->blendSrcAlpha) : GPU_ONE;
    GPU_BLENDFACTOR dstAlpha =
        state->blendEnabled ? C3D_blendFactorFromGml(state->blendDstAlpha) : GPU_ZERO;
    n3ds_c3dSetBlend(&renderer->drawContext, state->blendEnabled, state->blendEq,
        state->blendEq, srcColor, dstColor, srcAlpha, dstAlpha);
    n3ds_c3dSetAlphaTest(
        &renderer->drawContext, state->alphaTestEnabled, GPU_GREATER, state->alphaTestRef);
    n3ds_c3dSetColorMask(&renderer->drawContext, state->colorWriteR, state->colorWriteG,
        state->colorWriteB, state->colorWriteA);
}

static bool C3D_atlasItemIsDrawable(const N3DSRenderer* renderer, uint32_t itemIndex) {
    if (!renderer->atlas.loaded || itemIndex >= renderer->atlas.itemCount)
        return false;
    const N3DSAtlasItem* item = &renderer->atlas.items[itemIndex];
    if (item->fragmentCount == 0u || item->fragmentStart >= renderer->atlas.fragmentCount)
        return false;
    for (uint32_t i = 0u; i < item->fragmentCount; ++i) {
        uint32_t fragmentIndex = item->fragmentStart + i;
        if (fragmentIndex >= renderer->atlas.fragmentCount) return false;
        if (renderer->atlas.fragments[fragmentIndex].pageIndex >= renderer->atlas.pageCount)
            return false;
    }
    return true;
}

static void C3D_destroyTpagBindings(N3DSRenderer* renderer) {
    free(renderer->tpagBindings);
    renderer->tpagBindings = NULL;
    renderer->tpagBindingCount = 0u;
    renderer->runtimeTpagStart = 0u;
}

static bool C3D_loadTpagBindingsFile(
    N3DSRenderer* renderer, DataWin* dataWin, FILE* fp) {
    if (dataWin == NULL || fp == NULL) return false;
    uint8_t header[16];
    bool ok = fread(header, 1, sizeof(header), fp) == sizeof(header);
    uint32_t version = ok ? C3D_readLe32(header + 0u) : 0u;
    uint32_t tpagCount = ok ? C3D_readLe32(header + 4u) : 0u;
    uint32_t tpagHash = ok ? C3D_readLe32(header + 8u) : 0u;
    uint32_t rowSize = ok ? C3D_readLe32(header + 12u) : 0u;
    if (!ok || version != N3DS_TPAG_BINDINGS_VERSION || tpagCount != dataWin->tpag.count ||
        tpagHash != C3D_computeTpagHash(dataWin) || rowSize != 16u) {
        return false;
    }

    N3DSTpagBinding* bindings =
        safeCalloc(tpagCount > 0u ? tpagCount : 1u, sizeof(N3DSTpagBinding));
    for (uint32_t i = 0; i < tpagCount; ++i) {
        uint8_t row[16];
        if (fread(row, 1, sizeof(row), fp) != sizeof(row)) {
            ok = false;
            break;
        }
        N3DSTpagBinding* binding = &bindings[i];
        binding->source = (N3DSTpagSource)row[0];
        binding->flags = row[1];
        binding->subtexIndex = (uint16_t)row[2] | (uint16_t)((uint16_t)row[3] << 8u);
        binding->resizeDivisor = (uint16_t)row[4] | (uint16_t)((uint16_t)row[5] << 8u);
        if (binding->resizeDivisor == 0u) binding->resizeDivisor = 1u;
        binding->atlasItemIndex = C3D_readLe32(row + 8u);
        if (binding->source == N3DS_TPAG_SOURCE_ATLAS) {
            binding->assetIndex = 0u;
            if (!C3D_atlasItemIsDrawable(renderer, binding->atlasItemIndex)) {
                ok = false;
                break;
            }
        } else if (binding->source == N3DS_TPAG_SOURCE_NONE) {
            binding->atlasItemIndex = UINT32_MAX;
            binding->assetIndex = 0u;
        } else {
            ok = false;
            break;
        }
    }

    if (!ok) {
        free(bindings);
        return false;
    }

    for (ptrdiff_t i = 0; i < hmlen(renderer->atlas.tilemapLookup); i++) {
        int32_t index = renderer->atlas.tilemapLookup[i].key.tpagIndex;
        if ((uint32_t)index < tpagCount) bindings[index].hasTilemapEntries = true;
    }
    C3D_destroyTpagBindings(renderer);
    renderer->tpagBindings = bindings;
    renderer->tpagBindingCount = tpagCount;
    renderer->runtimeTpagStart = tpagCount;
    return true;
}

static bool C3D_loadEmbedTpagBindings(N3DSRenderer* renderer, DataWin* dataWin) {
    if (dataWin == NULL || renderer->atlas.embeddedTpagBindingOffset == 0u ||
        renderer->atlas.embeddedTpagBindingSize < 16u || renderer->atlas.atlasPath[0] == '\0') {
        return false;
    }
    FILE* fp = n3ds_fsOpenReadPath(renderer->atlas.atlasPath);
    if (fp == NULL) return false;
    bool ok = fseek(fp, (long)renderer->atlas.embeddedTpagBindingOffset, SEEK_SET) == 0 &&
        C3D_loadTpagBindingsFile(renderer, dataWin, fp);
    fclose(fp);
    return ok;
}

static const N3DSRoomManifestEntry* C3D_findRoomManifestEntry(
    const N3DSRenderer* renderer, int32_t roomIndex) {
    if (!renderer->roomManifest.loaded || roomIndex < 0) return NULL;
    if (renderer->roomManifest.entryByRoomIndex != NULL &&
        (uint32_t)roomIndex < renderer->roomManifest.entryByRoomIndexCount) {
        int32_t entryIndex = renderer->roomManifest.entryByRoomIndex[roomIndex];
        if (entryIndex >= 0 && (uint32_t)entryIndex < renderer->roomManifest.entryCount) {
            return &renderer->roomManifest.entries[entryIndex];
        }
        return NULL;
    }
    for (uint32_t i = 0; i < renderer->roomManifest.entryCount; ++i) {
        if ((int32_t)renderer->roomManifest.entries[i].roomIndex == roomIndex)
            return &renderer->roomManifest.entries[i];
    }
    return NULL;
}

static N3DSRoomTileLookupKey C3D_roomTileLookupKey(const RoomTile* tile) {
    return (N3DSRoomTileLookupKey){
        .backgroundDefinition = tile->backgroundDefinition,
        .sourceX = tile->sourceX,
        .sourceY = tile->sourceY,
        .width = tile->width,
        .height = tile->height,
        .useSpriteDefinition = tile->useSpriteDefinition ? 1 : 0,
    };
}

static void C3D_buildRoomTileRanges(N3DSRenderer* renderer, int32_t roomIndex) {
    free(renderer->roomTileRanges);
    renderer->roomTileRanges = NULL;
    renderer->roomTileRangeCount = 0;
    hmfree(renderer->roomTileLookup);
    renderer->roomTileLookup = NULL;
    if (renderer->base.dataWin == NULL || roomIndex < 0 ||
        (uint32_t)roomIndex >= renderer->base.dataWin->room.count) {
        return;
    }

    Room* room = &renderer->base.dataWin->room.rooms[roomIndex];
    uint32_t capacity = room->layerCount + (room->tileCount > 0u ? 1u : 0u);
    if (capacity == 0u) return;
    renderer->roomTileRanges = safeCalloc(capacity, sizeof(N3DSRoomTileRange));
    uint32_t tileOffset = 0u;
    if (room->tileCount > 0u) {
        renderer->roomTileRanges[renderer->roomTileRangeCount++] = (N3DSRoomTileRange){
            .tiles = room->tiles,
            .count = room->tileCount,
            .tileOffset = 0u,
        };
        tileOffset = room->tileCount;
    }
    for (uint32_t i = 0u; i < room->layerCount; ++i) {
        RoomLayerAssetsData* assets = room->layers[i].assetsData;
        if (assets == NULL || assets->legacyTileCount == 0u) continue;
        renderer->roomTileRanges[renderer->roomTileRangeCount++] = (N3DSRoomTileRange){
            .tiles = assets->legacyTiles,
            .count = assets->legacyTileCount,
            .tileOffset = tileOffset,
        };
        tileOffset += assets->legacyTileCount;
    }

    const N3DSRoomManifestEntry* entry = C3D_findRoomManifestEntry(renderer, roomIndex);
    if (entry == NULL || entry->tileCount == 0 || renderer->roomManifest.tileRefs == NULL) return;

    for (uint32_t i = 0; i < renderer->roomTileRangeCount; ++i) {
        const N3DSRoomTileRange* range = &renderer->roomTileRanges[i];
        for (uint32_t j = 0; j < range->count; ++j) {
            uint32_t tileOffset = range->tileOffset + j;
            if (tileOffset >= entry->tileCount) break;

            uint32_t refIndex = entry->tileStart + tileOffset;
            if (refIndex >= renderer->roomManifest.tileRefCount) break;

            N3DSRoomTileLookupKey key = C3D_roomTileLookupKey(&range->tiles[j]);
            if (hmgeti(renderer->roomTileLookup, key) < 0) {
                hmput(renderer->roomTileLookup, key, refIndex);
            }
        }
    }
}

void C3D_syncRoom(Renderer* base, int32_t roomIndex) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (roomIndex < 0 || renderer->activeRoomIndex == roomIndex) return;
    renderer->activeRoomIndex = roomIndex;
    C3D_buildRoomTileRanges(renderer, roomIndex);
    if (renderer->atlas.pages != NULL) {
        for (uint32_t i = 0; i < renderer->atlas.pageCount; ++i)
            renderer->atlas.pages[i].loadFailed = false;
    }
    if (renderer->atlas.roomWantedPages != NULL)
        memset(renderer->atlas.roomWantedPages, 0, sizeof(bool) * renderer->atlas.pageCount);

    const N3DSRoomManifestEntry* entry = NULL;
    if (renderer->roomManifest.loaded)
        entry = C3D_findRoomManifestEntry(renderer, roomIndex);

    DataWin* dataWin = renderer->base.dataWin;
    if (entry != NULL) {
        const char* roomName =
            (dataWin != NULL && roomIndex >= 0 && (uint32_t)roomIndex < dataWin->room.count &&
                dataWin->room.rooms[roomIndex].name != NULL)
                ? dataWin->room.rooms[roomIndex].name
                : "(unknown)";
        logEvent("renderer", "room atlas room=%s manifest=yes loading=on-demand", roomName);
    }

    if (renderer->atlas.loaded) C3D_evictAtlasPages(renderer, 0u, UINT32_MAX);
}

typedef struct {
    float x, y, width, height;
} N3DSAtlasSource;

typedef struct {
    float left, top, right, bottom;
} N3DSAtlasDest;

static void C3D_getAtlasFragmentRegion(const N3DSAtlasItem* item,
    const N3DSAtlasFragment* fragment, const N3DSAtlasPage* page, N3DSUVRect* region) {
    uint16_t divisor = item->resizeDivisor > 1u ? item->resizeDivisor : 1u;
    uint16_t width = divisor == 2u ? C3D_scaleDownHalf16(fragment->width) : fragment->width;
    uint16_t height = divisor == 2u ? C3D_scaleDownHalf16(fragment->height) : fragment->height;
    region->left = page->region.left + (float)fragment->x * page->uPerPixel;
    region->top = page->region.top + (float)fragment->y * page->vPerPixel;
    region->right = page->region.left + (float)(fragment->x + width) * page->uPerPixel;
    region->bottom = page->region.top + (float)(fragment->y + height) * page->vPerPixel;
    region->width = fragment->width;
    region->height = fragment->height;
}

static bool C3D_mapAtlasQuad(N3DSRenderer* renderer, const N3DSDrawMapper* mapper,
    N3DSAtlasDest dst, N3DSVertex* vertices) {
    if (mapper->kind == N3DS_MAP_AXIS && !renderer->screenTransformActive) {
        vertices[0].pos.x = mapper->axisX + dst.left * mapper->axisScaleX;
        vertices[0].pos.y = mapper->axisY + dst.top * mapper->axisScaleY;
        vertices[1].pos.x = mapper->axisX + dst.right * mapper->axisScaleX;
        vertices[1].pos.y = vertices[0].pos.y;
        vertices[2].pos.x = vertices[0].pos.x;
        vertices[2].pos.y = mapper->axisY + dst.bottom * mapper->axisScaleY;
        vertices[3].pos.x = vertices[1].pos.x;
        vertices[3].pos.y = vertices[2].pos.y;
        float minX = vertices[0].pos.x < vertices[1].pos.x
            ? vertices[0].pos.x
            : vertices[1].pos.x;
        float maxX = vertices[0].pos.x > vertices[1].pos.x
            ? vertices[0].pos.x
            : vertices[1].pos.x;
        float minY = vertices[0].pos.y < vertices[2].pos.y
            ? vertices[0].pos.y
            : vertices[2].pos.y;
        float maxY = vertices[0].pos.y > vertices[2].pos.y
            ? vertices[0].pos.y
            : vertices[2].pos.y;
        return C3D_axisRectVisible(renderer, minX, minY, maxX, maxY);
    }
    return C3D_mapVisibleSpriteQuad(renderer, mapper, dst.left, dst.top, dst.right,
        dst.bottom, &vertices[0].pos.x, &vertices[0].pos.y, &vertices[1].pos.x,
        &vertices[1].pos.y, &vertices[2].pos.x, &vertices[2].pos.y, &vertices[3].pos.x,
        &vertices[3].pos.y);
}

static bool C3D_emitSingleAtlasFragment(N3DSRenderer* renderer,
    const N3DSAtlasItem* item, const N3DSAtlasFragment* fragment, N3DSAtlasSource src,
    const N3DSDrawMapper* mapper, N3DSAtlasDest dst, uint32_t rgba) {
    float fragmentRight = (float)fragment->sourceX + (float)fragment->width;
    float fragmentBottom = (float)fragment->sourceY + (float)fragment->height;
    float srcRight = src.x + src.width;
    float srcBottom = src.y + src.height;
    if (src.x < fragment->sourceX || src.y < fragment->sourceY || srcRight > fragmentRight ||
        srcBottom > fragmentBottom) {
        float left = src.x > fragment->sourceX ? src.x : (float)fragment->sourceX;
        float top = src.y > fragment->sourceY ? src.y : (float)fragment->sourceY;
        float right = srcRight < fragmentRight ? srcRight : fragmentRight;
        float bottom = srcBottom < fragmentBottom ? srcBottom : fragmentBottom;
        if (left >= right || top >= bottom) return true;
        float width = dst.right - dst.left;
        float height = dst.bottom - dst.top;
        dst.right = dst.left + width * ((right - src.x) / src.width);
        dst.bottom = dst.top + height * ((bottom - src.y) / src.height);
        dst.left += width * ((left - src.x) / src.width);
        dst.top += height * ((top - src.y) / src.height);
        src.x = left;
        src.y = top;
        srcRight = right;
        srcBottom = bottom;
    }

    N3DSVertex vertices[4] = {0};
    if (!C3D_mapAtlasQuad(renderer, mapper, dst, vertices)) return true;
    if (fragment->pageIndex >= renderer->atlas.pageCount ||
        !C3D_loadRoomWantedAtlasPage(renderer, fragment->pageIndex, NULL)) {
        return true;
    }

    N3DSAtlasPage* page = &renderer->atlas.pages[fragment->pageIndex];
    N3DSUVRect region;
    C3D_getAtlasFragmentRegion(item, fragment, page, &region);
    if (item->resizeDivisor <= 1u) {
        vertices[0].uv = (N3DSUV){
            region.left + (src.x - fragment->sourceX) * page->uPerPixel,
            region.top + (src.y - fragment->sourceY) * page->vPerPixel};
        vertices[1].uv = (N3DSUV){
            region.left + (srcRight - fragment->sourceX) * page->uPerPixel, vertices[0].uv.v};
        vertices[2].uv = (N3DSUV){vertices[0].uv.u,
            region.top + (srcBottom - fragment->sourceY) * page->vPerPixel};
    } else {
        float invW = fragment->width > 0u ? 1.0f / (float)fragment->width : 0.0f;
        float invH = fragment->height > 0u ? 1.0f / (float)fragment->height : 0.0f;
        float u0 = (src.x - fragment->sourceX) * invW;
        float v0 = (src.y - fragment->sourceY) * invH;
        float u1 = (srcRight - fragment->sourceX) * invW;
        float v1 = (srcBottom - fragment->sourceY) * invH;
        float du = region.right - region.left;
        float dv = region.bottom - region.top;
        vertices[0].uv = (N3DSUV){region.left + du * u0, region.top + dv * v0};
        vertices[1].uv = (N3DSUV){region.left + du * u1, vertices[0].uv.v};
        vertices[2].uv = (N3DSUV){vertices[0].uv.u, region.top + dv * v1};
    }
    vertices[3].uv = (N3DSUV){vertices[1].uv.u, vertices[2].uv.v};
    n3ds_c3dQuad(&renderer->drawContext, &page->tex, vertices, rgba);
    return true;
}

static void C3D_emitAtlasFragment(N3DSRenderer* renderer, const N3DSAtlasItem* item,
    const N3DSAtlasFragment* fragment, N3DSAtlasSource src, const N3DSDrawMapper* mapper,
    N3DSAtlasDest dst, uint32_t rgba) {
    float overlapLeft = src.x > fragment->sourceX ? src.x : (float)fragment->sourceX;
    float overlapTop = src.y > fragment->sourceY ? src.y : (float)fragment->sourceY;
    float fragmentRight = (float)fragment->sourceX + (float)fragment->width;
    float fragmentBottom = (float)fragment->sourceY + (float)fragment->height;
    float overlapRight = src.x + src.width < fragmentRight ? src.x + src.width : fragmentRight;
    float overlapBottom = src.y + src.height < fragmentBottom
        ? src.y + src.height
        : fragmentBottom;
    if (overlapLeft >= overlapRight || overlapTop >= overlapBottom) return;

    float relLeft = (float)(overlapLeft - src.x) / (float)src.width;
    float relTop = (float)(overlapTop - src.y) / (float)src.height;
    float relRight = (float)(overlapRight - src.x) / (float)src.width;
    float relBottom = (float)(overlapBottom - src.y) / (float)src.height;
    N3DSAtlasDest clipped = {
        .left = dst.left + (dst.right - dst.left) * relLeft,
        .top = dst.top + (dst.bottom - dst.top) * relTop,
        .right = dst.left + (dst.right - dst.left) * relRight,
        .bottom = dst.top + (dst.bottom - dst.top) * relBottom,
    };
    if (!C3D_mappedRectVisible(
        renderer, mapper, clipped.left, clipped.top, clipped.right, clipped.bottom)) {
        return;
    }
    if (fragment->pageIndex >= renderer->atlas.pageCount ||
        !C3D_loadRoomWantedAtlasPage(renderer, fragment->pageIndex, NULL)) {
        return;
    }

    N3DSAtlasPage* page = &renderer->atlas.pages[fragment->pageIndex];
    N3DSUVRect region;
    C3D_getAtlasFragmentRegion(item, fragment, page, &region);
    C3D_emitMappedQuadSprite(renderer, &page->tex, &region, mapper,
        (float)(overlapLeft - fragment->sourceX), (float)(overlapTop - fragment->sourceY),
        (float)(overlapRight - fragment->sourceX), (float)(overlapBottom - fragment->sourceY),
        clipped.left, clipped.top, clipped.right, clipped.bottom, rgba, rgba, rgba, rgba);
}

static void C3D_emitAtlas(N3DSRenderer* renderer, const N3DSAtlasItem* item,
    N3DSAtlasSource src, const N3DSDrawMapper* mapper, N3DSAtlasDest dst, uint32_t rgba) {
    if (item == NULL || mapper == NULL || src.width <= 0 || src.height <= 0)
        return;
    if (item->fragmentCount == 0u || item->fragmentStart >= renderer->atlas.fragmentCount) return;

    N3DSAtlasFragment* first = &renderer->atlas.fragments[item->fragmentStart];
    if (item->fragmentCount == 1u) {
        if (C3D_emitSingleAtlasFragment(renderer, item, first, src, mapper, dst, rgba))
            return;
    } else if (!C3D_mappedRectVisible(
        renderer, mapper, dst.left, dst.top, dst.right, dst.bottom)) {
        return;
    }

    for (uint32_t i = 0; i < item->fragmentCount; ++i) {
        uint32_t fragmentIndex = item->fragmentStart + i;
        if (fragmentIndex >= renderer->atlas.fragmentCount) break;
        C3D_emitAtlasFragment(renderer, item, &renderer->atlas.fragments[fragmentIndex],
            src, mapper, dst, rgba);
    }
}

static void C3D_drawAtlas(N3DSRenderer* renderer, int32_t tpagIndex, N3DSAtlasSource src,
    const N3DSDrawMapper* mapper, N3DSAtlasDest dst, uint32_t rgba) {
    N3DSAtlasItem* item = NULL;
    if (mapper == NULL || src.width <= 0 || src.height <= 0 ||
        !C3D_getAtlasBinding(renderer, tpagIndex, NULL, &item)) {
        return;
    }
    C3D_emitAtlas(renderer, item, src, mapper, dst, rgba);
}

static bool C3D_tileArrayIndex(
    const RoomTile* tile, const RoomTile* tiles, uint32_t count, uint32_t* outIndex) {
    if (tile == NULL || tiles == NULL || count == 0u) return false;
    uintptr_t address = (uintptr_t)tile;
    uintptr_t start = (uintptr_t)tiles;
    uintptr_t bytes = (uintptr_t)count * sizeof(RoomTile);
    if (address < start || address - start >= bytes || (address - start) % sizeof(RoomTile) != 0u)
        return false;
    if (outIndex != NULL) *outIndex = (uint32_t)((address - start) / sizeof(RoomTile));
    return true;
}

static const N3DSRoomManifestTileRef* C3D_findRoomTileRef(
    N3DSRenderer* renderer, const RoomTile* tile) {
    if (tile == NULL || renderer->activeRoomIndex < 0 ||
        renderer->roomManifest.tileRefs == NULL || renderer->roomManifest.tileRefCount == 0u ||
        renderer->base.dataWin == NULL ||
        (uint32_t)renderer->activeRoomIndex >= renderer->base.dataWin->room.count) {
        return NULL;
    }
    const N3DSRoomManifestEntry* entry =
        C3D_findRoomManifestEntry(renderer, renderer->activeRoomIndex);
    if (entry == NULL || entry->tileCount == 0u) return NULL;

    uint32_t tileOffset = 0u;
    uint32_t localIndex = 0u;
    bool found = false;
    for (uint32_t i = 0u; i < renderer->roomTileRangeCount; ++i) {
        const N3DSRoomTileRange* range = &renderer->roomTileRanges[i];
        if (C3D_tileArrayIndex(tile, range->tiles, range->count, &localIndex)) {
            tileOffset = range->tileOffset + localIndex;
            found = true;
            break;
        }
    }
    uint32_t refIndex;
    if (found) {
        if (tileOffset >= entry->tileCount) return NULL;
        refIndex = entry->tileStart + tileOffset;
    } else {
        N3DSRoomTileLookupKey key = C3D_roomTileLookupKey(tile);
        ptrdiff_t lookupIndex = hmgeti(renderer->roomTileLookup, key);
        if (lookupIndex < 0) return NULL;
        refIndex = renderer->roomTileLookup[lookupIndex].value;
    }
    if (refIndex >= renderer->roomManifest.tileRefCount) return NULL;
    return &renderer->roomManifest.tileRefs[refIndex];
}

static const N3DSAtlasTileEntry* C3D_findAtlasTileByKey(N3DSRenderer* renderer,
    int16_t bgDef, uint16_t srcX, uint16_t srcY, uint16_t srcW, uint16_t srcH) {
    if (!renderer->atlas.loaded) return NULL;
    N3DSAtlasTileKey key = {
        .bgDef = bgDef,
        .srcX = srcX,
        .srcY = srcY,
        .srcW = srcW,
        .srcH = srcH,
    };
    uint32_t hash = (uint32_t)(uint16_t)bgDef * 2654435761u;
    hash ^= (uint32_t)srcX * 2246822519u;
    hash ^= (uint32_t)srcY * 3266489917u;
    hash ^= ((uint32_t)srcW << 16u) | srcH;
    N3DSAtlasTileCacheEntry* cached =
        &renderer->atlas.tileCache[hash & (N3DS_TILE_LOOKUP_CACHE_SIZE - 1u)];
    if (cached->valid && cached->key.bgDef == key.bgDef && cached->key.srcX == key.srcX &&
        cached->key.srcY == key.srcY && cached->key.srcW == key.srcW &&
        cached->key.srcH == key.srcH && cached->value < renderer->atlas.tileEntryCount) {
        return &renderer->atlas.tileEntries[cached->value];
    }
    ptrdiff_t lookupIndex = hmgeti(renderer->atlas.tileLookup, key);
    if (lookupIndex >= 0) {
        uint32_t entryIndex = renderer->atlas.tileLookup[lookupIndex].value;
        if (entryIndex < renderer->atlas.tileEntryCount) {
            cached->valid = true;
            cached->key = key;
            cached->value = entryIndex;
            return &renderer->atlas.tileEntries[entryIndex];
        }
    }
    return NULL;
}

static const N3DSAtlasTileEntry* C3D_findAtlasTileEntry(
    N3DSRenderer* renderer, const RoomTile* tile) {
    if (tile == NULL || tile->useSpriteDefinition) return NULL;
    if (tile->backgroundDefinition < 0 || tile->sourceX < 0 || tile->sourceY < 0 ||
        tile->width > UINT16_MAX || tile->height > UINT16_MAX)
        return NULL;
    return C3D_findAtlasTileByKey(renderer, (int16_t)tile->backgroundDefinition,
        (uint16_t)tile->sourceX, (uint16_t)tile->sourceY, (uint16_t)tile->width,
        (uint16_t)tile->height);
}

static const N3DSAtlasTileEntry* C3D_findTilemapAtlasEntry(N3DSRenderer* renderer,
    int32_t tpagIndex, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH) {
    if (tpagIndex < 0) return NULL;
    if (srcX < 0 || srcY < 0 || srcW <= 0 || srcH <= 0 || srcX > UINT16_MAX || srcY > UINT16_MAX ||
        srcW > UINT16_MAX || srcH > UINT16_MAX)
        return NULL;
    N3DSAtlasTilemapKey key = {
        .tpagIndex = tpagIndex,
        .srcX = (uint16_t)srcX,
        .srcY = (uint16_t)srcY,
        .srcW = (uint16_t)srcW,
        .srcH = (uint16_t)srcH,
    };
    ptrdiff_t lookupIndex = hmgeti(renderer->atlas.tilemapLookup, key);
    if (lookupIndex < 0) return NULL;
    uint32_t entryIndex = renderer->atlas.tilemapLookup[lookupIndex].value;
    return entryIndex < renderer->atlas.tileEntryCount
        ? &renderer->atlas.tileEntries[entryIndex]
        : NULL;
}

static bool C3D_drawMappedAtlasTile(N3DSRenderer* renderer,
    const N3DSAtlasTileEntry* entry, const N3DSDrawMapper* mapper, float dstLeft, float dstTop,
    float dstRight, float dstBottom, uint32_t rgba) {
    if (entry == NULL || mapper == NULL || entry->srcW == 0u ||
        entry->srcH == 0u)
        return false;
    if (entry->pageIndex != UINT16_MAX) {
        if (!C3D_mappedRectVisible(
                renderer, mapper, dstLeft, dstTop, dstRight, dstBottom))
            return true;
        if (entry->pageIndex >= renderer->atlas.pageCount ||
            !C3D_loadRoomWantedAtlasPage(renderer, entry->pageIndex, NULL)) {
            return false;
        }
        N3DSAtlasPage* page = &renderer->atlas.pages[entry->pageIndex];
        N3DSUVRect region;
        C3D_uvSubRect(&region, &page->region, (float)entry->x, (float)entry->y,
            (float)(entry->x + entry->width), (float)(entry->y + entry->height));
        region.width = entry->srcW;
        region.height = entry->srcH;
        C3D_emitMappedQuadSprite(renderer, &page->tex, &region, mapper, 0.0f, 0.0f,
            (float)entry->srcW, (float)entry->srcH, dstLeft, dstTop, dstRight, dstBottom, rgba,
            rgba, rgba, rgba);
        return true;
    }

    if (entry->fragmentCount == 0u || entry->fragmentStart >= renderer->atlas.fragmentCount)
        return false;
    float dstW = dstRight - dstLeft;
    float dstH = dstBottom - dstTop;
    for (uint32_t i = 0; i < entry->fragmentCount; ++i) {
        uint32_t fragmentIndex = entry->fragmentStart + i;
        if (fragmentIndex >= renderer->atlas.fragmentCount) break;
        N3DSAtlasFragment* fragment = &renderer->atlas.fragments[fragmentIndex];
        float fragDstLeft = dstLeft + dstW * ((float)fragment->sourceX / (float)entry->srcW);
        float fragDstTop = dstTop + dstH * ((float)fragment->sourceY / (float)entry->srcH);
        float fragDstRight =
            dstLeft + dstW * ((float)(fragment->sourceX + fragment->width) / (float)entry->srcW);
        float fragDstBottom =
            dstTop + dstH * ((float)(fragment->sourceY + fragment->height) / (float)entry->srcH);
        if (!C3D_mappedRectVisible(
                renderer, mapper, fragDstLeft, fragDstTop, fragDstRight, fragDstBottom))
            continue;
        if (fragment->pageIndex >= renderer->atlas.pageCount ||
            !C3D_loadRoomWantedAtlasPage(renderer, fragment->pageIndex, NULL)) {
            continue;
        }
        N3DSAtlasPage* page = &renderer->atlas.pages[fragment->pageIndex];
        N3DSUVRect region;
        C3D_uvSubRect(&region, &page->region, (float)fragment->x, (float)fragment->y,
            (float)(fragment->x + fragment->width), (float)(fragment->y + fragment->height));
        region.width = fragment->width;
        region.height = fragment->height;
        C3D_emitMappedQuadSprite(renderer, &page->tex, &region, mapper, 0.0f, 0.0f,
            (float)fragment->width, (float)fragment->height, fragDstLeft, fragDstTop, fragDstRight,
            fragDstBottom, rgba, rgba, rgba, rgba);
    }
    return true;
}

static bool C3D_drawAtlasTileEntry(N3DSRenderer* renderer, const N3DSAtlasTileEntry* entry,
    const RoomTile* tile, float offsetX, float offsetY) {
    if (entry == NULL || tile == NULL || entry->srcW == 0u || entry->srcH == 0u)
        return false;
    N3DSDrawMapper mapper;
    C3D_mapInitAxis(
        &mapper, (float)tile->x + offsetX, (float)tile->y + offsetY, tile->scaleX, tile->scaleY);

    if (entry->pageIndex != UINT16_MAX) {
        if (!renderer->screenTransformActive) {
            float x1 = mapper.axisX;
            float y1 = mapper.axisY;
            float x2 = x1 + (float)entry->srcW * mapper.axisScaleX;
            float y2 = y1 + (float)entry->srcH * mapper.axisScaleY;
            if (entry->pageIndex >= renderer->atlas.pageCount ||
                !C3D_loadRoomWantedAtlasPage(renderer, entry->pageIndex, NULL)) {
                return false;
            }

            N3DSAtlasPage* page = &renderer->atlas.pages[entry->pageIndex];
            float u1 = page->region.left + (float)entry->x * page->uPerPixel;
            float v1 = page->region.top + (float)entry->y * page->vPerPixel;
            float u2 = u1 + (float)entry->width * page->uPerPixel;
            float v2 = v1 + (float)entry->height * page->vPerPixel;
            uint32_t rgba = C3D_makeColor(tile->color & 0x00FFFFFFu, tile->alpha);
            const N3DSVertex vertices[4] = {
                {{x1, y1}, {u1, v1}},
                {{x2, y1}, {u2, v1}},
                {{x1, y2}, {u1, v2}},
                {{x2, y2}, {u2, v2}},
            };
            n3ds_c3dQuad(&renderer->drawContext, &page->tex, vertices, rgba);
            return true;
        }
        if (!C3D_mappedRectVisible(
                renderer, &mapper, 0.0f, 0.0f, (float)entry->srcW, (float)entry->srcH))
            return true;
        if (entry->pageIndex >= renderer->atlas.pageCount ||
            !C3D_loadRoomWantedAtlasPage(renderer, entry->pageIndex, NULL)) {
            return false;
        }
        N3DSAtlasPage* page = &renderer->atlas.pages[entry->pageIndex];
        N3DSUVRect region;
        C3D_uvSubRect(&region, &page->region, (float)entry->x, (float)entry->y,
            (float)(entry->x + entry->width), (float)(entry->y + entry->height));
        region.width = entry->srcW;
        region.height = entry->srcH;
        uint32_t rgba = C3D_makeColor(tile->color & 0x00FFFFFFu, tile->alpha);
        C3D_emitMappedQuadSprite(renderer, &page->tex, &region, &mapper, 0.0f, 0.0f,
            (float)entry->srcW, (float)entry->srcH, 0.0f, 0.0f, (float)entry->srcW,
            (float)entry->srcH, rgba, rgba, rgba, rgba);
        return true;
    }

    if (entry->fragmentCount == 0u || entry->fragmentStart >= renderer->atlas.fragmentCount)
        return false;
    uint32_t rgba = C3D_makeColor(tile->color & 0x00FFFFFFu, tile->alpha);
    for (uint32_t i = 0; i < entry->fragmentCount; ++i) {
        uint32_t fragmentIndex = entry->fragmentStart + i;
        if (fragmentIndex >= renderer->atlas.fragmentCount) break;
        N3DSAtlasFragment* fragment = &renderer->atlas.fragments[fragmentIndex];
        float dstLeft = (float)fragment->sourceX;
        float dstTop = (float)fragment->sourceY;
        float dstRight = (float)(fragment->sourceX + fragment->width);
        float dstBottom = (float)(fragment->sourceY + fragment->height);
        if (!C3D_mappedRectVisible(
                renderer, &mapper, dstLeft, dstTop, dstRight, dstBottom))
            continue;
        if (fragment->pageIndex >= renderer->atlas.pageCount ||
            !C3D_loadRoomWantedAtlasPage(renderer, fragment->pageIndex, NULL)) {
            continue;
        }
        N3DSAtlasPage* page = &renderer->atlas.pages[fragment->pageIndex];
        N3DSUVRect region;
        C3D_uvSubRect(&region, &page->region, (float)fragment->x, (float)fragment->y,
            (float)(fragment->x + fragment->width), (float)(fragment->y + fragment->height));
        region.width = fragment->width;
        region.height = fragment->height;
        C3D_emitMappedQuadSprite(renderer, &page->tex, &region, &mapper, 0.0f, 0.0f,
            (float)fragment->width, (float)fragment->height, dstLeft, dstTop, dstRight, dstBottom,
            rgba, rgba, rgba, rgba);
    }
    return true;
}

static void C3D_drawSpritePart(Renderer* base, int32_t tpagIndex, float srcOffX,
    float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale,
    float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha);

static void C3D_drawTileResolved(
    Renderer* base, RoomTile* tile, float offsetX, float offsetY, int32_t tpagIndex) {
    if (base->dataWin == NULL || tile == NULL) return;
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

    C3D_drawSpritePart(base, tpagIndex, srcX - tpag->targetX, srcY - tpag->targetY, srcW,
        srcH, drawX, drawY, tile->scaleX, tile->scaleY, 0.0f, 0.0f, 0.0f, tile->color & 0x00FFFFFFu,
        tile->alpha);
}

bool C3D_roomTileVisible(const Renderer* base, const RoomTile* tile, float offsetX, float offsetY) {
    const N3DSRenderer* renderer = (const N3DSRenderer*)base;
    if (!renderer->screenTransformActive) {
        float x1 = (float)tile->x + offsetX;
        float y1 = (float)tile->y + offsetY;
        float x2 = x1 + (float)tile->width * tile->scaleX;
        float y2 = y1 + (float)tile->height * tile->scaleY;
        float minX = x1 < x2 ? x1 : x2;
        float maxX = x1 > x2 ? x1 : x2;
        float minY = y1 < y2 ? y1 : y2;
        float maxY = y1 > y2 ? y1 : y2;
        return C3D_axisRectVisible(renderer, minX, minY, maxX, maxY);
    }
    return true;
}

static void C3D_drawTile(Renderer* base, RoomTile* tile, float offsetX, float offsetY) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (tile == NULL || !C3D_roomTileVisible(base, tile, offsetX, offsetY)) return;
    const N3DSRoomManifestTileRef* roomRef = C3D_findRoomTileRef(renderer, tile);
    const N3DSAtlasTileEntry* entry =
        roomRef != NULL && roomRef->tileEntryIndex < renderer->atlas.tileEntryCount
            ? &renderer->atlas.tileEntries[roomRef->tileEntryIndex]
            : NULL;
    if (entry == NULL && (roomRef == NULL || roomRef->tpagIndex < 0)) {
        entry = C3D_findAtlasTileEntry(renderer, tile);
    }
    if (entry != NULL && C3D_drawAtlasTileEntry(renderer, entry, tile, offsetX, offsetY))
        return;
    int32_t tpagIndex = roomRef != NULL && roomRef->tpagIndex >= 0
                            ? roomRef->tpagIndex
                            : Renderer_resolveObjectTPAGIndex(base->dataWin, tile);
    C3D_drawTileResolved(base, tile, offsetX, offsetY, tpagIndex);
}

static bool C3D_isNativeScreenOverride(
    N3DSRenderer* renderer, int32_t tpagIndex, N3DSAtlasItem** outItem) {
    TexturePageItem* tpag = NULL;
    N3DSAtlasItem* item = NULL;
    if (!C3D_getAtlasBinding(renderer, tpagIndex, &tpag, &item)) return false;
    if (item->width != N3DS_TOP_WIDTH || item->height != N3DS_TOP_HEIGHT) return false;
    if (item->width == tpag->sourceWidth && item->height == tpag->sourceHeight) return false;
    if (outItem != NULL) *outItem = item;
    return true;
}

static void C3D_floorSpritePosition(N3DSRenderer* renderer, float left,
    float top, float* x, float* y) {
    if (renderer->base.texFilter || !renderer->halfResApplicationSurface ||
        renderer->currentTargetSurfaceId != renderer->applicationSurfaceId ||
        renderer->customCameraTransformActive || renderer->screenTransformActive ||
        renderer->currentViewAngle != 0.0f || renderer->currentViewW <= 0 ||
        renderer->currentViewH <= 0 || renderer->base.CPortW <= 0 || renderer->base.CPortH <= 0) {
        *x = floorf(*x);
        *y = floorf(*y);
        return;
    }
    float scaleX = (float)renderer->base.CPortW / (float)renderer->currentViewW;
    float scaleY = (float)renderer->base.CPortH / (float)renderer->currentViewH;
    float screenX = (*x + left - (float)renderer->currentViewX) * scaleX;
    float screenY = (*y + top - (float)renderer->currentViewY) * scaleY;
    *x += (floorf(screenX) - screenX) / scaleX;
    *y += (floorf(screenY) - screenY) / scaleY;
}

static bool C3D_shouldFloorSprite(float xscale, float yscale, float angleDeg) {
    float scale = fabsf(xscale);
    return fabsf(angleDeg) <= 0.0001f && scale >= 1.0f &&
        scale == fabsf(yscale) && scale == floorf(scale);
}

static void C3D_drawSprite(Renderer* base, int32_t tpagIndex, float x, float y,
    float originX, float originY, float xscale, float yscale, float angleDeg, uint32_t color,
    float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->dataWin == NULL || tpagIndex < 0 ||
        (uint32_t)tpagIndex >= base->dataWin->tpag.count)
        return;
    TexturePageItem* tpag = &base->dataWin->tpag.items[tpagIndex];
    if (tpag->sourceWidth == 0u || tpag->sourceHeight == 0u || alpha <= 0.0f) return;
    uint32_t rgba = C3D_makeColor(color, alpha);
    N3DSAtlasItem* nativeItem = NULL;
    if (C3D_isNativeScreenOverride(renderer, tpagIndex, &nativeItem)) {
        if (renderer->currentTargetSurfaceId != RENDER_TARGET_HOST_FRAMEBUFFER) return;
        bool screenTransformActive = renderer->screenTransformActive;
        bool hostViewProjectionActive = renderer->hostViewProjectionActive;
        bool viewCullActive = renderer->viewCullActive;
        C3D_Mtx projection = renderer->drawContext.projection;
        if (hostViewProjectionActive) {
            C3D_updateHostProjection(renderer);
            renderer->viewCullActive = false;
        }
        renderer->screenTransformActive = false;
        N3DSDrawMapper nativeMapper;
        C3D_mapInitSprite(&nativeMapper, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
        C3D_drawAtlas(renderer, tpagIndex,
            (N3DSAtlasSource){0, 0, nativeItem->width, nativeItem->height}, &nativeMapper,
            (N3DSAtlasDest){0.0f, 0.0f, (float)nativeItem->width, (float)nativeItem->height}, rgba);
        renderer->screenTransformActive = screenTransformActive;
        if (hostViewProjectionActive) {
            n3ds_c3dSetProjectionC3D(&renderer->drawContext, &projection);
            renderer->hostViewProjectionActive = true;
            renderer->viewCullActive = viewCullActive;
        }
        return;
    }
    if (xscale == 0.0f || yscale == 0.0f) return;
    float left = ((float)tpag->targetX - originX) * xscale;
    float top = ((float)tpag->targetY - originY) * yscale;
    if (C3D_shouldFloorSprite(xscale, yscale, angleDeg)) {
        C3D_floorSpritePosition(renderer, left, top, &x, &y);
    }
    N3DSDrawMapper mapper;
    C3D_mapInitSprite(&mapper, x, y, 1.0f, 1.0f, angleDeg);
    if ((uint32_t)tpagIndex < renderer->tpagBindingCount &&
        renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_ATLAS) {
        float right = left + (float)tpag->targetWidth * xscale;
        float bottom = top + (float)tpag->targetHeight * yscale;
        C3D_drawAtlas(renderer, tpagIndex,
            (N3DSAtlasSource){0, 0, tpag->sourceWidth, tpag->sourceHeight}, &mapper,
            (N3DSAtlasDest){left, top, right, bottom}, rgba);
        return;
    }

    float right = left + (float)tpag->targetWidth * xscale;
    float bottom = top + (float)tpag->targetHeight * yscale;
    if (!C3D_mappedRectVisible(renderer, &mapper, left, top, right, bottom))
        return;

    C3D_Tex* tex = NULL;
    N3DSUVRect* region = NULL;
    if (!C3D_resolveTextureRegion(renderer, tpagIndex, &tex, &region)) return;
    if (renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_SURFACE) {
        uint32_t surfaceID = renderer->tpagBindings[tpagIndex].assetIndex;
        if (surfaceID < renderer->surfaceCount) {
            N3DSUVRect contentRegion;
            C3D_getSurfaceContentRegion(
                &contentRegion, region, tpag, &renderer->surfaces[surfaceID]);
            C3D_emitMappedQuadSprite(renderer, tex, &contentRegion, &mapper, 0.0f, 0.0f,
                (float)tpag->sourceWidth, (float)tpag->sourceHeight, left, top, right, bottom, rgba,
                rgba, rgba, rgba);
            return;
        }
    }
    C3D_emitMappedQuadSprite(renderer, tex, region, &mapper, 0.0f, 0.0f,
        (float)region->width, (float)region->height, left, top, right, bottom, rgba, rgba, rgba,
        rgba);
}

static void C3D_drawSpritePart(Renderer* base, int32_t tpagIndex, float srcOffX,
    float srcOffY, float srcW, float srcH, float x, float y, float xscale, float yscale,
    float angleDeg, float pivotX, float pivotY, uint32_t color, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (srcW <= 0 || srcH <= 0 || alpha <= 0.0f) return;
    if (C3D_isNativeScreenOverride(renderer, tpagIndex, NULL)) {
        C3D_drawSprite(
            base, tpagIndex, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, color, alpha);
        return;
    }
    if (xscale == 0.0f || yscale == 0.0f) return;
    N3DSDrawMapper mapper;
    if (C3D_shouldFloorSprite(xscale, yscale, angleDeg)) {
        C3D_floorSpritePosition(renderer, 0.0f, 0.0f, &x, &y);
    }
    C3D_mapInitPart(&mapper, srcW, srcH, x, y, xscale, yscale, angleDeg, pivotX, pivotY);
    float dstLeft = 0.0f;
    float dstTop = 0.0f;
    float dstRight = dstLeft + srcW;
    float dstBottom = dstTop + srcH;
    if (!C3D_mappedRectVisible(
            renderer, &mapper, dstLeft, dstTop, dstRight, dstBottom))
        return;
    uint32_t rgba = C3D_makeColor(color, alpha);
    if ((uint32_t)tpagIndex < renderer->tpagBindingCount &&
        renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_ATLAS) {
        const N3DSAtlasTileEntry* tileEntry = NULL;
        if (renderer->tpagBindings[tpagIndex].hasTilemapEntries &&
            srcOffX >= 0.0f && srcOffX <= UINT16_MAX && srcOffY >= 0.0f &&
            srcOffY <= UINT16_MAX && srcW > 0.0f && srcW <= UINT16_MAX && srcH > 0.0f &&
            srcH <= UINT16_MAX && (float)(uint16_t)srcOffX == srcOffX &&
            (float)(uint16_t)srcOffY == srcOffY && (float)(uint16_t)srcW == srcW &&
            (float)(uint16_t)srcH == srcH) {
            tileEntry = C3D_findTilemapAtlasEntry(renderer, tpagIndex,
                (int32_t)srcOffX, (int32_t)srcOffY, (int32_t)srcW, (int32_t)srcH);
        }
        if (tileEntry != NULL &&
            C3D_drawMappedAtlasTile(
                renderer, tileEntry, &mapper, dstLeft, dstTop, dstRight, dstBottom, rgba))
            return;
        C3D_drawAtlas(renderer, tpagIndex, (N3DSAtlasSource){srcOffX, srcOffY, srcW, srcH},
            &mapper, (N3DSAtlasDest){dstLeft, dstTop, dstRight, dstBottom}, rgba);
        return;
    }

    C3D_Tex* tex = NULL;
    N3DSUVRect* region = NULL;
    if (!C3D_resolveTextureRegion(renderer, tpagIndex, &tex, &region)) return;
    if (renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_SURFACE && tpagIndex >= 0 &&
        (uint32_t)tpagIndex < renderer->base.dataWin->tpag.count) {
        uint32_t surfaceID = renderer->tpagBindings[tpagIndex].assetIndex;
        if (surfaceID < renderer->surfaceCount) {
            N3DSUVRect contentRegion;
            C3D_getSurfaceContentRegion(&contentRegion, region,
                &renderer->base.dataWin->tpag.items[tpagIndex], &renderer->surfaces[surfaceID]);
            C3D_emitMappedQuadSprite(renderer, tex, &contentRegion, &mapper, srcOffX,
                srcOffY, srcOffX + srcW, srcOffY + srcH, dstLeft, dstTop, dstRight, dstBottom,
                rgba, rgba, rgba, rgba);
            return;
        }
    }
    C3D_emitMappedQuadSprite(renderer, tex, region, &mapper, srcOffX, srcOffY,
        srcOffX + srcW, srcOffY + srcH, dstLeft, dstTop, dstRight, dstBottom, rgba, rgba, rgba,
        rgba);
}

static void C3D_drawSpritePos(Renderer* base, int32_t tpagIndex, float x1, float y1,
    float x2, float y2, float x3, float y3, float x4, float y4, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->dataWin == NULL || tpagIndex < 0 ||
        (uint32_t)tpagIndex >= base->dataWin->tpag.count)
        return;
    if (C3D_isNativeScreenOverride(renderer, tpagIndex, NULL)) {
        C3D_drawSprite(
            base, tpagIndex, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0xFFFFFFu, alpha);
        return;
    }
    TexturePageItem* tpag = &base->dataWin->tpag.items[tpagIndex];
    if (tpag->sourceWidth == 0u || tpag->sourceHeight == 0u || alpha <= 0.0f) return;
    if (!C3D_quadVisible(renderer, x1, y1, x2, y2, x3, y3, x4, y4)) return;
    N3DSDrawMapper mapper;
    C3D_mapInitBilerp(&mapper, 0.0f, 0.0f, (float)tpag->sourceWidth, (float)tpag->sourceHeight,
        x1, y1, x2, y2, x4, y4, x3, y3);
    uint32_t rgba = C3D_makeColor(0xFFFFFFu, alpha);
    if ((uint32_t)tpagIndex < renderer->tpagBindingCount &&
        renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_ATLAS) {
        C3D_drawAtlas(renderer, tpagIndex,
            (N3DSAtlasSource){0, 0, tpag->sourceWidth, tpag->sourceHeight}, &mapper,
            (N3DSAtlasDest){0.0f, 0.0f, (float)tpag->sourceWidth, (float)tpag->sourceHeight}, rgba);
        return;
    }

    C3D_Tex* tex = NULL;
    N3DSUVRect* region = NULL;
    if (!C3D_resolveTextureRegion(renderer, tpagIndex, &tex, &region)) return;
    if (renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_SURFACE) {
        uint32_t surfaceID = renderer->tpagBindings[tpagIndex].assetIndex;
        if (surfaceID < renderer->surfaceCount) {
            N3DSUVRect contentRegion;
            C3D_getSurfaceContentRegion(
                &contentRegion, region, tpag, &renderer->surfaces[surfaceID]);
            C3D_emitMappedQuad(renderer, tex, &contentRegion, &mapper, 0.0f, 0.0f,
                (float)tpag->sourceWidth, (float)tpag->sourceHeight, 0.0f, 0.0f,
                (float)tpag->sourceWidth, (float)tpag->sourceHeight, rgba, rgba, rgba, rgba);
            return;
        }
    }
    C3D_emitMappedQuad(renderer, tex, region, &mapper, 0.0f, 0.0f, (float)region->width,
        (float)region->height, 0.0f, 0.0f, (float)tpag->sourceWidth, (float)tpag->sourceHeight,
        rgba, rgba, rgba, rgba);
}

static void C3D_drawRectangle(Renderer* base, float x1, float y1, float x2, float y2,
    uint32_t color, float alpha, bool outline) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (outline) {
        base->vtable->drawLine(base, x1, y1, x2, y1, 1.0f, color, alpha);
        base->vtable->drawLine(base, x2, y1, x2, y2, 1.0f, color, alpha);
        base->vtable->drawLine(base, x2, y2, x1, y2, 1.0f, color, alpha);
        base->vtable->drawLine(base, x1, y2, x1, y1, 1.0f, color, alpha);
        return;
    }
    N3DSDrawMapper mapper;
    C3D_mapInitAxis(&mapper, 0.0f, 0.0f, 1.0f, 1.0f);
    uint32_t rgba = C3D_makeColor(color, alpha);
    C3D_emitMappedSolidQuad(
        renderer, &mapper, x1, y1, x2, y2, rgba, rgba, rgba, rgba);
}

static void C3D_drawRectangleColor(Renderer* base, float x1, float y1, float x2, float y2,
    uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, float alpha, bool outline) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (outline) {
        base->vtable->drawLineColor(base, x1, y1, x2, y1, 1.0f, c1, c2, alpha);
        base->vtable->drawLineColor(base, x2, y1, x2, y2, 1.0f, c2, c4, alpha);
        base->vtable->drawLineColor(base, x2, y2, x1, y2, 1.0f, c4, c3, alpha);
        base->vtable->drawLineColor(base, x1, y2, x1, y1, 1.0f, c3, c1, alpha);
        return;
    }
    N3DSDrawMapper mapper;
    C3D_mapInitAxis(&mapper, 0.0f, 0.0f, 1.0f, 1.0f);
    C3D_emitMappedSolidQuad(renderer, &mapper, x1, y1, x2, y2,
        C3D_makeColor(c1, alpha), C3D_makeColor(c2, alpha), C3D_makeColor(c3, alpha),
        C3D_makeColor(c4, alpha));
}

static void C3D_drawTriangle(Renderer* base, float x1, float y1, float x2, float y2,
    float x3, float y3, uint32_t c1, uint32_t c2, uint32_t c3, float alpha, bool outline) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (alpha <= 0.0f) return;
    if (outline) {
        base->vtable->drawLineColor(base, x1, y1, x2, y2, 1.0f, c1, c2, alpha);
        base->vtable->drawLineColor(base, x2, y2, x3, y3, 1.0f, c2, c3, alpha);
        base->vtable->drawLineColor(base, x3, y3, x1, y1, 1.0f, c3, c1, alpha);
        return;
    }
    C3D_transformPoint(renderer, &x1, &y1);
    C3D_transformPoint(renderer, &x2, &y2);
    C3D_transformPoint(renderer, &x3, &y3);
    const N3DSVertex vertices[3] = {
        {{x1, y1}, {0.0f, 1.0f}},
        {{x2, y2}, {1.0f, 1.0f}},
        {{x3, y3}, {0.0f, 0.0f}},
    };
    const uint32_t colors[3] = {
        C3D_makeColor(c1, alpha),
        C3D_makeColor(c2, alpha),
        C3D_makeColor(c3, alpha),
    };
    n3ds_c3dTriangle(&renderer->drawContext, &renderer->drawContext.whiteTex, vertices, colors);
}

static void C3D_transformLinePoint(
    const N3DSRenderer* renderer, N3DSVec2* point) {
    float x = 0.0f;
    float y = 0.0f;
    Matrix4f_transformPoint(&renderer->screenTransform, point->x, point->y, &x, &y);
    point->x = x;
    point->y = y;
}

static void C3D_unprojectHost(const Matrix4f* matrix, N3DSVec2* point) {
    point->x = (point->x - matrix->m[12]) / matrix->m[0];
    point->y = (point->y - matrix->m[13]) / matrix->m[5];
}

static bool C3D_clipLine(
    N3DSRenderer* renderer, N3DSVec2* start, N3DSVec2* end) {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    return !C3D_getLineClipRect(renderer, &left, &top, &right, &bottom) ||
        C3D_clipLineToRect(
            &start->x, &start->y, &end->x, &end->y, left, top, right, bottom);
}

static bool C3D_makeLineQuad(
    N3DSVec2 start, N3DSVec2 end, float width, N3DSVec2 quad[4]) {
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float lengthSq = dx * dx + dy * dy;
    if (lengthSq < N3DS_LINE_LENGTH_EPSILON * N3DS_LINE_LENGTH_EPSILON) return false;
    float scale = fmaxf(width, 1.0f) * 0.5f / sqrtf(lengthSq);
    N3DSVec2 normal = {-dy * scale, dx * scale};
    quad[0] = (N3DSVec2){start.x - normal.x, start.y - normal.y};
    quad[1] = (N3DSVec2){end.x - normal.x, end.y - normal.y};
    quad[2] = (N3DSVec2){start.x + normal.x, start.y + normal.y};
    quad[3] = (N3DSVec2){end.x + normal.x, end.y + normal.y};
    return true;
}

static float C3D_minimumLineWidth(const N3DSRenderer* renderer, bool renderSpace) {
    if (renderer->currentTargetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER) return 1.0f;
    int32_t surfaceID = renderer->currentTargetSurfaceId;
    const N3DSSurface* surface = surfaceID >= 0 && (uint32_t)surfaceID < renderer->surfaceCount
        ? &renderer->surfaces[surfaceID] : NULL;
    float displayScale = 1.0f;
    int32_t appSurfaceID = renderer->base.runner != NULL
        ? renderer->base.runner->applicationSurfaceId : renderer->applicationSurfaceId;
    if (surface != NULL && surfaceID == appSurfaceID && surface->width > 0 &&
        surface->height > 0 && renderer->frameGameW > 0 && renderer->frameGameH > 0) {
        float left, top, right, bottom;
        C3D_letterboxRect(renderer->frameGameW, renderer->frameGameH,
            N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT, &left, &top, &right, &bottom);
        displayScale = fminf((right - left) / surface->width, (bottom - top) / surface->height);
        if (displayScale <= 0.0f) displayScale = 1.0f;
    }
    if (!renderSpace && renderer->currentViewW > 0 && renderer->currentViewH > 0 &&
        renderer->base.CPortW > 0 && renderer->base.CPortH > 0) {
        float unitsPerPixel = fmaxf((float)renderer->currentViewW / renderer->base.CPortW,
            (float)renderer->currentViewH / renderer->base.CPortH);
        return fmaxf(1.0f, unitsPerPixel / displayScale);
    }
    if (surface != NULL) {
        float scaleX = C3D_surfacePhysicalScaleX(surface);
        float scaleY = C3D_surfacePhysicalScaleY(surface);
        if (scaleX > 0.0f && scaleY > 0.0f)
            return fmaxf(1.0f, 1.0f / (fminf(scaleX, scaleY) * displayScale));
    }
    return 1.0f;
}

static void C3D_drawLinePoint(Renderer* base, N3DSRenderer* renderer, N3DSVec2 point,
    bool renderSpace, bool hostProjection, float minimumWidth, uint32_t color, float alpha) {
    if (!renderSpace) {
        base->vtable->drawRectangle(
            base, point.x, point.y, point.x + minimumWidth, point.y + minimumWidth,
            color, alpha, false);
        return;
    }
    N3DSVec2 opposite = {point.x + minimumWidth, point.y + minimumWidth};
    if (hostProjection) {
        C3D_unprojectHost(&renderer->screenTransform, &point);
        C3D_unprojectHost(&renderer->screenTransform, &opposite);
    }
    n3ds_c3dSolidRect(&renderer->drawContext, point,
        (N3DSVec2){opposite.x - point.x, opposite.y - point.y}, C3D_makeColor(color, alpha));
}

static void C3D_drawLineColor(Renderer* base, float x1, float y1, float x2, float y2,
    float width, uint32_t color1, uint32_t color2, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (alpha <= 0.0f) return;
    bool hostViewProjectionActive = renderer->hostViewProjectionActive;
    bool renderSpace = renderer->screenTransformActive || hostViewProjectionActive;
    float minimumWidth = C3D_minimumLineWidth(renderer, renderSpace);
    width = fmaxf(width, minimumWidth);
    bool pixelSpace = renderSpace ||
        renderer->currentTargetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER;
    if (!renderSpace && renderer->viewCullActive) {
        float margin = (width > 1.0f ? width : 1.0f) * 0.5f + 1.0f;
        if (!C3D_axisRectVisible(renderer, (x1 < x2 ? x1 : x2) - margin,
            (y1 < y2 ? y1 : y2) - margin, (x1 > x2 ? x1 : x2) + margin,
            (y1 > y2 ? y1 : y2) + margin))
            return;
    }
    N3DSVec2 start = {x1, y1};
    N3DSVec2 end = {x2, y2};
    if (renderSpace) {
        C3D_transformLinePoint(renderer, &start);
        C3D_transformLinePoint(renderer, &end);
    }
    if (pixelSpace && !C3D_clipLine(renderer, &start, &end)) return;

    N3DSVec2 quad[4];
    if (!C3D_makeLineQuad(start, end, width, quad)) {
        C3D_drawLinePoint(base, renderer, start, renderSpace,
            hostViewProjectionActive, minimumWidth, color1, alpha);
        return;
    }
    if (hostViewProjectionActive) {
        for (uint32_t i = 0u; i < 4u; ++i)
            C3D_unprojectHost(&renderer->screenTransform, &quad[i]);
    }
    uint32_t rgba1 = C3D_makeColor(color1, alpha);
    uint32_t rgba2 = color1 == color2 ? rgba1 : C3D_makeColor(color2, alpha);
    n3ds_c3dLineQuad(&renderer->drawContext, quad, rgba1, rgba2);
}

static void C3D_drawLine(Renderer* base, float x1, float y1, float x2, float y2,
    float width, uint32_t color, float alpha) {
    C3D_drawLineColor(base, x1, y1, x2, y2, width, color, color, alpha);
}

static bool C3D_resolveFontState(
    N3DSRenderer* renderer, DataWin* dataWin, Font* font, N3DSFontState* state) {
    if (dataWin == NULL || font == NULL || state == NULL) return false;
    state->font = font;
    state->spriteFontSprite = NULL;
    state->atlasItem = NULL;

    if (!font->isSpriteFont) {
        if (font->tpagIndex < 0 || (uint32_t)font->tpagIndex >= dataWin->tpag.count) return false;
        return C3D_getAtlasBinding(renderer, font->tpagIndex, NULL, &state->atlasItem);
    }
    if (font->spriteIndex < 0 || (uint32_t)font->spriteIndex >= dataWin->sprt.count) return false;
    state->spriteFontSprite = &dataWin->sprt.sprites[font->spriteIndex];
    return true;
}

static bool C3D_drawGlyph(N3DSRenderer* renderer, DataWin* dataWin,
    const N3DSFontState* state, FontGlyph* glyph, float cursorX, float cursorY,
    const N3DSDrawMapper* mapper, uint32_t rgba) {
    if (dataWin == NULL || state == NULL || glyph == NULL || glyph->sourceWidth == 0u ||
        glyph->sourceHeight == 0u)
        return false;
    Font* font = state->font;
    int32_t tpagIndex = font->tpagIndex;
    int32_t srcX = (int32_t)glyph->sourceX;
    int32_t srcY = (int32_t)glyph->sourceY;
    int32_t srcW = (int32_t)glyph->sourceWidth;
    int32_t srcH = (int32_t)glyph->sourceHeight;
    float left = cursorX + (float)glyph->offset;
    float top = cursorY;

    if (font->isSpriteFont) {
        Sprite* sprite = state->spriteFontSprite;
        ptrdiff_t glyphIndex = glyph - font->glyphs;
        if (sprite == NULL || glyphIndex < 0 || (uint32_t)glyphIndex >= sprite->textureCount)
            return false;
        tpagIndex = sprite->tpagIndices[glyphIndex];
        if (tpagIndex < 0 || (uint32_t)tpagIndex >= dataWin->tpag.count) return false;
        TexturePageItem* tpag = &dataWin->tpag.items[tpagIndex];
        srcX = 0;
        srcY = 0;
        srcW = (int32_t)tpag->sourceWidth;
        srcH = (int32_t)tpag->sourceHeight;
        top = cursorY + (float)tpag->targetY - (float)font->spriteOriginYAdjust;
    }

    N3DSDrawMapper alignedMapper;
    if (!renderer->base.texFilter && mapper->kind == N3DS_MAP_AXIS &&
        fabsf(mapper->axisScaleX) == 1.0f && fabsf(mapper->axisScaleY) == 1.0f) {
        alignedMapper = *mapper;
        C3D_floorSpritePosition(renderer, left * mapper->axisScaleX,
            top * mapper->axisScaleY, &alignedMapper.axisX, &alignedMapper.axisY);
        mapper = &alignedMapper;
    }

    float right = left + (float)srcW;
    float bottom = top + (float)srcH;
    if (state->atlasItem != NULL) {
        C3D_emitAtlas(renderer, state->atlasItem,
            (N3DSAtlasSource){srcX, srcY, srcW, srcH}, mapper,
            (N3DSAtlasDest){left, top, right, bottom}, rgba);
        return true;
    }
    if ((uint32_t)tpagIndex < renderer->tpagBindingCount &&
        renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_ATLAS) {
        C3D_drawAtlas(renderer, tpagIndex, (N3DSAtlasSource){srcX, srcY, srcW, srcH},
            mapper, (N3DSAtlasDest){left, top, right, bottom}, rgba);
        return true;
    }

    C3D_Tex* tex = NULL;
    N3DSUVRect* region = NULL;
    if (!C3D_resolveTextureRegion(renderer, tpagIndex, &tex, &region)) return false;
    if ((uint32_t)tpagIndex < renderer->tpagBindingCount &&
        renderer->tpagBindings[tpagIndex].source == N3DS_TPAG_SOURCE_SURFACE) {
        uint32_t surfaceID = renderer->tpagBindings[tpagIndex].assetIndex;
        if (surfaceID < renderer->surfaceCount) {
            N3DSUVRect contentRegion;
            C3D_getSurfaceContentRegion(&contentRegion, region,
                &dataWin->tpag.items[tpagIndex], &renderer->surfaces[surfaceID]);
            C3D_emitMappedQuadSprite(renderer, tex, &contentRegion, mapper, (float)srcX,
                (float)srcY, (float)(srcX + srcW), (float)(srcY + srcH), left, top, right, bottom,
                rgba, rgba, rgba, rgba);
            return true;
        }
    }
    C3D_emitMappedQuadSprite(renderer, tex, region, mapper, (float)srcX, (float)srcY,
        (float)(srcX + srcW), (float)(srcY + srcH), left, top, right, bottom, rgba, rgba, rgba,
        rgba);
    return true;
}

static void C3D_drawTextInternal(Renderer* base, const char* text, float x, float y,
    float xscale, float yscale, float angleDeg, uint32_t color, float alpha, float lineSeparation) {
    if (text == NULL || alpha <= 0.0f) return;
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    DataWin* dataWin = base->dataWin;
    if (dataWin == NULL) return;

    int32_t fontIndex = base->drawFont;
    if (fontIndex < 0 || (uint32_t)fontIndex >= dataWin->font.count) {
        return;
    }

    Font* font = &dataWin->font.fonts[fontIndex];
    N3DSFontState state;
    if (!C3D_resolveFontState(renderer, dataWin, font, &state)) {
        return;
    }

    int32_t textLen = (int32_t)strlen(text);
    if (textLen == 0) {
        return;
    }

    int32_t lineCount = TextUtils_countLines(text, textLen);
    float lineStride = C3D_textLineStride(font, lineSeparation);
    float valignOffset = 0.0f;
    if (base->drawValign == 1)
        valignOffset = -((float)lineCount * lineStride) * 0.5f;
    else if (base->drawValign == 2)
        valignOffset = -((float)lineCount * lineStride);

    float scaleX = xscale * font->scaleX;
    float scaleY = yscale * font->scaleY;
    N3DSDrawMapper mapper;
    if (fabsf(angleDeg) <= 0.0001f) {
        C3D_mapInitAxis(&mapper, x, y, scaleX, scaleY);
    } else {
        Matrix4f transform;
        Matrix4f_setTransform2D(&transform, x, y, scaleX, scaleY, -angleDeg * (float)M_PI / 180.0f);
        C3D_mapInitAffine(&mapper, &transform);
    }
    uint32_t rgba = C3D_makeColor(color, alpha);

    float cursorY = valignOffset - (float)font->ascenderOffset;
    int32_t lineStart = 0;
    for (int32_t lineIndex = 0; lineIndex < lineCount; ++lineIndex) {
        int32_t lineEnd = lineStart;
        while (lineEnd < textLen && !TextUtils_isNewlineChar(text[lineEnd]))
            lineEnd++;
        int32_t lineLen = lineEnd - lineStart;
        float lineWidth = (base->drawHalign == 1 || base->drawHalign == 2)
            ? TextUtils_measureLineWidth(font, text + lineStart, lineLen) : 0.0f;
        float cursorX = 0.0f;
        if (base->drawHalign == 1)
            cursorX = -lineWidth * 0.5f;
        else if (base->drawHalign == 2)
            cursorX = -lineWidth;

        int32_t pos = 0;
        bool hasCh = pos < lineLen;
        uint16_t ch = hasCh ? TextUtils_decodeUtf8(text + lineStart, lineLen, &pos) : 0u;
        while (hasCh) {
            FontGlyph* glyph = TextUtils_findGlyph(font, ch);
            uint16_t nextCh = 0u;
            bool hasNext = pos < lineLen;
            if (hasNext) nextCh = TextUtils_decodeUtf8(text + lineStart, lineLen, &pos);
            if (glyph != NULL) {
                bool drew = C3D_drawGlyph(
                    renderer, dataWin, &state, glyph, cursorX, cursorY, &mapper, rgba);
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

static void C3D_drawText(Renderer* base, const char* text, float x, float y, float xscale,
    float yscale, float angleDeg, float lineSeparation) {
    assert(base != NULL);
    C3D_drawTextInternal(base, text, x, y, xscale, yscale, angleDeg, base->drawColor,
        base->drawAlpha, lineSeparation);
}

static void C3D_drawTextColor(Renderer* base, const char* text, float x, float y,
    float xscale, float yscale, float angleDeg, int32_t c1, MAYBE_UNUSED int32_t c2,
    MAYBE_UNUSED int32_t c3, MAYBE_UNUSED int32_t c4, float alpha, float lineSeparation) {
    assert(base != NULL);
    C3D_drawTextInternal(
        base, text, x, y, xscale, yscale, angleDeg, (uint32_t)c1, alpha, lineSeparation);
}

static void C3D_flush(MAYBE_UNUSED Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    n3ds_c3dFlush(&renderer->drawContext);
}

static void C3D_getSurfaceRegion(const N3DSSurface* surface, N3DSUVRect* outRegion) {
    if (surface == NULL || outRegion == NULL) return;
    outRegion->left = 0.0f;
    outRegion->right =
        surface->texWidth > 0 ? (float)surface->width / (float)surface->texWidth : 1.0f;
    if (surface->topAligned) {
        outRegion->top =
            surface->texHeight > 0 ? (float)surface->height / (float)surface->texHeight : 1.0f;
        outRegion->bottom = 0.0f;
    } else {
        outRegion->top = 1.0f;
        outRegion->bottom = surface->texHeight > 0
                                ? 1.0f - (float)surface->height / (float)surface->texHeight
                                : 0.0f;
    }
    outRegion->width = (uint16_t)surface->width;
    outRegion->height = (uint16_t)surface->height;
}

static int32_t C3D_surfaceLogicalWidth(const N3DSSurface* surface) {
    if (surface == NULL) return 0;
    return surface->logicalWidth > 0 ? surface->logicalWidth : surface->width;
}

static int32_t C3D_surfaceLogicalHeight(const N3DSSurface* surface) {
    if (surface == NULL) return 0;
    return surface->logicalHeight > 0 ? surface->logicalHeight : surface->height;
}

static float C3D_surfacePhysicalScaleX(const N3DSSurface* surface) {
    int32_t logicalWidth = C3D_surfaceLogicalWidth(surface);
    return surface != NULL && logicalWidth > 0 ? (float)surface->width / (float)logicalWidth : 1.0f;
}

static float C3D_surfacePhysicalScaleY(const N3DSSurface* surface) {
    int32_t logicalHeight = C3D_surfaceLogicalHeight(surface);
    return surface != NULL && logicalHeight > 0 ? (float)surface->height / (float)logicalHeight
                                                : 1.0f;
}

static bool C3D_surfaceIsScaled(const N3DSSurface* surface) {
    return surface != NULL &&
        (C3D_surfaceLogicalWidth(surface) != surface->width ||
            C3D_surfaceLogicalHeight(surface) != surface->height);
}

static bool C3D_getSnapshotRect(N3DSRenderer* renderer,
    const N3DSSurface* source, bool useActiveProjection, int32_t x, int32_t y, int32_t w, int32_t h,
    N3DSSurfaceSnapshotRect* outRect) {
    if (source == NULL || outRect == NULL || w <= 0 || h <= 0) return false;

    int32_t srcRectX = x;
    int32_t srcRectY = y;
    int32_t srcRectRight = x + w;
    int32_t srcRectBottom = y + h;
    if (useActiveProjection &&
        (renderer->screenTransformActive || renderer->hostViewProjectionActive)) {
        float x0, y0, x1, y1, x2, y2, x3, y3;
        Matrix4f_transformPoint(&renderer->screenTransform, (float)x, (float)y, &x0, &y0);
        Matrix4f_transformPoint(&renderer->screenTransform, (float)(x + w), (float)y, &x1, &y1);
        Matrix4f_transformPoint(
            &renderer->screenTransform, (float)(x + w), (float)(y + h), &x2, &y2);
        Matrix4f_transformPoint(&renderer->screenTransform, (float)x, (float)(y + h), &x3, &y3);
        srcRectX = (int32_t)floorf(fminf(fminf(x0, x1), fminf(x2, x3)));
        srcRectY = (int32_t)floorf(fminf(fminf(y0, y1), fminf(y2, y3)));
        srcRectRight = (int32_t)ceilf(fmaxf(fmaxf(x0, x1), fmaxf(x2, x3)));
        srcRectBottom = (int32_t)ceilf(fmaxf(fmaxf(y0, y1), fmaxf(y2, y3)));
    } else if (useActiveProjection && renderer->viewCullActive && renderer->currentViewW != 0 &&
        renderer->currentViewH != 0) {
        float scaleX = (float)renderer->currentLogicalPortW / (float)renderer->currentViewW;
        float scaleY = (float)renderer->currentLogicalPortH / (float)renderer->currentViewH;
        float srcXf = ((float)x - (float)renderer->currentViewX) * scaleX +
            (float)renderer->currentLogicalPortX;
        float srcYf = ((float)y - (float)renderer->currentViewY) * scaleY +
            (float)renderer->currentLogicalPortY;
        float srcWf = (float)w * scaleX;
        float srcHf = (float)h * scaleY;
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
    if (snapshotW <= 0 || snapshotH <= 0) return false;

    int32_t copySrcX = srcRectX;
    int32_t copySrcY = srcRectY;
    int32_t copySrcRight = srcRectRight;
    int32_t copySrcBottom = srcRectBottom;
    int32_t sourceW = C3D_surfaceLogicalWidth(source);
    int32_t sourceH = C3D_surfaceLogicalHeight(source);
    if (copySrcX < 0) copySrcX = 0;
    if (copySrcY < 0) copySrcY = 0;
    if (copySrcRight > sourceW) copySrcRight = sourceW;
    if (copySrcBottom > sourceH) copySrcBottom = sourceH;
    int32_t copyW = copySrcRight - copySrcX;
    int32_t copyH = copySrcBottom - copySrcY;
    if (copyW <= 0 || copyH <= 0) return false;

    outRect->srcX = copySrcX;
    outRect->srcY = copySrcY;
    outRect->copyW = copyW;
    outRect->copyH = copyH;
    outRect->destX = copySrcX - srcRectX;
    outRect->destY = copySrcY - srcRectY;
    outRect->snapshotW = snapshotW;
    outRect->snapshotH = snapshotH;
    return true;
}

static bool C3D_appSurfaceTooLarge(int32_t width, int32_t height) {
    return width > N3DS_MAX_APPLICATION_SURFACE_WIDTH ||
        height > N3DS_MAX_APPLICATION_SURFACE_HEIGHT;
}

static void C3D_getAppSurfaceSize(
    N3DSRenderer* renderer, int32_t logicalW, int32_t logicalH, int32_t* outW, int32_t* outH) {
    if (renderer->halfResApplicationSurface) {
        logicalW = (logicalW + 1) / 2;
        logicalH = (logicalH + 1) / 2;
    }
    if (outW != NULL) *outW = logicalW;
    if (outH != NULL) *outH = logicalH;
}

static void C3D_bindTopScreenTarget(N3DSRenderer* renderer) {
    C3D_bindTarget(renderer, RENDER_TARGET_HOST_FRAMEBUFFER, renderer->gameOnBottomScreen);
    if (renderer->gameOnBottomScreen) renderer->bottomScreenUsedThisFrame = true;
    n3ds_c3dUpdateProjection(&renderer->drawContext,
        renderer->gameOnBottomScreen ? N3DS_BOTTOM_WIDTH : N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT);
    renderer->screenTransformActive = false;
    renderer->customCameraTransformActive = false;
    renderer->currentLogicalPortX = 0;
    renderer->currentLogicalPortY = 0;
    renderer->currentLogicalPortW = renderer->gameOnBottomScreen ? N3DS_BOTTOM_WIDTH : N3DS_TOP_WIDTH;
    renderer->currentLogicalPortH = N3DS_TOP_HEIGHT;
}

static void C3D_updateHostProjection(N3DSRenderer* renderer) {
    renderer->hostViewProjectionActive = false;
    n3ds_c3dUpdateProjection(&renderer->drawContext,
        renderer->currentHostTargetIsBottomScreen ? N3DS_BOTTOM_WIDTH : N3DS_TOP_WIDTH,
        renderer->currentHostTargetIsBottomScreen ? N3DS_BOTTOM_HEIGHT : N3DS_TOP_HEIGHT);
}

static void C3D_blitAppSurface(N3DSRenderer* renderer) {
    int32_t appSurfaceId = renderer->base.runner != NULL
        ? renderer->base.runner->applicationSurfaceId
        : renderer->applicationSurfaceId;
    if (appSurfaceId < 0 || (uint32_t)appSurfaceId >= renderer->surfaceCount) return;
    N3DSSurface* surface = &renderer->surfaces[appSurfaceId];
    if (!surface->used || renderer->frameGameW <= 0 || renderer->frameGameH <= 0) return;

    N3DSRenderState savedState = renderer->renderState;
    C3D_beginSurfaceCopyState(renderer);

    if (renderer->currentTargetSurfaceId == appSurfaceId) {
        C3D_safeFrameSplit(renderer);
        C3D_markSurfaceReady(renderer, appSurfaceId);
    }
    C3D_bindTopScreenTarget(renderer);
    if (!C3D_finishSurfaceUpload(renderer, appSurfaceId)) {
        C3D_restoreBlendState(renderer, &savedState);
        return;
    }

    float left = 0.0f;
    float top = 0.0f;
    float right = (float)N3DS_TOP_WIDTH;
    float bottom = (float)N3DS_TOP_HEIGHT;
    if (renderer->gameOnBottomScreen) {
        float scale = fmaxf((float)N3DS_BOTTOM_WIDTH / renderer->frameGameW,
            (float)N3DS_BOTTOM_HEIGHT / renderer->frameGameH);
        float w = renderer->frameGameW * scale;
        float h = renderer->frameGameH * scale;
        left = (N3DS_BOTTOM_WIDTH - w) * 0.5f;
        top = (N3DS_BOTTOM_HEIGHT - h) * 0.5f;
        right = left + w;
        bottom = top + h;
    } else {
        C3D_letterboxRect(renderer->frameGameW, renderer->frameGameH, N3DS_TOP_WIDTH,
            N3DS_TOP_HEIGHT, &left, &top, &right, &bottom);
    }
    float u1 = surface->texWidth > 0 ? (float)surface->width / (float)surface->texWidth : 1.0f;
    float v0 = surface->texHeight > 0
        ? (float)(surface->texHeight - surface->height) / (float)surface->texHeight
        : 0.0f;
    float v1 = 1.0f;
    bool downsampling = (float)surface->width > right - left ||
        (float)surface->height > bottom - top;
    bool presentFilter = renderer->base.texFilter || downsampling;
    n3ds_c3dSetTexFilter(&renderer->drawContext, presentFilter);
    const N3DSVertex vertices[4] = {
        {{left, top}, {0.0f, v1}},
        {{right, top}, {u1, v1}},
        {{left, bottom}, {0.0f, v0}},
        {{right, bottom}, {u1, v0}},
    };
    n3ds_c3dQuad(&renderer->drawContext, &surface->tex, vertices, 0xFFFFFFFFu);
    n3ds_c3dFlush(&renderer->drawContext);
    n3ds_c3dSetTexFilter(&renderer->drawContext, renderer->base.texFilter);
    C3D_restoreBlendState(renderer, &savedState);
}

static bool C3D_initSurfaceTexture(N3DSSurface* surface, bool preferVram,
    bool allowLinearFallback) {
    if (preferVram &&
        C3D_TexInitVRAM(&surface->tex, surface->texWidth, surface->texHeight,
            surface->texFormat)) return true;
    return allowLinearFallback &&
        C3D_TexInit(&surface->tex, surface->texWidth, surface->texHeight, surface->texFormat);
}

static bool C3D_allocSurfaceTexture(N3DSRenderer* renderer, N3DSSurface* surface,
    bool preferVram, bool allowLinearFallback) {
    if (C3D_initSurfaceTexture(surface, preferVram, allowLinearFallback)) return true;
    if (!allowLinearFallback) return false;

    bool reclaimed = false;
    ptrdiff_t pendingCount = arrlen(renderer->pendingSurfaceFrees);
    ptrdiff_t retainedCount = 0;
    for (ptrdiff_t i = 0; i < pendingCount; ++i) {
        int32_t surfaceID = renderer->pendingSurfaceFrees[i];
        if (surfaceID == renderer->currentTargetSurfaceId) {
            renderer->pendingSurfaceFrees[retainedCount++] = surfaceID;
            continue;
        }
        C3D_destroySurfaceSlotNow(renderer, surfaceID);
        reclaimed = true;
    }
    if (retainedCount == 0) {
        arrfree(renderer->pendingSurfaceFrees);
        renderer->pendingSurfaceFrees = NULL;
    } else {
        arrsetlen(renderer->pendingSurfaceFrees, retainedCount);
    }
    if (reclaimed &&
        C3D_initSurfaceTexture(surface, preferVram, allowLinearFallback)) return true;

    while (arrlen(renderer->freeSurfaceSpriteSurfaces) > 0) {
        int32_t surfaceID =
            renderer->freeSurfaceSpriteSurfaces[arrlen(renderer->freeSurfaceSpriteSurfaces) - 1];
        arrsetlen(
            renderer->freeSurfaceSpriteSurfaces, arrlen(renderer->freeSurfaceSpriteSurfaces) - 1);
        if (C3D_surfaceIdIsLive(renderer, surfaceID)) {
            C3D_destroySurfaceSlotNow(renderer, surfaceID);
        }
        if (C3D_initSurfaceTexture(surface, preferVram, allowLinearFallback)) return true;
    }

    while (C3D_evictOldestAtlasPage(renderer, UINT32_MAX)) {
        if (C3D_initSurfaceTexture(surface, preferVram, allowLinearFallback)) return true;
    }
    return false;
}

static int32_t C3D_createSurfaceStorageEx(Renderer* base, int32_t width, int32_t height,
    int32_t logicalWidth, int32_t logicalHeight, bool renderTarget, GPU_TEXCOLOR texFormat,
    bool preferVram, bool allowLinearFallback) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (width <= 0 || height <= 0 || width > N3DS_MAX_TEXTURE_SIZE ||
        height > N3DS_MAX_TEXTURE_SIZE) return -1;
    uint32_t slot = UINT32_MAX;
    for (uint32_t i = 0; i < renderer->surfaceCount; ++i) {
        if (!renderer->surfaces[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == UINT32_MAX) {
        slot = renderer->surfaceCount;
        n3ds_c3dFlush(&renderer->drawContext);
        renderer->surfaces = safeRealloc(renderer->surfaces, sizeof(N3DSSurface) * (slot + 1u));
        renderer->surfaceCount++;
    }
    memset(&renderer->surfaces[slot], 0, sizeof(N3DSSurface));
    N3DSSurface* surface = &renderer->surfaces[slot];
    surface->texWidth = n3ds_nextPow2(width);
    surface->texHeight = n3ds_nextPow2(height);
    surface->texFormat = texFormat;
    surface->transferFormat = C3D_transferFormatForTex(texFormat);
    if (!C3D_allocSurfaceTexture(renderer, surface, preferVram,
        allowLinearFallback)) {
        memset(surface, 0, sizeof(*surface));
        return -1;
    }
    if (renderTarget)
        surface->target = C3D_RenderTargetCreateFromTex(&surface->tex, GPU_TEXFACE_2D, 0, -1);
    if (renderTarget && surface->target == NULL) {
        C3D_TexDelete(&surface->tex);
        memset(surface, 0, sizeof(*surface));
        return -1;
    }
    C3D_setTextureFiltering(&surface->tex, base->texFilter);
    C3D_TexSetWrap(&surface->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    surface->used = true;
    surface->samplingReady = true;
    surface->generation = ++renderer->nextSurfaceGeneration;
    if (surface->generation == 0u) surface->generation = ++renderer->nextSurfaceGeneration;
    surface->width = width;
    surface->height = height;
    surface->logicalWidth = logicalWidth > 0 ? logicalWidth : width;
    surface->logicalHeight = logicalHeight > 0 ? logicalHeight : height;
    return (int32_t)slot;
}

static int32_t C3D_createSurfaceStorage(
    Renderer* base, int32_t width, int32_t height, bool renderTarget) {
    return C3D_createSurfaceStorageEx(
        base, width, height, width, height, renderTarget, N3DS_USER_SURFACE_FORMAT, true, true);
}

static int32_t C3D_createAppSurface(
    Renderer* base, int32_t width, int32_t height) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    int32_t physicalW;
    int32_t physicalH;
    C3D_getAppSurfaceSize(renderer, width, height, &physicalW, &physicalH);
    int32_t surfaceID = C3D_createSurfaceStorageEx(
        base, physicalW, physicalH, width, height, true, GPU_RGBA8, true, false);
    if (surfaceID >= 0) {
        C3D_setTextureFiltering(&renderer->surfaces[surfaceID].tex, base->texFilter);
    }
    return surfaceID;
}

static bool C3D_resizeAppSurface(
    N3DSRenderer* renderer, int32_t surfaceID, int32_t logicalW, int32_t logicalH) {
    if (!C3D_surfaceIdIsLive(renderer, surfaceID) || logicalW <= 0 || logicalH <= 0)
        return false;
    N3DSSurface* surface = &renderer->surfaces[surfaceID];
    int32_t physicalW;
    int32_t physicalH;
    C3D_getAppSurfaceSize(renderer, logicalW, logicalH, &physicalW, &physicalH);
    if (surface->target == NULL || surface->texFormat != GPU_RGBA8 ||
        surface->texWidth < n3ds_nextPow2(physicalW) ||
        surface->texHeight < n3ds_nextPow2(physicalH))
        return false;
    if (renderer->currentTargetSurfaceId == surfaceID) n3ds_c3dFlush(&renderer->drawContext);
    surface->width = physicalW;
    surface->height = physicalH;
    surface->logicalWidth = logicalW;
    surface->logicalHeight = logicalH;
    C3D_setTextureFiltering(&surface->tex, renderer->base.texFilter);
    surface->samplingReady = renderer->currentTargetSurfaceId != surfaceID;
    surface->generation = ++renderer->nextSurfaceGeneration;
    if (surface->generation == 0u) surface->generation = ++renderer->nextSurfaceGeneration;
    return true;
}

static int32_t C3D_createSurface(Renderer* base, int32_t width, int32_t height) {
    assert(base != NULL);
    return C3D_createSurfaceStorage(base, width, height, true);
}

static bool C3D_surfaceIdIsLive(const N3DSRenderer* renderer, int32_t surfaceID) {
    return surfaceID >= 0 && (uint32_t)surfaceID < renderer->surfaceCount &&
        renderer->surfaces[surfaceID].used && !renderer->surfaces[surfaceID].pendingFree;
}

static bool C3D_surfaceBindingMatches(
    const N3DSRenderer* renderer, const N3DSTpagBinding* binding) {
    if (binding == NULL || binding->source != N3DS_TPAG_SOURCE_SURFACE) return false;
    int32_t surfaceID = (int32_t)binding->assetIndex;
    return C3D_surfaceIdIsLive(renderer, surfaceID) &&
        renderer->surfaces[surfaceID].generation == binding->surfaceGeneration;
}

static void C3D_ensureTpagBindingCount(N3DSRenderer* renderer, uint32_t count) {
    if (count <= renderer->tpagBindingCount) return;
    renderer->tpagBindings =
        safeRealloc(renderer->tpagBindings, (size_t)count * sizeof(N3DSTpagBinding));
    for (uint32_t i = renderer->tpagBindingCount; i < count; ++i) {
        memset(&renderer->tpagBindings[i], 0, sizeof(renderer->tpagBindings[i]));
        renderer->tpagBindings[i].source = N3DS_TPAG_SOURCE_NONE;
        renderer->tpagBindings[i].atlasItemIndex = UINT32_MAX;
        renderer->tpagBindings[i].resizeDivisor = 1u;
    }
    renderer->tpagBindingCount = count;
}

static void C3D_recycleRuntimeTpag(N3DSRenderer* renderer, uint32_t tpagIndex) {
    if (tpagIndex < renderer->runtimeTpagStart) return;
    for (ptrdiff_t i = 0; i < arrlen(renderer->freeRuntimeTpagIndices); ++i) {
        if (renderer->freeRuntimeTpagIndices[i] == tpagIndex) return;
    }
    arrput(renderer->freeRuntimeTpagIndices, tpagIndex);
}

static uint32_t C3D_allocRuntimeTpag(N3DSRenderer* renderer, DataWin* dw) {
    if (dw == NULL) return UINT32_MAX;
    while (arrlen(renderer->freeRuntimeTpagIndices) > 0) {
        uint32_t tpagIndex =
            renderer->freeRuntimeTpagIndices[arrlen(renderer->freeRuntimeTpagIndices) - 1];
        arrsetlen(renderer->freeRuntimeTpagIndices, arrlen(renderer->freeRuntimeTpagIndices) - 1);
        if (tpagIndex >= renderer->runtimeTpagStart && tpagIndex < dw->tpag.count) {
            C3D_ensureTpagBindingCount(renderer, tpagIndex + 1u);
            return tpagIndex;
        }
    }
    uint32_t tpagIndex = dw->tpag.count++;
    dw->tpag.items = safeRealloc(dw->tpag.items, (size_t)dw->tpag.count * sizeof(TexturePageItem));
    C3D_ensureTpagBindingCount(renderer, dw->tpag.count);
    return tpagIndex;
}

static bool C3D_recycleSpriteSurface(N3DSRenderer* renderer, int32_t surfaceID) {
    if (!C3D_surfaceIdIsLive(renderer, surfaceID) ||
        renderer->surfaces[surfaceID].target == NULL)
        return false;
    for (ptrdiff_t i = 0; i < arrlen(renderer->freeSurfaceSpriteSurfaces); ++i) {
        if (renderer->freeSurfaceSpriteSurfaces[i] == surfaceID) return true;
    }
    if ((uint32_t)arrlen(renderer->freeSurfaceSpriteSurfaces) >= N3DS_SURFACE_SPRITE_POOL_MAX)
        return false;
    arrput(renderer->freeSurfaceSpriteSurfaces, surfaceID);
    return true;
}

static int32_t C3D_allocSpriteSurface(
    Renderer* base, int32_t width, int32_t height, int32_t logicalWidth, int32_t logicalHeight) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    int32_t wantedTexW = n3ds_nextPow2(width);
    int32_t wantedTexH = n3ds_nextPow2(height);
    for (ptrdiff_t i = arrlen(renderer->freeSurfaceSpriteSurfaces) - 1; i >= 0; --i) {
        int32_t surfaceID = renderer->freeSurfaceSpriteSurfaces[i];
        if (!C3D_surfaceIdIsLive(renderer, surfaceID) ||
            renderer->surfaces[surfaceID].target == NULL) {
            arrdel(renderer->freeSurfaceSpriteSurfaces, i);
            continue;
        }
        N3DSSurface* surface = &renderer->surfaces[surfaceID];
        if (surface->texWidth < wantedTexW || surface->texHeight < wantedTexH) {
            arrdel(renderer->freeSurfaceSpriteSurfaces, i);
            C3D_queueSurfaceFree(renderer, surfaceID);
            continue;
        }
        arrdel(renderer->freeSurfaceSpriteSurfaces, i);
        C3D_safeFrameSplit(renderer);
        surface->width = width;
        surface->height = height;
        surface->logicalWidth = logicalWidth > 0 ? logicalWidth : width;
        surface->logicalHeight = logicalHeight > 0 ? logicalHeight : height;
        surface->samplingReady = true;
        surface->generation = ++renderer->nextSurfaceGeneration;
        if (surface->generation == 0u) surface->generation = ++renderer->nextSurfaceGeneration;
        return surfaceID;
    }
    int32_t surfaceID = C3D_createSurfaceStorageEx(base, width, height, logicalWidth,
        logicalHeight, true, N3DS_USER_SURFACE_FORMAT, true, true);
    if (surfaceID >= 0) return surfaceID;
    while (arrlen(renderer->freeSurfaceSpriteSurfaces) > 0) {
        int32_t pooledSurfaceID =
            renderer->freeSurfaceSpriteSurfaces[arrlen(renderer->freeSurfaceSpriteSurfaces) - 1];
        arrsetlen(
            renderer->freeSurfaceSpriteSurfaces, arrlen(renderer->freeSurfaceSpriteSurfaces) - 1);
        C3D_queueSurfaceFree(renderer, pooledSurfaceID);
    }
    return C3D_createSurfaceStorageEx(base, width, height, logicalWidth, logicalHeight,
        true, N3DS_USER_SURFACE_FORMAT, true, true);
}

static void C3D_destroySurfaceSlotNow(N3DSRenderer* renderer, int32_t surfaceID) {
    if (surfaceID < 0 || (uint32_t)surfaceID >= renderer->surfaceCount ||
        !renderer->surfaces[surfaceID].used)
        return;
    N3DSSurface* surface = &renderer->surfaces[surfaceID];
    bool resumeFrame = false;
    if (surface->target != NULL)
        resumeFrame = C3D_suspendDrawFrame(renderer);
    else
        C3D_finishAtlasDraws(renderer);
    if (renderer->currentTargetSurfaceId == surfaceID) renderer->currentTargetSurfaceId = INT32_MIN;
    if (renderer->hostCaptureSurfaceId == surfaceID) renderer->hostCaptureSurfaceId = -1;
    if (renderer->bottomSurfaceId == surfaceID) renderer->bottomSurfaceId = -1;
    n3ds_c3dInvalidateTexture(&renderer->drawContext, &surface->tex);
    if (surface->target != NULL) C3D_RenderTargetDelete(surface->target);
    C3D_TexDelete(&surface->tex);
    memset(surface, 0, sizeof(*surface));
    if (resumeFrame) C3D_resumeDrawFrame(renderer);
}

static void C3D_destroySurfaceSlot(N3DSRenderer* renderer, int32_t surfaceID) {
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) return;
    C3D_destroySurfaceSlotNow(renderer, surfaceID);
}

static void C3D_queueSurfaceFree(N3DSRenderer* renderer, int32_t surfaceID) {
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) return;
    renderer->surfaces[surfaceID].pendingFree = true;
    for (ptrdiff_t i = 0; i < arrlen(renderer->pendingSurfaceFrees); ++i) {
        if (renderer->pendingSurfaceFrees[i] == surfaceID) return;
    }
    arrput(renderer->pendingSurfaceFrees, surfaceID);
}

void C3D_freePendingSurfaces(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    for (ptrdiff_t i = 0; i < arrlen(renderer->pendingSurfaceFrees); ++i) {
        C3D_destroySurfaceSlotNow(renderer, renderer->pendingSurfaceFrees[i]);
    }
    arrfree(renderer->pendingSurfaceFrees);
    renderer->pendingSurfaceFrees = NULL;
}

static bool C3D_finishSurfaceUpload(N3DSRenderer* renderer, int32_t surfaceID) {
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) return false;
    N3DSSurface* surface = &renderer->surfaces[surfaceID];
    if (surface->target == NULL) return true;
    if (renderer->currentTargetSurfaceId == surfaceID) return false;
    if (surface->samplingReady) return true;
    C3D_safeFrameSplit(renderer);
    C3D_TexFlush(&surface->tex);
    surface->samplingReady = true;
    return true;
}

static int32_t C3D_surfaceStorageYOffset(const N3DSSurface* surface) {
    if (surface == NULL || surface->texHeight <= surface->height) return 0;
    return surface->texHeight - surface->height;
}

static int32_t C3D_resolveAppSurface(Renderer* base) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->runner != NULL && !base->runner->usingAppSurface) return -1;
    int32_t surfaceID = base->runner != NULL
        ? base->runner->applicationSurfaceId
        : renderer->applicationSurfaceId;
    if (C3D_surfaceIdIsLive(renderer, surfaceID)) return surfaceID;
    return -1;
}

static int32_t C3D_resolveSampleSurface(
    Renderer* base, int32_t surfaceID, bool snapshotApplicationSurface, bool snapshotActiveTarget) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) return -1;
    bool applicationSurface = surfaceID == C3D_resolveAppSurface(base);
    if ((applicationSurface && snapshotApplicationSurface) ||
        (snapshotActiveTarget && renderer->currentTargetSurfaceId == surfaceID)) {
        int32_t snapshotId = C3D_snapshotSurface(base, surfaceID);
        if (C3D_surfaceIdIsLive(renderer, snapshotId) && snapshotId != surfaceID)
            surfaceID = snapshotId;
    }
    return surfaceID;
}

static bool C3D_transferSurface(
    N3DSRenderer* renderer, int32_t sourceID, int32_t destID) {
    if (!C3D_surfaceIdIsLive(renderer, sourceID) ||
        !C3D_surfaceIdIsLive(renderer, destID))
        return false;
    N3DSSurface* source = &renderer->surfaces[sourceID];
    N3DSSurface* dest = &renderer->surfaces[destID];
    if (source->target == NULL || dest->target == NULL ||
        source->target->frameBuf.colorBuf == NULL || dest->target->frameBuf.colorBuf == NULL)
        return false;
    n3ds_c3dFlush(&renderer->drawContext);
    if (renderer->currentTargetSurfaceId == sourceID) {
        n3ds_c3dFrameSplit(&renderer->drawContext);
    }
    uint16_t sourceW = source->target->frameBuf.width > 0
        ? source->target->frameBuf.width
        : (uint16_t)source->texWidth;
    uint16_t sourceH = source->target->frameBuf.height > 0
        ? source->target->frameBuf.height
        : (uint16_t)source->texHeight;
    uint16_t destW =
        dest->target->frameBuf.width > 0 ? dest->target->frameBuf.width : (uint16_t)dest->texWidth;
    uint16_t destH = dest->target->frameBuf.height > 0
        ? dest->target->frameBuf.height
        : (uint16_t)dest->texHeight;
    C3D_SyncDisplayTransfer((u32*)source->target->frameBuf.colorBuf,
        GX_BUFFER_DIM(sourceW, sourceH), (u32*)dest->target->frameBuf.colorBuf,
        GX_BUFFER_DIM(destW, destH),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
            GX_TRANSFER_IN_FORMAT(source->transferFormat) |
            GX_TRANSFER_OUT_FORMAT(dest->transferFormat) |
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    C3D_TexFlush(&dest->tex);
    dest->samplingReady = true;
    return true;
}

static int32_t C3D_snapshotSurface(Renderer* base, int32_t sourceID) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (!C3D_surfaceIdIsLive(renderer, sourceID)) return -1;
    N3DSSurface* source = &renderer->surfaces[sourceID];
    int32_t sourceW = source->width;
    int32_t sourceH = source->height;
    int32_t sourceLogicalW = C3D_surfaceLogicalWidth(source);
    int32_t sourceLogicalH = C3D_surfaceLogicalHeight(source);
    GPU_TEXCOLOR sourceFormat = source->texFormat;
    if (C3D_surfaceIdIsLive(renderer, renderer->hostCaptureSurfaceId)) {
        N3DSSurface* capture = &renderer->surfaces[renderer->hostCaptureSurfaceId];
        if (capture->width != sourceW || capture->height != sourceH ||
            C3D_surfaceLogicalWidth(capture) != sourceLogicalW ||
            C3D_surfaceLogicalHeight(capture) != sourceLogicalH ||
            capture->texFormat != sourceFormat || capture->target == NULL) {
            C3D_destroySurfaceSlot(renderer, renderer->hostCaptureSurfaceId);
        }
    }
    if (!C3D_surfaceIdIsLive(renderer, renderer->hostCaptureSurfaceId)) {
        renderer->hostCaptureSurfaceId = C3D_createSurfaceStorageEx(base, sourceW,
            sourceH, sourceLogicalW, sourceLogicalH, true, sourceFormat, true, true);
        if (!C3D_surfaceIdIsLive(renderer, renderer->hostCaptureSurfaceId)) {
            renderer->hostCaptureSurfaceId =
                C3D_createSurfaceStorageEx(base, sourceW, sourceH,
                    sourceLogicalW, sourceLogicalH, true, GPU_RGBA8, true, true);
        }
    }
    if (!C3D_surfaceIdIsLive(renderer, renderer->hostCaptureSurfaceId)) return -1;
    if (renderer->hostCaptureSurfaceId == sourceID) return sourceID;
    if (!C3D_surfaceIdIsLive(renderer, sourceID)) return -1;
    source = &renderer->surfaces[sourceID];
    N3DSSurface* capture = &renderer->surfaces[renderer->hostCaptureSurfaceId];
    capture->logicalWidth = sourceLogicalW;
    capture->logicalHeight = sourceLogicalH;
    capture->topAligned = source->topAligned;
    if (C3D_transferSurface(renderer, sourceID, renderer->hostCaptureSurfaceId))
        return renderer->hostCaptureSurfaceId;

    N3DSRenderStateSnapshot savedState;
    C3D_saveRenderState(renderer, &savedState);
    C3D_beginSurfaceCopyState(renderer);

    C3D_bindTarget(renderer, renderer->hostCaptureSurfaceId, false);
    n3ds_c3dSetSurfaceProj(
        &renderer->drawContext, (float)source->width, (float)source->height);
    renderer->screenTransformActive = false;
    C3D_RenderTargetClear(renderer->surfaces[renderer->hostCaptureSurfaceId].target, C3D_CLEAR_ALL,
        C3D_makeTextureClearColor(
            0x000000u, 0.0f, renderer->surfaces[renderer->hostCaptureSurfaceId].texFormat),
        0);
    N3DSUVRect region;
    C3D_getSurfaceRegion(source, &region);
    const N3DSVertex vertices[4] = {
        {{0.0f, 0.0f}, {region.left, region.top}},
        {{(float)source->width, 0.0f}, {region.right, region.top}},
        {{0.0f, (float)source->height}, {region.left, region.bottom}},
        {{(float)source->width, (float)source->height}, {region.right, region.bottom}},
    };
    n3ds_c3dQuad(&renderer->drawContext, &source->tex, vertices, 0xFFFFFFFFu);
    n3ds_c3dFlush(&renderer->drawContext);

    C3D_restoreRenderState(renderer, &savedState, renderer->hostCaptureSurfaceId);
    return renderer->hostCaptureSurfaceId;
}

static bool C3D_copySurfaceRect(Renderer* base, int32_t sourceID, int32_t destID,
    int32_t srcX, int32_t srcY, int32_t copyW, int32_t copyH, float destX, float destY, float destW,
    float destH) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (!C3D_surfaceIdIsLive(renderer, sourceID) ||
        !C3D_surfaceIdIsLive(renderer, destID))
        return false;
    N3DSSurface* source = &renderer->surfaces[sourceID];
    N3DSSurface* dest = &renderer->surfaces[destID];
    if (dest->target == NULL) {
        return false;
    }
    if (renderer->currentTargetSurfaceId == sourceID) {
        C3D_safeFrameSplit(renderer);
        C3D_markSurfaceReady(renderer, sourceID);
    } else if (!C3D_finishSurfaceUpload(renderer, sourceID)) {
        return false;
    }

    N3DSRenderStateSnapshot savedState;
    C3D_saveRenderState(renderer, &savedState);
    C3D_beginSurfaceCopyState(renderer);

    C3D_bindTarget(renderer, destID, false);
    n3ds_c3dSetSurfaceProj(
        &renderer->drawContext, (float)dest->width, (float)dest->height);
    renderer->screenTransformActive = false;
    renderer->viewCullActive = false;
    float destScaleX = C3D_surfacePhysicalScaleX(dest);
    float destScaleY = C3D_surfacePhysicalScaleY(dest);
    float dstLeft = destX * destScaleX;
    float dstTop = destY * destScaleY;
    float dstRight = (destX + destW) * destScaleX;
    float dstBottom = (destY + destH) * destScaleY;
    bool fullOverwrite = dstLeft <= 0.0f && dstTop <= 0.0f && dstRight >= (float)dest->width &&
        dstBottom >= (float)dest->height;
    if (!fullOverwrite)
        C3D_RenderTargetClear(dest->target, C3D_CLEAR_ALL,
            C3D_makeTextureClearColor(0x000000u, 0.0f, dest->texFormat), 0);
    if (copyW > 0 && copyH > 0) {
        N3DSUVRect region;
        Matrix4f transform;
        N3DSDrawMapper mapper;
        C3D_getSurfaceRegion(source, &region);
        Matrix4f_setTransform2D(&transform, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
        C3D_mapInitAffine(&mapper, &transform);
        float srcScaleX = C3D_surfacePhysicalScaleX(source);
        float srcScaleY = C3D_surfacePhysicalScaleY(source);
        C3D_emitMappedQuad(renderer, &source->tex, &region, &mapper,
            (float)srcX * srcScaleX, (float)srcY * srcScaleY, (float)(srcX + copyW) * srcScaleX,
            (float)(srcY + copyH) * srcScaleY, dstLeft, dstTop, dstRight, dstBottom, 0xFFFFFFFFu,
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
    }
    n3ds_c3dFlush(&renderer->drawContext);
    C3D_safeFrameSplit(renderer);
    C3D_markSurfaceReady(renderer, destID);

    C3D_restoreRenderState(renderer, &savedState, destID);
    return true;
}

static bool C3D_surfaceExists(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    return C3D_surfaceIdIsLive(renderer, surfaceID);
}

static int32_t C3D_resolveTargetSurfaceID(Renderer* base, int32_t surfaceID) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    int32_t currentAppSurfaceId = base->runner != NULL
        ? base->runner->applicationSurfaceId
        : renderer->applicationSurfaceId;
    bool appSurfaceTarget =
        surfaceID == APPLICATION_SURFACE_ID || (surfaceID >= 0 && surfaceID == currentAppSurfaceId);
    if (appSurfaceTarget && base->runner != NULL && base->runner->usingAppSurface) {
        return renderer->applicationSurfaceId;
    }
    return surfaceID;
}

static bool C3D_setTargetProjection(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    int32_t targetSurfaceID = C3D_resolveTargetSurfaceID(base, surfaceID);
    if (targetSurfaceID == RENDER_TARGET_HOST_FRAMEBUFFER) {
        C3D_bindTopScreenTarget(renderer);
        renderer->viewCullActive = false;
        return true;
    }
    if (!C3D_surfaceExists(base, surfaceID)) return false;
    surfaceID = targetSurfaceID;
    if (surfaceID >= 0 && renderer->surfaces[surfaceID].target == NULL) return false;
    C3D_bindTarget(renderer, surfaceID, false);
    if (surfaceID >= 0) {
        N3DSSurface* surface = &renderer->surfaces[surfaceID];
        n3ds_c3dSetSurfaceProj(&renderer->drawContext,
            (float)C3D_surfaceLogicalWidth(surface),
            (float)C3D_surfaceLogicalHeight(surface));
        renderer->currentLogicalPortX = 0;
        renderer->currentLogicalPortY = 0;
        renderer->currentLogicalPortW = C3D_surfaceLogicalWidth(surface);
        renderer->currentLogicalPortH = C3D_surfaceLogicalHeight(surface);
    } else {
        n3ds_c3dUpdateProjection(&renderer->drawContext, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT);
        renderer->currentLogicalPortX = 0;
        renderer->currentLogicalPortY = 0;
        renderer->currentLogicalPortW = N3DS_TOP_WIDTH;
        renderer->currentLogicalPortH = N3DS_TOP_HEIGHT;
    }
    renderer->currentViewX = 0;
    renderer->currentViewY = 0;
    renderer->currentViewW = renderer->currentLogicalPortW;
    renderer->currentViewH = renderer->currentLogicalPortH;
    renderer->currentViewAngle = 0.0f;
    renderer->screenTransformActive = false;
    renderer->customCameraTransformActive = false;
    renderer->viewCullActive = false;
    return true;
}

static bool C3D_setRenderTarget(
    Renderer* base, int32_t surfaceID, bool implicitApplicationSurface) {
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (!implicitApplicationSurface && !C3D_surfaceExists(base, surfaceID)) return false;
    int32_t depth = 0;
    if (base->runner != NULL) {
        while (depth < MAX_SURFACES && base->runner->surfaceStack[depth] != -1) depth++;
    }

    if (depth < arrlen(renderer->targetStack)) {
        C3D_restoreViewState(renderer, &renderer->targetStack[depth], -1);
        arrsetlen(renderer->targetStack, depth);
        return true;
    }

    bool pushed = depth > arrlen(renderer->targetStack);
    if (pushed) {
        N3DSRenderStateSnapshot state;
        C3D_saveRenderState(renderer, &state);
        arrput(renderer->targetStack, state);
    }
    if (C3D_setTargetProjection(base, surfaceID)) return true;
    if (pushed) (void)arrpop(renderer->targetStack);
    return false;
}

static int32_t C3D_ensureAppSurface(
    Renderer* base, int32_t width, int32_t height) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->runner == NULL || !base->runner->usingAppSurface || width <= 0 || height <= 0) {
        if (renderer->applicationSurfaceId >= 0) {
            C3D_destroySurfaceSlot(renderer, renderer->applicationSurfaceId);
            renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
        }
        return APPLICATION_SURFACE_ID;
    }

    if (C3D_appSurfaceTooLarge(width, height)) {
        if (renderer->applicationSurfaceId >= 0) {
            C3D_destroySurfaceSlot(renderer, renderer->applicationSurfaceId);
        }
        renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
        if (renderer->failedApplicationSurfaceW != width ||
            renderer->failedApplicationSurfaceH != height) {
            renderer->failedApplicationSurfaceW = width;
            renderer->failedApplicationSurfaceH = height;
            logEvent("renderer", "app surface unavailable logical=%ldx%ld max=%dx%d", (long)width,
                (long)height, N3DS_MAX_APPLICATION_SURFACE_WIDTH,
                N3DS_MAX_APPLICATION_SURFACE_HEIGHT);
        }
        return APPLICATION_SURFACE_ID;
    }

    if (C3D_surfaceIdIsLive(renderer, renderer->applicationSurfaceId)) {
        N3DSSurface* surface = &renderer->surfaces[renderer->applicationSurfaceId];
        if (C3D_surfaceLogicalWidth(surface) == width &&
            C3D_surfaceLogicalHeight(surface) == height) {
            return renderer->applicationSurfaceId;
        }
        int32_t appSurfaceId = renderer->applicationSurfaceId;
        if (C3D_resizeAppSurface(renderer, appSurfaceId, width, height)) {
            return appSurfaceId;
        }
        C3D_surfaceResize(base, appSurfaceId, width, height);
        if (C3D_surfaceIdIsLive(renderer, appSurfaceId)) {
            return appSurfaceId;
        }
        renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
    }

    int32_t surfaceID = C3D_createAppSurface(base, width, height);
    if (surfaceID >= 0) {
        renderer->failedApplicationSurfaceW = 0;
        renderer->failedApplicationSurfaceH = 0;
        renderer->applicationSurfaceId = surfaceID;
        logEvent("renderer",
            "app surface ready id=%ld phys=%ldx%ld storage=%ldx%ld logical=%ldx%ld fmt=%ld",
            (long)surfaceID, (long)renderer->surfaces[surfaceID].width,
            (long)renderer->surfaces[surfaceID].height,
            (long)renderer->surfaces[surfaceID].texWidth,
            (long)renderer->surfaces[surfaceID].texHeight,
            (long)C3D_surfaceLogicalWidth(&renderer->surfaces[surfaceID]),
            (long)C3D_surfaceLogicalHeight(&renderer->surfaces[surfaceID]),
            (long)renderer->surfaces[surfaceID].texFormat);
    } else {
        renderer->failedApplicationSurfaceW = width;
        renderer->failedApplicationSurfaceH = height;
        renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
        logEvent("renderer",
            "app surface create failed logical=%ldx%ld linFree=%lu appFree=%lu vramFree=%lu",
            (long)width, (long)height, (unsigned long)n3ds_getFreeLinearBytes(),
            (unsigned long)n3ds_getFreeHeapBudgetBytes(),
            (unsigned long)vramSpaceFree());
    }
    return renderer->applicationSurfaceId;
}

static float C3D_getSurfaceWidth(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    return C3D_surfaceExists(base, surfaceID)
        ? (float)C3D_surfaceLogicalWidth(&renderer->surfaces[surfaceID])
        : 0.0f;
}

static float C3D_getSurfaceHeight(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    return C3D_surfaceExists(base, surfaceID)
        ? (float)C3D_surfaceLogicalHeight(&renderer->surfaces[surfaceID])
        : 0.0f;
}

static void C3D_drawSurface(Renderer* base, int32_t surfaceID, int32_t srcLeft,
    int32_t srcTop, int32_t srcWidth, int32_t srcHeight, float x, float y, float xscale,
    float yscale, float angleDeg, uint32_t color, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    surfaceID = C3D_resolveSampleSurface(base, surfaceID, false, true);
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) return;
    if (!C3D_finishSurfaceUpload(renderer, surfaceID)) return;
    N3DSSurface* surface = &renderer->surfaces[surfaceID];
    int32_t logicalW = C3D_surfaceLogicalWidth(surface);
    int32_t logicalH = C3D_surfaceLogicalHeight(surface);
    if (srcLeft < 0) srcLeft = 0;
    if (srcTop < 0) srcTop = 0;
    if (srcWidth < 0) srcWidth = logicalW - srcLeft;
    if (srcHeight < 0) srcHeight = logicalH - srcTop;
    if (srcLeft >= logicalW || srcTop >= logicalH || srcWidth <= 0 || srcHeight <= 0) return;
    if (srcLeft + srcWidth > logicalW) srcWidth = logicalW - srcLeft;
    if (srcTop + srcHeight > logicalH) srcHeight = logicalH - srcTop;
    if (srcWidth <= 0 || srcHeight <= 0) return;
    N3DSDrawMapper mapper;
    C3D_mapInitSprite(&mapper, x, y, xscale, yscale, angleDeg);
    N3DSUVRect region;
    C3D_Tex* tex = &surface->tex;
    C3D_getSurfaceRegion(surface, &region);
    uint32_t rgba = C3D_makeColor(color, alpha);
    float physScaleX = C3D_surfacePhysicalScaleX(surface);
    float physScaleY = C3D_surfacePhysicalScaleY(surface);
    C3D_emitMappedQuad(renderer, tex, &region, &mapper, (float)srcLeft * physScaleX,
        (float)srcTop * physScaleY, (float)(srcLeft + srcWidth) * physScaleX,
        (float)(srcTop + srcHeight) * physScaleY, 0.0f, 0.0f, (float)srcWidth, (float)srcHeight,
        rgba, rgba, rgba, rgba);
}

static void C3D_drawSurfaceTiled(Renderer* base, int32_t surfaceID, float x, float y,
    float xscale, float yscale, float roomW, float roomH, uint32_t color, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->vtable == NULL || !C3D_surfaceIdIsLive(renderer, surfaceID))
        return;
    N3DSSurface* surface = &renderer->surfaces[surfaceID];
    int32_t logicalW = C3D_surfaceLogicalWidth(surface);
    int32_t logicalH = C3D_surfaceLogicalHeight(surface);
    if (logicalW <= 0 || logicalH <= 0) return;
    float axScale = fabsf(xscale);
    float ayScale = fabsf(yscale);
    float stepX = (float)logicalW * axScale;
    float stepY = (float)logicalH * ayScale;
    if (stepX <= 0.0f || stepY <= 0.0f) return;

    float startX = fmodf(x, stepX);
    float startY = fmodf(y, stepY);
    if (startX > 0.0f) startX -= stepX;
    if (startY > 0.0f) startY -= stepY;
    float endX = roomW;
    float endY = roomH;

    if (renderer->viewCullActive && !renderer->screenTransformActive &&
        fabsf(renderer->currentViewAngle) <= 0.0001f) {
        float clipLeft = (float)renderer->currentViewX;
        float clipTop = (float)renderer->currentViewY;
        float clipRight = clipLeft + (float)renderer->currentViewW;
        float clipBottom = clipTop + (float)renderer->currentViewH;

        float firstVisibleX = clipLeft - stepX;
        if (firstVisibleX > startX) {
            startX += floorf((firstVisibleX - startX) / stepX) * stepX;
            if (startX + stepX <= clipLeft) startX += stepX;
        }
        endX = fminf(endX, clipRight);

        float firstVisibleY = clipTop - stepY;
        if (firstVisibleY > startY) {
            startY += floorf((firstVisibleY - startY) / stepY) * stepY;
            if (startY + stepY <= clipTop) startY += stepY;
        }
        endY = fminf(endY, clipBottom);
    }

    if (startX >= endX || startY >= endY) return;
    int32_t tilesX = (int32_t)((endX - startX) / stepX) + 1;
    int32_t tilesY = (int32_t)((endY - startY) / stepY) + 1;
    if (tilesX <= 0 || tilesY <= 0) return;

    repeat(tilesY, iy) {
        float py = startY + (float)iy * stepY;
        if (py >= endY) break;
        repeat(tilesX, ix) {
            float px = startX + (float)ix * stepX;
            if (px >= endX) break;
            base->vtable->drawSurface(
                base, surfaceID, 0, 0, -1, -1, px, py, xscale, yscale, 0.0f, color, alpha);
        }
    }
}

static void C3D_surfaceResize(
    Renderer* base, int32_t surfaceID, int32_t width, int32_t height) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (surfaceID == APPLICATION_SURFACE_ID || !C3D_surfaceIdIsLive(renderer, surfaceID) ||
        width <= 0 || height <= 0)
        return;
    if (C3D_surfaceLogicalWidth(&renderer->surfaces[surfaceID]) == width &&
        C3D_surfaceLogicalHeight(&renderer->surfaces[surfaceID]) == height)
        return;
    bool wasApplicationSurface = renderer->applicationSurfaceId == surfaceID;
    bool wasBottomSurface = renderer->bottomSurfaceId == surfaceID;
    if (wasApplicationSurface &&
        C3D_resizeAppSurface(renderer, surfaceID, width, height))
        return;
    C3D_destroySurfaceSlot(renderer, surfaceID);
    int32_t newId;
    if (wasApplicationSurface) {
        newId = C3D_createAppSurface(base, width, height);
    } else if (wasBottomSurface) {
        newId = C3D_createSurfaceStorageEx(
            base, width, height, width, height, true, GPU_RGBA4, true, true);
    } else {
        newId = C3D_createSurface(base, width, height);
    }
    if (newId < 0) {
        memset(&renderer->surfaces[surfaceID], 0, sizeof(renderer->surfaces[surfaceID]));
        if (wasApplicationSurface) {
            renderer->applicationSurfaceId = APPLICATION_SURFACE_ID;
            if (base->runner != NULL) base->runner->applicationSurfaceId = APPLICATION_SURFACE_ID;
        }
        return;
    }
    if (newId != surfaceID) {
        renderer->surfaces[surfaceID] = renderer->surfaces[newId];
        memset(&renderer->surfaces[newId], 0, sizeof(renderer->surfaces[newId]));
    }
    renderer->surfaces[surfaceID].used = true;
    if (wasBottomSurface) renderer->bottomSurfaceId = surfaceID;
    if (wasApplicationSurface) {
        renderer->applicationSurfaceId = surfaceID;
        if (base->runner != NULL) base->runner->applicationSurfaceId = surfaceID;
    }
}

static void C3D_surfaceFree(Renderer* base, int32_t surfaceID) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (surfaceID == APPLICATION_SURFACE_ID || !C3D_surfaceIdIsLive(renderer, surfaceID))
        return;
    if (surfaceID == renderer->applicationSurfaceId) return;
    C3D_queueSurfaceFree(renderer, surfaceID);
}

static void C3D_surfaceCopy(Renderer* base, int32_t destSurfaceID, int32_t destX,
    int32_t destY, int32_t srcSurfaceID, int32_t srcX, int32_t srcY, int32_t srcW, int32_t srcH,
    bool part) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (!C3D_surfaceExists(base, destSurfaceID) ||
        !C3D_surfaceExists(base, srcSurfaceID))
        return;
    N3DSRenderStateSnapshot savedState;
    C3D_saveRenderState(renderer, &savedState);
    C3D_setTargetProjection(base, destSurfaceID);
    if (part)
        base->vtable->drawSurface(base, srcSurfaceID, srcX, srcY, srcW, srcH, (float)destX,
            (float)destY, 1.0f, 1.0f, 0.0f, 0xFFFFFFu, 1.0f);
    else
        base->vtable->drawSurface(base, srcSurfaceID, 0, 0, srcW, srcH, (float)destX, (float)destY,
            1.0f, 1.0f, 0.0f, 0xFFFFFFu, 1.0f);
    n3ds_c3dFlush(&renderer->drawContext);
    C3D_restoreRenderState(renderer, &savedState, destSurfaceID);
}

static bool C3D_surfaceGetPixels(Renderer* base, int32_t surfaceID, uint8_t* outRGBA) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (outRGBA == NULL) return false;
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) {
        return false;
    }
    N3DSSurface* surface = &renderer->surfaces[surfaceID];
    size_t bytes = (size_t)surface->texWidth * (size_t)surface->texHeight * 4u;
    uint8_t* temp = linearAlloc(bytes);
    if (temp == NULL) {
        return false;
    }
    n3ds_c3dFlush(&renderer->drawContext);
    if (renderer->currentTargetSurfaceId == surfaceID) {
        C3D_safeFrameSplit(renderer);
        gspWaitForP3D();
    }
    C3D_TexFlush(&surface->tex);
    void* colorBuf = surface->target != NULL && surface->target->frameBuf.colorBuf != NULL
        ? surface->target->frameBuf.colorBuf
        : surface->tex.data;
    uint16_t transferW = surface->target != NULL && surface->target->frameBuf.width > 0
        ? surface->target->frameBuf.width
        : surface->texWidth;
    uint16_t transferH = surface->target != NULL && surface->target->frameBuf.height > 0
        ? surface->target->frameBuf.height
        : surface->texHeight;
    C3D_SyncDisplayTransfer((u32*)colorBuf, GX_BUFFER_DIM(transferW, transferH), (u32*)temp,
        GX_BUFFER_DIM(surface->texWidth, surface->texHeight),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
            GX_TRANSFER_IN_FORMAT(surface->transferFormat) |
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    gspWaitForPPF();
    size_t srcPitch = (size_t)surface->texWidth * 4u;
    int32_t srcYOffset = C3D_surfaceStorageYOffset(surface);
    int32_t readW = C3D_surfaceLogicalWidth(surface);
    int32_t readH = C3D_surfaceLogicalHeight(surface);
    if (readW <= 0 || readH <= 0) {
        linearFree(temp);
        return false;
    }
    size_t dstPitch = (size_t)readW * 4u;
    if (C3D_surfaceIsScaled(surface)) {
        for (int32_t y = 0; y < readH; ++y) {
            int32_t sy = (int32_t)((int64_t)y * (int64_t)surface->height / (int64_t)readH);
            if (sy < 0) sy = 0;
            if (sy >= surface->height) sy = surface->height - 1;
            for (int32_t x = 0; x < readW; ++x) {
                int32_t sx = (int32_t)((int64_t)x * (int64_t)surface->width / (int64_t)readW);
                if (sx < 0) sx = 0;
                if (sx >= surface->width) sx = surface->width - 1;
                memcpy(outRGBA + (size_t)y * dstPitch + (size_t)x * 4u,
                    temp + (size_t)(srcYOffset + sy) * srcPitch + (size_t)sx * 4u, 4u);
            }
        }
    } else {
        for (int32_t y = 0; y < readH; ++y) {
            memcpy(outRGBA + (size_t)y * dstPitch, temp + (size_t)(srcYOffset + y) * srcPitch,
                (size_t)readW * 4u);
        }
    }
    linearFree(temp);
    return true;
}

static void C3D_drawTiledPart(Renderer* base, int32_t tpagIndex, int32_t srcX,
    int32_t srcY, int32_t srcW, int32_t srcH, float dstX, float dstY, float dstW, float dstH,
    uint32_t color, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    float cursorY = 0.0f;
    float endX = dstW;
    float endY = dstH;
    if (renderer->viewCullActive && !renderer->screenTransformActive &&
        fabsf(renderer->currentViewAngle) <= 0.0001f && srcW > 0 && srcH > 0) {
        float clipLeft = (float)renderer->currentViewX;
        float clipTop = (float)renderer->currentViewY;
        float clipRight = clipLeft + (float)renderer->currentViewW;
        float clipBottom = clipTop + (float)renderer->currentViewH;
        if (dstX >= clipRight || dstY >= clipBottom || dstX + dstW <= clipLeft ||
            dstY + dstH <= clipTop)
            return;
        if (dstY < clipTop) {
            float skipY = floorf((clipTop - dstY) / (float)srcH) * (float)srcH;
            if (skipY > 0.0f) cursorY = skipY;
        }
        endX = fminf(dstW, clipRight - dstX + (float)srcW);
        endY = fminf(dstH, clipBottom - dstY + (float)srcH);
    }
    while (cursorY < dstH && cursorY < endY) {
        float drawH = fminf((float)srcH, dstH - cursorY);
        float cursorX = 0.0f;
        if (renderer->viewCullActive && !renderer->screenTransformActive &&
            fabsf(renderer->currentViewAngle) <= 0.0001f && srcW > 0) {
            float clipLeft = (float)renderer->currentViewX;
            if (dstX < clipLeft) {
                float skipX = floorf((clipLeft - dstX) / (float)srcW) * (float)srcW;
                if (skipX > 0.0f) cursorX = skipX;
            }
        }
        while (cursorX < dstW && cursorX < endX && cursorY < endY) {
            float drawW = fminf((float)srcW, dstW - cursorX);
            base->vtable->drawSpritePart(base, tpagIndex, srcX, srcY, (int32_t)drawW,
                (int32_t)drawH, dstX + cursorX, dstY + cursorY, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, color,
                alpha);
            cursorX += drawW;
        }
        cursorY += drawH;
    }
}

static void C3D_drawTiled(Renderer* base, int32_t tpagIndex, float originX, float originY,
    float x, float y, float xscale, float yscale, bool tileX, bool tileY, float roomW, float roomH,
    uint32_t color, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->dataWin == NULL || tpagIndex < 0 ||
        (uint32_t)tpagIndex >= base->dataWin->tpag.count)
        return;
    if (C3D_isNativeScreenOverride(renderer, tpagIndex, NULL)) {
        C3D_drawSprite(
            base, tpagIndex, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, color, alpha);
        return;
    }
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
    if (renderer->viewCullActive && !renderer->screenTransformActive &&
        fabsf(renderer->currentViewAngle) <= 0.0001f) {
        float clipLeft = (float)renderer->currentViewX;
        float clipTop = (float)renderer->currentViewY;
        float clipRight = clipLeft + (float)renderer->currentViewW;
        float clipBottom = clipTop + (float)renderer->currentViewH;
        float spriteX1 = (float)tpag->targetX * xscale;
        float spriteX2 = (float)(tpag->targetX + tpag->targetWidth) * xscale;
        float spriteY1 = (float)tpag->targetY * yscale;
        float spriteY2 = (float)(tpag->targetY + tpag->targetHeight) * yscale;
        float minSpriteX = spriteX1 < spriteX2 ? spriteX1 : spriteX2;
        float maxSpriteX = spriteX1 > spriteX2 ? spriteX1 : spriteX2;
        float minSpriteY = spriteY1 < spriteY2 ? spriteY1 : spriteY2;
        float maxSpriteY = spriteY1 > spriteY2 ? spriteY1 : spriteY2;

        if (tileX) {
            float firstVisibleX = clipLeft - maxSpriteX;
            if (firstVisibleX > startX) {
                startX += floorf((firstVisibleX - startX) / stepX) * stepX;
                if (startX + maxSpriteX <= clipLeft) startX += stepX;
            }
            endX = fminf(endX, clipRight - minSpriteX);
        } else if (x + maxSpriteX <= clipLeft || x + minSpriteX >= clipRight) {
            return;
        }

        if (tileY) {
            float firstVisibleY = clipTop - maxSpriteY;
            if (firstVisibleY > startY) {
                startY += floorf((firstVisibleY - startY) / stepY) * stepY;
                if (startY + maxSpriteY <= clipTop) startY += stepY;
            }
            endY = fminf(endY, clipBottom - minSpriteY);
        } else if (y + maxSpriteY <= clipTop || y + minSpriteY >= clipBottom) {
            return;
        }
    }
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

static int32_t C3D_createSpriteFromSurface(Renderer* base, int32_t surfaceID, int32_t x,
    int32_t y, int32_t w, int32_t h, MAYBE_UNUSED bool removeback, bool smooth, int32_t xorig,
    int32_t yorig) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    DataWin* dw = base->dataWin;
    if (dw == NULL || w <= 0 || h <= 0) return -1;
    int32_t requestedW = w;
    int32_t requestedH = h;
    int32_t originalSurfaceID = surfaceID;
    int32_t currentAppSurfaceId = renderer->base.runner != NULL
        ? renderer->base.runner->applicationSurfaceId
        : renderer->applicationSurfaceId;
    bool appSurfaceSource = originalSurfaceID >= 0 && originalSurfaceID == currentAppSurfaceId;
    int32_t appSurfaceId = -1;
    if (appSurfaceSource && !C3D_surfaceIdIsLive(renderer, surfaceID)) {
        appSurfaceId = C3D_resolveAppSurface(base);
        if (C3D_surfaceIdIsLive(renderer, appSurfaceId)) surfaceID = appSurfaceId;
    }
    if (appSurfaceSource && !C3D_surfaceIdIsLive(renderer, surfaceID)) {
        if (C3D_surfaceIdIsLive(renderer, renderer->applicationSurfaceId))
            surfaceID = renderer->applicationSurfaceId;
    }
    if (!C3D_surfaceIdIsLive(renderer, surfaceID)) {
        return -1;
    }

    N3DSSurface* source = &renderer->surfaces[surfaceID];
    N3DSSurfaceSnapshotRect snapshot;
    bool useActiveProjection = appSurfaceSource && renderer->currentTargetSurfaceId == surfaceID;
    if (!C3D_getSnapshotRect(
            renderer, source, useActiveProjection, x, y, requestedW, requestedH, &snapshot))
        return -1;

    int32_t targetPhysicalW =
        (int32_t)ceilf((float)snapshot.snapshotW * C3D_surfacePhysicalScaleX(source));
    int32_t targetPhysicalH =
        (int32_t)ceilf((float)snapshot.snapshotH * C3D_surfacePhysicalScaleY(source));
    if (targetPhysicalW < 1) targetPhysicalW = 1;
    if (targetPhysicalH < 1) targetPhysicalH = 1;
    int32_t privateSurfaceID = -1;
    privateSurfaceID = C3D_allocSpriteSurface(
        base, targetPhysicalW, targetPhysicalH, requestedW, requestedH);
    if (privateSurfaceID < 0) {
        return -1;
    }
    float dstX = (float)snapshot.destX * (float)requestedW / (float)snapshot.snapshotW;
    float dstY = (float)snapshot.destY * (float)requestedH / (float)snapshot.snapshotH;
    float dstW = (float)snapshot.copyW * (float)requestedW / (float)snapshot.snapshotW;
    float dstH = (float)snapshot.copyH * (float)requestedH / (float)snapshot.snapshotH;
    bool copied = C3D_copySurfaceRect(base, surfaceID, privateSurfaceID,
        snapshot.srcX, snapshot.srcY, snapshot.copyW, snapshot.copyH, dstX, dstY, dstW, dstH);
    if (!copied) {
        C3D_destroySurfaceSlot(renderer, privateSurfaceID);
        return -1;
    }
    w = requestedW;
    h = requestedH;

    uint32_t tpagIndex = C3D_allocRuntimeTpag(renderer, dw);
    if (tpagIndex == UINT32_MAX) {
        C3D_destroySurfaceSlot(renderer, privateSurfaceID);
        return -1;
    }
    TexturePageItem* tpag = &dw->tpag.items[tpagIndex];
    memset(tpag, 0, sizeof(*tpag));
    tpag->present = true;
    tpag->sourceWidth = (uint16_t)w;
    tpag->sourceHeight = (uint16_t)h;
    tpag->targetWidth = (uint16_t)w;
    tpag->targetHeight = (uint16_t)h;
    tpag->boundingWidth = (uint16_t)w;
    tpag->boundingHeight = (uint16_t)h;
    tpag->texturePageId = 0;

    renderer->tpagBindings[tpagIndex] = (N3DSTpagBinding){
        .source = N3DS_TPAG_SOURCE_SURFACE,
        .assetIndex = (uint32_t)privateSurfaceID,
        .surfaceGeneration = renderer->surfaces[privateSurfaceID].generation,
        .resizeDivisor = 1u,
        .atlasItemIndex = UINT32_MAX,
    };

    uint32_t spriteIndex = DataWin_allocSpriteSlot(dw, dw->sprt.parsedCount);
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    sprite->present = true;
    sprite->width = (uint32_t)w;
    sprite->height = (uint32_t)h;
    sprite->marginLeft = 0;
    sprite->marginTop = 0;
    sprite->marginRight = w - 1;
    sprite->marginBottom = h - 1;
    sprite->transparent = true;
    sprite->smooth = smooth;
    sprite->preload = true;
    sprite->originX = xorig;
    sprite->originY = yorig;
    sprite->textureCount = 1;
    sprite->tpagIndices = safeMalloc(sizeof(int32_t));
    sprite->tpagIndices[0] = (int32_t)tpagIndex;
    sprite->maskCount = 0u;
    sprite->masks = NULL;
    sprite->maskWidth = (uint32_t)w;
    sprite->maskHeight = (uint32_t)h;
    return (int32_t)spriteIndex;
}

static void C3D_deleteSprite(Renderer* base, int32_t spriteIndex) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    DataWin* dw = base->dataWin;
    if (dw == NULL || spriteIndex < 0 ||
        (uint32_t)spriteIndex >= dw->sprt.count || (uint32_t)spriteIndex < dw->sprt.parsedCount)
        return;
    Sprite* sprite = &dw->sprt.sprites[spriteIndex];
    if (sprite->tpagIndices != NULL && sprite->textureCount > 0) {
        for (uint32_t i = 0; i < sprite->textureCount; ++i) {
            int32_t tpagIndex = sprite->tpagIndices[i];
            if (tpagIndex < 0 || (uint32_t)tpagIndex >= renderer->tpagBindingCount) continue;
            N3DSTpagBinding* binding = &renderer->tpagBindings[tpagIndex];
            int32_t surfaceID = (int32_t)binding->assetIndex;
            bool matches = C3D_surfaceBindingMatches(renderer, binding);
            bool borrowedSurface = (binding->flags & N3DS_TPAG_BINDING_FLAG_BORROWED_SURFACE) != 0u;
            bool pooled = matches && !borrowedSurface &&
                C3D_recycleSpriteSurface(renderer, surfaceID);
            if (matches && !borrowedSurface && !pooled)
                C3D_queueSurfaceFree(renderer, surfaceID);
            memset(binding, 0, sizeof(*binding));
            binding->source = N3DS_TPAG_SOURCE_NONE;
            binding->atlasItemIndex = UINT32_MAX;
            binding->resizeDivisor = 1u;
            if ((uint32_t)tpagIndex < dw->tpag.count) {
                memset(&dw->tpag.items[tpagIndex], 0, sizeof(dw->tpag.items[tpagIndex]));
                C3D_recycleRuntimeTpag(renderer, (uint32_t)tpagIndex);
            }
        }
    }
    const char* name = sprite->name;
    free(sprite->tpagIndices);
    if (sprite->masks != NULL) {
        for (uint32_t i = 0; i < sprite->maskCount; ++i)
            free(sprite->masks[i]);
        free(sprite->masks);
    }
    memset(sprite, 0, sizeof(*sprite));
    sprite->name = name;
}

static void C3D_gpuSetBlendMode(Renderer* base, int32_t mode) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    N3DSRenderState* state = &renderer->renderState;
    state->blendEnabled = true;
    state->blendEq = GPU_BLEND_ADD;
    state->blendSrc = bm_src_alpha;
    state->blendDst = bm_inv_src_alpha;
    state->blendSrcAlpha = bm_one;
    state->blendDstAlpha = bm_inv_src_alpha;
    if (mode == bm_add) {
        state->blendDst = bm_one;
        state->blendDstAlpha = bm_one;
    } else if (mode == bm_subtract) {
        state->blendSrc = bm_zero;
        state->blendDst = bm_inv_src_color;
        state->blendSrcAlpha = bm_zero;
        state->blendDstAlpha = bm_inv_src_alpha;
    } else if (mode == bm_reverse_subtract)
        state->blendEq = GPU_BLEND_REVERSE_SUBTRACT;
    else if (mode == bm_min)
        state->blendEq = GPU_BLEND_MIN;
    else if (mode == bm_max)
        state->blendEq = GPU_BLEND_MAX;
    C3D_applyBlendState(renderer);
}

static void C3D_gpuSetBlendModeExt(Renderer* base, int32_t sfactor, int32_t dfactor,
    int32_t sfactor_alpha, int32_t dfactor_alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->renderState.blendEnabled = true;
    renderer->renderState.blendSrc = sfactor;
    renderer->renderState.blendDst = dfactor;
    renderer->renderState.blendSrcAlpha = sfactor_alpha;
    renderer->renderState.blendDstAlpha = dfactor_alpha;
    renderer->renderState.blendEq = GPU_BLEND_ADD;
    C3D_applyBlendState(renderer);
}

static void C3D_gpuSetBlendEnable(Renderer* base, bool enable) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->renderState.blendEnabled = enable;
    C3D_applyBlendState(renderer);
}

static void C3D_gpuSetTexFilter(Renderer* base, bool enable) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->texFilter == enable) return;
    base->texFilter = enable;
    n3ds_c3dSetTexFilter(&renderer->drawContext, enable);
}

static bool C3D_gpuGetBlendEnable(Renderer* base) {
    assert(base != NULL);
    return ((N3DSRenderer*)base)->renderState.blendEnabled;
}

static BlendFactors C3D_gpuGetBlendFactors(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    BlendFactors factors = {bm_src_alpha, bm_inv_src_alpha, bm_src_alpha, bm_inv_src_alpha};
    factors.src = renderer->renderState.blendSrc;
    factors.dst = renderer->renderState.blendDst;
    factors.srcAlpha = renderer->renderState.blendSrcAlpha;
    factors.dstAlpha = renderer->renderState.blendDstAlpha;
    return factors;
}

static int32_t C3D_gpuGetBlendMode(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    N3DSRenderState* state = &renderer->renderState;
    if (state->blendEq == GPU_BLEND_ADD && state->blendSrc == bm_src_alpha &&
        state->blendDst == bm_inv_src_alpha)
        return bm_normal;
    if (state->blendEq == GPU_BLEND_ADD && state->blendSrc == bm_src_alpha &&
        state->blendDst == bm_one)
        return bm_add;
    if (state->blendEq == GPU_BLEND_ADD && state->blendSrc == bm_zero &&
        state->blendDst == bm_inv_src_color && state->blendSrcAlpha == bm_zero &&
        state->blendDstAlpha == bm_inv_src_alpha)
        return bm_subtract;
    if (state->blendEq == GPU_BLEND_REVERSE_SUBTRACT) return bm_reverse_subtract;
    if (state->blendEq == GPU_BLEND_MIN) return bm_min;
    if (state->blendEq == GPU_BLEND_MAX) return bm_max;
    return bm_complex;
}

static void C3D_gpuSetAlphaTestEnable(Renderer* base, bool enable) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->renderState.alphaTestEnabled = enable;
    C3D_applyBlendState(renderer);
}

static void C3D_gpuSetAlphaTestRef(Renderer* base, uint8_t ref) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->renderState.alphaTestRef = ref;
    C3D_applyBlendState(renderer);
}

static void C3D_gpuSetColorWriteEnable(
    Renderer* base, bool red, bool green, bool blue, bool alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->renderState.colorWriteR = red;
    renderer->renderState.colorWriteG = green;
    renderer->renderState.colorWriteB = blue;
    renderer->renderState.colorWriteA = alpha;
    C3D_applyBlendState(renderer);
}

static void C3D_gpuGetColorWriteEnable(
    Renderer* base, bool* red, bool* green, bool* blue, bool* alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (red != NULL) *red = renderer->renderState.colorWriteR;
    if (green != NULL) *green = renderer->renderState.colorWriteG;
    if (blue != NULL) *blue = renderer->renderState.colorWriteB;
    if (alpha != NULL) *alpha = renderer->renderState.colorWriteA;
}

static void C3D_gpuSetFog(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED bool enable, MAYBE_UNUSED uint32_t color,
    MAYBE_UNUSED float start, MAYBE_UNUSED float end) {
    assert(base != NULL);
}

static void C3D_gpuSetShader(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t shaderIndex) {
    assert(base != NULL);
}
static void C3D_gpuResetShader(MAYBE_UNUSED Renderer* base) {
    assert(base != NULL);
}

static int32_t C3D_shaderGetUniform(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) {
    assert(base != NULL);
    return -1;
}

static int32_t C3D_shaderGetSamplerIndex(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t shaderIndex, MAYBE_UNUSED char* uniform) {
    assert(base != NULL);
    return -1;
}

static void C3D_shaderSetUniformF(MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t handle,
    MAYBE_UNUSED int32_t count, MAYBE_UNUSED float value1, MAYBE_UNUSED float value2,
    MAYBE_UNUSED float value3, MAYBE_UNUSED float value4) {
    assert(base != NULL);
}

static void C3D_shaderSetUniformFArray(MAYBE_UNUSED Renderer* base,
    MAYBE_UNUSED int32_t handle, MAYBE_UNUSED float* values, MAYBE_UNUSED uint32_t count) {
    assert(base != NULL);
}

static void C3D_shaderSetUniformI(MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t handle,
    MAYBE_UNUSED int32_t count, MAYBE_UNUSED int32_t value1, MAYBE_UNUSED int32_t value2,
    MAYBE_UNUSED int32_t value3, MAYBE_UNUSED int32_t value4) {
    assert(base != NULL);
}

static uint32_t C3D_spriteGetTexture(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t tpagIndex) {
    assert(base != NULL);
    return 0u;
}

static uint32_t C3D_surfaceGetTexture(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t surfaceID) {
    assert(base != NULL);
    return 0u;
}

static float C3D_textureGetTexelWidth(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED uint32_t texID) {
    assert(base != NULL);
    return 1.0f;
}

static float C3D_textureGetTexelHeight(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED uint32_t texID) {
    assert(base != NULL);
    return 1.0f;
}

static bool C3D_textureGetUVs(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED uint32_t texID, float* outUVs) {
    assert(base != NULL);
    if (outUVs == NULL) return false;
    outUVs[0] = 0.0f;
    outUVs[1] = 0.0f;
    outUVs[2] = 1.0f;
    outUVs[3] = 1.0f;
    return true;
}

static void C3D_textureSetStage(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t slot, MAYBE_UNUSED uint32_t texID) {
    assert(base != NULL);
}

static bool C3D_shaderIsCompiled(
    MAYBE_UNUSED Renderer* base, MAYBE_UNUSED int32_t shader) {
    assert(base != NULL);
    return false;
}

static bool C3D_shadersSupported(void) {
    return false;
}

static void C3D_clearActive(Renderer* base, uint32_t color, float alpha) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    n3ds_c3dFlush(&renderer->drawContext);
    if (renderer->currentTargetSurfaceId >= 0 &&
        (uint32_t)renderer->currentTargetSurfaceId < renderer->surfaceCount &&
        renderer->surfaces[renderer->currentTargetSurfaceId].target != NULL) {
        uint32_t rgba = C3D_makeTextureClearColor(
            color, alpha, renderer->surfaces[renderer->currentTargetSurfaceId].texFormat);
        C3D_RenderTargetClear(
            renderer->surfaces[renderer->currentTargetSurfaceId].target, C3D_CLEAR_ALL, rgba, 0);
    } else {
        uint32_t rgba = C3D_makeClearColor(color, alpha);
        C3D_RenderTargetClear(
            renderer->currentHostTargetIsBottomScreen
                ? renderer->bottomTarget
                : renderer->topTarget,
            C3D_CLEAR_ALL, rgba, 0);
    }
}

static void C3D_init(Renderer* base, DataWin* dataWin) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    base->dataWin = dataWin;
    if (!n3ds_c3dInit(&renderer->drawContext, N3DS_C3D_MAX_QUADS)) {
        C3D_setError(renderer, "Failed to initialize 3DS draw context");
        return;
    }
    renderer->drawContextReady = true;
    renderer->drawContext.finishDraws = C3D_finishVertexStream;
    renderer->drawContext.finishDrawsUser = renderer;
    n3ds_c3dSetTexFilter(&renderer->drawContext, base->texFilter);
    renderer->topTarget = C3D_RenderTargetCreate(
        N3DS_TOP_HEIGHT, N3DS_TOP_WIDTH, GPU_RB_RGBA8, -1);
#if !N3DS_DISABLE_GAME_BOTTOM_SCREEN
    renderer->bottomTarget = C3D_RenderTargetCreate(
        N3DS_BOTTOM_HEIGHT, N3DS_BOTTOM_WIDTH, GPU_RB_RGBA8, -1);
#endif
    if (renderer->topTarget == NULL
#if !N3DS_DISABLE_GAME_BOTTOM_SCREEN
        || renderer->bottomTarget == NULL
#endif
    ) {
        C3D_setError(renderer, "Failed to create 3DS render targets");
        return;
    }
    C3D_RenderTargetSetOutput(renderer->topTarget, GFX_TOP, GFX_LEFT, N3DS_TRANSFER_FLAGS);
#if !N3DS_DISABLE_GAME_BOTTOM_SCREEN
    C3D_RenderTargetSetOutput(renderer->bottomTarget, GFX_BOTTOM, GFX_LEFT, N3DS_TRANSFER_FLAGS);
#endif
    if (base->runner != NULL && base->runner->appSurfaceEnabled && base->runner->usingAppSurface) {
        int32_t appW = base->runner->applicationWidth > 0
            ? base->runner->applicationWidth
            : (dataWin != NULL ? (int32_t)dataWin->gen8.defaultWindowWidth : 0);
        int32_t appH = base->runner->applicationHeight > 0
            ? base->runner->applicationHeight
            : (dataWin != NULL ? (int32_t)dataWin->gen8.defaultWindowHeight : 0);
        base->runner->usingAppSurface = true;
        base->runner->applicationSurfaceId =
            C3D_ensureAppSurface(base, appW, appH);
        logEvent("renderer", "startup app surface reserve id=%ld logical=%ldx%ld vramFree=%lu",
            (long)base->runner->applicationSurfaceId, (long)appW, (long)appH,
            (unsigned long)vramSpaceFree());
    }
    if (!C3D_loadAtlas(renderer)) return;
    C3D_loadEmbedRoomManifest(renderer);
    bool loadedEmbeddedBindings = C3D_loadEmbedTpagBindings(renderer, dataWin);
    if (!loadedEmbeddedBindings) {
        C3D_setError(renderer, "Failed to initialize 3DS texture bindings");
        return;
    }
    renderer->ready = true;
}

static void C3D_destroy(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (renderer->drawContextReady) n3ds_c3dFlush(&renderer->drawContext);
    for (uint32_t i = 0; i < renderer->surfaceCount; ++i) {
        if (!renderer->surfaces[i].used) continue;
        if (renderer->surfaces[i].target != NULL)
            C3D_RenderTargetDelete(renderer->surfaces[i].target);
        C3D_TexDelete(&renderer->surfaces[i].tex);
    }
    free(renderer->surfaces);
    arrfree(renderer->freeRuntimeTpagIndices);
    arrfree(renderer->freeSurfaceSpriteSurfaces);
    arrfree(renderer->pendingSurfaceFrees);
    arrfree(renderer->targetStack);
    free(renderer->roomTileRanges);
    hmfree(renderer->roomTileLookup);
    C3D_destroyTpagBindings(renderer);
    C3D_destroyAtlas(renderer);
    C3D_destroyRoomManifest(renderer);
    if (renderer->topTarget != NULL) C3D_RenderTargetDelete(renderer->topTarget);
    if (renderer->bottomTarget != NULL) C3D_RenderTargetDelete(renderer->bottomTarget);
    if (renderer->drawContextReady) n3ds_c3dDestroy(&renderer->drawContext);
    free(renderer);
}

static void C3D_beginFrame(Renderer* base, int32_t gameW, int32_t gameH,
    MAYBE_UNUSED int32_t windowW, MAYBE_UNUSED int32_t windowH) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->frameCounter++;
    if (renderer->frameCounter == 0u) renderer->frameCounter = 1u;
    renderer->currentTargetSurfaceId = INT32_MIN;
    renderer->currentHostTargetIsBottomScreen = false;
    C3D_invalidateViewportCache(renderer);
    renderer->frameGameW = gameW;
    renderer->frameGameH = gameH;
    renderer->bottomScreenUsedThisFrame = false;
    renderer->screenTransformActive = false;
    renderer->customCameraTransformActive = false;
    renderer->viewCullActive = false;
    C3D_bindTopScreenTarget(renderer);
    C3D_clearActive(base, 0x000000u, 1.0f);
    if (base->runner != NULL && base->runner->usingAppSurface) {
        int32_t appSurfaceId = base->runner->applicationSurfaceId;
        if (appSurfaceId == APPLICATION_SURFACE_ID) {
            C3D_bindTopScreenTarget(renderer);
        } else if (appSurfaceId >= 0 && C3D_surfaceExists(base, appSurfaceId)) {
            C3D_bindTarget(renderer, appSurfaceId, false);
            N3DSSurface* surface = &renderer->surfaces[appSurfaceId];
            n3ds_c3dSetSurfaceProj(&renderer->drawContext,
                (float)C3D_surfaceLogicalWidth(surface),
                (float)C3D_surfaceLogicalHeight(surface));
            renderer->currentLogicalPortX = 0;
            renderer->currentLogicalPortY = 0;
            renderer->currentLogicalPortW = C3D_surfaceLogicalWidth(surface);
            renderer->currentLogicalPortH = C3D_surfaceLogicalHeight(surface);
        } else if (appSurfaceId != APPLICATION_SURFACE_ID) {
            C3D_bindTopScreenTarget(renderer);
        }
    } else {
        C3D_bindTopScreenTarget(renderer);
    }
    C3D_applyBlendState(renderer);
    if (renderer->currentTargetSurfaceId >= 0) {
        uint32_t clearColor = base->runner != NULL && base->runner->drawBackgroundColor
            ? base->runner->backgroundColor
            : 0x000000u;
        C3D_clearActive(base, clearColor, 1.0f);
    }
}

static void C3D_endFrameInit(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    n3ds_c3dFlush(&renderer->drawContext);
    if (base->runner != NULL && base->runner->usingAppSurface &&
        !base->runner->appSurfaceAutoDraw) {
        C3D_bindTopScreenTarget(renderer);
    }
}

static void C3D_endFrameEnd(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (base->runner != NULL && base->runner->usingAppSurface && base->runner->appSurfaceAutoDraw &&
        base->runner->applicationSurfaceId >= 0) {
        C3D_blitAppSurface(renderer);
    }
    n3ds_c3dFlush(&renderer->drawContext);
}

static void C3D_updateGmlMatrixState(Renderer* base, const Matrix4f* viewMatrix,
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

static void C3D_setViewProjection(Renderer* base, float viewX, float viewY,
    float viewW, float viewH, float viewAngle) {
    if (viewW == 0.0f) viewW = 1.0f;
    if (viewH == 0.0f) viewH = 1.0f;

    Matrix4f view;
    Matrix4f projection;
    Matrix4f_identity(&view);
    Matrix4f_viewProjection(&projection, viewX, viewY, viewW, viewH, viewAngle);
    C3D_updateGmlMatrixState(base, &view, &projection, NULL);
}

static bool C3D_setScreenTransform(N3DSRenderer* renderer,
    const Matrix4f* worldToClip, int32_t portX, int32_t portY, int32_t portW, int32_t portH) {
    if (worldToClip == NULL || portW <= 0 || portH <= 0) return false;
    if (!Matrix4f_isAffine2D(worldToClip)) return false;

    Matrix4f clipToPort;
    Matrix4f_identity(&clipToPort);
    clipToPort.m[0] = (float)portW * 0.5f;
    clipToPort.m[5] = (float)portH * -0.5f;
    clipToPort.m[12] = (float)portX + (float)portW * 0.5f;
    clipToPort.m[13] = (float)portY + (float)portH * 0.5f;

    Matrix4f screenTransform;
    Matrix4f_multiply(&screenTransform, &clipToPort, worldToClip);
    if (!Matrix4f_isAffine2D(&screenTransform)) return false;
    renderer->screenTransform = screenTransform;
    renderer->screenTransformActive = true;
    renderer->customCameraTransformActive = true;
    renderer->viewCullActive = false;
    return true;
}

static void C3D_beginView(Renderer* base, int32_t viewX, int32_t viewY, int32_t viewW,
    int32_t viewH, int32_t portX, int32_t portY, int32_t portW, int32_t portH, float viewAngle) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->hostViewProjectionActive = false;
    float viewXf = (float)viewX;
    float viewYf = (float)viewY;
    renderer->currentViewX = viewX;
    renderer->currentViewY = viewY;
    renderer->currentViewW = viewW;
    renderer->currentViewH = viewH;
    renderer->currentViewAngle = viewAngle;
    renderer->viewCullActive = true;
    renderer->customCameraTransformActive = false;
    C3D_setViewProjection(base, viewXf, viewYf, (float)viewW, (float)viewH, viewAngle);
    if (renderer->currentTargetSurfaceId >= 0 &&
        (uint32_t)renderer->currentTargetSurfaceId < renderer->surfaceCount &&
        renderer->surfaces[renderer->currentTargetSurfaceId].target != NULL) {
        N3DSSurface* surface = &renderer->surfaces[renderer->currentTargetSurfaceId];
        float scaleX = C3D_surfacePhysicalScaleX(surface);
        float scaleY = C3D_surfacePhysicalScaleY(surface);
        int32_t physPortX = (int32_t)floorf((float)portX * scaleX + 0.0001f);
        int32_t physPortY = (int32_t)floorf((float)portY * scaleY + 0.0001f);
        int32_t physPortW = (int32_t)ceilf((float)(portX + portW) * scaleX - (float)physPortX);
        int32_t physPortH = (int32_t)ceilf((float)(portY + portH) * scaleY - (float)physPortY);
        int32_t viewportY = surface->target->frameBuf.height - physPortY - physPortH;
        renderer->currentLogicalPortX = portX;
        renderer->currentLogicalPortY = portY;
        renderer->currentLogicalPortW = portW;
        renderer->currentLogicalPortH = portH;
        renderer->base.CPortX = physPortX;
        renderer->base.CPortY = viewportY;
        renderer->base.CPortW = physPortW;
        renderer->base.CPortH = physPortH;
        C3D_setViewportAndScissor(
            renderer, physPortX, viewportY, physPortW, physPortH);
        renderer->screenTransformActive = false;
        C3D_Mtx proj;
        C3D_makeSurfaceProjection(
            &proj, viewXf, viewYf, (float)viewW, (float)viewH, viewAngle);
        n3ds_c3dSetProjectionC3D(&renderer->drawContext, &proj);
        return;
    }
    int32_t hostPortX = 0;
    int32_t hostPortY = 0;
    int32_t hostPortW = 0;
    int32_t hostPortH = 0;
    C3D_getHostViewport(
        renderer, portX, portY, portW, portH, &hostPortX, &hostPortY, &hostPortW, &hostPortH);
    renderer->base.CPortX = hostPortX;
    renderer->base.CPortY = hostPortY;
    renderer->base.CPortW = hostPortW;
    renderer->base.CPortH = hostPortH;
    renderer->currentLogicalPortX = hostPortX;
    renderer->currentLogicalPortY = hostPortY;
    renderer->currentLogicalPortW = hostPortW;
    renderer->currentLogicalPortH = hostPortH;
    C3D_setHostViewport(renderer);
    C3D_setScreenRect(renderer, viewXf, viewYf, viewXf + (float)viewW,
        viewYf + (float)viewH, viewAngle, hostPortX, hostPortY, hostPortW, hostPortH);
    C3D_updateHostProjection(renderer);
    if (viewAngle == 0.0f && viewW > 0 && viewH > 0 && hostPortW > 0 && hostPortH > 0) {
        C3D_Mtx proj = renderer->drawContext.projection;
        const Matrix4f* m = &renderer->screenTransform;
        for (int i = 0; i < 4; ++i) {
            proj.r[i].w += proj.r[i].x * m->m[12] + proj.r[i].y * m->m[13];
            proj.r[i].x *= m->m[0];
            proj.r[i].y *= m->m[5];
        }
        n3ds_c3dSetProjectionC3D(&renderer->drawContext, &proj);
        renderer->screenTransformActive = false;
        renderer->hostViewProjectionActive = true;
    }
}

static void C3D_endView(Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->viewCullActive = false;
    n3ds_c3dFlush(&renderer->drawContext);
}

static void C3D_applyProjection(
    Renderer* base, const Matrix4f* viewMatrix, const Matrix4f* projectionMatrix) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (viewMatrix == NULL || projectionMatrix == NULL) return;

    Matrix4f worldToClip;
    C3D_updateGmlMatrixState(base, viewMatrix, projectionMatrix, &worldToClip);

    if (renderer->currentTargetSurfaceId >= 0 &&
        (uint32_t)renderer->currentTargetSurfaceId < renderer->surfaceCount) {
        if (C3D_setScreenTransform(renderer, &worldToClip,
                renderer->currentLogicalPortX, renderer->currentLogicalPortY,
                renderer->currentLogicalPortW, renderer->currentLogicalPortH)) {
            C3D_Mtx proj;
            Mtx_Ortho(&proj, (float)renderer->currentLogicalPortX,
                (float)(renderer->currentLogicalPortX + renderer->currentLogicalPortW),
                (float)(renderer->currentLogicalPortY + renderer->currentLogicalPortH),
                (float)renderer->currentLogicalPortY, -1.0f, 1.0f, true);
            n3ds_c3dSetProjectionC3D(&renderer->drawContext, &proj);
            return;
        }
        renderer->screenTransformActive = false;
        renderer->customCameraTransformActive = false;
        n3ds_c3dSetProjectionMatrix(&renderer->drawContext, &worldToClip);
        return;
    }
    if (renderer->currentTargetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER &&
        C3D_setScreenTransform(renderer, &worldToClip,
            renderer->currentLogicalPortX, renderer->currentLogicalPortY,
            renderer->currentLogicalPortW, renderer->currentLogicalPortH)) {
        C3D_updateHostProjection(renderer);
        return;
    }
}

static void C3D_setMatrix(Renderer* base, int32_t matrixType, Matrix4f matrix) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    if (matrixType < 0 || matrixType > MATRIX_WORLD) return;
    n3ds_c3dFlush(&renderer->drawContext);
    base->gmlMatrices[matrixType] = matrix;

    Matrix4f world = base->gmlMatrices[MATRIX_WORLD];
    Matrix4f view = base->gmlMatrices[MATRIX_VIEW];
    Matrix4f projection = base->gmlMatrices[MATRIX_PROJECTION];
    Matrix4f worldView;
    Matrix4f_multiply(&worldView, &view, &world);
    Matrix4f worldToClip;
    Matrix4f_multiply(&worldToClip, &projection, &worldView);

    base->gmlMatrices[MATRIX_WORLD_VIEW] = worldView;
    base->gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION] = worldToClip;

    if (renderer->currentTargetSurfaceId >= 0 &&
        (uint32_t)renderer->currentTargetSurfaceId < renderer->surfaceCount) {
        if (renderer->customCameraTransformActive &&
            C3D_setScreenTransform(renderer, &worldToClip,
                renderer->currentLogicalPortX, renderer->currentLogicalPortY,
                renderer->currentLogicalPortW, renderer->currentLogicalPortH)) {
            C3D_Mtx proj;
            Mtx_Ortho(&proj, (float)renderer->currentLogicalPortX,
                (float)(renderer->currentLogicalPortX + renderer->currentLogicalPortW),
                (float)(renderer->currentLogicalPortY + renderer->currentLogicalPortH),
                (float)renderer->currentLogicalPortY, -1.0f, 1.0f, true);
            n3ds_c3dSetProjectionC3D(&renderer->drawContext, &proj);
            return;
        }
        renderer->screenTransformActive = false;
        renderer->customCameraTransformActive = false;
        n3ds_c3dSetProjectionMatrix(&renderer->drawContext, &worldToClip);
    } else if (renderer->currentTargetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER &&
        renderer->customCameraTransformActive) {
        if (C3D_setScreenTransform(renderer, &worldToClip,
                renderer->currentLogicalPortX, renderer->currentLogicalPortY,
                renderer->currentLogicalPortW, renderer->currentLogicalPortH)) {
            C3D_updateHostProjection(renderer);
        }
    }
}

static void C3D_beginGUI(Renderer* base, int32_t guiW, int32_t guiH, int32_t portX,
    int32_t portY, int32_t portW, int32_t portH, int32_t targetSurfaceId) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    renderer->viewCullActive = false;
    renderer->customCameraTransformActive = false;
    int32_t resolvedTargetSurfaceId =
        targetSurfaceId == RENDER_TARGET_HOST_FRAMEBUFFER
            ? RENDER_TARGET_HOST_FRAMEBUFFER
            : C3D_resolveTargetSurfaceID(base, targetSurfaceId);
    C3D_bindTarget(renderer, resolvedTargetSurfaceId, false);
    if (resolvedTargetSurfaceId >= 0 &&
        (uint32_t)resolvedTargetSurfaceId < renderer->surfaceCount &&
        renderer->surfaces[resolvedTargetSurfaceId].target != NULL) {
        N3DSSurface* surface = &renderer->surfaces[resolvedTargetSurfaceId];
        float scaleX = C3D_surfacePhysicalScaleX(surface);
        float scaleY = C3D_surfacePhysicalScaleY(surface);
        int32_t physPortX = (int32_t)floorf((float)portX * scaleX + 0.0001f);
        int32_t physPortY = (int32_t)floorf((float)portY * scaleY + 0.0001f);
        int32_t physPortW = (int32_t)ceilf((float)(portX + portW) * scaleX - (float)physPortX);
        int32_t physPortH = (int32_t)ceilf((float)(portY + portH) * scaleY - (float)physPortY);
        int32_t viewportY = surface->target->frameBuf.height - physPortY - physPortH;
        renderer->base.CPortX = physPortX;
        renderer->base.CPortY = viewportY;
        renderer->base.CPortW = physPortW;
        renderer->base.CPortH = physPortH;
        renderer->currentLogicalPortX = portX;
        renderer->currentLogicalPortY = portY;
        renderer->currentLogicalPortW = portW;
        renderer->currentLogicalPortH = portH;
        C3D_setViewportAndScissor(
            renderer, physPortX, viewportY, physPortW, physPortH);
        renderer->base.vtable->setGuiProjection(base, guiW, guiH, portW, portH, true);
        return;
    }
    int32_t hostPortX = 0;
    int32_t hostPortY = 0;
    int32_t hostPortW = 0;
    int32_t hostPortH = 0;
    C3D_getHostGuiViewport(
        guiW, guiH, portX, portY, portW, portH, &hostPortX, &hostPortY, &hostPortW, &hostPortH);
    renderer->base.CPortX = hostPortX;
    renderer->base.CPortY = hostPortY;
    renderer->base.CPortW = hostPortW;
    renderer->base.CPortH = hostPortH;
    renderer->currentLogicalPortX = hostPortX;
    renderer->currentLogicalPortY = hostPortY;
    renderer->currentLogicalPortW = hostPortW;
    renderer->currentLogicalPortH = hostPortH;
    C3D_setHostViewport(renderer);
    renderer->base.vtable->setGuiProjection(base, guiW, guiH, hostPortW, hostPortH, false);
}

static void C3D_setGuiProjection(Renderer* base, int32_t guiW, int32_t guiH, int32_t portW,
    int32_t portH, bool renderingToUserSurface) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    float guiLeft = 0.0f;
    float guiTop = 0.0f;
    float guiRight = (float)guiW;
    float guiBottom = (float)guiH;
    C3D_computeGuiSourceRect(
        guiW, guiH, portW, portH, &guiLeft, &guiTop, &guiRight, &guiBottom);
    renderer->customCameraTransformActive = false;
    if (renderingToUserSurface &&
        renderer->currentTargetSurfaceId != RENDER_TARGET_HOST_FRAMEBUFFER) {
        renderer->screenTransformActive = false;
        renderer->currentViewX = 0;
        renderer->currentViewY = 0;
        renderer->currentViewW = guiW;
        renderer->currentViewH = guiH;
        renderer->currentViewAngle = 0.0f;
        C3D_Mtx proj;
        C3D_makeSurfaceGuiProj(&proj, guiW, guiH);
        n3ds_c3dSetProjectionC3D(&renderer->drawContext, &proj);
        return;
    }
    C3D_setScreenRect(renderer, guiLeft, guiTop, guiRight, guiBottom, 0.0f,
        renderer->base.CPortX, renderer->base.CPortY, renderer->base.CPortW, renderer->base.CPortH);
    C3D_updateHostProjection(renderer);
}

static void C3D_endGUI(MAYBE_UNUSED Renderer* base) {
    assert(base != NULL);
    N3DSRenderer* renderer = (N3DSRenderer*)base;
    n3ds_c3dFlush(&renderer->drawContext);
}

static RendererVtable gN3DSRendererVtable = {
    .init = C3D_init,
    .destroy = C3D_destroy,
    .beginFrame = C3D_beginFrame,
    .endFrameInit = C3D_endFrameInit,
    .endFrameEnd = C3D_endFrameEnd,
    .beginView = C3D_beginView,
    .endView = C3D_endView,
    .applyProjection = C3D_applyProjection,
    .beginGUI = C3D_beginGUI,
    .setGuiProjection = C3D_setGuiProjection,
    .endGUI = C3D_endGUI,
    .drawSprite = C3D_drawSprite,
    .drawSpritePart = C3D_drawSpritePart,
    .drawSpritePos = C3D_drawSpritePos,
    .drawRectangle = C3D_drawRectangle,
    .drawRectangleColor = C3D_drawRectangleColor,
    .drawLine = C3D_drawLine,
    .drawTriangle = C3D_drawTriangle,
    .drawLineColor = C3D_drawLineColor,
    .drawText = C3D_drawText,
    .drawTextColor = C3D_drawTextColor,
    .flush = C3D_flush,
    .clearScreen = C3D_clearActive,
    .createSpriteFromSurface = C3D_createSpriteFromSurface,
    .deleteSprite = C3D_deleteSprite,
    .gpuGetBlendFactors = C3D_gpuGetBlendFactors,
    .gpuGetBlendMode = C3D_gpuGetBlendMode,
    .gpuSetBlendMode = C3D_gpuSetBlendMode,
    .gpuSetBlendModeExt = C3D_gpuSetBlendModeExt,
    .gpuSetBlendEnable = C3D_gpuSetBlendEnable,
    .gpuSetTexFilter = C3D_gpuSetTexFilter,
    .gpuSetAlphaTestEnable = C3D_gpuSetAlphaTestEnable,
    .gpuSetAlphaTestRef = C3D_gpuSetAlphaTestRef,
    .gpuSetColorWriteEnable = C3D_gpuSetColorWriteEnable,
    .gpuGetColorWriteEnable = C3D_gpuGetColorWriteEnable,
    .gpuGetBlendEnable = C3D_gpuGetBlendEnable,
    .gpuSetFog = C3D_gpuSetFog,
    .drawTile = C3D_drawTile,
    .drawSpriteTiled = C3D_drawTiled,
    .createSurface = C3D_createSurface,
    .surfaceExists = C3D_surfaceExists,
    .setRenderTarget = C3D_setRenderTarget,
    .ensureApplicationSurface = C3D_ensureAppSurface,
    .getSurfaceWidth = C3D_getSurfaceWidth,
    .getSurfaceHeight = C3D_getSurfaceHeight,
    .drawSurface = C3D_drawSurface,
    .drawSurfaceTiled = C3D_drawSurfaceTiled,
    .surfaceResize = C3D_surfaceResize,
    .surfaceFree = C3D_surfaceFree,
    .surfaceCopy = C3D_surfaceCopy,
    .surfaceGetPixels = C3D_surfaceGetPixels,
    .drawTiledPart = C3D_drawTiledPart,
    .gpuSetShader = C3D_gpuSetShader,
    .gpuResetShader = C3D_gpuResetShader,
    .shaderGetUniform = C3D_shaderGetUniform,
    .shaderGetSamplerIndex = C3D_shaderGetSamplerIndex,
    .shaderSetUniformF = C3D_shaderSetUniformF,
    .shaderSetUniformFArray = C3D_shaderSetUniformFArray,
    .shaderSetUniformI = C3D_shaderSetUniformI,
    .spriteGetTexture = C3D_spriteGetTexture,
    .surfaceGetTexture = C3D_surfaceGetTexture,
    .textureGetTexelWidth = C3D_textureGetTexelWidth,
    .textureGetTexelHeight = C3D_textureGetTexelHeight,
    .textureGetUVs = C3D_textureGetUVs,
    .textureSetStage = C3D_textureSetStage,
    .shaderIsCompiled = C3D_shaderIsCompiled,
    .shadersSupported = C3D_shadersSupported,
    .setMatrix = C3D_setMatrix,
};

void C3D_getViewRect(const Renderer* renderer, int32_t rect[4]) {
    const N3DSRenderer* n3ds = (const N3DSRenderer*)renderer;
    rect[0] = n3ds->currentViewX;
    rect[1] = n3ds->currentViewY;
    rect[2] = n3ds->currentViewW;
    rect[3] = n3ds->currentViewH;
}

Renderer* C3D_create(
    bool cacheAtlas, uint32_t atlasTextureCacheBytes, bool halfResApplicationSurface) {
    N3DSRenderer* renderer = safeCalloc(1, sizeof(N3DSRenderer));
    renderer->base.vtable = &gN3DSRendererVtable;
    renderer->cacheAtlas = cacheAtlas;
#if defined(APPSURFACE_HALF) && APPSURFACE_HALF
    halfResApplicationSurface = true;
#endif
    renderer->halfResApplicationSurface = halfResApplicationSurface;
    renderer->atlasTextureCacheBytes = atlasTextureCacheBytes;
    renderer->base.drawColor = 0xFFFFFFu;
    renderer->base.drawAlpha = 1.0f;
    renderer->base.drawFont = -1;
    renderer->base.drawHalign = 0;
    renderer->base.drawValign = 0;
    renderer->base.circlePrecision = 24;
    renderer->base.currentShader = -1;
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_WORLD]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_VIEW]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_PROJECTION]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_WORLD_VIEW]);
    Matrix4f_identity(&renderer->base.gmlMatrices[MATRIX_WORLD_VIEW_PROJECTION]);
    renderer->renderState = (N3DSRenderState){
        .blendEnabled = true,
        .blendSrc = bm_src_alpha,
        .blendDst = bm_inv_src_alpha,
        .blendSrcAlpha = bm_one,
        .blendDstAlpha = bm_inv_src_alpha,
        .blendEq = GPU_BLEND_ADD,
        .colorWriteR = true,
        .colorWriteG = true,
        .colorWriteB = true,
        .colorWriteA = true,
    };
    renderer->currentTargetSurfaceId = INT32_MIN;
    renderer->applicationSurfaceId = -1;
    renderer->bottomSurfaceId = -1;
    renderer->hostCaptureSurfaceId = -1;
    renderer->activeRoomIndex = -1;
    renderer->viewCullActive = false;
    return &renderer->base;
}

void C3D_beginDrawFrame(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    n3ds_c3dBeginFrame(&n3ds->drawContext);
    n3ds_c3dSetTexFilter(&n3ds->drawContext, renderer->texFilter);
}

void C3D_endDrawFrame(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    while (gspIsPresentPending(GFX_TOP) || gspIsPresentPending(GFX_BOTTOM))
        gspWaitForAnyEvent();
    n3ds_c3dEndFrame(&n3ds->drawContext);
}

N3DSC3DVertex* C3D_beginVertices(
    Renderer* renderer, C3D_Tex* texture, uint32_t vertexCount) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return n3ds_c3dBeginVertices(&n3ds->drawContext, texture, vertexCount);
}

void C3D_commitVertices(Renderer* renderer, uint32_t vertexCount) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    n3ds_c3dCommitVertices(&n3ds->drawContext, vertexCount);
}

bool C3D_isReady(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return n3ds->ready;
}

const char* C3D_getStartupError(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return n3ds->startupError;
}

#if N3DS_HAS_BOTTOM_TARGET
static bool C3D_ensureBottomTarget(N3DSRenderer* renderer) {
    if (renderer->bottomTarget != NULL) return true;
    renderer->bottomTarget = C3D_RenderTargetCreate(
        N3DS_BOTTOM_HEIGHT, N3DS_BOTTOM_WIDTH, GPU_RB_RGBA8, -1);
    if (renderer->bottomTarget == NULL) return false;
    C3D_RenderTargetSetOutput(renderer->bottomTarget, GFX_BOTTOM, GFX_LEFT, N3DS_TRANSFER_FLAGS);
    return true;
}

static void C3D_beginBottomGUIImpl(
    Renderer* renderer, int32_t guiW, int32_t guiH, bool clear) {
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (!C3D_ensureBottomTarget(n3ds)) return;
    n3ds->bottomScreenUsedThisFrame = true;
    C3D_bindTarget(n3ds, RENDER_TARGET_HOST_FRAMEBUFFER, true);
    if (clear)
        C3D_RenderTargetClear(
            n3ds->bottomTarget, C3D_CLEAR_ALL, n3ds_c3dClearColor(0, 0, 0, 0), 0);
    n3ds->base.CPortX = 0;
    n3ds->base.CPortY = 0;
    n3ds->base.CPortW = N3DS_BOTTOM_WIDTH;
    n3ds->base.CPortH = N3DS_BOTTOM_HEIGHT;
    n3ds->currentLogicalPortX = 0;
    n3ds->currentLogicalPortY = 0;
    n3ds->currentLogicalPortW = N3DS_BOTTOM_WIDTH;
    n3ds->currentLogicalPortH = N3DS_BOTTOM_HEIGHT;
    n3ds->base.vtable->setGuiProjection(
        renderer, guiW, guiH, N3DS_BOTTOM_WIDTH, N3DS_BOTTOM_HEIGHT, false);
}
#endif

void C3D_presentBottomSurface(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
#if !N3DS_HAS_BOTTOM_TARGET
    return;
#else
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (!C3D_surfaceIdIsLive(n3ds, n3ds->bottomSurfaceId)) return;

    int32_t surfaceID = n3ds->bottomSurfaceId;
    N3DSSurface* surface = &n3ds->surfaces[surfaceID];
    int32_t width = C3D_surfaceLogicalWidth(surface);
    int32_t height = C3D_surfaceLogicalHeight(surface);
    if (width <= 0 || height <= 0 || !C3D_ensureBottomTarget(n3ds)) return;

    N3DSRenderStateSnapshot savedState;
    C3D_saveRenderState(n3ds, &savedState);
    if (n3ds->currentTargetSurfaceId == surfaceID) {
        C3D_safeFrameSplit(n3ds);
        C3D_markSurfaceReady(n3ds, surfaceID);
    } else if (!C3D_finishSurfaceUpload(n3ds, surfaceID)) {
        return;
    }

    C3D_beginBottomGUIImpl(
        renderer, N3DS_BOTTOM_WIDTH, N3DS_BOTTOM_HEIGHT, true);
    renderer->vtable->drawSurface(renderer, surfaceID, 0, 0, width, height, 0.0f, 0.0f,
        (float)N3DS_BOTTOM_WIDTH / (float)width, (float)N3DS_BOTTOM_HEIGHT / (float)height, 0.0f,
        0xFFFFFFu, 1.0f);
    n3ds_c3dFlush(&n3ds->drawContext);
    C3D_restoreRenderState(n3ds, &savedState, -1);
#endif
}

void C3D_beginBottomOverlay(MAYBE_UNUSED Renderer* renderer,
    MAYBE_UNUSED int32_t guiW, MAYBE_UNUSED int32_t guiH, MAYBE_UNUSED bool clearIfUnused) {
    assert(renderer != NULL);
#if !N3DS_HAS_BOTTOM_TARGET
    return;
#else
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    C3D_beginBottomGUIImpl(
        renderer, guiW, guiH, clearIfUnused && !n3ds->bottomScreenUsedThisFrame);
#endif
}

void C3D_endBottomScreenGUI(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    n3ds_c3dFlush(&n3ds->drawContext);
    n3ds->screenTransformActive = false;
    n3ds->customCameraTransformActive = false;
}

void C3D_clearBottomScreen(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
#if !N3DS_HAS_BOTTOM_TARGET
    return;
#else
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (n3ds->bottomTarget == NULL) return;

    N3DSRenderStateSnapshot savedState;
    C3D_saveRenderState(n3ds, &savedState);
    n3ds_c3dFlush(&n3ds->drawContext);
    C3D_bindTarget(n3ds, RENDER_TARGET_HOST_FRAMEBUFFER, true);
    C3D_RenderTargetClear(n3ds->bottomTarget, C3D_CLEAR_ALL, n3ds_c3dClearColor(0, 0, 0, 0), 0);
    n3ds->bottomScreenUsedThisFrame = true;
    C3D_restoreRenderState(n3ds, &savedState, -1);
#endif
}

void C3D_beginTopOverlay(Renderer* renderer, int32_t guiW, int32_t guiH) {
    if (renderer == NULL) return;
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (n3ds->topTarget == NULL) return;
    C3D_bindTarget(n3ds, RENDER_TARGET_HOST_FRAMEBUFFER, false);
    C3D_RenderTargetClear(n3ds->topTarget, C3D_CLEAR_ALL, n3ds_c3dClearColor(0, 0, 0, 0), 0);
    renderer->CPortX = 0;
    renderer->CPortY = 0;
    renderer->CPortW = N3DS_TOP_WIDTH;
    renderer->CPortH = N3DS_TOP_HEIGHT;
    n3ds->currentLogicalPortX = 0;
    n3ds->currentLogicalPortY = 0;
    n3ds->currentLogicalPortW = N3DS_TOP_WIDTH;
    n3ds->currentLogicalPortH = N3DS_TOP_HEIGHT;
    renderer->vtable->setGuiProjection(
        renderer, guiW, guiH, N3DS_TOP_WIDTH, N3DS_TOP_HEIGHT, false);
}

void C3D_clearTopScreen(Renderer* renderer) {
    if (renderer == NULL) return;
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (n3ds->topTarget == NULL) return;
    n3ds_c3dFlush(&n3ds->drawContext);
    C3D_bindTarget(n3ds, RENDER_TARGET_HOST_FRAMEBUFFER, false);
    C3D_RenderTargetClear(n3ds->topTarget, C3D_CLEAR_ALL, n3ds_c3dClearColor(0, 0, 0, 0), 0);
}

void C3D_setGameOnBottomScreen(Renderer* renderer, bool enabled) {
    if (renderer == NULL) return;
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
#if N3DS_HAS_BOTTOM_TARGET
    if (enabled && !C3D_ensureBottomTarget(n3ds)) return;
#else
    if (enabled) return;
#endif
    n3ds->gameOnBottomScreen = enabled;
}

void C3D_skipTopScreenPresent(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (n3ds->topTarget == NULL) return;
    n3ds_c3dFlush(&n3ds->drawContext);
    C3D_RenderTargetDetachOutput(n3ds->topTarget);
    n3ds->topTarget->used = false;
}

void C3D_restoreTopScreenOutput(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    if (n3ds->topTarget == NULL) return;
    C3D_RenderTargetSetOutput(n3ds->topTarget, GFX_TOP, GFX_LEFT, N3DS_TRANSFER_FLAGS);
}

bool C3D_bottomScreenUsed(MAYBE_UNUSED Renderer* renderer) {
    assert(renderer != NULL);
#if !N3DS_HAS_BOTTOM_TARGET
    return false;
#else
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return n3ds->bottomScreenUsedThisFrame;
#endif
}

uint32_t C3D_getAtlasVramBytes(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return n3ds->atlas.residentBytes;
}

uint32_t C3D_getAtlasPages(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return C3D_countAtlasPages(n3ds);
}

uint32_t C3D_getAtlasPageLimit(Renderer* renderer) {
    assert(renderer != NULL);
    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    return n3ds->atlas.pageCount;
}

uint32_t C3D_getAtlasSizeBuckets(
    Renderer* renderer, N3DSRendererAtlasSizeBucket* outBuckets, uint32_t maxBuckets) {
    assert(renderer != NULL);
    if (outBuckets == NULL || maxBuckets == 0u) return 0u;

    N3DSRenderer* n3ds = (N3DSRenderer*)renderer;
    uint32_t bucketCount = 0u;
    for (uint32_t pageIndex = 0u; pageIndex < n3ds->atlas.pageCount; ++pageIndex) {
        const N3DSAtlasPageDesc* desc = &n3ds->atlas.pageDescs[pageIndex];
        uint32_t bucketIndex = bucketCount;
        for (uint32_t i = 0u; i < bucketCount; ++i) {
            if (outBuckets[i].width == desc->width && outBuckets[i].height == desc->height) {
                bucketIndex = i;
                break;
            }
        }
        if (bucketIndex == bucketCount) {
            if (bucketCount >= maxBuckets) continue;
            outBuckets[bucketIndex].width = desc->width;
            outBuckets[bucketIndex].height = desc->height;
            outBuckets[bucketIndex].totalPages = 0u;
            outBuckets[bucketIndex].residentPages = 0u;
            bucketCount++;
        }
        outBuckets[bucketIndex].totalPages++;
        if (n3ds->atlas.pages != NULL && n3ds->atlas.pages[pageIndex].active) {
            outBuckets[bucketIndex].residentPages++;
        }
    }

    return bucketCount;
}
