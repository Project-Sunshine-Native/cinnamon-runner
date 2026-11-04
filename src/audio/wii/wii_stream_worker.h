#pragma once

#include "../common/streamed_audio.h"

typedef struct WiiStreamWorker WiiStreamWorker;
typedef struct WiiStreamJob WiiStreamJob;

WiiStreamWorker* WiiStream_create(void);
void WiiStream_destroy(WiiStreamWorker* worker);
void WiiStream_wake(WiiStreamWorker* worker);

WiiStreamJob* WiiStream_open(WiiStreamWorker* worker, const StreamedBank* bank,
    const StreamedBankEntry* entry, const char* path, uint32_t sampleRate,
    uint32_t sampleCount, uint8_t channels, bool dspAdpcm, bool loop);
void WiiJob_release(WiiStreamJob* job);
void WiiJob_seek(WiiStreamJob* job, uint32_t sample);

int32_t WiiJob_read(WiiStreamJob* job, uint32_t sample, int16_t* pcm,
    uint32_t maxFrames, uint32_t* sampleCount);
