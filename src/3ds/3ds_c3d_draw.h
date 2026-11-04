#pragma once
#include "../matrix_math.h"
#include <3ds.h>
#include <citro3d.h>

typedef struct {
    float x;
    float y;
    float u;
    float v;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} N3DSC3DVertex;

typedef struct {
    float x;
    float y;
} N3DSVec2;

typedef struct {
    float u;
    float v;
} N3DSUV;

typedef struct {
    N3DSVec2 pos;
    N3DSUV uv;
} N3DSVertex;

typedef struct {
    DVLB_s* dvlb;
    shaderProgram_s shaderProg;
    C3D_AttrInfo attrInfo;
    C3D_BufInfo bufInfo;
    int uLocProjection;
    int uLocTexelBias;
    uint16_t texelBiasWidth;
    uint16_t texelBiasHeight;
    bool texelBiasNearest;
    C3D_Mtx projection;
    C3D_Tex whiteTex;
    bool whiteTexReady;
    N3DSC3DVertex* vertexBuffer;
    N3DSC3DVertex* vertices;
    uint16_t* quadIndexBuffer;
    uint32_t vertexCapacity;
    uint32_t quadCapacity;
    uint32_t streamVertexCapacity;
    uint32_t streamVertexOffset;
    uint32_t submitVertexOffset;
    uint32_t vertexCount;
    C3D_Tex* currentTexture;
    C3D_Tex* boundTexture;
    C3D_Tex boundTextureState;
    bool blendEnabled;
    GPU_BLENDEQUATION blendColorEq;
    GPU_BLENDEQUATION blendAlphaEq;
    GPU_BLENDFACTOR blendSrcColor;
    GPU_BLENDFACTOR blendDstColor;
    GPU_BLENDFACTOR blendSrcAlpha;
    GPU_BLENDFACTOR blendDstAlpha;
    bool alphaTestEnabled;
    GPU_TESTFUNC alphaTestFunc;
    int alphaTestRef;
    uint8_t colorMask;
    bool batchIndexedQuads;
    bool texFilter;
    bool batchTexFilter;
    bool bufferDirty;
    bool stateDirty;
    bool projectionDirty;
    bool frameActive;
} N3DSC3DDrawContext;

bool n3ds_c3dInit(N3DSC3DDrawContext* ctx, uint32_t maxQuads);
void n3ds_c3dDestroy(N3DSC3DDrawContext* ctx);
void n3ds_c3dBeginFrame(N3DSC3DDrawContext* ctx);
void n3ds_c3dEndFrame(N3DSC3DDrawContext* ctx);
void n3ds_c3dUpdateProjection(N3DSC3DDrawContext* ctx, float width, float height);
void n3ds_c3dSetSurfaceProj(N3DSC3DDrawContext* ctx, float width, float height);
void n3ds_c3dSetProjectionC3D(N3DSC3DDrawContext* ctx, const C3D_Mtx* matrix);
void n3ds_c3dSetProjectionMatrix(N3DSC3DDrawContext* ctx, const Matrix4f* matrix);
void n3ds_c3dSetBlend(N3DSC3DDrawContext* ctx, bool enabled, GPU_BLENDEQUATION colorEq,
    GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcColor, GPU_BLENDFACTOR dstColor,
    GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha);
void n3ds_c3dSetAlphaTest(N3DSC3DDrawContext* ctx, bool enabled, GPU_TESTFUNC func, int ref);
void n3ds_c3dSetTexFilter(N3DSC3DDrawContext* ctx, bool linear);
void n3ds_c3dSetColorMask(N3DSC3DDrawContext* ctx, bool red, bool green, bool blue, bool alpha);
void n3ds_c3dFlush(N3DSC3DDrawContext* ctx);
void n3ds_c3dInvalidateTexture(N3DSC3DDrawContext* ctx, C3D_Tex* tex);
void n3ds_c3dFrameSplit(N3DSC3DDrawContext* ctx);
N3DSC3DVertex* n3ds_c3dBeginVertices(
    N3DSC3DDrawContext* ctx, C3D_Tex* tex, uint32_t vertexCount);
void n3ds_c3dCommitVertices(N3DSC3DDrawContext* ctx, uint32_t vertexCount);
void n3ds_c3dQuad(N3DSC3DDrawContext* ctx, C3D_Tex* tex,
    const N3DSVertex vertices[4], uint32_t color);
void n3ds_c3dQuadGradientUV(N3DSC3DDrawContext* ctx, C3D_Tex* tex,
    const N3DSVertex vertices[4], const uint32_t colors[4]);
void n3ds_c3dLineQuad(N3DSC3DDrawContext* ctx, const N3DSVec2 quad[4],
    uint32_t color1, uint32_t color2);
void n3ds_c3dTriangle(N3DSC3DDrawContext* ctx, C3D_Tex* tex,
    const N3DSVertex vertices[3], const uint32_t colors[3]);
void n3ds_c3dSolidRect(N3DSC3DDrawContext* ctx, N3DSVec2 pos, N3DSVec2 size, uint32_t color);
uint32_t n3ds_c3dClearColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
