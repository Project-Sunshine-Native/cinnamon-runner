#include "3ds_menu_icon.h"
#include "../image/image_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t iconRead16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t iconRead32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t* iconDecodeBitmap(const uint8_t* data, uint32_t size, int width, int height) {
    if (size < 40) return NULL;
    uint32_t header = iconRead32(data);
    uint16_t bits = iconRead16(data + 14);
    if (header < 40 || header > size || iconRead32(data + 4) != (uint32_t)width ||
        iconRead32(data + 8) != (uint32_t)height * 2 || iconRead16(data + 12) != 1 ||
        iconRead32(data + 16) != 0 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32))
        return NULL;

    uint32_t colors = bits <= 8 ? iconRead32(data + 32) : 0;
    if (bits <= 8 && colors == 0) colors = 1u << bits;
    if (colors > 256 || colors > (size - header) / 4) return NULL;
    uint32_t offset = header + colors * 4;
    uint32_t stride = ((width * bits + 31) / 32) * 4;
    uint32_t maskStride = ((width + 31) / 32) * 4;
    uint32_t imageSize = stride * height;
    if (imageSize > size - offset) return NULL;
    const uint8_t* bitmap = data + offset;
    const uint8_t* mask = bitmap + imageSize;
    bool hasMask = maskStride * height <= size - offset - imageSize;
    if (bits != 32 && !hasMask) return NULL;
    uint8_t* pixels = malloc((size_t)width * height * 4);
    if (pixels == NULL) return NULL;
    bool hasAlpha = false;
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = bitmap + (height - 1 - y) * stride;
        for (int x = 0; x < width; ++x) {
            uint8_t* pixel = pixels + (y * width + x) * 4;
            pixel[3] = 255;
            if (bits <= 8) {
                unsigned index = bits == 8 ? row[x] : bits == 4
                    ? (row[x / 2] >> ((1 - x % 2) * 4)) & 15
                    : (row[x / 8] >> (7 - x % 8)) & 1;
                if (index >= colors) {
                    free(pixels);
                    return NULL;
                }
                const uint8_t* color = data + header + index * 4;
                pixel[0] = color[2];
                pixel[1] = color[1];
                pixel[2] = color[0];
            } else if (bits == 16) {
                uint16_t color = iconRead16(row + x * 2);
                pixel[0] = ((color >> 10) & 31) * 255 / 31;
                pixel[1] = ((color >> 5) & 31) * 255 / 31;
                pixel[2] = (color & 31) * 255 / 31;
            } else {
                const uint8_t* color = row + x * (bits / 8);
                pixel[0] = color[2];
                pixel[1] = color[1];
                pixel[2] = color[0];
                if (bits == 32) {
                    pixel[3] = color[3];
                    if (pixel[3] != 0) hasAlpha = true;
                }
            }
        }
    }
    // older 32 bit icons leave alpha empty i think
    if (!hasAlpha) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                bool transparent = hasMask &&
                    (mask[(height - 1 - y) * maskStride + x / 8] & (0x80 >> (x % 8)));
                pixels[(y * width + x) * 4 + 3] = transparent ? 0 : 255;
            }
        }
    }
    return pixels;
}

static bool iconUpload(N3DSMenuIcon* icon, const uint8_t* pixels, int width, int height) {
    int sourceWidth = width, sourceHeight = height;
    int longest = width > height ? width : height;
    if (longest > 128) {
        width = width * 128 / longest;
        height = height * 128 / longest;
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }
    int texWidth = 8, texHeight = 8;
    while (texWidth < width) texWidth *= 2;
    while (texHeight < height) texHeight *= 2;
    if (!C3D_TexInit(&icon->texture, texWidth, texHeight, GPU_RGBA8)) return false;
    memset(icon->texture.data, 0, icon->texture.size);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned morton = 0;
            for (unsigned bit = 0; bit < 3; ++bit) {
                morton |= ((x >> bit) & 1) << (bit * 2);
                morton |= ((y >> bit) & 1) << (bit * 2 + 1);
            }
            unsigned index = ((y / 8) * (texWidth / 8) + x / 8) * 64 + morton;
            int sourceX = x * sourceWidth / width;
            int sourceY = y * sourceHeight / height;
            const uint8_t* src = pixels + (sourceY * sourceWidth + sourceX) * 4;
            uint8_t* dst = (uint8_t*)icon->texture.data + index * 4;
            dst[0] = src[3];
            dst[1] = src[2];
            dst[2] = src[1];
            dst[3] = src[0];
        }
    }
    C3D_TexFlush(&icon->texture);
    C3D_TexSetFilter(&icon->texture, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&icon->texture, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    icon->subtex = (Tex3DS_SubTexture){
        .width = width, .height = height,
        .left = 0, .right = (float)width / texWidth,
        .top = (float)height / texHeight, .bottom = 0,
    };
    icon->loaded = true;
    return true;
}

void n3ds_menuIconFree(N3DSMenuIcon* icon) {
    if (icon->loaded) C3D_TexDelete(&icon->texture);
    memset(icon, 0, sizeof(*icon));
}

void n3ds_menuIconLoad(N3DSMenuIcon* icon, const char* folder) {
    n3ds_menuIconFree(icon);
    char path[1100];
    if (snprintf(path, sizeof(path), "%s/icon.ico", folder) >= (int)sizeof(path)) return;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return;
    uint8_t header[6];
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    long fileSize = ftell(file);
    if (fileSize < 6 || fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header) ||
        iconRead16(header) != 0 || iconRead16(header + 2) != 1)
        goto done;
    unsigned count = iconRead16(header + 4);
    if (count == 0 || count > (unsigned long)(fileSize - 6) / 16) goto done;
    // try larger images first, falling back when an entry uses an unsuported encoding
    uint64_t limit = UINT64_MAX;
    while (!icon->loaded) {
        uint64_t bestScore = 0;
        uint32_t bestOffset = 0, bestSize = 0;
        int width = 0, height = 0;
        for (unsigned i = 0; i < count; ++i) {
            uint8_t entry[16];
            if (fseek(file, 6 + i * 16, SEEK_SET) != 0 ||
                fread(entry, 1, sizeof(entry), file) != sizeof(entry)) goto done;
            int w = entry[0] == 0 ? 256 : entry[0];
            int h = entry[1] == 0 ? 256 : entry[1];
            unsigned area = w * h;
            uint64_t score = ((uint64_t)area << 32) |
                ((uint64_t)iconRead16(entry + 6) << 16) | i;
            uint32_t size = iconRead32(entry + 8), offset = iconRead32(entry + 12);
            if (score <= bestScore || score >= limit || size == 0 || size > 1024 * 1024 ||
                offset < 6 + count * 16 || offset > (unsigned long)fileSize ||
                size > (unsigned long)fileSize - offset) continue;
            bestScore = score;
            bestOffset = offset;
            bestSize = size;
            width = w;
            height = h;
        }
        if (bestScore == 0) break;
        limit = bestScore;
        uint8_t* data = malloc(bestSize);
        if (data == NULL) break;
        uint8_t* pixels = NULL;
        if (fseek(file, bestOffset, SEEK_SET) == 0 && fread(data, 1, bestSize, file) == bestSize) {
            if (bestSize >= 24 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
                if (data[16] == 0 && data[17] == 0 && data[20] == 0 && data[21] == 0 &&
                    ((int)data[18] * 256 + data[19]) == width &&
                    ((int)data[22] * 256 + data[23]) == height) {
                    int decodedW, decodedH;
                    pixels = ImageDecoder_decodeToRgba(data, bestSize, false, &decodedW, &decodedH);
                    if (pixels != NULL && (decodedW != width || decodedH != height)) {
                        free(pixels);
                        pixels = NULL;
                    }
                }
            } else pixels = iconDecodeBitmap(data, bestSize, width, height);
        }
        free(data);
        if (pixels != NULL) {
            iconUpload(icon, pixels, width, height);
            free(pixels);
        }
    }
done:
    fclose(file);
}
