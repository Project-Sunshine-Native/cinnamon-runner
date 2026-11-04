#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_SOUND_INSTANCE_ID_BASE 100000
#define AUDIO_STREAM_INDEX_BASE 300000

static inline bool Audio_isInstanceId(int32_t id) {
    return id >= AUDIO_SOUND_INSTANCE_ID_BASE && id < AUDIO_STREAM_INDEX_BASE;
}
