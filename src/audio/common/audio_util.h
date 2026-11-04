#pragma once

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

static inline uint16_t Audio_readLe16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t Audio_readLe32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline float Audio_sanitizeGain(float gain) {
    if (!isfinite(gain)) return 1.0f;
    if (gain < 0.0f) return 0.0f;
    if (gain > 1.0f) return 1.0f;
    return gain;
}

static inline float Audio_sanitizePitch(float pitch) {
    if (!isfinite(pitch) || pitch <= 0.0f) return 1.0f;
    if (pitch < 0.0625f) return 0.0625f;
    if (pitch > 4.0f) return 4.0f;
    return pitch;
}

static inline const char* Audio_pathFileName(const char* path) {
    if (path == NULL) return NULL;
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    const char* separator = slash;
    if (backslash != NULL && (separator == NULL || backslash > separator)) separator = backslash;
    return separator != NULL ? separator + 1 : path;
}

static inline bool Audio_pathHasExtension(const char* path, const char* extension) {
    const char* name = Audio_pathFileName(path);
    const char* dot = name != NULL ? strrchr(name, '.') : NULL;
    if (dot == NULL) return false;
    while (*dot != '\0' && *extension != '\0') {
        if (tolower((unsigned char)*dot++) != tolower((unsigned char)*extension++)) return false;
    }
    return *dot == '\0' && *extension == '\0';
}
