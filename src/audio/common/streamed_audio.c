#include "streamed_audio.h"
#include "sound_bank.h"
#include "audio_util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbisfile.h>

#define WDSP_BUFFER_BYTES 4096u

typedef struct {
    FILE* file;
    uint32_t offset;
    uint32_t size;
    uint32_t position;
} StreamedSlice;

typedef struct {
    uint32_t offset;
    uint32_t size;
    int16_t coefs[16];
    int16_t startHist1;
    int16_t startHist2;
    int16_t hist1;
    int16_t hist2;
    uint32_t bufferOffset;
    uint32_t bufferSize;
    uint8_t buffer[WDSP_BUFFER_BYTES];
    int16_t samples[14];
} StreamedDspChannel;

struct StreamedAudio {
    StreamedSlice slice;
    StreamedBankEntry entry;
    uint32_t sample;
    bool vorbisOpen;
    OggVorbis_File vorbis;
    StreamedDspChannel channels[2];
};

void StreamedBank_free(StreamedBank* bank) {
    if (bank == NULL) return;
    for (uint32_t i = 0u; i < bank->count; ++i)
        free(bank->entries[i].externalPath);
    free(bank->path);
    free(bank->entries);
    memset(bank, 0, sizeof(*bank));
}

bool StreamedBank_load(StreamedBank* bank, const char* path, uint32_t soundCount) {
    if (bank == NULL || path == NULL) return false;

    StreamedBank loaded = {0};
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    bool ok = false;
    uint8_t header[16];
    if (fseek(file, 0, SEEK_END) != 0) goto cleanup;
    long fileSize = ftell(file);
    if (fileSize < 16 || (uint64_t)fileSize > UINT32_MAX) goto cleanup;
    rewind(file);
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) goto cleanup;
    uint32_t count = Audio_readLe32(header + 8u);
    uint32_t dataOffset = Audio_readLe32(header + 12u);
    if (Audio_readLe32(header) != STREAMED_BANK_MAGIC ||
        Audio_readLe32(header + 4u) != STREAMED_BANK_VERSION || count != soundCount ||
        count > (UINT32_MAX - 16u) / 20u || dataOffset < 16u + count * 20u ||
        dataOffset > (uint32_t)fileSize || (uint64_t)count * sizeof(*loaded.entries) > SIZE_MAX)
        goto cleanup;
    loaded.entries = calloc(count != 0u ? count : 1u, sizeof(*loaded.entries));
    if (loaded.entries == NULL) goto cleanup;
    loaded.count = count;
    for (uint32_t i = 0u; i < count; ++i) {
        uint8_t row[20];
        if (fread(row, 1, sizeof(row), file) != sizeof(row)) goto cleanup;
        StreamedBankEntry* entry = &loaded.entries[i];
        *entry = (StreamedBankEntry){
            Audio_readLe32(row),
            Audio_readLe32(row + 4u),
            Audio_readLe32(row + 8u),
            Audio_readLe32(row + 12u),
            Audio_readLe32(row + 16u),
            NULL,
        };
        if (entry->size == 0u) {
            if (entry->offset || entry->sampleRate || entry->sampleCount || entry->flags)
                goto cleanup;
            continue;
        }
        uint32_t channels = entry->flags & 255u;
        uint32_t format = entry->flags & ~255u;
        uint32_t codec = format & ~STREAMED_BANK_EXTERNAL_FILE;
        if (entry->offset < dataOffset || entry->offset > (uint32_t)fileSize ||
            entry->size > (uint32_t)fileSize - entry->offset || entry->sampleRate == 0u ||
            entry->sampleCount == 0u || channels == 0u || channels > 2u ||
            (codec != STREAMED_BANK_DSP && codec != STREAMED_BANK_VORBIS) ||
            ((format & STREAMED_BANK_EXTERNAL_FILE) != 0u &&
                (codec != STREAMED_BANK_VORBIS || entry->size > 1023u)))
            goto cleanup;
    }
    for (uint32_t i = 0u; i < count; ++i) {
        StreamedBankEntry* entry = &loaded.entries[i];
        if ((entry->flags & STREAMED_BANK_EXTERNAL_FILE) == 0u) continue;
        entry->externalPath = malloc((size_t)entry->size + 1u);
        if (entry->externalPath == NULL || fseek(file, (long)entry->offset, SEEK_SET) != 0 ||
            fread(entry->externalPath, 1, entry->size, file) != entry->size)
            goto cleanup;
        entry->externalPath[entry->size] = '\0';
        if (entry->externalPath[0] == '/' || entry->externalPath[0] == '\\' ||
            memchr(entry->externalPath, '\0', entry->size) != NULL ||
            strchr(entry->externalPath, ':') != NULL)
            goto cleanup;
    }
    loaded.path = malloc(strlen(path) + 1u);
    if (loaded.path == NULL) goto cleanup;
    strcpy(loaded.path, path);
    StreamedBank_free(bank);
    *bank = loaded;
    ok = true;
cleanup:
    fclose(file);
    if (!ok) StreamedBank_free(&loaded);
    return ok;
}

const StreamedBankEntry* StreamedBank_find(const StreamedBank* bank, int32_t soundIndex) {
    if (bank == NULL || soundIndex < 0 || (uint32_t)soundIndex >= bank->count ||
        bank->entries[soundIndex].size == 0u)
        return NULL;
    return &bank->entries[soundIndex];
}

static size_t sliceRead(void* out, size_t size, size_t count, void* context) {
    StreamedSlice* slice = context;
    if (out == NULL || slice == NULL || slice->file == NULL || size == 0u ||
        slice->position > slice->size) {
        return 0u;
    }
    size_t available = (slice->size - slice->position) / size;
    if (count > available) count = available;
    size_t bytes = fread(out, 1, count * size, slice->file);
    slice->position += (uint32_t)bytes;
    return bytes / size;
}

static int sliceSeek(void* context, ogg_int64_t offset, int whence) {
    StreamedSlice* slice = context;
    if (slice == NULL || slice->file == NULL) return -1;

    int64_t base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = slice->position;
        break;
    case SEEK_END:
        base = slice->size;
        break;
    default:
        return -1;
    }
    if (offset < -base || offset > (int64_t)slice->size - base) return -1;
    uint32_t position = (uint32_t)(base + offset);
    uint64_t absolute = (uint64_t)slice->offset + position;
    if (absolute > LONG_MAX || fseek(slice->file, (long)absolute, SEEK_SET) != 0) return -1;
    slice->position = position;
    return 0;
}

static long sliceTell(void* context) {
    StreamedSlice* slice = context;
    return slice != NULL ? (long)slice->position : -1;
}

static bool openDsp(StreamedAudio* stream) {
    uint8_t header[WDSP_HEADER_SIZE];
    WdspSound info;
    if (sliceRead(header, 1, sizeof(header), &stream->slice) != sizeof(header) ||
        !Wdsp_parseHeader(header, stream->slice.size, &info) ||
        info.channelCount != (stream->entry.flags & 255u) ||
        info.sampleRate != stream->entry.sampleRate ||
        info.sampleCount != stream->entry.sampleCount)
        return false;
    for (uint32_t i = 0u; i < info.channelCount; ++i) {
        const WdspChannel* source = &info.channels[i];
        StreamedDspChannel* ch = &stream->channels[i];
        if (source->offset < Audio_readLe32(header + 24u)) return false;
        ch->offset = source->offset;
        ch->size = source->size;
        for (uint32_t j = 0u; j < 16u; ++j)
            ch->coefs[j] = (int16_t)source->coefs[j];
        ch->startHist1 = ch->hist1 = source->startHistory1;
        ch->startHist2 = ch->hist2 = source->startHistory2;
    }
    return true;
}

static char* resolveExternalPath(const char* bankPath, const char* relativePath) {
    const char* slash = strrchr(bankPath, '/');
    const char* backslash = strrchr(bankPath, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    size_t parentLength = slash != NULL ? (size_t)(slash - bankPath) : 0u;
    size_t relativeLength = strlen(relativePath);
    if (parentLength > SIZE_MAX - relativeLength - 2u) return NULL;
    char* path = malloc(parentLength + relativeLength + 2u);
    if (path == NULL) return NULL;
    if (parentLength != 0u) {
        memcpy(path, bankPath, parentLength);
        path[parentLength++] = '/';
    }
    memcpy(path + parentLength, relativePath, relativeLength + 1u);
    return path;
}

StreamedAudio* StreamedAudio_open(const StreamedBank* bank, const StreamedBankEntry* entry) {
    if (bank == NULL || bank->path == NULL || entry == NULL || entry->size == 0u) return NULL;
    StreamedAudio* stream = calloc(1, sizeof(*stream));
    if (stream == NULL) return NULL;
    stream->entry = *entry;
    char* externalPath = NULL;
    if (entry->externalPath != NULL) {
        externalPath = resolveExternalPath(bank->path, entry->externalPath);
        if (externalPath == NULL) goto fail;
        stream->slice.file = fopen(externalPath, "rb");
        free(externalPath);
        if (stream->slice.file == NULL || fseek(stream->slice.file, 0, SEEK_END) != 0)
            goto fail;
        long size = ftell(stream->slice.file);
        if (size <= 0 || (uint64_t)size > UINT32_MAX) goto fail;
        stream->slice.size = (uint32_t)size;
    } else {
        stream->slice.offset = entry->offset;
        stream->slice.size = entry->size;
        stream->slice.file = fopen(bank->path, "rb");
    }
    if (stream->slice.file == NULL || sliceSeek(&stream->slice, 0, SEEK_SET) != 0) goto fail;
    bool valid;
    if ((entry->flags & STREAMED_BANK_VORBIS) != 0u) {

        ov_callbacks callbacks = {sliceRead, sliceSeek, NULL, sliceTell};
        if (ov_open_callbacks(&stream->slice, &stream->vorbis, NULL, 0, callbacks) < 0) goto fail;
        stream->vorbisOpen = true;
        vorbis_info* info = ov_info(&stream->vorbis, -1);
        ogg_int64_t sampleCount = ov_pcm_total(&stream->vorbis, -1);
        valid = ov_streams(&stream->vorbis) == 1 && info != NULL &&
                info->rate == (long)entry->sampleRate &&
                info->channels == (int)(entry->flags & 255u) &&
                sampleCount > 0 && sampleCount <= UINT32_MAX &&
                ov_pcm_seek(&stream->vorbis, 0) == 0;
        if (valid) stream->entry.sampleCount = (uint32_t)sampleCount;
    } else {
        valid = openDsp(stream);
    }
    if (valid) return stream;
fail:
    StreamedAudio_close(stream);
    return NULL;
}

void StreamedAudio_close(StreamedAudio* stream) {
    if (stream == NULL) return;
    if (stream->vorbisOpen) ov_clear(&stream->vorbis);
    if (stream->slice.file != NULL) fclose(stream->slice.file);
    free(stream);
}

static bool decodeDspFrame(StreamedAudio* stream, StreamedDspChannel* ch) {
    uint32_t offset = (stream->sample / 14u) * 8u;
    if (offset > ch->size || ch->size - offset < 8u) return false;
    if (offset < ch->bufferOffset || offset - ch->bufferOffset >= ch->bufferSize) {
        uint32_t bytes = ch->size - offset;
        if (bytes > WDSP_BUFFER_BYTES) bytes = WDSP_BUFFER_BYTES;
        if (sliceSeek(&stream->slice, (int64_t)ch->offset + offset, SEEK_SET) != 0 ||
            sliceRead(ch->buffer, 1, bytes, &stream->slice) != bytes)
            return false;
        ch->bufferOffset = offset;
        ch->bufferSize = bytes;
    }
    const uint8_t* frame = ch->buffer + offset - ch->bufferOffset;
    unsigned predictor = frame[0] >> 4;
    if (predictor >= 8u) return false;
    int scale = 1 << (frame[0] & 15u);
    int coef1 = ch->coefs[predictor * 2u];
    int coef2 = ch->coefs[predictor * 2u + 1u];
    for (unsigned i = 0u; i < 14u; ++i) {
        int nibble = (frame[1u + i / 2u] >> (i % 2u ? 0 : 4)) & 15;
        if (nibble >= 8) nibble -= 16;
        int64_t sample = (int64_t)nibble * scale * 2048 + 1024 + (int64_t)coef1 * ch->hist1 +
            (int64_t)coef2 * ch->hist2;
        sample >>= 11;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        ch->hist2 = ch->hist1;
        ch->hist1 = (int16_t)sample;
        ch->samples[i] = (int16_t)sample;
    }
    return true;
}

int32_t StreamedAudio_read(StreamedAudio* stream, int16_t* out, uint32_t frames) {
    if (stream == NULL || out == NULL) return -1;
    uint32_t channels = stream->entry.flags & 255u;
    if (frames > (uint32_t)INT_MAX / (channels * 2u)) return -1;
    uint32_t remaining = stream->entry.sampleCount - stream->sample;
    if (frames > remaining) frames = remaining;
    if (frames == 0u) return 0;
    if (stream->vorbisOpen) {
        int section = 0;
        long bytes = 0;
        for (int holes = 0; holes < 8; ++holes) {
            bytes = ov_read(&stream->vorbis, (char*)out, (int)(frames * channels * 2u), &section);
            if (bytes != OV_HOLE) break;
        }
        if (bytes == 0) return 0;
        if (bytes < 0 || bytes % (channels * 2u) != 0) return -1;
        uint32_t read = (uint32_t)bytes / (channels * 2u);
        stream->sample += read;
        return (int32_t)read;
    }
    uint32_t produced = 0u;
    while (produced < frames) {
        uint32_t inFrame = stream->sample % 14u;
        if (inFrame == 0u) {
            for (uint32_t ch = 0u; ch < channels; ++ch)
                if (!decodeDspFrame(stream, &stream->channels[ch])) return -1;
        }
        uint32_t count = 14u - inFrame;
        if (count > frames - produced) count = frames - produced;
        for (uint32_t i = 0u; i < count; ++i)
            for (uint32_t ch = 0u; ch < channels; ++ch)
                out[(produced + i) * channels + ch] = stream->channels[ch].samples[inFrame + i];
        stream->sample += count;
        produced += count;
    }
    return (int32_t)produced;
}

bool StreamedAudio_seek(StreamedAudio* stream, uint32_t sample) {
    if (stream == NULL) return false;

    if (sample > stream->entry.sampleCount) sample = stream->entry.sampleCount;
    if (stream->vorbisOpen) {
        if (ov_pcm_seek(&stream->vorbis, sample) != 0) return false;
        stream->sample = sample;
        return true;
    }
    if (sample < stream->sample) {
        stream->sample = 0u;
        for (uint32_t ch = 0u; ch < (stream->entry.flags & 255u); ++ch) {
            stream->channels[ch].hist1 = stream->channels[ch].startHist1;
            stream->channels[ch].hist2 = stream->channels[ch].startHist2;
        }
    }

    int16_t scratch[512];
    while (stream->sample < sample) {
        uint32_t frames = sample - stream->sample;
        if (frames > 256u) frames = 256u;
        if (StreamedAudio_read(stream, scratch, frames) <= 0) return false;
    }
    return true;
}

uint32_t StreamedAudio_tell(const StreamedAudio* stream) {
    return stream != NULL ? stream->sample : 0u;
}

uint32_t StreamedAudio_sampleCount(const StreamedAudio* stream) {
    return stream != NULL ? stream->entry.sampleCount : 0u;
}
