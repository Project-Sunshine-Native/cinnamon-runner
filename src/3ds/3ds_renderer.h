#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../renderer.h"
#include "3ds_c3d_draw.h"
#include "3ds_settings.h"

Renderer* C3D_create(const N3DSRunnerSettings* settings);
bool C3D_drawTrueLavaWave(Renderer* renderer, int32_t tpagIndex, float srcLeft,
    float srcWidth, float xOffset, GMLReal baseY, const GMLReal waveX[20], float alpha);
bool C3D_drawRoundEdge(Renderer* renderer, int32_t tpagIndex, GMLReal imageX,
    GMLReal partWidth, GMLReal roomWidth, uint32_t color, float alpha);
void C3D_drawGlyphGlowRun(Renderer* renderer, const RendererGlyph* glyphs, int32_t count,
    const float alpha[3]);
void C3D_beginDrawFrame(Renderer* renderer);
void C3D_deferDrawFrame(Renderer* renderer);
void C3D_acquireDrawFrame(Renderer* renderer);
typedef struct {
    u64 frameWaitTicks;
    float previousGpuMs;
} N3DSFrameGpuTimings;
N3DSFrameGpuTimings C3D_getFrameGpuTimings(const Renderer* renderer);
void C3D_endDrawFrame(Renderer* renderer);
N3DSC3DVertex* C3D_beginVertices(
    Renderer* renderer, C3D_Tex* texture, uint32_t vertexCount);
void C3D_commitVertices(Renderer* renderer, uint32_t vertexCount);
void C3D_getViewRect(const Renderer* renderer, int32_t rect[4]);
bool C3D_getTileCullRect(const Renderer* renderer, float rect[4]);
bool C3D_drawPackedTilemap(Renderer* renderer, const RoomLayerTilesData* data,
    const Background* tileset, uint32_t firstX, uint32_t firstY, uint32_t endX,
    uint32_t endY, float offsetX, float offsetY);
bool C3D_roomTileVisible(const Renderer* renderer, const RoomTile* tile, float offsetX, float offsetY);

typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t totalPages;
    uint32_t residentPages;
} N3DSRendererAtlasSizeBucket;

bool C3D_isReady(Renderer* renderer);
const char* C3D_getStartupError(Renderer* renderer);
void C3D_beginBottomOverlay(
    Renderer* renderer, int32_t guiW, int32_t guiH, bool clearIfUnused);
int32_t C3D_getOverlayWidth(const Renderer* renderer);
void C3D_presentBottomSurface(Renderer* renderer);
void C3D_endBottomScreenGUI(Renderer* renderer);
void C3D_clearBottomScreen(Renderer* renderer);
void C3D_skipTopScreenPresent(Renderer* renderer);
void C3D_restoreTopScreenOutput(Renderer* renderer);
void C3D_freePendingSurfaces(Renderer* renderer);
bool C3D_bottomScreenUsed(Renderer* renderer);
uint32_t C3D_getAtlasVramBytes(Renderer* renderer);
uint32_t C3D_getAtlasPages(Renderer* renderer);
uint32_t C3D_getAtlasPageLimit(Renderer* renderer);
uint32_t C3D_getAtlasSizeBuckets(
    Renderer* renderer, N3DSRendererAtlasSizeBucket* outBuckets, uint32_t maxBuckets);
void C3D_syncRoom(Renderer* renderer, int32_t roomIndex);
