#include "3ds_audio_system.h"
#include "../common/audio_ids.h"
#include "../common/audio_util.h"
#include "../common/sound_bank.h"
#include "../common/streamed_audio.h"
#include "../common/vorbis_probe.h"
#include "../../3ds/3ds_log.h"
#include "../../log.h"
#include "3ds/3ds_platform_config.h"
#include "data_win.h"
#include "utils.h"
#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stb/ds/stb_ds.h>
#include <tremor/ivorbisfile.h>

#define N3DS_MAX_NDSP_CHANNELS 24
#define N3DS_STREAM_CHANNEL_FIRST 0
#define N3DS_STREAM_CHANNEL_COUNT 6
#define N3DS_SOUND_CHANNEL_FIRST (N3DS_STREAM_CHANNEL_FIRST + N3DS_STREAM_CHANNEL_COUNT)
#define N3DS_SOUND_CHANNEL_COUNT (N3DS_MAX_NDSP_CHANNELS - N3DS_SOUND_CHANNEL_FIRST)
#define N3DS_MAX_SOUND_INSTANCES 24
#define N3DS_RESERVED_STREAM_INSTANCES 4
#define N3DS_SOUND_INSTANCE_ID_BASE AUDIO_SOUND_INSTANCE_ID_BASE
#define N3DS_AUDIO_STREAM_INDEX_BASE AUDIO_STREAM_INDEX_BASE
#define N3DS_MAX_STREAMS 4
#define N3DS_STREAM_BUFFER_COUNT 4
#define N3DS_STREAM_BASE_CHUNK_SAMPLES (14u * 512u)
#define N3DS_STREAM_MAX_CHUNK_SAMPLES (N3DS_STREAM_BASE_CHUNK_SAMPLES * 4u)
#define N3DS_STREAM_WORKER_STACK_SIZE (32u * 1024u)
#define N3DS_STREAM_WORKER_PRIORITY 0x34
#define N3DS_STREAM_WORKER_CORE_ID 1
#define N3DS_STREAM_WORKER_CPU_LIMIT 30u
#define N3DS_AUDIO_FILE_BUFFER_SIZE (128u * 1024u)
#define N3DS_DSP_FIRMWARE_PATH "sdmc:/3ds/dspfirm.cdc"
#define N3DS_DSP_MIN_FIRMWARE_SIZE (4u * 1024u)

typedef struct {
    uint8_t predictorScale;
    int16_t yn1;
    int16_t yn2;
} N3DSDspContext;

typedef struct {
    uint16_t coefs[16];
    uint32_t dataOffset;
    uint32_t dataSize;
    N3DSDspContext startContext;
    N3DSDspContext loopContext;
} N3DSDspAdpcmChannel;

typedef struct {
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint32_t loopStart;
    uint32_t loopEnd;
    bool loop;
    uint8_t channelCount;
    N3DSDspAdpcmChannel channels[2];
} N3DSDspAdpcm;

typedef enum {
    N3DS_STREAM_FORMAT_NONE = 0,
    N3DS_STREAM_FORMAT_DSP_ADPCM,
    N3DS_STREAM_FORMAT_OGG_VORBIS,
    N3DS_STREAM_FORMAT_PCM_WAV,
    N3DS_STREAM_FORMAT_ARCHIVE,
} N3DSStreamFormat;

typedef struct {
    bool active;
    char* path;
    N3DSStreamFormat format;
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint8_t channelCount;
    uint32_t wavDataOffset;
    N3DSDspAdpcm dspAdpcm;
    float gain;
    float pitch;
    uint32_t refCount;
} N3DSStreamEntry;

typedef struct {
    bool requested;
    bool inProgress;
    bool ready;
    bool success;
    int bufferIndex;
    uint32_t generation;
    uint32_t bufferStartSample;
} N3DSAsyncStreamFill;

typedef struct {
    bool active;
    bool paused;
    bool loop;
    bool isStream;
    bool streamFinished;
    bool pendingStart;
    int32_t soundIndex;
    int32_t instanceId;
    int32_t priority;
    int channelId;
    int secondaryChannelId;
    float gain;
    float pitch;
    float baseRate;
    float gainFadeStart;
    float gainFadeTarget;
    float gainFadeRemaining;
    float gainFadeDuration;
    uint32_t sampleCount;
    uint32_t sampleRate;
    uint8_t channelCount;
    N3DSStreamFormat streamFormat;
    bool useNativeAdpcm;
    ndspWaveBuf waveBufs[N3DS_STREAM_BUFFER_COUNT];
    ndspWaveBuf secondaryWaveBufs[N3DS_STREAM_BUFFER_COUNT];
    ndspAdpcmData adpcmStates[4];
    uint32_t bufferStartSample[N3DS_STREAM_BUFFER_COUNT];
    int16_t* streamPcm[N3DS_STREAM_BUFFER_COUNT];
    FILE* streamFile;
    uint32_t wavDataOffset;
    OggVorbis_File vorbisFile;
    bool vorbisOpen;
    StreamedAudio* archiveStream;
    const uint8_t* streamBlob;
    uint32_t streamBlobSize;
    bool ownsStreamBlob;
    bool streamBlobLinear;
    N3DSDspAdpcm dspAdpcm;
    uint32_t currentSample;
    uint32_t streamGeneration;
    N3DSAsyncStreamFill asyncFill;
    u64 playbackStartTick;
    u64 playbackReleaseTick;
} N3DSSoundInstance;

typedef SbkEntry N3DSPackedSoundBankEntry;

struct N3DSAudioSystem {
    AudioSystem base;
    FileSystem* fileSystem;
    bool initialized;
    bool ndspAvailable;
    float masterGain;
    N3DSStreamEntry streams[N3DS_MAX_STREAMS];
    N3DSSoundInstance instances[N3DS_MAX_SOUND_INSTANCES];
    int32_t lastResolveFailureSound;
    bool ndspChannelInUse[N3DS_MAX_NDSP_CHANNELS];
    uint8_t nextStreamChannel;
    uint8_t nextSoundChannel;
    uint32_t nextInstanceCounter;
    LightLock lock;
    CondVar workerCond;
    LightEvent workerEvent;
    Thread workerThread;
    bool workerStop;
    bool workerEnabled;
    const char* startupError;
    uint8_t* packedSoundBankData;
    uint32_t packedSoundBankSize;
    N3DSPackedSoundBankEntry* packedSoundBankEntries;
    uint32_t packedSoundBankEntryCount;
    StreamedBank streamedBank;
};

static void n3ds_audioCancelFillLocked(N3DSAudioSystem* audio, N3DSSoundInstance* inst);
static char* n3ds_audioResolveAudioPath(N3DSAudioSystem* audio, const char* name);
static bool n3ds_audioPlaybackFinished(const N3DSSoundInstance* inst, u64 nowTick);
static void n3ds_audioReleaseInstance(N3DSAudioSystem* audio, N3DSSoundInstance* inst);

static bool n3ds_audioCanAttemptNdspInit(void) {
    struct stat st;
    if (stat(N3DS_DSP_FIRMWARE_PATH, &st) == 0) {
        if (st.st_size >= (off_t)N3DS_DSP_MIN_FIRMWARE_SIZE) return true;
        logEvent("audio", "DSP firmware too small: path=%s size=%ld min=%lu",
            N3DS_DSP_FIRMWARE_PATH, (long)st.st_size, (unsigned long)N3DS_DSP_MIN_FIRMWARE_SIZE);
        return false;
    }

    Handle hbNdsp = envGetHandle("hb:ndsp");
    if (hbNdsp != 0) return true;

    logEvent("audio", "DSP firmware missing: %s", N3DS_DSP_FIRMWARE_PATH);
    return false;
}

static bool n3ds_audioStreamUsesPcm(const N3DSSoundInstance* inst) {
    return inst != NULL &&
        (inst->streamFormat == N3DS_STREAM_FORMAT_OGG_VORBIS ||
            inst->streamFormat == N3DS_STREAM_FORMAT_PCM_WAV ||
            inst->streamFormat == N3DS_STREAM_FORMAT_ARCHIVE);
}

static bool n3ds_audioParsePcmWavFile(
    const char* path, uint32_t* rate, uint32_t* samples, uint8_t* channels, uint32_t* dataOffset) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    uint8_t header[16];
    bool valid = false;
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    long fileSize = ftell(file);
    if (fileSize < 44 || fseek(file, 0, SEEK_SET) != 0 || fread(header, 1, 12, file) != 12 ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        goto done;
    uint16_t format = 0, bits = 0, channelCount = 0;
    uint32_t sampleRate = 0, offset = 0, bytes = 0;
    for (long pos = 12; pos <= fileSize - 8;) {
        if (fseek(file, pos, SEEK_SET) != 0 || fread(header, 1, 8, file) != 8) break;
        uint32_t size = Audio_readLe32(header + 4);
        if (size > (uint32_t)(fileSize - pos - 8)) break;
        if (memcmp(header, "fmt ", 4) == 0 && size >= 16) {
            if (fread(header, 1, 16, file) != 16) break;
            format = Audio_readLe16(header);
            channelCount = Audio_readLe16(header + 2);
            sampleRate = Audio_readLe32(header + 4);
            bits = Audio_readLe16(header + 14);
        } else if (memcmp(header, "data", 4) == 0) {
            offset = (uint32_t)pos + 8u;
            bytes = size;
        }
        pos += 8 + (long)((size + 1u) & ~1u);
    }
    if (format == 1 && bits == 16 && channelCount >= 1 && channelCount <= 2 && sampleRate > 0 &&
        sampleRate <= 96000 && offset >= 44 && bytes >= channelCount * 2u &&
        bytes % (channelCount * 2u) == 0) {
        *rate = sampleRate;
        *samples = bytes / (channelCount * 2u);
        *channels = (uint8_t)channelCount;
        *dataOffset = offset;
        valid = true;
    }
done:
    fclose(file);
    return valid;
}

static void n3ds_audioSetFileBuffer(FILE* file) {
    if (file == NULL) return;
    setvbuf(file, NULL, _IOFBF, N3DS_AUDIO_FILE_BUFFER_SIZE);
}

static bool n3ds_audioGetFileSize(FILE* file, uint32_t* outSize) {
    if (file == NULL || outSize == NULL) return false;

    if (fseek(file, 0, SEEK_END) != 0) return false;
    long size = ftell(file);
    if (size <= 0) {
        fseek(file, 0, SEEK_SET);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) return false;

    *outSize = (uint32_t)size;
    return true;
}

static const N3DSPackedSoundBankEntry* n3ds_audioGetSoundEntry(
    const N3DSAudioSystem* audio, int32_t soundIndex) {
    if (audio == NULL || soundIndex < 0 || (uint32_t)soundIndex >= audio->packedSoundBankEntryCount)
        return NULL;
    if (audio->packedSoundBankEntries == NULL) return NULL;
    return &audio->packedSoundBankEntries[soundIndex];
}

static bool n3ds_audioIsPackedDspAdpcm(const N3DSPackedSoundBankEntry* entry) {
    return entry != NULL && (entry->flags & SBK_FLAG_DSP_ADPCM) != 0;
}

static const uint8_t* n3ds_audioGetSoundData(
    const N3DSAudioSystem* audio, int32_t soundIndex, uint32_t* outSize) {
    if (outSize != NULL) *outSize = 0;
    if (audio == NULL || soundIndex < 0 || (uint32_t)soundIndex >= audio->packedSoundBankEntryCount)
        return NULL;
    if (audio->packedSoundBankEntries == NULL || audio->packedSoundBankData == NULL) return NULL;

    const N3DSPackedSoundBankEntry* entry = &audio->packedSoundBankEntries[soundIndex];
    if (entry->size == 0) return NULL;
    if (entry->offset >= audio->packedSoundBankSize) return NULL;
    if (entry->size > audio->packedSoundBankSize - entry->offset) return NULL;

    if (outSize != NULL) *outSize = entry->size;
    return audio->packedSoundBankData + entry->offset;
}

static bool n3ds_audioLoadSoundBank(N3DSAudioSystem* audio) {
    if (audio == NULL || audio->fileSystem == NULL) return false;
    if (audio->packedSoundBankData != NULL && audio->packedSoundBankEntries != NULL) return true;

    char* path = audio->fileSystem->vtable->resolvePath(audio->fileSystem, "audio/sound_bank.bin");
    if (path == NULL) return false;

    uint32_t bankSize = 0;
    N3DSPackedSoundBankEntry* entries = NULL;
    FILE* file = NULL;
    bool loaded = false;
    uint8_t header[16];
    uint8_t* bankData = NULL;
    uint8_t* tableData = NULL;

    file = fopen(path, "rb");
    if (file == NULL) goto cleanup;
    n3ds_audioSetFileBuffer(file);
    if (!n3ds_audioGetFileSize(file, &bankSize)) goto cleanup;
    if (bankSize < 16u) goto cleanup;
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) goto cleanup;
    SbkHeader bankHeader;
    if (!Sbk_parseHeader(header, bankSize, &bankHeader)) goto cleanup;
    uint32_t entryCount = bankHeader.count;
    uint32_t tableBytes = entryCount * SBK_ENTRY_STRIDE;

    if (tableBytes > 0) {
        tableData = safeMalloc(tableBytes);
        if (fread(tableData, 1, tableBytes, file) != tableBytes) goto cleanup;
    }

    entries = safeCalloc(entryCount, sizeof(N3DSPackedSoundBankEntry));
    if (!Sbk_parseEntries(tableData, &bankHeader, bankSize, entries)) goto cleanup;

    if (fseek(file, 0, SEEK_SET) != 0) goto cleanup;
    bankData = linearAlloc(bankSize);
    if (bankData == NULL) goto cleanup;
    if (fread(bankData, 1, bankSize, file) != bankSize) goto cleanup;
    DSP_FlushDataCache(bankData, bankSize);

    audio->packedSoundBankData = bankData;
    audio->packedSoundBankSize = bankSize;
    audio->packedSoundBankEntries = entries;
    audio->packedSoundBankEntryCount = entryCount;
    path = NULL;
    bankData = NULL;
    entries = NULL;
    loaded = true;
    logEvent("audio_bank", "loaded version=%lu entries=%lu sizeKB=%lu", (unsigned long)SBK_VERSION,
        (unsigned long)entryCount, (unsigned long)(bankSize / 1024u));

cleanup:
    if (file != NULL) fclose(file);
    if (bankData != NULL) linearFree(bankData);
    free(tableData);
    free(entries);
    free(path);
    return loaded;
}

static bool n3ds_audioPrimeNativeAdpcm(N3DSSoundInstance* inst);

static void n3ds_audioCopyDspAdpcmInfo(const WdspSound* wdsp, N3DSDspAdpcm* out) {
    memset(out, 0, sizeof(*out));
    out->sampleRate = wdsp->sampleRate;
    out->sampleCount = wdsp->sampleCount;
    out->loopStart = 0u;
    out->loopEnd = wdsp->sampleCount;
    out->loop = false;
    out->channelCount = wdsp->channelCount;

    for (uint32_t i = 0u; i < wdsp->channelCount; ++i) {
        const WdspChannel* source = &wdsp->channels[i];
        N3DSDspAdpcmChannel* channel = &out->channels[i];
        channel->dataOffset = source->offset;
        channel->dataSize = source->size;
        memcpy(channel->coefs, source->coefs, sizeof(channel->coefs));
        channel->startContext.predictorScale = (uint8_t)source->startPredictorScale;
        channel->startContext.yn1 = source->startHistory1;
        channel->startContext.yn2 = source->startHistory2;
        channel->loopContext.predictorScale = (uint8_t)source->loopPredictorScale;
        channel->loopContext.yn1 = source->loopHistory1;
        channel->loopContext.yn2 = source->loopHistory2;
    }
}

static bool n3ds_audioParseDspAdpcmBlob(const uint8_t* data, uint32_t size, N3DSDspAdpcm* out) {
    WdspSound wdsp;
    if (!Wdsp_parse(data, size, &wdsp)) return false;
    n3ds_audioCopyDspAdpcmInfo(&wdsp, out);
    return true;
}

static bool n3ds_audioParseDspAdpcmFile(const char* path, N3DSDspAdpcm* out) {
    if (path == NULL || out == NULL) return false;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    n3ds_audioSetFileBuffer(file);

    uint32_t fileSize = 0;
    uint8_t header[WDSP_HEADER_SIZE];
    WdspSound wdsp;
    bool success = n3ds_audioGetFileSize(file, &fileSize) &&
        fread(header, 1, sizeof(header), file) == sizeof(header) &&
        Wdsp_parseHeader(header, fileSize, &wdsp);
    fclose(file);
    if (success) n3ds_audioCopyDspAdpcmInfo(&wdsp, out);
    return success;
}

static uint8_t* n3ds_audioLoadFileLinear(const char* path, uint32_t* outSize) {
    if (path == NULL || outSize == NULL) return NULL;

    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;
    n3ds_audioSetFileBuffer(file);

    uint32_t fileSize = 0;
    uint8_t* data = NULL;
    if (n3ds_audioGetFileSize(file, &fileSize) && fileSize > 0) {
        data = linearAlloc(fileSize);
        if (data != NULL) {
            if (fseek(file, 0, SEEK_SET) != 0 || fread(data, 1, fileSize, file) != fileSize) {
                linearFree(data);
                data = NULL;
            } else {
                DSP_FlushDataCache(data, fileSize);
                *outSize = fileSize;
            }
        }
    }

    fclose(file);
    return data;
}

static char* n3ds_audioResolveCandidate(N3DSAudioSystem* audio, const char* name) {
    if (audio->fileSystem == NULL || name == NULL || name[0] == '\0') return NULL;

    bool absolute = strstr(name, ":/") != NULL;
    char* path = absolute
        ? safeStrdup(name)
        : audio->fileSystem->vtable->resolvePath(audio->fileSystem, name);
    struct stat st;
    if (path != NULL && stat(path, &st) == 0) return path;
    free(path);
    if (absolute) return NULL;

    size_t len = strlen(name);
    char* audioName = safeMalloc(len + sizeof("audio/"));
    memcpy(audioName, "audio/", sizeof("audio/") - 1u);
    memcpy(audioName + sizeof("audio/") - 1u, name, len + 1u);
    path = audio->fileSystem->vtable->resolvePath(audio->fileSystem, audioName);
    free(audioName);
    if (path != NULL && stat(path, &st) == 0) return path;
    free(path);
    return NULL;
}

static char* n3ds_audioResolveAudioPath(N3DSAudioSystem* audio, const char* name) {
    if (name == NULL || name[0] == '\0') return NULL;

    const char* base = Audio_pathFileName(name);
    const char* dot = strrchr(base, '.');
    size_t prefixLen = dot != NULL && dot > base ? (size_t)(dot - name) : strlen(name);
    char* candidate = safeMalloc(prefixLen + sizeof(".wdsp"));
    memcpy(candidate, name, prefixLen);

    strcpy(candidate + prefixLen, ".ogg");
    char* path = n3ds_audioResolveCandidate(audio, candidate);
    if (path == NULL) path = n3ds_audioResolveCandidate(audio, name);
    if (path == NULL) {
        strcpy(candidate + prefixLen, ".wdsp");
        path = n3ds_audioResolveCandidate(audio, candidate);
    }
    free(candidate);
    if (path != NULL || base == name || base[0] == '\0') return path;

    return n3ds_audioResolveAudioPath(audio, base);
}

static N3DSSoundInstance* n3ds_audioFindInstanceById(N3DSAudioSystem* audio, int32_t instanceId) {
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active && audio->instances[i].instanceId == instanceId) {
            return &audio->instances[i];
        }
    }
    return NULL;
}

static N3DSSoundInstance* n3ds_audioFindInstance(N3DSAudioSystem* audio, int32_t soundOrInstance) {
    if (Audio_isInstanceId(soundOrInstance))
        return n3ds_audioFindInstanceById(audio, soundOrInstance);
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        N3DSSoundInstance* inst = &audio->instances[i];
        if (inst->active && inst->soundIndex == soundOrInstance) return inst;
    }
    return NULL;
}

static int32_t n3ds_audioStreamSlotForSound(int32_t soundIndex) {
    int32_t slot = soundIndex - N3DS_AUDIO_STREAM_INDEX_BASE;
    return (slot >= 0 && slot < N3DS_MAX_STREAMS) ? slot : -1;
}

static int n3ds_audioAcquireChanRange(
    N3DSAudioSystem* audio, uint32_t firstChannel, uint32_t channelCount, uint8_t* nextChannel) {
    uint32_t start = *nextChannel;
    if (start < firstChannel || start >= firstChannel + channelCount) start = firstChannel;

    repeat(channelCount, offset) {
        uint32_t channel = firstChannel + ((start - firstChannel + offset) % channelCount);
        if (!audio->ndspChannelInUse[channel]) {
            int channelId = (int)channel;
            audio->ndspChannelInUse[channel] = true;
            channel++;
            *nextChannel =
                (uint8_t)(channel < firstChannel + channelCount ? channel : firstChannel);
            return channelId;
        }
    }

    return -1;
}

static int n3ds_audioAcquireChannel(N3DSAudioSystem* audio, bool stream) {
    if (audio == NULL) return -1;

    uint8_t* nextChannel = stream ? &audio->nextStreamChannel : &audio->nextSoundChannel;
    uint32_t firstChannel = stream ? N3DS_STREAM_CHANNEL_FIRST : N3DS_SOUND_CHANNEL_FIRST;
    uint32_t channelCount = stream ? N3DS_STREAM_CHANNEL_COUNT : N3DS_SOUND_CHANNEL_COUNT;
    int channel = n3ds_audioAcquireChanRange(audio, firstChannel, channelCount, nextChannel);
    if (channel >= 0 || stream) return channel;

    u64 nowTick = svcGetSystemTick();
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        N3DSSoundInstance* inst = &audio->instances[i];
        if (n3ds_audioPlaybackFinished(inst, nowTick)) {
            n3ds_audioReleaseInstance(audio, inst);
        }
    }

    return n3ds_audioAcquireChanRange(audio, firstChannel, channelCount, nextChannel);
}

static void n3ds_audioResetChannel(int channelId) {
    if (channelId < 0 || channelId >= N3DS_MAX_NDSP_CHANNELS) return;
    ndspChnSetPaused(channelId, true);
    ndspChnWaveBufClear(channelId);
    ndspChnReset(channelId);
    ndspChnSetInterp(channelId, NDSP_INTERP_LINEAR);
}

static void n3ds_audioReleaseChannel(N3DSAudioSystem* audio, int channelId) {
    if (channelId < 0 || channelId >= N3DS_MAX_NDSP_CHANNELS) return;
    n3ds_audioResetChannel(channelId);
    audio->ndspChannelInUse[channelId] = false;
}

static uint32_t n3ds_audioAdpcmOffset(uint32_t sampleIndex) {
    return (sampleIndex / 14u) * 8u;
}

static bool n3ds_audioIsFrameAligned(uint32_t sampleIndex) {
    return (sampleIndex % 14u) == 0;
}

static void n3ds_audioRefreshReleaseTick(N3DSSoundInstance* inst) {
    if (inst == NULL || inst->isStream || inst->loop || inst->sampleRate == 0 ||
        inst->sampleCount == 0) {
        return;
    }

    float pitch = Audio_sanitizePitch(inst->pitch);
    double durationSeconds = ((double)inst->sampleCount / (double)inst->sampleRate) / (double)pitch;
    if (!(durationSeconds > 0.0)) durationSeconds = 0.001;

    u64 durationTicks = (u64)(durationSeconds * (double)SYSCLOCK_ARM11);
    if (durationTicks == 0) durationTicks = 1;
    inst->playbackReleaseTick = inst->playbackStartTick + durationTicks;
}

static bool n3ds_audioPlaybackFinished(const N3DSSoundInstance* inst, u64 nowTick) {
    if (inst == NULL || !inst->active || inst->isStream || inst->loop || inst->paused) return false;
    if (nowTick < inst->playbackReleaseTick) return false;
    if (inst->waveBufs[0].status != NDSP_WBUF_DONE) return false;
    if (ndspChnIsPlaying(inst->channelId)) return false;
    if (inst->useNativeAdpcm && inst->secondaryChannelId >= 0) {
        if (inst->secondaryWaveBufs[0].status != NDSP_WBUF_DONE) return false;
        if (ndspChnIsPlaying(inst->secondaryChannelId)) return false;
    }
    return true;
}

static void n3ds_audioApplyMix(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    float mix[12];
    memset(mix, 0, sizeof(mix));
    inst->gain = Audio_sanitizeGain(inst->gain);
    float gain =
        inst->gain * audio->masterGain * AudioSystem_soundGroupGain(&audio->base, inst->soundIndex);
    if (inst->useNativeAdpcm && inst->secondaryChannelId >= 0) {
        mix[0] = gain;
        ndspChnSetMix(inst->channelId, mix);

        memset(mix, 0, sizeof(mix));
        mix[1] = gain;
        ndspChnSetMix(inst->secondaryChannelId, mix);
    } else {
        mix[0] = gain;
        mix[1] = gain;
        ndspChnSetMix(inst->channelId, mix);
    }

    float pitch = Audio_sanitizePitch(inst->pitch);
    float rate = inst->baseRate * pitch;
    if (!isfinite(rate) || rate < 4000.0f) rate = 4000.0f;
    if (rate > 96000.0f) rate = 96000.0f;
    inst->pitch = pitch;
    ndspChnSetRate(inst->channelId, rate);
    if (inst->useNativeAdpcm && inst->secondaryChannelId >= 0) {
        ndspChnSetRate(inst->secondaryChannelId, rate);
    }
}

static void n3ds_audioResetWaveBufs(N3DSSoundInstance* inst) {
    if (inst == NULL) return;
    memset(inst->waveBufs, 0, sizeof(inst->waveBufs));
    memset(inst->secondaryWaveBufs, 0, sizeof(inst->secondaryWaveBufs));
    memset(inst->adpcmStates, 0, sizeof(inst->adpcmStates));
    memset(inst->bufferStartSample, 0, sizeof(inst->bufferStartSample));
}

static void n3ds_audioSetInstancePaused(N3DSSoundInstance* inst, bool paused) {
    inst->paused = paused;
    ndspChnSetPaused(inst->channelId, paused || inst->pendingStart);
    if (inst->secondaryChannelId >= 0)
        ndspChnSetPaused(inst->secondaryChannelId, paused || inst->pendingStart);
}

static void n3ds_audioRebuildChannels(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (audio == NULL || inst == NULL || inst->channelId < 0) return;

    if (inst->isStream) inst->pendingStart = true;
    n3ds_audioResetChannel(inst->channelId);
    if (inst->secondaryChannelId >= 0) {
        n3ds_audioResetChannel(inst->secondaryChannelId);
    }

    if (inst->useNativeAdpcm) {
        ndspChnSetFormat(inst->channelId, NDSP_FORMAT_MONO_ADPCM);
        ndspChnSetAdpcmCoefs(inst->channelId, inst->dspAdpcm.channels[0].coefs);
        if (inst->secondaryChannelId >= 0) {
            ndspChnSetFormat(inst->secondaryChannelId, NDSP_FORMAT_MONO_ADPCM);
            ndspChnSetAdpcmCoefs(inst->secondaryChannelId, inst->dspAdpcm.channels[1].coefs);
        }
    } else {
        ndspChnSetFormat(inst->channelId,
            inst->channelCount == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    }

    n3ds_audioApplyMix(audio, inst);
    ndspChnSetPaused(inst->channelId, inst->paused || inst->pendingStart);
    if (inst->secondaryChannelId >= 0) {
        ndspChnSetPaused(inst->secondaryChannelId, inst->paused || inst->pendingStart);
    }
}

static N3DSStreamEntry* n3ds_audioActiveStreamEntry(N3DSAudioSystem* audio, int32_t soundIndex) {
    int32_t slot = n3ds_audioStreamSlotForSound(soundIndex);
    if (slot < 0 || !audio->streams[slot].active) return NULL;
    return &audio->streams[slot];
}

static bool n3ds_audioCanUseNativeAdpcm(const N3DSDspAdpcm* dspAdpcm, bool loop) {
    if (dspAdpcm == NULL || dspAdpcm->sampleRate == 0 || dspAdpcm->channelCount == 0 ||
        dspAdpcm->channelCount > 2)
        return false;
    if (!loop) return true;
    return !dspAdpcm->loop || dspAdpcm->loopEnd <= dspAdpcm->loopStart ||
        n3ds_audioIsFrameAligned(dspAdpcm->loopStart);
}

static void n3ds_audioCleanupStream(
    N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (inst == NULL) return;

    n3ds_audioReleaseChannel(audio, inst->channelId);
    n3ds_audioReleaseChannel(audio, inst->secondaryChannelId);
    inst->channelId = -1;
    inst->secondaryChannelId = -1;

    StreamedAudio_close(inst->archiveStream);
    inst->archiveStream = NULL;
    if (inst->vorbisOpen) {
        ov_clear(&inst->vorbisFile);
        memset(&inst->vorbisFile, 0, sizeof(inst->vorbisFile));
        inst->vorbisOpen = false;
    } else if (inst->streamFile != NULL) {
        fclose(inst->streamFile);
    }
    inst->streamFile = NULL;
    repeat(N3DS_STREAM_BUFFER_COUNT, i) {
        if (inst->streamPcm[i] != NULL) {
            linearFree(inst->streamPcm[i]);
            inst->streamPcm[i] = NULL;
        }
    }
    if (inst->ownsStreamBlob && inst->streamBlob != NULL) {
        if (inst->streamBlobLinear)
            linearFree((void*)inst->streamBlob);
        else
            free((void*)inst->streamBlob);
    }
    inst->streamBlob = NULL;
    inst->streamBlobSize = 0;
    inst->ownsStreamBlob = false;
    inst->streamBlobLinear = false;

    inst->isStream = false;
    inst->streamFormat = N3DS_STREAM_FORMAT_NONE;
    inst->useNativeAdpcm = false;
    inst->streamFinished = false;
    inst->pendingStart = false;
    inst->sampleRate = 0;
    inst->sampleCount = 0;
    inst->channelCount = 0;
    inst->baseRate = 0.0f;
    memset(&inst->dspAdpcm, 0, sizeof(inst->dspAdpcm));
    n3ds_audioResetWaveBufs(inst);
}

static bool n3ds_audioStartAdpcm(N3DSAudioSystem* audio, N3DSSoundInstance* inst,
    const uint8_t* blob, uint32_t blobSize, const N3DSDspAdpcm* dspAdpcm, bool stream,
    bool ownsBlob) {
    if (audio == NULL || inst == NULL || blob == NULL || dspAdpcm == NULL) return false;

    inst->streamBlob = blob;
    inst->streamBlobSize = blobSize;
    inst->ownsStreamBlob = ownsBlob;
    inst->streamBlobLinear = true;
    inst->channelId = n3ds_audioAcquireChannel(audio, stream);
    if (inst->channelId < 0) {
        n3ds_audioCleanupStream(audio, inst);
        return false;
    }
    if (dspAdpcm->channelCount == 2) {
        inst->secondaryChannelId = n3ds_audioAcquireChannel(audio, stream);
        if (inst->secondaryChannelId < 0) {
            n3ds_audioCleanupStream(audio, inst);
            return false;
        }
    }

    inst->isStream = stream;
    inst->streamFinished = stream;
    inst->streamFormat = stream ? N3DS_STREAM_FORMAT_DSP_ADPCM : N3DS_STREAM_FORMAT_NONE;
    inst->useNativeAdpcm = true;
    inst->dspAdpcm = *dspAdpcm;
    inst->sampleRate = dspAdpcm->sampleRate;
    inst->sampleCount = dspAdpcm->sampleCount;
    inst->channelCount = dspAdpcm->channelCount;
    inst->baseRate = (float)dspAdpcm->sampleRate;
    inst->playbackStartTick = svcGetSystemTick();
    n3ds_audioRefreshReleaseTick(inst);

    n3ds_audioRebuildChannels(audio, inst);
    if (!n3ds_audioPrimeNativeAdpcm(inst)) {
        n3ds_audioCleanupStream(audio, inst);
        return false;
    }
    return true;
}

static bool n3ds_audioPrimeNativeAdpcm(N3DSSoundInstance* inst) {
    if (!inst->useNativeAdpcm || inst->streamBlob == NULL) return false;

    uint32_t loopStart = 0;
    uint32_t loopEnd = inst->dspAdpcm.sampleCount;
    bool useLoopRegion = inst->loop && inst->dspAdpcm.loop && inst->dspAdpcm.loopStart > 0 &&
        inst->dspAdpcm.loopEnd > inst->dspAdpcm.loopStart;
    if (useLoopRegion) {
        loopStart = inst->dspAdpcm.loopStart;
        loopEnd = inst->dspAdpcm.loopEnd;
    }

    n3ds_audioResetWaveBufs(inst);
    inst->bufferStartSample[0] = 0;
    inst->bufferStartSample[1] = loopStart;

    inst->adpcmStates[0].index = inst->dspAdpcm.channels[0].startContext.predictorScale;
    inst->adpcmStates[0].history0 = inst->dspAdpcm.channels[0].startContext.yn1;
    inst->adpcmStates[0].history1 = inst->dspAdpcm.channels[0].startContext.yn2;

    inst->waveBufs[0].data_adpcm =
        (uint8_t*)inst->streamBlob + inst->dspAdpcm.channels[0].dataOffset;
    inst->waveBufs[0].nsamples = useLoopRegion ? loopStart : inst->dspAdpcm.sampleCount;
    inst->waveBufs[0].adpcm_data = &inst->adpcmStates[0];
    inst->waveBufs[0].looping = inst->loop && !useLoopRegion;

    if (inst->secondaryChannelId >= 0) {
        inst->adpcmStates[1].index = inst->dspAdpcm.channels[1].startContext.predictorScale;
        inst->adpcmStates[1].history0 = inst->dspAdpcm.channels[1].startContext.yn1;
        inst->adpcmStates[1].history1 = inst->dspAdpcm.channels[1].startContext.yn2;

        inst->secondaryWaveBufs[0].data_adpcm =
            (uint8_t*)inst->streamBlob + inst->dspAdpcm.channels[1].dataOffset;
        inst->secondaryWaveBufs[0].nsamples = inst->waveBufs[0].nsamples;
        inst->secondaryWaveBufs[0].adpcm_data = &inst->adpcmStates[1];
        inst->secondaryWaveBufs[0].looping = inst->waveBufs[0].looping;
    }

    if (useLoopRegion) {
        inst->adpcmStates[2].index = inst->dspAdpcm.channels[0].loopContext.predictorScale;
        inst->adpcmStates[2].history0 = inst->dspAdpcm.channels[0].loopContext.yn1;
        inst->adpcmStates[2].history1 = inst->dspAdpcm.channels[0].loopContext.yn2;

        inst->waveBufs[1].data_adpcm = (uint8_t*)inst->streamBlob +
            inst->dspAdpcm.channels[0].dataOffset + n3ds_audioAdpcmOffset(loopStart);
        inst->waveBufs[1].nsamples = loopEnd - loopStart;
        inst->waveBufs[1].adpcm_data = &inst->adpcmStates[2];
        inst->waveBufs[1].looping = true;

        if (inst->secondaryChannelId >= 0) {
            inst->adpcmStates[3].index = inst->dspAdpcm.channels[1].loopContext.predictorScale;
            inst->adpcmStates[3].history0 = inst->dspAdpcm.channels[1].loopContext.yn1;
            inst->adpcmStates[3].history1 = inst->dspAdpcm.channels[1].loopContext.yn2;

            inst->secondaryWaveBufs[1].data_adpcm = (uint8_t*)inst->streamBlob +
                                                    inst->dspAdpcm.channels[1].dataOffset +
                                                    n3ds_audioAdpcmOffset(loopStart);
            inst->secondaryWaveBufs[1].nsamples = inst->waveBufs[1].nsamples;
            inst->secondaryWaveBufs[1].adpcm_data = &inst->adpcmStates[3];
            inst->secondaryWaveBufs[1].looping = true;
        }
    }

    DSP_FlushDataCache((void*)inst->streamBlob, inst->streamBlobSize);
    ndspChnWaveBufAdd(inst->channelId, &inst->waveBufs[0]);
    if (inst->secondaryChannelId >= 0) {
        ndspChnWaveBufAdd(inst->secondaryChannelId, &inst->secondaryWaveBufs[0]);
    }

    if (useLoopRegion) {
        ndspChnWaveBufAdd(inst->channelId, &inst->waveBufs[1]);
        if (inst->secondaryChannelId >= 0) {
            ndspChnWaveBufAdd(inst->secondaryChannelId, &inst->secondaryWaveBufs[1]);
        }
    }
    return true;
}

static void n3ds_audioReleaseInstance(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (!inst->active) return;
    if (inst->isStream) {
        logEvent("audio_stop", "instance=%ld sound=%ld native=%d sample=%lu/%lu ch=%ld/%ld",
            (long)inst->instanceId, (long)inst->soundIndex, inst->useNativeAdpcm ? 1 : 0,
            (unsigned long)inst->currentSample, (unsigned long)inst->sampleCount,
            (long)inst->channelId, (long)inst->secondaryChannelId);
    }
    if (audio != NULL && inst->isStream) {
        n3ds_audioCancelFillLocked(audio, inst);
    }

    n3ds_audioCleanupStream(audio, inst);
    memset(inst, 0, sizeof(*inst));
}

static N3DSSoundInstance* n3ds_audioFindFreeRange(
    N3DSAudioSystem* audio, int32_t start, int32_t endExclusive) {
    u64 nowTick = svcGetSystemTick();
    for (int32_t i = start; i < endExclusive; ++i) {
        if (!audio->instances[i].active) return &audio->instances[i];
    }
    for (int32_t i = start; i < endExclusive; ++i) {
        bool finished = n3ds_audioPlaybackFinished(&audio->instances[i], nowTick);
        if (finished) {
            n3ds_audioReleaseInstance(audio, &audio->instances[i]);
            return &audio->instances[i];
        }
    }
    return NULL;
}

static N3DSSoundInstance* n3ds_audioFindFreeInstance(
    N3DSAudioSystem* audio, bool preferStreamSlots, int32_t priority) {
    int32_t split = N3DS_MAX_SOUND_INSTANCES - N3DS_RESERVED_STREAM_INSTANCES;
    N3DSSoundInstance* inst;

    if (preferStreamSlots) {
        inst = n3ds_audioFindFreeRange(audio, split, N3DS_MAX_SOUND_INSTANCES);
        if (inst != NULL) return inst;
        inst = n3ds_audioFindFreeRange(audio, 0, split);
    } else {
        inst = n3ds_audioFindFreeRange(audio, 0, split);
        if (inst != NULL) return inst;
        inst = n3ds_audioFindFreeRange(audio, split, N3DS_MAX_SOUND_INSTANCES);
    }
    if (inst != NULL) return inst;

    N3DSSoundInstance* victim = NULL;
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        N3DSSoundInstance* candidate = &audio->instances[i];
        if (candidate->priority <= priority &&
            (victim == NULL || candidate->priority < victim->priority))
            victim = candidate;
    }
    if (victim != NULL) n3ds_audioReleaseInstance(audio, victim);
    return victim;
}

static bool n3ds_audioFillPcmWaveBuf(N3DSSoundInstance* inst, int bufferIndex) {
    if (inst == NULL || !n3ds_audioStreamUsesPcm(inst)) return false;

    size_t stride = inst->channelCount;
    int16_t* out = inst->streamPcm[bufferIndex];
    uint32_t chunkSamples = N3DS_STREAM_BASE_CHUNK_SAMPLES;
    float pitch = Audio_sanitizePitch(inst->pitch);
    if (pitch > 1.0f) {
        chunkSamples = (uint32_t)ceilf((float)N3DS_STREAM_BASE_CHUNK_SAMPLES * pitch);
        if (chunkSamples > N3DS_STREAM_MAX_CHUNK_SAMPLES) {
            chunkSamples = N3DS_STREAM_MAX_CHUNK_SAMPLES;
        }
    }
    uint32_t produced = 0;
    uint32_t startSample = inst->currentSample;

    while (produced < chunkSamples) {
        if (inst->archiveStream != NULL) {
            int32_t frames = StreamedAudio_read(
                inst->archiveStream, out + produced * stride, chunkSamples - produced);
            if (frames < 0) return false;
            if (frames == 0) {
                if (!inst->loop || produced > 0) break;
                if (!StreamedAudio_seek(inst->archiveStream, 0)) return false;
                inst->currentSample = 0;
                startSample = 0;
                continue;
            }
            produced += (uint32_t)frames;
            inst->currentSample = StreamedAudio_tell(inst->archiveStream);
            continue;
        }
        if (inst->streamFormat == N3DS_STREAM_FORMAT_PCM_WAV) {
            if (inst->currentSample == inst->sampleCount) {
                if (!inst->loop || produced > 0) break;
                if (fseek(inst->streamFile, inst->wavDataOffset, SEEK_SET) != 0) return false;
                inst->currentSample = 0;
                startSample = 0;
            }
            uint32_t count = chunkSamples - produced;
            if (count > inst->sampleCount - inst->currentSample) {
                count = inst->sampleCount - inst->currentSample;
            }
            if (fread(out + produced * stride, stride * sizeof(int16_t), count, inst->streamFile) !=
                count)
                return false;
            produced += count;
            inst->currentSample += count;
            continue;
        }
        if (!inst->vorbisOpen) return false;
        int requestedBytes = (int)((chunkSamples - produced) * stride * sizeof(int16_t));
        int bitstream = 0;
        long bytesRead = ov_read(
            &inst->vorbisFile, (char*)(out + produced * stride), requestedBytes, &bitstream);
        if (bytesRead == 0) {
            if (inst->loop) {
                if (produced > 0) break;
                if (ov_pcm_seek(&inst->vorbisFile, 0) != 0) return false;
                inst->currentSample = 0;
                startSample = 0;
                continue;
            }
            break;
        }
        if (bytesRead == OV_HOLE) continue;
        if (bytesRead < 0 || (bytesRead % (long)(stride * sizeof(int16_t))) != 0) return false;

        produced += (uint32_t)(bytesRead / (long)(stride * sizeof(int16_t)));
        ogg_int64_t currentSample = ov_pcm_tell(&inst->vorbisFile);
        inst->currentSample = currentSample >= 0 && currentSample <= UINT32_MAX
            ? (uint32_t)currentSample
            : startSample + produced;
    }

    if (produced == 0) return false;

    memset(&inst->waveBufs[bufferIndex], 0, sizeof(inst->waveBufs[bufferIndex]));
    inst->waveBufs[bufferIndex].data_pcm16 = out;
    inst->waveBufs[bufferIndex].nsamples = produced;
    inst->waveBufs[bufferIndex].looping = false;
    inst->bufferStartSample[bufferIndex] = startSample;
    DSP_FlushDataCache(out, produced * stride * sizeof(int16_t));
    return true;
}

static void n3ds_audioWaitForFillLocked(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (audio == NULL || inst == NULL || !audio->workerEnabled) return;
    while (inst->asyncFill.inProgress) {
        CondVar_Wait(&audio->workerCond, &audio->lock);
    }
}

static void n3ds_audioCancelFillLocked(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (audio == NULL || inst == NULL || !inst->isStream) return;
    n3ds_audioWaitForFillLocked(audio, inst);
    inst->streamGeneration += 1u;
    memset(&inst->asyncFill, 0, sizeof(inst->asyncFill));
}

static void n3ds_audioRequestFillLocked(
    N3DSAudioSystem* audio, N3DSSoundInstance* inst, int bufferIndex) {
    if (audio == NULL || inst == NULL || !audio->workerEnabled || !inst->isStream) return;
    if (inst->asyncFill.requested || inst->asyncFill.inProgress || inst->asyncFill.ready) return;

    inst->asyncFill.requested = true;
    inst->asyncFill.inProgress = false;
    inst->asyncFill.ready = false;
    inst->asyncFill.success = false;
    inst->asyncFill.bufferIndex = bufferIndex;
    inst->asyncFill.generation = inst->streamGeneration;
    LightEvent_Signal(&audio->workerEvent);
}

static bool n3ds_audioApplyFillLocked(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (audio == NULL || inst == NULL || !inst->asyncFill.ready) return false;

    int bufferIndex = inst->asyncFill.bufferIndex;
    bool success = inst->asyncFill.success;

    if (success) {
        inst->bufferStartSample[bufferIndex] = inst->asyncFill.bufferStartSample;
        ndspChnWaveBufAdd(inst->channelId, &inst->waveBufs[bufferIndex]);
    } else {
        inst->streamFinished = true;
    }

    memset(&inst->asyncFill, 0, sizeof(inst->asyncFill));
    CondVar_Broadcast(&audio->workerCond);
    return success;
}

static bool n3ds_audioFillPendingLocked(const N3DSAudioSystem* audio) {
    if (audio == NULL || !audio->workerEnabled) return false;
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        const N3DSSoundInstance* inst = &audio->instances[i];
        if (!inst->active || !inst->isStream) continue;
        if (inst->asyncFill.requested || inst->asyncFill.inProgress) return true;
    }
    return false;
}

static void n3ds_audioStreamWorkerMain(void* arg) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)arg;
    if (audio == NULL) threadExit(0);

    while (true) {
        LightEvent_Wait(&audio->workerEvent);

        while (true) {
            int32_t jobSlot = -1;
            int bufferIndex = -1;
            uint32_t generation = 0;

            LightLock_Lock(&audio->lock);
            if (audio->workerStop) {
                LightLock_Unlock(&audio->lock);
                threadExit(0);
            }

            repeat(N3DS_MAX_SOUND_INSTANCES, i) {
                N3DSSoundInstance* inst = &audio->instances[i];
                if (!inst->active || !inst->isStream) continue;
                if (!inst->asyncFill.requested || inst->asyncFill.inProgress ||
                    inst->asyncFill.ready)
                    continue;
                inst->asyncFill.inProgress = true;
                jobSlot = (int32_t)i;
                bufferIndex = inst->asyncFill.bufferIndex;
                generation = inst->asyncFill.generation;
                break;
            }

            if (jobSlot < 0) {
                LightLock_Unlock(&audio->lock);
                break;
            }

            N3DSSoundInstance* inst = &audio->instances[jobSlot];
            uint32_t startSample = 0;
            bool success = false;
            LightLock_Unlock(&audio->lock);
            success = n3ds_audioFillPcmWaveBuf(inst, bufferIndex);
            if (success) {
                startSample = inst->bufferStartSample[bufferIndex];
            }
            LightLock_Lock(&audio->lock);

            inst = &audio->instances[jobSlot];
            if (inst->active && inst->isStream && inst->asyncFill.inProgress &&
                inst->asyncFill.requested && inst->asyncFill.generation == generation &&
                inst->asyncFill.bufferIndex == bufferIndex) {
                inst->asyncFill.inProgress = false;
                inst->asyncFill.ready = true;
                inst->asyncFill.success = success;
                inst->asyncFill.bufferStartSample = startSample;
            }
            CondVar_Broadcast(&audio->workerCond);
            bool morePending = n3ds_audioFillPendingLocked(audio);
            bool shouldStop = audio->workerStop;
            LightLock_Unlock(&audio->lock);

            if (shouldStop) threadExit(0);
            if (!morePending) break;
        }
    }
}

static bool n3ds_audioStartAdpcmFile(N3DSAudioSystem* audio,
    N3DSSoundInstance* inst, const char* path, const N3DSDspAdpcm* dspAdpcm) {
    if (audio == NULL || inst == NULL || path == NULL || dspAdpcm == NULL) return false;
    if (!n3ds_audioCanUseNativeAdpcm(dspAdpcm, inst->loop)) return false;

    uint32_t blobSize = 0;
    uint8_t* blob = n3ds_audioLoadFileLinear(path, &blobSize);
    if (blob == NULL) return false;

    bool started =
        n3ds_audioStartAdpcm(audio, inst, blob, blobSize, dspAdpcm, true, true);
    return started;
}

static bool n3ds_audioStartPcmStream(N3DSAudioSystem* audio, N3DSSoundInstance* inst) {
    if (!audio->workerEnabled) return false;

    size_t samplesPerBuffer = (size_t)N3DS_STREAM_MAX_CHUNK_SAMPLES * inst->channelCount;
    repeat(N3DS_STREAM_BUFFER_COUNT, i) {
        inst->streamPcm[i] = linearAlloc(samplesPerBuffer * sizeof(int16_t));
        if (inst->streamPcm[i] == NULL) goto fail;
    }
    inst->currentSample = 0;
    n3ds_audioRebuildChannels(audio, inst);
    n3ds_audioRequestFillLocked(audio, inst, 0);
    return true;

fail:
    n3ds_audioCleanupStream(audio, inst);
    return false;
}

static bool n3ds_audioStartArchiveStream(
    N3DSAudioSystem* audio, N3DSSoundInstance* inst, const StreamedBankEntry* entry) {
    inst->archiveStream = StreamedAudio_open(&audio->streamedBank, entry);
    if (inst->archiveStream == NULL) return false;
    inst->channelId = n3ds_audioAcquireChannel(audio, true);
    if (inst->channelId < 0) {
        n3ds_audioCleanupStream(audio, inst);
        return false;
    }
    inst->isStream = true;
    inst->streamFormat = N3DS_STREAM_FORMAT_ARCHIVE;
    inst->sampleRate = entry->sampleRate;
    inst->sampleCount = StreamedAudio_sampleCount(inst->archiveStream);
    inst->channelCount = (uint8_t)(entry->flags & 255u);
    inst->baseRate = (float)entry->sampleRate;
    return n3ds_audioStartPcmStream(audio, inst);
}

static bool n3ds_audioStartVorbisStream(
    N3DSAudioSystem* audio, N3DSSoundInstance* inst, const char* path) {
    if (audio == NULL || inst == NULL || path == NULL) return false;

    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    n3ds_audioSetFileBuffer(file);

    memset(&inst->vorbisFile, 0, sizeof(inst->vorbisFile));
    if (ov_open(file, &inst->vorbisFile, NULL, 0) < 0) {
        fclose(file);
        return false;
    }
    inst->vorbisOpen = true;

    vorbis_info* info = ov_info(&inst->vorbisFile, -1);
    ogg_int64_t totalSamples = ov_pcm_total(&inst->vorbisFile, -1);
    if (info == NULL || info->rate <= 0 || info->channels <= 0 || info->channels > 2 ||
        totalSamples <= 0 || totalSamples > UINT32_MAX) {
        n3ds_audioCleanupStream(audio, inst);
        return false;
    }

    inst->channelId = n3ds_audioAcquireChannel(audio, true);
    if (inst->channelId < 0) {
        n3ds_audioCleanupStream(audio, inst);
        return false;
    }

    inst->isStream = true;
    inst->streamFormat = N3DS_STREAM_FORMAT_OGG_VORBIS;
    inst->sampleRate = (uint32_t)info->rate;
    inst->sampleCount = (uint32_t)totalSamples;
    inst->channelCount = (uint8_t)info->channels;
    inst->baseRate = (float)info->rate;

    return n3ds_audioStartPcmStream(audio, inst);
}

static bool n3ds_audioStartWavStream(N3DSAudioSystem* audio, N3DSSoundInstance* inst,
    const char* path, const N3DSStreamEntry* stream) {
    if (audio == NULL || inst == NULL || path == NULL || stream == NULL) return false;
    FILE* file = fopen(path, "rb");
    if (file == NULL || fseek(file, stream->wavDataOffset, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    n3ds_audioSetFileBuffer(file);
    inst->channelId = n3ds_audioAcquireChannel(audio, true);
    if (inst->channelId < 0) {
        fclose(file);
        return false;
    }
    inst->streamFile = file;
    inst->isStream = true;
    inst->streamFormat = N3DS_STREAM_FORMAT_PCM_WAV;
    inst->sampleRate = stream->sampleRate;
    inst->sampleCount = stream->sampleCount;
    inst->channelCount = stream->channelCount;
    inst->wavDataOffset = stream->wavDataOffset;
    inst->baseRate = (float)stream->sampleRate;

    return n3ds_audioStartPcmStream(audio, inst);
}

static void n3ds_audioInit(AudioSystem* base, DataWin* dataWin, FileSystem* fileSystem) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (audio == NULL || audio->initialized) return;

    LightLock_Init(&audio->lock);
    CondVar_Init(&audio->workerCond);
    LightEvent_Init(&audio->workerEvent, RESET_ONESHOT);
    audio->fileSystem = fileSystem;
    audio->masterGain = 1.0f;
    base->dw = dataWin;
    arrput(base->audioGroups, dataWin);
    n3ds_audioLoadSoundBank(audio);
    if (fileSystem != NULL && fileSystem->vtable->fileExists(fileSystem, "audio/streamed.bin")) {
        char* path = fileSystem->vtable->resolvePath(fileSystem, "audio/streamed.bin");
        if (path != NULL && !StreamedBank_load(&audio->streamedBank, path, dataWin->sond.count))
            logEvent("audio_fail", "invalid streamed bank: %s", path);
        free(path);
    }

    if (!n3ds_audioCanAttemptNdspInit()) {
        logEvent("audio", "NDSP disabled before init");
        audio->initialized = true;
        audio->ndspAvailable = false;
        return;
    }

    Result ndspResult = ndspInit();
    if (R_FAILED(ndspResult)) {
        logEvent("audio", "ndspInit failed rc=0x%08lx; audio disabled", (unsigned long)ndspResult);
        audio->initialized = true;
        audio->ndspAvailable = false;
        return;
    }
    audio->ndspAvailable = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    logEvent("audio", "init complete sounds=%lu packedBankKB=%lu",
        (unsigned long)dataWin->sond.count, (unsigned long)(audio->packedSoundBankSize / 1024u));
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        ndspChnReset((int)i);
        ndspChnSetInterp((int)i, NDSP_INTERP_LINEAR);
    }
    Result cpuLimitResult = APT_SetAppCpuTimeLimit(N3DS_STREAM_WORKER_CPU_LIMIT);
    if (R_FAILED(cpuLimitResult)) {
        audio->startupError =
            "Failed to reserve core 1 for audio streaming. Press START.";
        logEvent("audio_fail", "core 1 reservation failed rc=0x%08lx",
            (unsigned long)cpuLimitResult);
    } else {
        audio->workerThread = threadCreate(n3ds_audioStreamWorkerMain, audio,
            N3DS_STREAM_WORKER_STACK_SIZE, N3DS_STREAM_WORKER_PRIORITY,
            N3DS_STREAM_WORKER_CORE_ID, false);
        if (audio->workerThread == NULL) {
            audio->startupError =
                "Failed to start the core 1 audio worker. Press START.";
            logEvent("audio_fail", "core 1 worker creation failed");
        } else {
            audio->workerEnabled = true;
        }
    }
    logEvent("audio",
        "worker enabled=%d requestedCore=%d cpuLimit=%lu nativeAdpcm=%d",
        audio->workerEnabled ? 1 : 0, N3DS_STREAM_WORKER_CORE_ID,
        (unsigned long)N3DS_STREAM_WORKER_CPU_LIMIT, 1);
    audio->initialized = true;
}

static void n3ds_audioDestroy(AudioSystem* base) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (audio->workerEnabled) {
        LightLock_Lock(&audio->lock);
        audio->workerStop = true;
        LightLock_Unlock(&audio->lock);
        LightEvent_Signal(&audio->workerEvent);
        threadJoin(audio->workerThread, U64_MAX);
        threadFree(audio->workerThread);
        audio->workerThread = NULL;
        audio->workerEnabled = false;
    }
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        n3ds_audioReleaseInstance(audio, &audio->instances[i]);
    }
    repeat(N3DS_MAX_STREAMS, i) {
        free(audio->streams[i].path);
    }
    if (audio->packedSoundBankData != NULL) linearFree(audio->packedSoundBankData);
    free(audio->packedSoundBankEntries);
    StreamedBank_free(&audio->streamedBank);
    if (audio->ndspAvailable) ndspExit();
    for (ptrdiff_t i = 1; i < arrlen(base->audioGroups); ++i) {
        if (base->audioGroups[i] != NULL) DataWin_free(base->audioGroups[i]);
    }
    arrfree(base->audioGroups);
    free(base->groupGains);
    free(audio);
}

static void n3ds_audioUpdate(AudioSystem* base, float deltaTime) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    u64 nowTick = svcGetSystemTick();
    LightLock_Lock(&audio->lock);
    if (AudioSystem_updateGroupGains(base, deltaTime)) {
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            if (audio->instances[i].active) n3ds_audioApplyMix(audio, &audio->instances[i]);
        }
    }
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        N3DSSoundInstance* inst = &audio->instances[i];
        if (!inst->active) continue;

        if (!inst->paused && inst->gainFadeRemaining > 0.0f && deltaTime > 0.0f) {
            inst->gainFadeRemaining -= deltaTime;
            inst->gain = inst->gainFadeRemaining <= 0.0f
                ? inst->gainFadeTarget
                : inst->gainFadeStart + (inst->gainFadeTarget - inst->gainFadeStart) *
                    (1.0f - inst->gainFadeRemaining / inst->gainFadeDuration);
            n3ds_audioApplyMix(audio, inst);
        }

        if (!inst->isStream) {
            bool finished = n3ds_audioPlaybackFinished(inst, nowTick);
            if (finished && !inst->loop) {
                n3ds_audioReleaseInstance(audio, inst);
            }
            continue;
        }

        bool anyQueued = false;
        repeat(N3DS_STREAM_BUFFER_COUNT, bufferIndex) {
            ndspWaveBuf* waveBuf = &inst->waveBufs[bufferIndex];
            bool secondaryReadyToReuse = true;
            if (inst->useNativeAdpcm && inst->secondaryChannelId >= 0) {
                secondaryReadyToReuse =
                    inst->secondaryWaveBufs[bufferIndex].status == NDSP_WBUF_DONE;
            }
            if (waveBuf->status == NDSP_WBUF_DONE && secondaryReadyToReuse) {
                waveBuf->status = NDSP_WBUF_FREE;
                if (inst->useNativeAdpcm && inst->secondaryChannelId >= 0) {
                    inst->secondaryWaveBufs[bufferIndex].status = NDSP_WBUF_FREE;
                }
            }
            if (waveBuf->status == NDSP_WBUF_FREE && inst->asyncFill.ready &&
                inst->asyncFill.bufferIndex == (int)bufferIndex) {
                n3ds_audioApplyFillLocked(audio, inst);
            }
            if (waveBuf->status == NDSP_WBUF_FREE && !inst->useNativeAdpcm &&
                !inst->streamFinished && !inst->asyncFill.requested &&
                !inst->asyncFill.inProgress && !inst->asyncFill.ready) {
                n3ds_audioRequestFillLocked(audio, inst, (int)bufferIndex);
            }
            bool primaryBusy = waveBuf->status != NDSP_WBUF_FREE;
            bool secondaryBusy = inst->useNativeAdpcm && inst->secondaryChannelId >= 0 &&
                inst->secondaryWaveBufs[bufferIndex].status != NDSP_WBUF_FREE;
            if (primaryBusy || secondaryBusy) {
                anyQueued = true;
            }
        }

        if (inst->pendingStart && (inst->useNativeAdpcm || anyQueued)) {
            n3ds_audioApplyMix(audio, inst);
            inst->pendingStart = false;
            ndspChnSetPaused(inst->channelId, inst->paused);
            if (inst->secondaryChannelId >= 0) {
                ndspChnSetPaused(inst->secondaryChannelId, inst->paused);
            }
        }

        if (!anyQueued && !inst->asyncFill.requested && !inst->asyncFill.inProgress &&
            !inst->asyncFill.ready) {
            inst->streamFinished = true;
            n3ds_audioReleaseInstance(audio, inst);
        }
    }
    LightLock_Unlock(&audio->lock);
}

static int32_t n3ds_audioPlaySound(
    AudioSystem* base, int32_t soundIndex, int32_t priority, bool loop) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return -1;
    LightLock_Lock(&audio->lock);
    char* path = NULL;
    bool useStreamingPath = soundIndex >= N3DS_AUDIO_STREAM_INDEX_BASE;
    bool useArchiveStream = false;
    Sound* sound = NULL;
    const uint8_t* packedData = NULL;
    uint32_t packedDataSize = 0;
    bool hasPackedBlob = false;
    const N3DSPackedSoundBankEntry* packedEntry = NULL;
    const StreamedBankEntry* archiveEntry = NULL;

    if (useStreamingPath) {
        N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundIndex);
        if (stream == NULL) {
            LightLock_Unlock(&audio->lock);
            return -1;
        }
        path = safeStrdup(stream->path);
    } else {
        DataWin* dw = audio->base.audioGroups[0];
        if (soundIndex < 0 || (uint32_t)soundIndex >= dw->sond.count) {
            LightLock_Unlock(&audio->lock);
            return -1;
        }
        sound = &dw->sond.sounds[soundIndex];
        packedEntry = n3ds_audioGetSoundEntry(audio, soundIndex);
        archiveEntry = StreamedBank_find(&audio->streamedBank, soundIndex);
        useArchiveStream = archiveEntry != NULL;
        if (archiveEntry != NULL) {
            path = safeStrdup(audio->streamedBank.path);
        } else {
            hasPackedBlob = packedEntry != NULL && packedEntry->size > 0;
        }
    }

    if (path == NULL && !hasPackedBlob) {
        if (audio->lastResolveFailureSound != soundIndex) {
            audio->lastResolveFailureSound = soundIndex;
            logEvent("audio_fail",
                "could not resolve sound %ld name=%s file=%s audioFile=%ld group=%ld",
                (long)soundIndex, sound != NULL && sound->name != NULL ? sound->name : "<null>",
                sound != NULL && sound->file != NULL ? sound->file : "<null>",
                sound != NULL ? (long)sound->audioFile : -1L,
                sound != NULL ? (long)sound->audioGroup : -1L);
        }
        LightLock_Unlock(&audio->lock);
        return -1;
    }

    if (!useStreamingPath && !useArchiveStream && !loop) {
        u64 nowTick = svcGetSystemTick();
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            N3DSSoundInstance* existing = &audio->instances[i];
            if (!existing->active || existing->soundIndex != soundIndex || existing->isStream)
                continue;
            if (n3ds_audioPlaybackFinished(existing, nowTick)) {
                n3ds_audioReleaseInstance(audio, existing);
            }
        }
    }

    bool preserveExistingMusicInstance = (useStreamingPath || useArchiveStream) && loop;
    if (preserveExistingMusicInstance) {
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            N3DSSoundInstance* existing = &audio->instances[i];
            if (!existing->active || existing->soundIndex != soundIndex || !existing->isStream ||
                !existing->loop)
                continue;
            if (useStreamingPath) {
                N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundIndex);
                if (stream != NULL) {
                    existing->gain = stream->gain;
                    existing->pitch = stream->pitch;
                    n3ds_audioApplyMix(audio, existing);
                }
            }
            if (existing->paused) {
                n3ds_audioSetInstancePaused(existing, false);
            }
            existing->priority = priority;
            free(path);
            int32_t instanceId = existing->instanceId;
            LightLock_Unlock(&audio->lock);
            return instanceId;
        }
    }

    bool shouldReplaceExistingInstance = useStreamingPath || (useArchiveStream && loop);
    if (shouldReplaceExistingInstance) {
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            if (audio->instances[i].active && audio->instances[i].soundIndex == soundIndex) {
                n3ds_audioReleaseInstance(audio, &audio->instances[i]);
            }
        }
    }

    N3DSSoundInstance* inst =
        n3ds_audioFindFreeInstance(audio, useStreamingPath || useArchiveStream, priority);
    if (inst == NULL) {
        free(path);
        LightLock_Unlock(&audio->lock);
        return -1;
    }

    memset(inst, 0, sizeof(*inst));
    inst->loop = loop;
    inst->soundIndex = soundIndex;
    inst->priority = priority;
    do {
        if (audio->nextInstanceCounter >=
            N3DS_AUDIO_STREAM_INDEX_BASE - N3DS_SOUND_INSTANCE_ID_BASE) {
            audio->nextInstanceCounter = 0;
        }
        inst->instanceId = N3DS_SOUND_INSTANCE_ID_BASE + (int32_t)audio->nextInstanceCounter++;
    } while (n3ds_audioFindInstanceById(audio, inst->instanceId) != NULL);
    inst->active = true;
    inst->channelId = -1;
    inst->secondaryChannelId = -1;
    inst->gain = Audio_sanitizeGain(sound != NULL ? sound->volume : 1.0f);
    inst->pitch = Audio_sanitizePitch((sound != NULL && sound->pitch > 0.0f) ? sound->pitch : 1.0f);
    inst->streamGeneration = 1u;

    if (useStreamingPath || useArchiveStream) {
        N3DSDspAdpcm musicDspAdpcm;
        memset(&musicDspAdpcm, 0, sizeof(musicDspAdpcm));
        N3DSStreamFormat streamFormat = N3DS_STREAM_FORMAT_NONE;

        N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundIndex);
        if (useStreamingPath) {
            if (stream == NULL) {
                goto fail;
            }
            inst->gain = stream->gain;
            inst->pitch = stream->pitch;
            streamFormat = stream->format;
            musicDspAdpcm = stream->dspAdpcm;
        } else if (archiveEntry != NULL) {
            streamFormat = N3DS_STREAM_FORMAT_ARCHIVE;
        } else {
            bool isVorbis = Audio_pathHasExtension(path, ".ogg");
            streamFormat = isVorbis ? N3DS_STREAM_FORMAT_OGG_VORBIS : N3DS_STREAM_FORMAT_DSP_ADPCM;
            if (streamFormat == N3DS_STREAM_FORMAT_DSP_ADPCM &&
                !n3ds_audioParseDspAdpcmFile(path, &musicDspAdpcm)) {
                logEvent("audio_fail", "failed to parse music header %s", path);
                goto fail;
            }
        }

        bool started = false;
        if (streamFormat == N3DS_STREAM_FORMAT_ARCHIVE) {
            started = n3ds_audioStartArchiveStream(audio, inst, archiveEntry);
        } else if (streamFormat == N3DS_STREAM_FORMAT_PCM_WAV) {
            started = n3ds_audioStartWavStream(audio, inst, path, stream);
        } else if (streamFormat == N3DS_STREAM_FORMAT_OGG_VORBIS) {
            started = n3ds_audioStartVorbisStream(audio, inst, path);
        } else {
            started =
                n3ds_audioStartAdpcmFile(audio, inst, path, &musicDspAdpcm);
        }
        if (!started) {
            logEvent("audio_fail", "stream start failed sound=%ld path=%s", (long)soundIndex, path);
            goto fail;
        }
        logEvent("audio_start",
            "stream sound=%ld instance=%ld format=%d native=%d loop=%d rate=%lu ch=%lu path=%s",
            (long)soundIndex, (long)inst->instanceId, (int)inst->streamFormat,
            inst->useNativeAdpcm ? 1 : 0, loop ? 1 : 0, (unsigned long)inst->sampleRate,
            (unsigned long)inst->channelCount, path);
        free(path);
        int32_t instanceId = inst->instanceId;
        LightLock_Unlock(&audio->lock);
        return instanceId;
    }

    const uint8_t* blob = NULL;
    uint32_t blobSize = 0;
    N3DSDspAdpcm dspAdpcm;
    memset(&dspAdpcm, 0, sizeof(dspAdpcm));

    packedData = n3ds_audioGetSoundData(audio, soundIndex, &packedDataSize);
    if (packedData == NULL) {
        logEvent("audio_fail", "bank read failed sound=%ld", (long)soundIndex);
        goto fail;
    }
    blob = packedData;
    blobSize = packedDataSize;
    bool parsed = n3ds_audioIsPackedDspAdpcm(packedEntry) &&
        n3ds_audioParseDspAdpcmBlob(blob, blobSize, &dspAdpcm);
    if (!parsed) {
        logEvent("audio_fail", "bank parse failed sound=%ld", (long)soundIndex);
        goto fail;
    }
    free(path);
    path = NULL;

    if (!n3ds_audioCanUseNativeAdpcm(&dspAdpcm, loop) ||
        !n3ds_audioStartAdpcm(audio, inst, blob, blobSize, &dspAdpcm, false, false)) {
        logEvent("audio_fail", "native start failed sound=%ld", (long)soundIndex);
        goto fail;
    }

    int32_t instanceId = inst->instanceId;
    LightLock_Unlock(&audio->lock);
    return instanceId;

fail:
    free(path);
    n3ds_audioReleaseInstance(audio, inst);
    LightLock_Unlock(&audio->lock);
    return -1;
}

static void n3ds_audioStopSound(AudioSystem* base, int32_t soundOrInstance) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    LightLock_Lock(&audio->lock);
    if (Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        if (inst != NULL) n3ds_audioReleaseInstance(audio, inst);
        LightLock_Unlock(&audio->lock);
        return;
    }
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active && audio->instances[i].soundIndex == soundOrInstance) {
            n3ds_audioReleaseInstance(audio, &audio->instances[i]);
        }
    }
    LightLock_Unlock(&audio->lock);
}

static void n3ds_audioStopAll(AudioSystem* base) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    LightLock_Lock(&audio->lock);
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        n3ds_audioReleaseInstance(audio, &audio->instances[i]);
    }
    LightLock_Unlock(&audio->lock);
}

static bool n3ds_audioIsPlaying(AudioSystem* base, int32_t soundOrInstance) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return false;
    LightLock_Lock(&audio->lock);
    if (Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        bool isPlaying = inst != NULL && ndspChnIsPlaying(inst->channelId) && !inst->paused;
        LightLock_Unlock(&audio->lock);
        return isPlaying;
    }
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active && audio->instances[i].soundIndex == soundOrInstance) {
            bool isPlaying =
                ndspChnIsPlaying(audio->instances[i].channelId) && !audio->instances[i].paused;
            LightLock_Unlock(&audio->lock);
            return isPlaying;
        }
    }
    LightLock_Unlock(&audio->lock);
    return false;
}

static void n3ds_audioSetPaused(AudioSystem* base, int32_t soundOrInstance, bool all, bool paused) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    LightLock_Lock(&audio->lock);
    if (!all && Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        if (inst != NULL) n3ds_audioSetInstancePaused(inst, paused);
    } else {
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            N3DSSoundInstance* inst = &audio->instances[i];
            if (inst->active && (all || inst->soundIndex == soundOrInstance)) {
                n3ds_audioSetInstancePaused(inst, paused);
            }
        }
    }
    LightLock_Unlock(&audio->lock);
}

static void n3ds_audioPauseSound(AudioSystem* base, int32_t soundOrInstance) {
    n3ds_audioSetPaused(base, soundOrInstance, false, true);
}

static void n3ds_audioResumeSound(AudioSystem* base, int32_t soundOrInstance) {
    n3ds_audioSetPaused(base, soundOrInstance, false, false);
}

static void n3ds_audioPauseAll(AudioSystem* base) {
    n3ds_audioSetPaused(base, 0, true, true);
}

static void n3ds_audioResumeAll(AudioSystem* base) {
    n3ds_audioSetPaused(base, 0, true, false);
}

static void n3ds_audioSetInstanceGain(
    N3DSAudioSystem* audio, N3DSSoundInstance* inst, float gain, uint32_t timeMs) {
    if (timeMs == 0u) {
        inst->gainFadeRemaining = 0.0f;
        inst->gain = gain;
        n3ds_audioApplyMix(audio, inst);
    } else {
        inst->gainFadeStart = inst->gain;
        inst->gainFadeTarget = gain;
        inst->gainFadeDuration = (float)timeMs / 1000.0f;
        inst->gainFadeRemaining = inst->gainFadeDuration;
    }
}

static void n3ds_audioSetSoundGain(
    AudioSystem* base, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    LightLock_Lock(&audio->lock);
    gain = Audio_sanitizeGain(gain);
    if (Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        if (inst == NULL) {
            LightLock_Unlock(&audio->lock);
            return;
        }
        n3ds_audioSetInstanceGain(audio, inst, gain, timeMs);
        if (inst->soundIndex >= N3DS_AUDIO_STREAM_INDEX_BASE) {
            N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, inst->soundIndex);
            if (stream != NULL) stream->gain = gain;
        }
        LightLock_Unlock(&audio->lock);
        return;
    }
    N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundOrInstance);
    if (stream != NULL) stream->gain = gain;
    bool streamIndex = n3ds_audioStreamSlotForSound(soundOrInstance) >= 0;
    if (!streamIndex || DataWin_isVersionAtLeast(base->dw, 2024, 11, 0, 0)) {
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            N3DSSoundInstance* inst = &audio->instances[i];
            if (inst->active && inst->soundIndex == soundOrInstance) {
                n3ds_audioSetInstanceGain(audio, inst, gain, timeMs);
            }
        }
    }
    LightLock_Unlock(&audio->lock);
}

static float n3ds_audioGetSoundGain(AudioSystem* base, int32_t soundOrInstance) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return 1.0f;
    LightLock_Lock(&audio->lock);
    if (Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        float gain = inst != NULL ? inst->gain : 1.0f;
        LightLock_Unlock(&audio->lock);
        return gain;
    }
    N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundOrInstance);
    if (stream != NULL) {
        float gain = stream->gain;
        LightLock_Unlock(&audio->lock);
        return gain;
    }
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active && audio->instances[i].soundIndex == soundOrInstance) {
            float gain = audio->instances[i].gain;
            LightLock_Unlock(&audio->lock);
            return gain;
        }
    }
    LightLock_Unlock(&audio->lock);
    return 1.0f;
}

static void n3ds_audioSetSoundPitch(AudioSystem* base, int32_t soundOrInstance, float pitch) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    LightLock_Lock(&audio->lock);
    pitch = Audio_sanitizePitch(pitch);
    if (Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        if (inst == NULL) {
            LightLock_Unlock(&audio->lock);
            return;
        }
        inst->pitch = pitch;
        if (inst->soundIndex >= N3DS_AUDIO_STREAM_INDEX_BASE) {
            N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, inst->soundIndex);
            if (stream != NULL) stream->pitch = pitch;
        }
        n3ds_audioApplyMix(audio, inst);
        n3ds_audioRefreshReleaseTick(inst);
        LightLock_Unlock(&audio->lock);
        return;
    }
    N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundOrInstance);
    if (stream != NULL) stream->pitch = pitch;
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        N3DSSoundInstance* inst = &audio->instances[i];
        if (inst->active && inst->soundIndex == soundOrInstance) {
            inst->pitch = pitch;
            n3ds_audioApplyMix(audio, inst);
            n3ds_audioRefreshReleaseTick(inst);
        }
    }
    LightLock_Unlock(&audio->lock);
}

static float n3ds_audioGetSoundPitch(AudioSystem* base, int32_t soundOrInstance) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return 1.0f;
    LightLock_Lock(&audio->lock);
    if (Audio_isInstanceId(soundOrInstance)) {
        N3DSSoundInstance* inst = n3ds_audioFindInstanceById(audio, soundOrInstance);
        float pitch = inst != NULL ? inst->pitch : 1.0f;
        LightLock_Unlock(&audio->lock);
        return pitch;
    }
    N3DSStreamEntry* stream = n3ds_audioActiveStreamEntry(audio, soundOrInstance);
    if (stream != NULL) {
        float pitch = stream->pitch;
        LightLock_Unlock(&audio->lock);
        return pitch;
    }
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active && audio->instances[i].soundIndex == soundOrInstance) {
            float pitch = audio->instances[i].pitch;
            LightLock_Unlock(&audio->lock);
            return pitch;
        }
    }
    LightLock_Unlock(&audio->lock);
    return 1.0f;
}

static float n3ds_audioGetTrackPosition(AudioSystem* base, int32_t soundOrInstance) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return 0.0f;
    LightLock_Lock(&audio->lock);
    N3DSSoundInstance* inst = n3ds_audioFindInstance(audio, soundOrInstance);
    if (inst == NULL || inst->sampleRate == 0) {
        LightLock_Unlock(&audio->lock);
        return 0.0f;
    }

    if (inst->useNativeAdpcm) {
        uint16_t currentSeq = ndspChnGetWaveBufSeq(inst->channelId);
        repeat(N3DS_STREAM_BUFFER_COUNT, i) {
            if (inst->waveBufs[i].sequence_id == currentSeq) {
                uint32_t samplePos = ndspChnGetSamplePos(inst->channelId);
                float position =
                    (float)(inst->bufferStartSample[i] + samplePos) / (float)inst->sampleRate;
                LightLock_Unlock(&audio->lock);
                return position;
            }
        }
        float position = (float)ndspChnGetSamplePos(inst->channelId) / (float)inst->sampleRate;
        LightLock_Unlock(&audio->lock);
        return position;
    }

    uint16_t currentSeq = ndspChnGetWaveBufSeq(inst->channelId);
    repeat(N3DS_STREAM_BUFFER_COUNT, i) {
        if (inst->waveBufs[i].sequence_id == currentSeq) {
            uint32_t samplePos = ndspChnGetSamplePos(inst->channelId);
            float position =
                (float)(inst->bufferStartSample[i] + samplePos) / (float)inst->sampleRate;
            LightLock_Unlock(&audio->lock);
            return position;
        }
    }

    float position = (float)inst->currentSample / (float)inst->sampleRate;
    LightLock_Unlock(&audio->lock);
    return position;
}

static void n3ds_audioSetTrackPosition(
    AudioSystem* base, int32_t soundOrInstance, float positionSeconds) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return;
    LightLock_Lock(&audio->lock);
    N3DSSoundInstance* inst = n3ds_audioFindInstance(audio, soundOrInstance);
    if (inst == NULL || inst->sampleRate == 0) {
        LightLock_Unlock(&audio->lock);
        return;
    }

    if (!isfinite(positionSeconds) || positionSeconds < 0.0f) positionSeconds = 0.0f;
    double requestedSample = (double)positionSeconds * inst->sampleRate;
    uint32_t targetSample =
        requestedSample >= inst->sampleCount ? inst->sampleCount : (uint32_t)requestedSample;
    if (inst->useNativeAdpcm) {
        if (targetSample != 0) {
            logEvent("audio_seek", "reject native instance=%ld target=%lu", (long)inst->instanceId,
                (unsigned long)targetSample);
            LightLock_Unlock(&audio->lock);
            return;
        }
        logEvent("audio_seek", "native instance=%ld target=%lu", (long)inst->instanceId,
            (unsigned long)targetSample);
        n3ds_audioRebuildChannels(audio, inst);
        if (!n3ds_audioPrimeNativeAdpcm(inst)) {
            LightLock_Unlock(&audio->lock);
            return;
        }
        if (inst->paused) {
            ndspChnSetPaused(inst->channelId, true);
            if (inst->secondaryChannelId >= 0) ndspChnSetPaused(inst->secondaryChannelId, true);
        }
        LightLock_Unlock(&audio->lock);
        return;
    }

    n3ds_audioCancelFillLocked(audio, inst);
    logEvent("audio_seek", "stream instance=%ld target=%lu", (long)inst->instanceId,
        (unsigned long)targetSample);
    n3ds_audioRebuildChannels(audio, inst);
    if (inst->archiveStream != NULL) {
        if (!StreamedAudio_seek(inst->archiveStream, targetSample)) {
            LightLock_Unlock(&audio->lock);
            return;
        }
        inst->currentSample = StreamedAudio_tell(inst->archiveStream);
    } else if (inst->streamFormat == N3DS_STREAM_FORMAT_PCM_WAV) {
        long offset =
            (long)inst->wavDataOffset + (long)targetSample * inst->channelCount * sizeof(int16_t);
        if (inst->streamFile == NULL || fseek(inst->streamFile, offset, SEEK_SET) != 0) {
            LightLock_Unlock(&audio->lock);
            return;
        }
        inst->currentSample = targetSample;
    } else {
        if (!inst->vorbisOpen || ov_pcm_seek(&inst->vorbisFile, (ogg_int64_t)targetSample) != 0) {
            LightLock_Unlock(&audio->lock);
            return;
        }
        ogg_int64_t currentSample = ov_pcm_tell(&inst->vorbisFile);
        inst->currentSample = currentSample >= 0 && currentSample <= UINT32_MAX
            ? (uint32_t)currentSample
            : targetSample;
    }
    n3ds_audioResetWaveBufs(inst);
    inst->streamFinished = false;
    n3ds_audioRequestFillLocked(audio, inst, 0);
    LightLock_Unlock(&audio->lock);
}

static float n3ds_audioGetSoundLength(AudioSystem* base, int32_t soundOrInstance) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return 0.0f;
    LightLock_Lock(&audio->lock);
    N3DSSoundInstance* inst = n3ds_audioFindInstance(audio, soundOrInstance);
    if (inst != NULL && inst->sampleRate > 0) {
        float length = (float)inst->sampleCount / (float)inst->sampleRate;
        LightLock_Unlock(&audio->lock);
        return length;
    }

    const StreamedBankEntry* entry = StreamedBank_find(&audio->streamedBank, soundOrInstance);
    if (entry != NULL) {
        float length = (float)entry->sampleCount / (float)entry->sampleRate;
        LightLock_Unlock(&audio->lock);
        return length;
    }

    int32_t slot = n3ds_audioStreamSlotForSound(soundOrInstance);
    if (slot >= 0) {
        N3DSStreamEntry* stream = &audio->streams[slot];
        if (stream->active && stream->sampleRate > 0) {
            float length = (float)stream->sampleCount / (float)stream->sampleRate;
            LightLock_Unlock(&audio->lock);
            return length;
        }
    }

    LightLock_Unlock(&audio->lock);
    return 0.0f;
}

static void n3ds_audioSetMasterGain(AudioSystem* base, float gain) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    gain = Audio_sanitizeGain(gain);
    if (!audio->ndspAvailable) {
        audio->masterGain = gain;
        return;
    }
    LightLock_Lock(&audio->lock);
    audio->masterGain = gain;
    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active) n3ds_audioApplyMix(audio, &audio->instances[i]);
    }
    LightLock_Unlock(&audio->lock);
}

static void n3ds_audioSetListenerGain(AudioSystem* base, float gain, int32_t listenerId) {
    if (listenerId != 0) return;
    n3ds_audioSetMasterGain(base, gain);
}

static void n3ds_audioSetSoundSpatial(MAYBE_UNUSED AudioSystem* base,
    MAYBE_UNUSED int32_t instanceId, MAYBE_UNUSED float x, MAYBE_UNUSED float y,
    MAYBE_UNUSED float z, MAYBE_UNUSED float vx, MAYBE_UNUSED float vy, MAYBE_UNUSED float vz,
    MAYBE_UNUSED float ref, MAYBE_UNUSED float max, MAYBE_UNUSED float factor) {}
static void n3ds_audioSetListenerPos(MAYBE_UNUSED AudioSystem* base, MAYBE_UNUSED float x,
    MAYBE_UNUSED float y, MAYBE_UNUSED float z) {}
static void n3ds_audioSetGroupGain(
    AudioSystem* base, int32_t groupIndex, float gain, uint32_t timeMs) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    LightLock_Lock(&audio->lock);
    AudioSystem_setGroupGain(base, groupIndex, Audio_sanitizeGain(gain), timeMs);
    if (timeMs == 0 && audio->ndspAvailable) {
        repeat(N3DS_MAX_SOUND_INSTANCES, i) {
            if (audio->instances[i].active) n3ds_audioApplyMix(audio, &audio->instances[i]);
        }
    }
    LightLock_Unlock(&audio->lock);
}

static void n3ds_audioSetChannelCount(MAYBE_UNUSED AudioSystem* base, MAYBE_UNUSED int32_t count) {}
static void n3ds_audioGroupLoad(AudioSystem* base, int32_t groupIndex) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (groupIndex <= 0 || base->dw == NULL || (uint32_t)groupIndex >= base->dw->agrp.count) return;
    while (arrlen(base->audioGroups) <= groupIndex)
        arrput(base->audioGroups, NULL);
    if (base->audioGroups[groupIndex] != NULL) return;

    const char* path =
        base->dw->agrp.audioGroups != NULL ? base->dw->agrp.audioGroups[groupIndex].path : NULL;
    char fallback[32];
    if (path == NULL || path[0] == '\0') {
        snprintf(fallback, sizeof(fallback), "audiogroup%ld.dat", (long)groupIndex);
        path = fallback;
    }
    DataWin* group = NULL;
    if (audio->fileSystem != NULL &&
        audio->fileSystem->vtable->fileExists(audio->fileSystem, path)) {
        char* resolved = audio->fileSystem->vtable->resolvePath(audio->fileSystem, path);
        if (resolved != NULL) {
            DataWinParserOptions options = {0};
            options.parseAudo = true;
            options.lazyLoadAudio = base->dw->lazyLoadAudio;
            if (base->dw->mappedFile != NULL) options.loadType = DATAWINLOADTYPE_MAP_FILE;
            group = DataWin_parse(resolved, options);
            free(resolved);
        }
    }
    base->audioGroups[groupIndex] = group != NULL ? group : safeCalloc(1, sizeof(DataWin));
}
static bool n3ds_audioGroupIsLoaded(AudioSystem* base, int32_t groupIndex) {
    return groupIndex >= 0 && arrlen(base->audioGroups) > groupIndex &&
        base->audioGroups[groupIndex] != NULL;
}

static int32_t n3ds_audioCreateStream(AudioSystem* base, const char* filename) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return -1;
    LightLock_Lock(&audio->lock);
    if (filename == NULL || filename[0] == '\0') {
        LightLock_Unlock(&audio->lock);
        return -1;
    }

    char* path = n3ds_audioResolveAudioPath(audio, filename);
    if (path == NULL) {
        logEvent("audio_fail", "createStream failed to resolve %s", filename);
        LightLock_Unlock(&audio->lock);
        return -1;
    }

    repeat(N3DS_MAX_STREAMS, i) {
        if (audio->streams[i].active && audio->streams[i].path != NULL &&
            strcmp(audio->streams[i].path, path) == 0) {
            audio->streams[i].refCount += 1u;
            free(path);
            LightLock_Unlock(&audio->lock);
            return N3DS_AUDIO_STREAM_INDEX_BASE + (int32_t)i;
        }
    }

    repeat(N3DS_MAX_STREAMS, i) {
        if (!audio->streams[i].active) {
            N3DSStreamEntry* stream = &audio->streams[i];
            bool isWav = Audio_pathHasExtension(path, ".wav");
            bool isVorbis = Audio_pathHasExtension(path, ".ogg");
            if (isWav) {
                if (!n3ds_audioParsePcmWavFile(path, &stream->sampleRate, &stream->sampleCount,
                        &stream->channelCount, &stream->wavDataOffset)) {
                    free(path);
                    LightLock_Unlock(&audio->lock);
                    return -1;
                }
                stream->format = N3DS_STREAM_FORMAT_PCM_WAV;
                memset(&stream->dspAdpcm, 0, sizeof(stream->dspAdpcm));
            } else if (isVorbis) {
                if (!Audio_readVorbisStreamInfo(path, N3DS_AUDIO_FILE_BUFFER_SIZE,
                        &stream->sampleRate, &stream->sampleCount, &stream->channelCount)) {
                    free(path);
                    LightLock_Unlock(&audio->lock);
                    return -1;
                }
                stream->format = N3DS_STREAM_FORMAT_OGG_VORBIS;
                memset(&stream->dspAdpcm, 0, sizeof(stream->dspAdpcm));
            } else {
                N3DSDspAdpcm dspAdpcm;
                if (!n3ds_audioParseDspAdpcmFile(path, &dspAdpcm)) {
                    free(path);
                    LightLock_Unlock(&audio->lock);
                    return -1;
                }
                stream->format = N3DS_STREAM_FORMAT_DSP_ADPCM;
                stream->sampleRate = dspAdpcm.sampleRate;
                stream->sampleCount = dspAdpcm.sampleCount;
                stream->channelCount = dspAdpcm.channelCount;
                stream->dspAdpcm = dspAdpcm;
            }

            stream->active = true;
            stream->path = path;
            stream->gain = 1.0f;
            stream->pitch = 1.0f;
            stream->refCount = 1u;
            LightLock_Unlock(&audio->lock);
            return N3DS_AUDIO_STREAM_INDEX_BASE + (int32_t)i;
        }
    }
    logEvent("audio_fail", "createStream exhausted slots for %s", path);
    free(path);
    LightLock_Unlock(&audio->lock);
    return -1;
}

static bool n3ds_audioDestroyStream(AudioSystem* base, int32_t streamIndex) {
    N3DSAudioSystem* audio = (N3DSAudioSystem*)base;
    if (!audio->ndspAvailable) return false;
    LightLock_Lock(&audio->lock);
    int32_t slot = streamIndex - N3DS_AUDIO_STREAM_INDEX_BASE;
    if (slot < 0 || slot >= N3DS_MAX_STREAMS || !audio->streams[slot].active) {
        LightLock_Unlock(&audio->lock);
        return false;
    }

    if (audio->streams[slot].refCount > 1u) {
        audio->streams[slot].refCount -= 1u;
        LightLock_Unlock(&audio->lock);
        return true;
    }

    repeat(N3DS_MAX_SOUND_INSTANCES, i) {
        if (audio->instances[i].active && audio->instances[i].soundIndex == streamIndex) {
            n3ds_audioReleaseInstance(audio, &audio->instances[i]);
        }
    }

    free(audio->streams[slot].path);
    memset(&audio->streams[slot], 0, sizeof(audio->streams[slot]));
    LightLock_Unlock(&audio->lock);
    return true;
}

static AudioSystemVtable N3DSAudio_vtable = {
    .init = n3ds_audioInit,
    .destroy = n3ds_audioDestroy,
    .update = n3ds_audioUpdate,
    .playSound = n3ds_audioPlaySound,
    .stopSound = n3ds_audioStopSound,
    .stopAll = n3ds_audioStopAll,
    .isPlaying = n3ds_audioIsPlaying,
    .pauseSound = n3ds_audioPauseSound,
    .resumeSound = n3ds_audioResumeSound,
    .pauseAll = n3ds_audioPauseAll,
    .resumeAll = n3ds_audioResumeAll,
    .suspend = n3ds_audioPauseAll,
    .resume = n3ds_audioResumeAll,
    .setSoundSpatial = n3ds_audioSetSoundSpatial,
    .setListenerPosition = n3ds_audioSetListenerPos,
    .setSoundGain = n3ds_audioSetSoundGain,
    .getSoundGain = n3ds_audioGetSoundGain,
    .setSoundPitch = n3ds_audioSetSoundPitch,
    .getSoundPitch = n3ds_audioGetSoundPitch,
    .getTrackPosition = n3ds_audioGetTrackPosition,
    .setTrackPosition = n3ds_audioSetTrackPosition,
    .getSoundLength = n3ds_audioGetSoundLength,
    .setMasterGain = n3ds_audioSetMasterGain,
    .setMasterGainForListener = n3ds_audioSetListenerGain,
    .setChannelCount = n3ds_audioSetChannelCount,
    .setGroupGain = n3ds_audioSetGroupGain,
    .groupLoad = n3ds_audioGroupLoad,
    .groupIsLoaded = n3ds_audioGroupIsLoaded,
    .createStream = n3ds_audioCreateStream,
    .destroyStream = n3ds_audioDestroyStream,
};

N3DSAudioSystem* n3ds_audioCreate(void) {
    N3DSAudioSystem* audio = safeCalloc(1, sizeof(N3DSAudioSystem));
    audio->base.vtable = &N3DSAudio_vtable;
    audio->masterGain = 1.0f;
    audio->nextStreamChannel = N3DS_STREAM_CHANNEL_FIRST;
    audio->nextSoundChannel = N3DS_SOUND_CHANNEL_FIRST;
    audio->lastResolveFailureSound = INT32_MIN;
    return audio;
}

bool n3ds_audioIsReady(const N3DSAudioSystem* audio) {
    return audio != NULL && audio->startupError == NULL;
}

const char* n3ds_audioGetStartupError(const N3DSAudioSystem* audio) {
    return audio != NULL ? audio->startupError : NULL;
}
