#include "3ds_c3d_draw.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t n3ds_2d_shbin[];
extern const size_t n3ds_2d_shbin_size;

static void n3ds_c3dUpdateWritePtr(N3DSC3DDrawContext* ctx) {
    if (ctx->vertexBuffer == NULL) return;
    ctx->vertices = ctx->vertexBuffer + ctx->streamVertexOffset;
}

static void n3ds_c3dResetBatch(N3DSC3DDrawContext* ctx) {
    ctx->vertexCount = 0;
    ctx->currentTexture = NULL;
    n3ds_c3dUpdateWritePtr(ctx);
}

static void n3ds_c3dFlushSubmitRange(N3DSC3DDrawContext* ctx) {
    if (ctx == NULL || ctx->vertexBuffer == NULL ||
        ctx->streamVertexOffset <= ctx->submitVertexOffset)
        return;
    N3DSC3DVertex* start = ctx->vertexBuffer + ctx->submitVertexOffset;
    uint32_t count = ctx->streamVertexOffset - ctx->submitVertexOffset;
    GSPGPU_FlushDataCache(start, (u32)(count * sizeof(N3DSC3DVertex)));
    ctx->submitVertexOffset = ctx->streamVertexOffset;
}

static void n3ds_c3dBindBuffer(N3DSC3DDrawContext* ctx) {
    C3D_SetBufInfo(&ctx->bufInfo);
    ctx->bufferDirty = false;
}

static void n3ds_c3dBindState(N3DSC3DDrawContext* ctx) {
    C3D_BindProgram(&ctx->shaderProg);
    C3D_SetAttrInfo(&ctx->attrInfo);
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    if (ctx->blendEnabled) {
        C3D_AlphaBlend(ctx->blendColorEq, ctx->blendAlphaEq, ctx->blendSrcColor, ctx->blendDstColor,
            ctx->blendSrcAlpha, ctx->blendDstAlpha);
    } else {
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }
    C3D_AlphaTest(ctx->alphaTestEnabled, ctx->alphaTestFunc, ctx->alphaTestRef);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0xFF);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_FrameBuf* frameBuf = C3D_GetFrameBuf();
    if (frameBuf != NULL) frameBuf->colorMask = ctx->colorMask;
    ctx->stateDirty = false;
}

static bool n3ds_c3dWrapStream(N3DSC3DDrawContext* ctx) {
    if (ctx->finishDraws == NULL) return false;
    ctx->finishDraws(ctx->finishDrawsUser);
    ctx->streamVertexOffset = 0;
    ctx->submitVertexOffset = 0;
    n3ds_c3dUpdateWritePtr(ctx);
    return true;
}

static bool n3ds_c3dReserve(
    N3DSC3DDrawContext* ctx, C3D_Tex* tex, uint32_t vertexCount, bool indexedQuads,
    bool linear) {
    if (ctx == NULL) return false;
    uint32_t batchVertexCapacity = indexedQuads ? ctx->quadCapacity * 4u : ctx->vertexCapacity;
    if (vertexCount > batchVertexCapacity) return false;
    if (tex == NULL) tex = &ctx->whiteTex;
    if (ctx->currentTexture != NULL && ctx->currentTexture != tex) {
        n3ds_c3dFlush(ctx);
    }
    if (ctx->vertexCount > 0u &&
        (ctx->batchIndexedQuads != indexedQuads || ctx->batchTexFilter != linear))
        n3ds_c3dFlush(ctx);
    if (indexedQuads && ctx->vertexCount == 0u && (ctx->streamVertexOffset & 3u) != 0u) {
        uint32_t alignedOffset = (ctx->streamVertexOffset + 3u) & ~3u;
        if (alignedOffset >= ctx->streamVertexCapacity) {
            if (!n3ds_c3dWrapStream(ctx)) return false;
        } else {
            ctx->streamVertexOffset = alignedOffset;
            n3ds_c3dUpdateWritePtr(ctx);
        }
    }
    if (ctx->streamVertexOffset + ctx->vertexCount + vertexCount > ctx->streamVertexCapacity) {
        n3ds_c3dFlush(ctx);
        if (ctx->streamVertexOffset + vertexCount > ctx->streamVertexCapacity) {
            if (!n3ds_c3dWrapStream(ctx)) return false;
        }
    }
    if (ctx->vertexCount + vertexCount > batchVertexCapacity) {
        n3ds_c3dFlush(ctx);
    }
    ctx->currentTexture = tex;
    ctx->batchIndexedQuads = indexedQuads;
    ctx->batchTexFilter = linear;
    return ctx->vertexCount + vertexCount <= batchVertexCapacity;
}

static inline N3DSC3DVertex* n3ds_c3dAllocQuad(N3DSC3DDrawContext* ctx, C3D_Tex* tex) {
    if (ctx != NULL && ctx->currentTexture == tex && ctx->batchIndexedQuads &&
        ctx->batchTexFilter == ctx->texFilter &&
        ctx->vertexCount != 0u && ctx->vertexCount + 4u <= ctx->quadCapacity * 4u &&
        ctx->streamVertexOffset + ctx->vertexCount + 4u <= ctx->streamVertexCapacity)
        return &ctx->vertices[ctx->vertexCount];
    if (!n3ds_c3dReserve(ctx, tex, 4u, true, ctx != NULL && ctx->texFilter)) return NULL;
    return &ctx->vertices[ctx->vertexCount];
}

static inline void n3ds_c3dWriteVertex(
    N3DSC3DVertex* out, const N3DSVertex* vertex, uint32_t color) {
    out->x = vertex->pos.x;
    out->y = vertex->pos.y;
    out->u = vertex->uv.u;
    out->v = vertex->uv.v;
    out->r = (uint8_t)(color & 0xFFu);
    out->g = (uint8_t)((color >> 8) & 0xFFu);
    out->b = (uint8_t)((color >> 16) & 0xFFu);
    out->a = (uint8_t)((color >> 24) & 0xFFu);
}

bool n3ds_c3dInit(N3DSC3DDrawContext* ctx, uint32_t maxQuads) {
    if (ctx == NULL || maxQuads == 0u || maxQuads > (UINT16_MAX + 1u) / 6u ||
        (size_t)maxQuads > SIZE_MAX / (16u * sizeof(N3DSC3DVertex)) ||
        (size_t)maxQuads > SIZE_MAX / (24u * sizeof(uint16_t))) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));

    ctx->dvlb = DVLB_ParseFile((u32*)n3ds_2d_shbin, n3ds_2d_shbin_size);
    if (ctx->dvlb == NULL) return false;
    if (R_FAILED(shaderProgramInit(&ctx->shaderProg))) {
        DVLB_Free(ctx->dvlb);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }
    if (R_FAILED(shaderProgramSetVsh(&ctx->shaderProg, &ctx->dvlb->DVLE[0]))) {
        shaderProgramFree(&ctx->shaderProg);
        DVLB_Free(ctx->dvlb);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }
    ctx->uLocProjection =
        shaderInstanceGetUniformLocation(ctx->shaderProg.vertexShader, "uProjMtx");
    ctx->uLocTexelBias =
        shaderInstanceGetUniformLocation(ctx->shaderProg.vertexShader, "uTexelBias");

    AttrInfo_Init(&ctx->attrInfo);
    AttrInfo_AddLoader(&ctx->attrInfo, 0, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&ctx->attrInfo, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(&ctx->attrInfo, 2, GPU_UNSIGNED_BYTE, 4);
    BufInfo_Init(&ctx->bufInfo);

    ctx->vertexCapacity = maxQuads * 6u;
    ctx->quadCapacity = maxQuads;
    ctx->streamVertexCapacity = maxQuads * 16u;
    if (ctx->streamVertexCapacity > UINT16_MAX + 1u)
        ctx->streamVertexCapacity = UINT16_MAX + 1u;
    ctx->vertexBuffer = linearAlloc(sizeof(N3DSC3DVertex) * ctx->streamVertexCapacity);
    if (ctx->vertexBuffer == NULL) {
        shaderProgramFree(&ctx->shaderProg);
        DVLB_Free(ctx->dvlb);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }
    ctx->vertices = ctx->vertexBuffer;
    BufInfo_Add(&ctx->bufInfo, ctx->vertexBuffer, sizeof(N3DSC3DVertex), 3, 0x210);
    uint32_t streamQuadCapacity = ctx->streamVertexCapacity / 4u;
    uint32_t quadIndexCount = streamQuadCapacity * 6u;
    uint32_t totalIndexCount = quadIndexCount + ctx->streamVertexCapacity;
    ctx->quadIndexBuffer = linearAlloc(sizeof(uint16_t) * totalIndexCount);
    if (ctx->quadIndexBuffer == NULL) {
        linearFree(ctx->vertexBuffer);
        shaderProgramFree(&ctx->shaderProg);
        DVLB_Free(ctx->dvlb);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }
    for (uint32_t i = 0; i < streamQuadCapacity; ++i) {
        uint16_t base = (uint16_t)(i * 4u);
        uint16_t* indices = &ctx->quadIndexBuffer[i * 6u];
        indices[0] = base;
        indices[1] = base + 1u;
        indices[2] = base + 3u;
        indices[3] = base;
        indices[4] = base + 3u;
        indices[5] = base + 2u;
    }
    ctx->triangleIndexBuffer = ctx->quadIndexBuffer + quadIndexCount;
    for (uint32_t i = 0; i < ctx->streamVertexCapacity; ++i)
        ctx->triangleIndexBuffer[i] = (uint16_t)i;
    GSPGPU_FlushDataCache(ctx->quadIndexBuffer, (u32)(sizeof(uint16_t) * totalIndexCount));

    if (!C3D_TexInit(&ctx->whiteTex, 8, 8, GPU_RGBA8)) {
        linearFree(ctx->quadIndexBuffer);
        linearFree(ctx->vertexBuffer);
        shaderProgramFree(&ctx->shaderProg);
        DVLB_Free(ctx->dvlb);
        memset(ctx, 0, sizeof(*ctx));
        return false;
    }
    uint32_t whitePixels[64];
    for (uint32_t i = 0; i < 64u; ++i)
        whitePixels[i] = 0xFFFFFFFFu;
    C3D_TexUpload(&ctx->whiteTex, whitePixels);
    C3D_TexSetFilter(&ctx->whiteTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&ctx->whiteTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    ctx->whiteTexReady = true;

    ctx->blendEnabled = true;
    ctx->blendColorEq = GPU_BLEND_ADD;
    ctx->blendAlphaEq = GPU_BLEND_ADD;
    ctx->blendSrcColor = GPU_SRC_ALPHA;
    ctx->blendDstColor = GPU_ONE_MINUS_SRC_ALPHA;
    ctx->blendSrcAlpha = GPU_ONE;
    ctx->blendDstAlpha = GPU_ONE_MINUS_SRC_ALPHA;
    ctx->alphaTestEnabled = true;
    ctx->alphaTestFunc = GPU_GREATER;
    ctx->alphaTestRef = 0;
    ctx->colorMask = 0xFu;
    ctx->bufferDirty = true;
    ctx->stateDirty = true;
    ctx->projectionDirty = true;
    ctx->currentTexture = &ctx->whiteTex;
    n3ds_c3dUpdateProjection(ctx, 400.0f, 240.0f);
    return true;
}

void n3ds_c3dBeginFrame(N3DSC3DDrawContext* ctx) {
    if (ctx == NULL) return;
    ctx->frameActive = true;
    ctx->vertexCount = 0;
    ctx->streamVertexOffset = 0;
    ctx->submitVertexOffset = 0;
    ctx->currentTexture = NULL;
    ctx->boundTexture = NULL;
    ctx->bufferDirty = true;
    ctx->stateDirty = true;
    ctx->projectionDirty = true;
    n3ds_c3dUpdateWritePtr(ctx);
}

void n3ds_c3dEndFrame(N3DSC3DDrawContext* ctx) {
    n3ds_c3dFlush(ctx);
    n3ds_c3dFlushSubmitRange(ctx);
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    ctx->frameActive = false;
}

void n3ds_c3dDestroy(N3DSC3DDrawContext* ctx) {
    if (ctx == NULL) return;
    n3ds_c3dFlush(ctx);
    if (ctx->whiteTexReady) C3D_TexDelete(&ctx->whiteTex);
    if (ctx->quadIndexBuffer != NULL) linearFree(ctx->quadIndexBuffer);
    if (ctx->vertexBuffer != NULL) linearFree(ctx->vertexBuffer);
    shaderProgramFree(&ctx->shaderProg);
    if (ctx->dvlb != NULL) DVLB_Free(ctx->dvlb);
    memset(ctx, 0, sizeof(*ctx));
}

void n3ds_c3dUpdateProjection(N3DSC3DDrawContext* ctx, float width, float height) {
    if (ctx == NULL) return;
    C3D_Mtx projection;
    Mtx_OrthoTilt(&projection, 0.0f, width, height, 0.0f, 0.0f, 1.0f, true);
    if (memcmp(&ctx->projection, &projection, sizeof(projection)) == 0) return;
    n3ds_c3dFlush(ctx);
    ctx->projection = projection;
    ctx->projectionDirty = true;
}

void n3ds_c3dSetSurfaceProj(N3DSC3DDrawContext* ctx, float width, float height) {
    if (ctx == NULL) return;
    C3D_Mtx projection;
    Mtx_Ortho(&projection, 0.0f, width, height, 0.0f, 0.0f, 1.0f, true);
    if (memcmp(&ctx->projection, &projection, sizeof(projection)) == 0) return;
    n3ds_c3dFlush(ctx);
    ctx->projection = projection;
    ctx->projectionDirty = true;
}

void n3ds_c3dSetProjectionC3D(N3DSC3DDrawContext* ctx, const C3D_Mtx* matrix) {
    if (ctx == NULL || matrix == NULL) return;
    if (memcmp(&ctx->projection, matrix, sizeof(ctx->projection)) == 0) return;
    n3ds_c3dFlush(ctx);
    memcpy(&ctx->projection, matrix, sizeof(ctx->projection));
    ctx->projectionDirty = true;
}

void n3ds_c3dSetProjectionMatrix(N3DSC3DDrawContext* ctx, const Matrix4f* matrix) {
    if (ctx == NULL || matrix == NULL) return;
    C3D_Mtx projection;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            projection.m[row * 4 + col] = matrix->m[col * 4 + row];
        }
    }
    if (memcmp(&ctx->projection, &projection, sizeof(projection)) == 0) return;
    n3ds_c3dFlush(ctx);
    ctx->projection = projection;
    ctx->projectionDirty = true;
}

void n3ds_c3dSetBlend(N3DSC3DDrawContext* ctx, bool enabled, GPU_BLENDEQUATION colorEq,
    GPU_BLENDEQUATION alphaEq, GPU_BLENDFACTOR srcColor, GPU_BLENDFACTOR dstColor,
    GPU_BLENDFACTOR srcAlpha, GPU_BLENDFACTOR dstAlpha) {
    if (ctx == NULL) return;
    if (ctx->blendEnabled == enabled && ctx->blendColorEq == colorEq &&
        ctx->blendAlphaEq == alphaEq && ctx->blendSrcColor == srcColor &&
        ctx->blendDstColor == dstColor && ctx->blendSrcAlpha == srcAlpha &&
        ctx->blendDstAlpha == dstAlpha) {
        return;
    }
    n3ds_c3dFlush(ctx);
    ctx->blendEnabled = enabled;
    ctx->blendColorEq = colorEq;
    ctx->blendAlphaEq = alphaEq;
    ctx->blendSrcColor = srcColor;
    ctx->blendDstColor = dstColor;
    ctx->blendSrcAlpha = srcAlpha;
    ctx->blendDstAlpha = dstAlpha;
    ctx->stateDirty = true;
}

void n3ds_c3dSetAlphaTest(N3DSC3DDrawContext* ctx, bool enabled, GPU_TESTFUNC func, int ref) {
    if (ctx == NULL) return;
    if (ctx->alphaTestEnabled == enabled && ctx->alphaTestFunc == func &&
        ctx->alphaTestRef == ref) {
        return;
    }
    n3ds_c3dFlush(ctx);
    ctx->alphaTestEnabled = enabled;
    ctx->alphaTestFunc = func;
    ctx->alphaTestRef = ref;
    ctx->stateDirty = true;
}

void n3ds_c3dSetColorMask(
    N3DSC3DDrawContext* ctx, bool red, bool green, bool blue, bool alpha) {
    if (ctx == NULL) return;
    uint8_t colorMask = (red ? 1u : 0u) | (green ? 2u : 0u) | (blue ? 4u : 0u) | (alpha ? 8u : 0u);
    if (ctx->colorMask == colorMask) return;
    n3ds_c3dFlush(ctx);
    ctx->colorMask = colorMask;
    ctx->stateDirty = true;
}

void n3ds_c3dSetTexFilter(N3DSC3DDrawContext* ctx, bool linear) {
    if (ctx != NULL) ctx->texFilter = linear;
}

void n3ds_c3dFlush(N3DSC3DDrawContext* ctx) {
    if (ctx == NULL || ctx->vertexCount == 0) return;
    if (ctx->currentTexture == NULL) ctx->currentTexture = &ctx->whiteTex;
    uint32_t drawVertexCount = ctx->vertexCount;
    uint32_t commandSize, commandOffset;
    GPUCMD_GetBuffer(NULL, &commandSize, &commandOffset);
    if (ctx->finishDraws != NULL && commandSize - commandOffset < 0x1000u) {
        ctx->vertexCount = 0;
        ctx->finishDraws(ctx->finishDrawsUser);
        ctx->vertexCount = drawVertexCount;
    }
    uint32_t firstVertex = ctx->streamVertexOffset;
    bool needStateUpload = ctx->stateDirty;
    bool needProjectionUpload = needStateUpload || ctx->projectionDirty;
    if (ctx->stateDirty) n3ds_c3dBindState(ctx);
    if (ctx->bufferDirty) n3ds_c3dBindBuffer(ctx);
    if (needProjectionUpload) {
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, ctx->uLocProjection, &ctx->projection);
        ctx->projectionDirty = false;
    }
    C3D_TexSetFilter(ctx->currentTexture,
        ctx->batchTexFilter ? GPU_LINEAR : GPU_NEAREST,
        ctx->batchTexFilter ? GPU_LINEAR : GPU_NEAREST);
    const C3D_Tex* tex = ctx->currentTexture;
    if (ctx->boundTexture != tex || ctx->boundTextureState.data != tex->data ||
        ctx->boundTextureState.dim != tex->dim || ctx->boundTextureState.param != tex->param ||
        ctx->boundTextureState.border != tex->border ||
        ctx->boundTextureState.lodParam != tex->lodParam || ctx->boundTextureState.fmt != tex->fmt) {
        C3D_TexBind(0, ctx->currentTexture);
        ctx->boundTexture = ctx->currentTexture;
        ctx->boundTextureState = *tex;
    }
    bool nearest = !ctx->batchTexFilter;
    if (needStateUpload || ctx->texelBiasWidth != tex->width ||
        ctx->texelBiasHeight != tex->height || ctx->texelBiasNearest != nearest) {
        float biasU = nearest && tex->width > 0u ? -(1.0f / 16.0f) / (float)tex->width : 0.0f;
        float biasV = nearest && tex->height > 0u ? (1.0f / 16.0f) / (float)tex->height : 0.0f;
        C3D_FVUnifSet(GPU_VERTEX_SHADER, ctx->uLocTexelBias, biasU, biasV, 0.0f, 0.0f);
        ctx->texelBiasWidth = tex->width;
        ctx->texelBiasHeight = tex->height;
        ctx->texelBiasNearest = nearest;
    }
    if (ctx->batchIndexedQuads) {
        uint32_t indexCount = drawVertexCount / 4u * 6u;
        uint32_t firstIndex = firstVertex / 4u * 6u;
        C3D_DrawElements(GPU_TRIANGLES, (int)indexCount, C3D_UNSIGNED_SHORT,
            ctx->quadIndexBuffer + firstIndex);
    } else {
        C3D_DrawElements(GPU_TRIANGLES, (int)drawVertexCount, C3D_UNSIGNED_SHORT,
            ctx->triangleIndexBuffer + firstVertex);
    }
    ctx->streamVertexOffset += drawVertexCount;
    n3ds_c3dResetBatch(ctx);
}

void n3ds_c3dInvalidateTexture(N3DSC3DDrawContext* ctx, C3D_Tex* tex) {
    if (ctx == NULL || tex == NULL) return;
    if (ctx->currentTexture == tex) {
        n3ds_c3dFlush(ctx);
        ctx->currentTexture = NULL;
    }
    if (ctx->boundTexture == tex) ctx->boundTexture = NULL;
}

void n3ds_c3dFrameSplit(N3DSC3DDrawContext* ctx) {
    if (ctx != NULL) {
        n3ds_c3dFlushSubmitRange(ctx);
        ctx->boundTexture = NULL;
        ctx->bufferDirty = true;
        ctx->stateDirty = true;
        ctx->projectionDirty = true;
    }
    C3D_FrameSplit(GX_CMDLIST_FLUSH);
}

N3DSC3DVertex* n3ds_c3dBeginVertices(
    N3DSC3DDrawContext* ctx, C3D_Tex* tex, uint32_t vertexCount) {
    if (vertexCount == 0u) return NULL;
    bool linear = tex != NULL &&
        (tex->param & (GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) |
            GPU_TEXTURE_MIN_FILTER(GPU_LINEAR))) != 0u;
    if (!n3ds_c3dReserve(ctx, tex, vertexCount, false, linear)) return NULL;
    return &ctx->vertices[ctx->vertexCount];
}

void n3ds_c3dCommitVertices(N3DSC3DDrawContext* ctx, uint32_t vertexCount) {
    if (ctx == NULL || vertexCount == 0u) return;
    ctx->vertexCount += vertexCount;
}

void n3ds_c3dQuad(N3DSC3DDrawContext* ctx, C3D_Tex* tex,
    const N3DSVertex vertices[4], uint32_t color) {
    N3DSC3DVertex* out = n3ds_c3dAllocQuad(ctx, tex);
    if (out == NULL) return;
    n3ds_c3dWriteVertex(&out[0], &vertices[0], color);
    n3ds_c3dWriteVertex(&out[1], &vertices[1], color);
    n3ds_c3dWriteVertex(&out[2], &vertices[2], color);
    n3ds_c3dWriteVertex(&out[3], &vertices[3], color);
    ctx->vertexCount += 4u;
}

void n3ds_c3dQuadGradientUV(N3DSC3DDrawContext* ctx, C3D_Tex* tex,
    const N3DSVertex vertices[4], const uint32_t colors[4]) {
    N3DSC3DVertex* out = n3ds_c3dAllocQuad(ctx, tex);
    if (out == NULL) return;
    n3ds_c3dWriteVertex(&out[0], &vertices[0], colors[0]);
    n3ds_c3dWriteVertex(&out[1], &vertices[1], colors[1]);
    n3ds_c3dWriteVertex(&out[2], &vertices[2], colors[2]);
    n3ds_c3dWriteVertex(&out[3], &vertices[3], colors[3]);
    ctx->vertexCount += 4u;
}

void n3ds_c3dLineQuad(N3DSC3DDrawContext* ctx, const N3DSVec2 quad[4],
    uint32_t color1, uint32_t color2) {
    if (ctx == NULL) return;
    N3DSC3DVertex* out = n3ds_c3dAllocQuad(ctx, &ctx->whiteTex);
    if (out == NULL) return;
    for (uint32_t i = 0u; i < 4u; i++) {
        uint32_t color = i & 1u ? color2 : color1;
        out[i].x = quad[i].x;
        out[i].y = quad[i].y;
        out[i].u = 0.0f;
        out[i].v = 0.0f;
        out[i].r = (uint8_t)(color & 0xFFu);
        out[i].g = (uint8_t)((color >> 8) & 0xFFu);
        out[i].b = (uint8_t)((color >> 16) & 0xFFu);
        out[i].a = (uint8_t)((color >> 24) & 0xFFu);
    }
    ctx->vertexCount += 4u;
}

void n3ds_c3dTriangle(N3DSC3DDrawContext* ctx, C3D_Tex* tex,
    const N3DSVertex vertices[3], const uint32_t colors[3]) {
    if (!n3ds_c3dReserve(ctx, tex, 3u, false, ctx != NULL && ctx->texFilter)) return;
    N3DSC3DVertex* out = &ctx->vertices[ctx->vertexCount];
    n3ds_c3dWriteVertex(&out[0], &vertices[0], colors[0]);
    n3ds_c3dWriteVertex(&out[1], &vertices[1], colors[1]);
    n3ds_c3dWriteVertex(&out[2], &vertices[2], colors[2]);
    ctx->vertexCount += 3u;
}

void n3ds_c3dSolidRect(
    N3DSC3DDrawContext* ctx, N3DSVec2 pos, N3DSVec2 size, uint32_t color) {
    const N3DSVertex vertices[4] = {
        {{pos.x, pos.y}, {0.0f, 1.0f}},
        {{pos.x + size.x, pos.y}, {1.0f, 1.0f}},
        {{pos.x, pos.y + size.y}, {0.0f, 0.0f}},
        {{pos.x + size.x, pos.y + size.y}, {1.0f, 0.0f}},
    };
    n3ds_c3dQuad(ctx, &ctx->whiteTex, vertices, color);
}

uint32_t n3ds_c3dClearColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}
