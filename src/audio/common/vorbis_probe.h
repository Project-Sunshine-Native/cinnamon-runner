#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool Audio_readVorbisStreamInfo(
    const char* path, size_t fileBufferSize, uint32_t* rate, uint32_t* samples, uint8_t* channels);
