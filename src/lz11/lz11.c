#include "lz11.h"

#include <string.h>

static uint32_t LZ11_readLe32(const uint8_t* src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8u) | ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

bool LZ11_getDecompressedSize(const uint8_t* src, size_t srcSize, uint32_t* outSize) {
    if (src == NULL || srcSize < 4u || outSize == NULL || src[0] != 0x11u) {
        return false;
    }

    uint32_t size24 = (uint32_t)src[1] | ((uint32_t)src[2] << 8u) | ((uint32_t)src[3] << 16u);
    if (size24 != 0u) {
        *outSize = size24;
        return true;
    }

    if (srcSize < 8u) return false;
    *outSize = LZ11_readLe32(src + 4u);
    return *outSize != 0u;
}

static uint8_t* LZ11_copyMatch(uint8_t* out, size_t distance, size_t length) {
    const uint8_t* src = out - distance;

    if (distance == 1u) {
        memset(out, src[0], length);
        return out + length;
    }

    if (distance >= length) {
        memcpy(out, src, length);
        return out + length;
    }

    if (distance >= 16u) {
        size_t remaining = length;
        while (remaining >= 16u) {
            memcpy(out, src, 16u);
            out += 16u;
            src += 16u;
            remaining -= 16u;
        }
        if (remaining != 0u) memcpy(out, src, remaining);
        return out + remaining;
    }

    while (length-- != 0u)
        *out++ = *src++;
    return out;
}

bool LZ11_decompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize) {
    uint32_t declaredSize = 0u;
    if (src == NULL || dst == NULL || !LZ11_getDecompressedSize(src, srcSize, &declaredSize) ||
        (size_t)declaredSize != dstSize) {
        return false;
    }

    size_t srcPos = src[1] == 0u && src[2] == 0u && src[3] == 0u ? 8u : 4u;
    const uint8_t* srcEnd = src + srcSize;
    const uint8_t* in = src + srcPos;
    uint8_t* out = dst;
    uint8_t* dstEnd = dst + dstSize;
    while (out < dstEnd) {
        if (in >= srcEnd) return false;

        uint8_t flags = *in++;
        uint8_t mask = 0x80u;
        uint32_t remainingFlags = 8u;
        while (remainingFlags > 0u && out < dstEnd) {
            if ((flags & mask) == 0u) {
                size_t literalCount = 1u;
                while (literalCount < remainingFlags && out + literalCount < dstEnd &&
                       (flags & (mask >> literalCount)) == 0u) {
                    literalCount++;
                }
                if ((size_t)(srcEnd - in) < literalCount) return false;
                memcpy(out, in, literalCount);
                in += literalCount;
                out += literalCount;
                mask >>= literalCount;
                remainingFlags -= (uint32_t)literalCount;
                continue;
            }

            if (in >= srcEnd) return false;
            uint8_t b1 = *in++;
            uint32_t indicator = b1 >> 4u;
            uint32_t length = 0u;
            uint32_t disp = 0u;
            if (indicator == 0u) {
                if ((size_t)(srcEnd - in) < 2u) return false;
                uint8_t b2 = *in++;
                uint8_t b3 = *in++;
                length = (((uint32_t)(b1 & 0x0Fu) << 4u) | ((uint32_t)b2 >> 4u)) + 0x11u;
                disp = (((uint32_t)b2 & 0x0Fu) << 8u) | (uint32_t)b3;
            } else if (indicator == 1u) {
                if ((size_t)(srcEnd - in) < 3u) return false;
                uint8_t b2 = *in++;
                uint8_t b3 = *in++;
                uint8_t b4 = *in++;
                length = (((uint32_t)(b1 & 0x0Fu) << 12u) | ((uint32_t)b2 << 4u) |
                             ((uint32_t)b3 >> 4u)) +
                         0x111u;
                disp = (((uint32_t)b3 & 0x0Fu) << 8u) | (uint32_t)b4;
            } else {
                if (in >= srcEnd) return false;
                uint8_t b2 = *in++;
                length = indicator + 1u;
                disp = (((uint32_t)b1 & 0x0Fu) << 8u) | (uint32_t)b2;
            }

            size_t distance = (size_t)disp + 1u;
            size_t dstPos = (size_t)(out - dst);
            if (distance > dstPos || dstPos + length > dstSize) return false;
            out = LZ11_copyMatch(out, distance, length);
            mask >>= 1u;
            remainingFlags--;
        }
    }
    return true;
}
