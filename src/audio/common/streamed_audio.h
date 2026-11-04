#ifndef STREAMED_AUDIO_H
#define STREAMED_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define STREAMED_BANK_MAGIC 0x314D5453u
#define STREAMED_BANK_VERSION 1u
#define STREAMED_BANK_DSP 0x0800u
#define STREAMED_BANK_VORBIS 0x2000u
#define STREAMED_BANK_EXTERNAL_FILE 0x4000u

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint32_t flags;
    char* externalPath;
} StreamedBankEntry;

typedef struct {
    char* path;
    StreamedBankEntry* entries;
    uint32_t count;
} StreamedBank;

typedef struct StreamedAudio StreamedAudio;

bool StreamedBank_load(StreamedBank* bank, const char* path, uint32_t soundCount);
void StreamedBank_free(StreamedBank* bank);
const StreamedBankEntry* StreamedBank_find(const StreamedBank* bank, int32_t soundIndex);
StreamedAudio* StreamedAudio_open(const StreamedBank* bank, const StreamedBankEntry* entry);
void StreamedAudio_close(StreamedAudio* stream);

int32_t StreamedAudio_read(StreamedAudio* stream, int16_t* out, uint32_t frames);
bool StreamedAudio_seek(StreamedAudio* stream, uint32_t sample);
uint32_t StreamedAudio_tell(const StreamedAudio* stream);
uint32_t StreamedAudio_sampleCount(const StreamedAudio* stream);

#endif
