#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool LZ11_getDecompressedSize(const uint8_t* src, size_t srcSize, uint32_t* outSize);

bool LZ11_decompress(const uint8_t* src, size_t srcSize, uint8_t* dst, size_t dstSize);
