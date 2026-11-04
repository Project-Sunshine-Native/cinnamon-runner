#include "wii_audio_system.h"
#include "wii_stream_worker.h"
#include "../common/audio_ids.h"
#include "../common/audio_util.h"
#include "../common/sound_bank.h"
#include "../common/streamed_audio.h"
#include "../common/vorbis_probe.h"
#include "../../data_win.h"
#include "../../log.h"
#include "../../utils.h"
#include "../../wii/wii_log.h"
#include "stb_ds.h"
#include <gccore.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WII_AUDIO_MAX_INSTANCES 64
#define WII_AUDIO_MAX_STREAMS 16
#define WII_AUDIO_INSTANCE_BASE AUDIO_SOUND_INSTANCE_ID_BASE
#define WII_AUDIO_STREAM_INDEX_BASE AUDIO_STREAM_INDEX_BASE
#define WII_AUDIO_STREAM_DECODE_BUFFER_FRAMES 32768u
#define WII_AUDIO_STREAM_REFILL_TARGET_FRAMES 16384u
#define WII_AUDIO_STREAM_MAX_COPY_FRAMES_PER_PUMP 2048u
#define WII_AUDIO_OUTPUT_SAMPLE_RATE 48000u
#define WII_AUDIO_DMA_BUFFER_FRAMES 400u
#define WII_AUDIO_DMA_BUFFER_BYTES (WII_AUDIO_DMA_BUFFER_FRAMES * 4u)
#define WII_AUDIO_DMA_BUFFER_COUNT 6u
#define WII_AUDIO_DMA_NO_BUFFER UINT32_MAX
#define WII_AUDIO_STOP_RAMP_FRAMES 256u

typedef enum {
    WII_DMA_BUFFER_FREE,
    WII_DMA_BUFFER_MIXING,
    WII_DMA_BUFFER_READY,
    WII_DMA_BUFFER_IN_FLIGHT,
} WiiDmaBufferState;

typedef struct {
    uint32_t size;
    uint16_t coefs[16];
    uint16_t startHistory1;
    uint16_t startHistory2;
    const uint8_t* cachedData;
} WiiAdpcmChannel;

typedef struct {
    bool loaded;
    uint32_t frames;
    uint32_t sampleRate;
    uint8_t channels;
    WiiAdpcmChannel channel[2];
} WiiAdpcmSound;

typedef struct {
    bool active;
    char* path;
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint8_t channels;
    uint32_t refCount;
    float gain;
    float pitch;
    bool dspAdpcm;
} WiiAudioStream;

typedef struct {
    bool active;
    bool paused;
    bool loop;
    bool streaming;
    bool streamFinished;
    int32_t soundIndex;
    int32_t instanceId;
    int32_t priority;
    float gain;
    float gainFadeStart;
    float gainFadeTarget;
    float gainFadeDuration;
    float gainFadeElapsed;
    float pitch;
    float elapsed;
    bool stopping;
    uint32_t stopRampFrames;
    uint32_t stopRampTotalFrames;
    const WiiAdpcmSound* sound;
    const WiiAudioStream* stream;
    uint32_t streamSampleRate;
    uint32_t streamSampleCount;
    WiiStreamJob* streamJob;
    uint8_t streamChannels;
    int16_t* streamDecodePcm;
    uint32_t streamDecodeCapacityFrames;
    uint8_t streamDecodeAllocatedChannels;
    uint32_t streamDecodeBaseFrame;
    uint32_t streamDecodedFrames;
    int16_t streamHoldLeft;
    int16_t streamHoldRight;
    double softwarePosition;
    double softwareStep;
    uint32_t softwareDecodedFrameIndex[2];
    uint32_t softwareFrameBaseSample[2];
    int16_t softwareHist1[2];
    int16_t softwareHist2[2];
    int16_t softwareFrameSamples[2][14];
    bool softwareFrameValid[2];
} WiiSoundInstance;

struct WiiAudioSystem {
    AudioSystem base;
    FileSystem* fileSystem;
    uint8_t* soundBankData;
    uint32_t soundBankSize;
    SbkEntry* entries;
    uint32_t entryCount;
    StreamedBank streamedBank;
    WiiAdpcmSound* sounds;
    WiiAudioStream streams[WII_AUDIO_MAX_STREAMS];
    WiiSoundInstance instances[WII_AUDIO_MAX_INSTANCES];
    WiiStreamWorker* streamWorker;
    uint8_t* dmaBuffers[WII_AUDIO_DMA_BUFFER_COUNT];
    uint8_t* dmaSilenceBuffer;
    volatile WiiDmaBufferState dmaBufferStates[WII_AUDIO_DMA_BUFFER_COUNT];
    int32_t mixAccumLeft[WII_AUDIO_DMA_BUFFER_FRAMES];
    int32_t mixAccumRight[WII_AUDIO_DMA_BUFFER_FRAMES];
    uint32_t dmaReadBuffer;
    uint32_t dmaWriteBuffer;
    uint32_t dmaPlayingBuffer;
    uint32_t dmaQueuedBuffer;
    volatile bool dmaStarted;
    float masterGain;
    uint32_t nextInstanceCounter;
    bool initialized;
};

static WiiAudioSystem* gWiiAudioDmaSystem = NULL;

static void releaseInstance(WiiSoundInstance* inst);
static int findInstanceSlot(WiiAudioSystem* audio, int32_t priority);
static void stopSoftwareDma(WiiAudioSystem* audio);
static void refillSoftwareStream(WiiSoundInstance* inst);

static char* resolveStreamCandidate(WiiAudioSystem* audio, const char* candidate) {
    if (audio == NULL || audio->fileSystem == NULL || candidate == NULL || candidate[0] == '\0')
        return NULL;
    FileSystem* fs = audio->fileSystem;
    if (fs->vtable->fileExists(fs, candidate)) return fs->vtable->resolvePath(fs, candidate);
    if (strchr(candidate, ':') != NULL || candidate[0] == '/') return NULL;
    size_t len = strlen(candidate);
    char* audioCandidate = safeMalloc(len + sizeof("audio/"));
    snprintf(audioCandidate, len + sizeof("audio/"), "audio/%s", candidate);
    char* path = fs->vtable->fileExists(fs, audioCandidate)
        ? fs->vtable->resolvePath(fs, audioCandidate)
        : NULL;
    free(audioCandidate);
    return path;
}

static char* resolveStreamPath(WiiAudioSystem* audio, const char* filename) {
    if (filename == NULL || filename[0] == '\0') return NULL;

    const char* base = Audio_pathFileName(filename);
    const char* dot = strrchr(base, '.');
    if (dot == base) return NULL;

    char candidate[512];
    size_t dirLen = (size_t)(base - filename);
    size_t baseLen = dot != NULL ? (size_t)(dot - base) : strlen(base);
    if (dirLen + baseLen + 6u > sizeof(candidate)) return NULL;
    memcpy(candidate, filename, dirLen);
    memcpy(candidate + dirLen, base, baseLen);
    strcpy(candidate + dirLen + baseLen, ".ogg");
    char* resolved = resolveStreamCandidate(audio, candidate);
    if (resolved != NULL) return resolved;

    resolved = resolveStreamCandidate(audio, filename);
    if (resolved != NULL &&
        (Audio_pathHasExtension(resolved, ".wdsp") || Audio_pathHasExtension(resolved, ".ogg")))
        return resolved;
    free(resolved);

    strcpy(candidate + dirLen + baseLen, ".wdsp");
    return resolveStreamCandidate(audio, candidate);
}

static bool readFileFully(const char* path, uint8_t** outData, uint32_t* outSize) {
    *outData = NULL;
    *outSize = 0u;
    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size <= 0 || size > 0x7FFFFFFF) {
        fclose(file);
        return false;
    }
    rewind(file);
    uint8_t* data = safeMalloc((size_t)size);
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(data);
        return false;
    }
    fclose(file);
    *outData = data;
    *outSize = (uint32_t)size;
    return true;
}

static void clearSoundBankIndex(WiiAudioSystem* audio) {
    if (audio == NULL) return;
    free(audio->sounds);
    free(audio->entries);
    audio->sounds = NULL;
    audio->entries = NULL;
    audio->entryCount = 0u;
}

static void writeBeS16Buffer(uint8_t* p, int16_t value) {
    p[0] = (uint8_t)(((uint16_t)value >> 8) & 0xFFu);
    p[1] = (uint8_t)((uint16_t)value & 0xFFu);
}

static int signExtend4(int value) {
    return (value & 0x08) ? (value - 0x10) : value;
}

static int16_t clampS16(int value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}

static float instanceGain(const WiiSoundInstance* inst, float masterGain) {
    if (inst == NULL) return 0.0f;
    float gain = Audio_sanitizeGain(masterGain * inst->gain);
    if (inst->stopping && inst->stopRampTotalFrames > 0u) {
        uint32_t rampFrames = inst->stopRampFrames;
        if (rampFrames > inst->stopRampTotalFrames) rampFrames = inst->stopRampTotalFrames;
        gain *= (float)rampFrames / (float)inst->stopRampTotalFrames;
    }
    return gain;
}

static int32_t applyGain(int16_t sample, float gain) {
    return (int32_t)((float)sample * gain);
}

static void advanceStopRamp(WiiSoundInstance* inst) {
    if (inst == NULL || !inst->stopping) return;
    if (inst->stopRampFrames > 0u) inst->stopRampFrames--;
}

static int16_t lerpS16(int16_t a, int16_t b, double frac) {
    if (frac <= 0.0) return a;
    if (frac >= 1.0) return b;
    double sample = (double)a + ((double)b - (double)a) * frac;
    return clampS16((int)(sample >= 0.0 ? sample + 0.5 : sample - 0.5));
}

static double computeSoftwareStep(uint32_t sampleRate, float pitch) {
    if (sampleRate == 0u) sampleRate = WII_AUDIO_OUTPUT_SAMPLE_RATE;
    if (pitch < 0.01f) pitch = 0.01f;
    double step = ((double)sampleRate * (double)pitch) / (double)WII_AUDIO_OUTPUT_SAMPLE_RATE;
    return step > 0.0 ? step : 0.01;
}

static void resetSoftwareChannel(WiiSoundInstance* inst, uint32_t channelIndex) {
    if (inst == NULL || inst->sound == NULL || channelIndex >= 2u) return;
    const WiiAdpcmChannel* channel = &inst->sound->channel[channelIndex];
    inst->softwareDecodedFrameIndex[channelIndex] = UINT32_MAX;
    inst->softwareFrameBaseSample[channelIndex] = 0u;
    inst->softwareHist1[channelIndex] = (int16_t)channel->startHistory1;
    inst->softwareHist2[channelIndex] = (int16_t)channel->startHistory2;
    inst->softwareFrameValid[channelIndex] = false;
    memset(inst->softwareFrameSamples[channelIndex], 0,
        sizeof(inst->softwareFrameSamples[channelIndex]));
}

static void resetSoftwareDecoder(WiiSoundInstance* inst) {
    if (inst == NULL) return;
    resetSoftwareChannel(inst, 0u);
    resetSoftwareChannel(inst, 1u);
}

static bool decodeSoftwareSamples(const WiiAdpcmChannel* channel, uint32_t frameIndex,
    int16_t* hist1, int16_t* hist2, int16_t out[14]) {
    if (channel->cachedData == NULL || channel->size < 8u || frameIndex > (channel->size - 8u) / 8u)
        return false;

    const uint8_t* frame = channel->cachedData + frameIndex * 8u;
    uint32_t predictor = frame[0] >> 4u;
    if (predictor >= 8u) return false;
    int scale = 1 << (frame[0] & 0x0Fu);
    int coef1 = (int16_t)channel->coefs[predictor * 2u];
    int coef2 = (int16_t)channel->coefs[predictor * 2u + 1u];
    int sampleIndex = 0;

    for (int byteIndex = 1; byteIndex < 8; ++byteIndex) {
        int nibbles[2] = {signExtend4(frame[byteIndex] >> 4), signExtend4(frame[byteIndex] & 0x0F)};
        repeat(2u, nibbleIndex) {
            int sample = nibbles[nibbleIndex] * scale * 2048;
            sample += 1024 + coef1 * *hist1 + coef2 * *hist2;
            int16_t clamped = clampS16(sample >> 11);
            out[sampleIndex++] = clamped;
            *hist2 = *hist1;
            *hist1 = clamped;
        }
    }
    return true;
}

static bool decodeSoftwareFrame(
    WiiSoundInstance* inst, uint32_t channelIndex, uint32_t frameIndex) {
    if (inst == NULL || inst->sound == NULL || channelIndex >= inst->sound->channels) return false;
    int16_t hist1 = inst->softwareHist1[channelIndex];
    int16_t hist2 = inst->softwareHist2[channelIndex];
    if (!decodeSoftwareSamples(&inst->sound->channel[channelIndex], frameIndex, &hist1, &hist2,
            inst->softwareFrameSamples[channelIndex]))
        return false;

    inst->softwareHist1[channelIndex] = hist1;
    inst->softwareHist2[channelIndex] = hist2;
    inst->softwareDecodedFrameIndex[channelIndex] = frameIndex;
    inst->softwareFrameBaseSample[channelIndex] = frameIndex * 14u;
    inst->softwareFrameValid[channelIndex] = true;
    return true;
}

static bool previewSoftwareFrame(WiiSoundInstance* inst, uint32_t channelIndex,
    uint32_t frameIndex, int16_t hist1, int16_t hist2, int16_t outSamples[14]) {
    if (inst == NULL || inst->sound == NULL || outSamples == NULL ||
        channelIndex >= inst->sound->channels)
        return false;
    return decodeSoftwareSamples(
        &inst->sound->channel[channelIndex], frameIndex, &hist1, &hist2, outSamples);
}

static bool ensureSoftwareFrame(
    WiiSoundInstance* inst, uint32_t channelIndex, uint32_t sampleIndex) {
    if (inst == NULL || inst->sound == NULL || channelIndex >= inst->sound->channels) return false;
    uint32_t targetFrame = sampleIndex / 14u;
    if (!inst->softwareFrameValid[channelIndex] ||
        inst->softwareDecodedFrameIndex[channelIndex] == UINT32_MAX ||
        targetFrame < inst->softwareDecodedFrameIndex[channelIndex]) {
        resetSoftwareChannel(inst, channelIndex);
    }

    while (!inst->softwareFrameValid[channelIndex] ||
        inst->softwareDecodedFrameIndex[channelIndex] < targetFrame) {
        uint32_t nextFrame = inst->softwareFrameValid[channelIndex] &&
                inst->softwareDecodedFrameIndex[channelIndex] != UINT32_MAX
            ? inst->softwareDecodedFrameIndex[channelIndex] + 1u
            : 0u;
        if (!decodeSoftwareFrame(inst, channelIndex, nextFrame)) return false;
    }
    return true;
}

static int16_t softwareSampleForChannel(
    WiiSoundInstance* inst, uint32_t channelIndex, uint32_t sampleIndex) {
    if (!ensureSoftwareFrame(inst, channelIndex, sampleIndex)) return 0;
    uint32_t frameOffset = sampleIndex - inst->softwareFrameBaseSample[channelIndex];
    return frameOffset < 14u ? inst->softwareFrameSamples[channelIndex][frameOffset] : 0;
}

static int16_t sampleSoftwareChannel(
    WiiSoundInstance* inst, uint32_t channelIndex, double position) {
    if (inst == NULL || inst->sound == NULL || inst->sound->frames == 0u) return 0;
    if (position < 0.0) position = 0.0;
    uint32_t sampleIndex = (uint32_t)position;
    double frac = position - (double)sampleIndex;
    int16_t current = softwareSampleForChannel(inst, channelIndex, sampleIndex);
    if (frac <= 0.0) return current;

    uint32_t nextIndex = sampleIndex + 1u;
    if (nextIndex >= inst->sound->frames) return current;

    int16_t next = current;
    uint32_t sampleFrame = sampleIndex / 14u;
    uint32_t nextFrame = nextIndex / 14u;
    if (nextFrame == sampleFrame) {
        next = softwareSampleForChannel(inst, channelIndex, nextIndex);
    } else if (nextFrame == sampleFrame + 1u && inst->softwareFrameValid[channelIndex] &&
        inst->softwareDecodedFrameIndex[channelIndex] == sampleFrame) {
        int16_t previewSamples[14];
        if (previewSoftwareFrame(inst, channelIndex, nextFrame,
                inst->softwareHist1[channelIndex], inst->softwareHist2[channelIndex],
                previewSamples)) {
            next = previewSamples[0];
        }
    }
    return lerpS16(current, next, frac);
}

static bool startSoftwareInstance(WiiAudioSystem* audio, WiiSoundInstance* inst) {
    if (audio == NULL || inst == NULL || inst->sound == NULL || inst->sound->sampleRate == 0u ||
        inst->sound->frames == 0u)
        return false;
    inst->streaming = false;
    inst->softwarePosition = 0.0;
    inst->softwareStep = computeSoftwareStep(inst->sound->sampleRate, inst->pitch);
    resetSoftwareDecoder(inst);
    inst->elapsed = 0.0f;
    inst->paused = false;
    return true;
}

static bool sampleSoftwareStream(
    WiiSoundInstance* inst, double position, int16_t* outLeft, int16_t* outRight);

static void finishSoftwareInstance(WiiSoundInstance* inst) {
    if (inst == NULL) return;
    inst->active = false;
    inst->streamFinished = true;
}

static void mixSoftwareAudio(WiiAudioSystem* audio, void* audioBuffer, uint32_t bufferLength) {
    if (audio == NULL || audioBuffer == NULL) return;

    uint8_t* out = (uint8_t*)audioBuffer;
    uint32_t frameCount = bufferLength / 4u;
    while (frameCount > 0u) {
        uint32_t chunkFrames =
            frameCount > WII_AUDIO_DMA_BUFFER_FRAMES ? WII_AUDIO_DMA_BUFFER_FRAMES : frameCount;
        int32_t* accumLeft = audio->mixAccumLeft;
        int32_t* accumRight = audio->mixAccumRight;
        memset(accumLeft, 0, chunkFrames * sizeof(int32_t));
        memset(accumRight, 0, chunkFrames * sizeof(int32_t));
        float masterGain = audio->masterGain;

        for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
            WiiSoundInstance* inst = &audio->instances[i];
            if (!inst->active || inst->paused) continue;
            float gain = instanceGain(
                inst, masterGain * AudioSystem_soundGroupGain(&audio->base, inst->soundIndex));
            double step = inst->softwareStep;
            double pos = inst->softwarePosition;
            bool nativeRate = fabs(step - 1.0) < 0.000001;
            bool ended = false;

            if (inst->streaming) {
                for (uint32_t frameIndex = 0u; frameIndex < chunkFrames; ++frameIndex) {
                    uint32_t sample = (uint32_t)pos;
                    if ((!inst->loop && sample < inst->streamSampleCount) || inst->loop) {
                        if (sample < inst->streamDecodeBaseFrame ||
                            sample - inst->streamDecodeBaseFrame >= inst->streamDecodedFrames) {
                            if (inst->stopping) ended = true;
                            break;
                        }
                    }
                    int16_t sampleLeft = 0;
                    int16_t sampleRight = 0;
                    if (!sampleSoftwareStream(inst, pos, &sampleLeft, &sampleRight)) {
                        ended = true;
                        break;
                    }
                    accumLeft[frameIndex] += applyGain(sampleLeft, gain);
                    accumRight[frameIndex] += applyGain(sampleRight, gain);
                    pos += step;
                    advanceStopRamp(inst);
                    if (inst->stopping && inst->stopRampFrames == 0u) {
                        ended = true;
                        break;
                    }
                }
                inst->softwarePosition = pos;
                if (ended) finishSoftwareInstance(inst);
                continue;
            }

            if (inst->sound == NULL) continue;
            uint32_t totalFrames = inst->sound->frames;
            if (totalFrames == 0u) {
                finishSoftwareInstance(inst);
                continue;
            }

            for (uint32_t frameIndex = 0u; frameIndex < chunkFrames; ++frameIndex) {
                uint32_t sampleIndex = pos >= 0.0 ? (uint32_t)pos : 0u;
                if (sampleIndex >= totalFrames) {
                    if (inst->loop) {
                        pos = fmod(pos, (double)totalFrames);
                        if (pos < 0.0) pos = 0.0;
                        resetSoftwareDecoder(inst);
                        sampleIndex = (uint32_t)pos;
                    } else {
                        ended = true;
                        break;
                    }
                }

                int16_t sampleLeft = nativeRate ? softwareSampleForChannel(inst, 0u, sampleIndex)
                                                : sampleSoftwareChannel(inst, 0u, pos);
                int16_t sampleRight = sampleLeft;
                if (inst->sound->channels > 1u) {
                    sampleRight = nativeRate
                        ? softwareSampleForChannel(inst, 1u, sampleIndex)
                        : sampleSoftwareChannel(inst, 1u, pos);
                }
                accumLeft[frameIndex] += applyGain(sampleLeft, gain);
                accumRight[frameIndex] += applyGain(sampleRight, gain);

                pos += step;
                advanceStopRamp(inst);
                if (inst->stopping && inst->stopRampFrames == 0u) {
                    ended = true;
                    break;
                }
            }

            inst->softwarePosition = pos;
            if (ended) finishSoftwareInstance(inst);
        }

        for (uint32_t frameIndex = 0u; frameIndex < chunkFrames; ++frameIndex) {
            uint8_t* frameOut = out + frameIndex * 4u;
            writeBeS16Buffer(frameOut + 0u, clampS16(accumRight[frameIndex]));
            writeBeS16Buffer(frameOut + 2u, clampS16(accumLeft[frameIndex]));
        }

        out += chunkFrames * 4u;
        frameCount -= chunkFrames;
    }
}

static void refillSoftwareDmaBuffers(WiiAudioSystem* audio) {
    for (uint32_t filled = 0u; audio->dmaStarted && filled < WII_AUDIO_DMA_BUFFER_COUNT;
         ++filled) {
        u32 irqLevel = IRQ_Disable();
        uint32_t bufferIndex = audio->dmaWriteBuffer;
        if (audio->dmaBufferStates[bufferIndex] != WII_DMA_BUFFER_FREE) {
            IRQ_Restore(irqLevel);
            break;
        }
        audio->dmaBufferStates[bufferIndex] = WII_DMA_BUFFER_MIXING;
        IRQ_Restore(irqLevel);

        uint8_t* buffer = audio->dmaBuffers[bufferIndex];
        mixSoftwareAudio(audio, buffer, WII_AUDIO_DMA_BUFFER_BYTES);
        DCFlushRange(buffer, WII_AUDIO_DMA_BUFFER_BYTES);

        irqLevel = IRQ_Disable();
        audio->dmaBufferStates[bufferIndex] = WII_DMA_BUFFER_READY;
        audio->dmaWriteBuffer = (bufferIndex + 1u) % WII_AUDIO_DMA_BUFFER_COUNT;
        IRQ_Restore(irqLevel);
    }
}

void WiiAudio_pump(WiiAudioSystem* audio) {
    if (audio == NULL || !audio->initialized) return;
    for (int32_t i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (inst->active && !inst->paused && inst->streaming && !inst->stopping)
            refillSoftwareStream(inst);
    }
    WiiStream_wake(audio->streamWorker);
    refillSoftwareDmaBuffers(audio);
}

static void softwareDmaCallback(void) {
    WiiAudioSystem* audio = gWiiAudioDmaSystem;
    if (audio == NULL || !audio->dmaStarted) return;

    if (audio->dmaPlayingBuffer != WII_AUDIO_DMA_NO_BUFFER)
        audio->dmaBufferStates[audio->dmaPlayingBuffer] = WII_DMA_BUFFER_FREE;
    audio->dmaPlayingBuffer = audio->dmaQueuedBuffer;

    uint32_t bufferIndex = audio->dmaReadBuffer;
    uint8_t* buffer = audio->dmaSilenceBuffer;
    audio->dmaQueuedBuffer = WII_AUDIO_DMA_NO_BUFFER;
    if (audio->dmaBufferStates[bufferIndex] == WII_DMA_BUFFER_READY) {
        buffer = audio->dmaBuffers[bufferIndex];
        audio->dmaBufferStates[bufferIndex] = WII_DMA_BUFFER_IN_FLIGHT;
        audio->dmaQueuedBuffer = bufferIndex;
        audio->dmaReadBuffer = (bufferIndex + 1u) % WII_AUDIO_DMA_BUFFER_COUNT;
    }
    AUDIO_InitDMA((uintptr_t)buffer, WII_AUDIO_DMA_BUFFER_BYTES);
}

static bool startSoftwareDma(WiiAudioSystem* audio) {
    if (audio == NULL) return false;
    AUDIO_Init(NULL);
    AUDIO_StopDMA();
    AUDIO_SetDSPSampleRate(AI_SAMPLERATE_48KHZ);
    audio->dmaPlayingBuffer = WII_AUDIO_DMA_NO_BUFFER;
    audio->dmaQueuedBuffer = 0u;
    audio->dmaReadBuffer = 1u;
    audio->dmaWriteBuffer = 0u;

    audio->dmaSilenceBuffer = memalign(32, WII_AUDIO_DMA_BUFFER_BYTES);
    if (audio->dmaSilenceBuffer == NULL) return false;
    memset(audio->dmaSilenceBuffer, 0, WII_AUDIO_DMA_BUFFER_BYTES);
    DCFlushRange(audio->dmaSilenceBuffer, WII_AUDIO_DMA_BUFFER_BYTES);

    for (uint32_t i = 0u; i < WII_AUDIO_DMA_BUFFER_COUNT; ++i) {
        audio->dmaBuffers[i] = memalign(32, WII_AUDIO_DMA_BUFFER_BYTES);
        if (audio->dmaBuffers[i] == NULL) {
            stopSoftwareDma(audio);
            return false;
        }
        memset(audio->dmaBuffers[i], 0, WII_AUDIO_DMA_BUFFER_BYTES);
        DCFlushRange(audio->dmaBuffers[i], WII_AUDIO_DMA_BUFFER_BYTES);
        audio->dmaBufferStates[i] = WII_DMA_BUFFER_READY;
    }
    audio->dmaBufferStates[0] = WII_DMA_BUFFER_IN_FLIGHT;

    audio->dmaStarted = true;
    gWiiAudioDmaSystem = audio;
    AUDIO_RegisterDMACallback(softwareDmaCallback);
    AUDIO_InitDMA((uintptr_t)audio->dmaBuffers[0], WII_AUDIO_DMA_BUFFER_BYTES);
    AUDIO_StartDMA();
    return true;
}

static void stopSoftwareDma(WiiAudioSystem* audio) {
    if (audio == NULL) return;
    u32 irqLevel = IRQ_Disable();
    audio->dmaStarted = false;
    if (gWiiAudioDmaSystem == audio) gWiiAudioDmaSystem = NULL;
    AUDIO_StopDMA();
    AUDIO_RegisterDMACallback(NULL);
    IRQ_Restore(irqLevel);
    for (uint32_t i = 0u; i < WII_AUDIO_DMA_BUFFER_COUNT; ++i) {
        free(audio->dmaBuffers[i]);
        audio->dmaBuffers[i] = NULL;
    }
    free(audio->dmaSilenceBuffer);
    audio->dmaSilenceBuffer = NULL;
}

static bool seekStreamSample(WiiSoundInstance* inst, uint32_t sample) {
    if (inst == NULL || inst->streamJob == NULL) return false;
    WiiJob_seek(inst->streamJob, sample);
    inst->streamDecodeBaseFrame = sample;
    inst->streamDecodedFrames = 0u;
    return true;
}

static bool softwareStreamSampleAt(
    WiiSoundInstance* inst, uint32_t sampleIndex, int16_t* outLeft, int16_t* outRight) {
    if (inst == NULL || outLeft == NULL || outRight == NULL || inst->streamChannels == 0u)
        return false;
    if (!inst->loop && sampleIndex >= inst->streamSampleCount) {
        *outLeft = 0;
        *outRight = 0;
        return false;
    }
    if (sampleIndex < inst->streamDecodeBaseFrame ||
        sampleIndex >= inst->streamDecodeBaseFrame + inst->streamDecodedFrames) {
        *outLeft = inst->streamHoldLeft;
        *outRight = inst->streamHoldRight;
        return true;
    }

    uint32_t offset = sampleIndex - inst->streamDecodeBaseFrame;
    uint32_t ringFrame = (inst->streamDecodeBaseFrame + offset) % inst->streamDecodeCapacityFrames;
    int16_t* frame = inst->streamDecodePcm + ringFrame * inst->streamChannels;
    *outLeft = frame[0];
    *outRight = inst->streamChannels > 1u ? frame[1] : frame[0];
    inst->streamHoldLeft = *outLeft;
    inst->streamHoldRight = *outRight;
    return true;
}

static bool sampleSoftwareStream(
    WiiSoundInstance* inst, double position, int16_t* outLeft, int16_t* outRight) {
    if (inst == NULL || outLeft == NULL || outRight == NULL) return false;
    if (position < 0.0) position = 0.0;
    uint32_t sampleIndex = (uint32_t)position;
    double frac = position - (double)sampleIndex;
    int16_t left0 = 0;
    int16_t right0 = 0;
    if (!softwareStreamSampleAt(inst, sampleIndex, &left0, &right0)) return false;
    if (frac <= 0.0) {
        *outLeft = left0;
        *outRight = right0;
        return true;
    }

    uint32_t nextIndex = sampleIndex + 1u;
    if (!inst->loop && inst->streamSampleCount > 0u && nextIndex >= inst->streamSampleCount) {
        *outLeft = left0;
        *outRight = right0;
        return true;
    }

    int16_t left1 = left0;
    int16_t right1 = right0;
    if (!softwareStreamSampleAt(inst, nextIndex, &left1, &right1)) {
        *outLeft = left0;
        *outRight = right0;
        return true;
    }
    *outLeft = lerpS16(left0, left1, frac);
    *outRight = lerpS16(right0, right1, frac);
    return true;
}

static void refillSoftwareStream(WiiSoundInstance* inst) {
    if (inst == NULL || inst->paused || !inst->streaming || inst->streamJob == NULL ||
        inst->streamDecodePcm == NULL || inst->streamDecodeCapacityFrames == 0u) return;

    uint32_t currentSample = (uint32_t)inst->softwarePosition;
    uint32_t decodedEnd = inst->streamDecodeBaseFrame + inst->streamDecodedFrames;
    if (currentSample > decodedEnd) {
        seekStreamSample(inst, currentSample);
    } else if (currentSample > inst->streamDecodeBaseFrame) {
        uint32_t consumed = currentSample - inst->streamDecodeBaseFrame;
        inst->streamDecodeBaseFrame += consumed;
        inst->streamDecodedFrames -= consumed;
    }
    uint32_t target = WII_AUDIO_STREAM_REFILL_TARGET_FRAMES;
    uint32_t budget = WII_AUDIO_STREAM_MAX_COPY_FRAMES_PER_PUMP;
    while (budget != 0u && inst->streamDecodedFrames < target) {
        uint32_t sample = inst->streamDecodeBaseFrame + inst->streamDecodedFrames;
        uint32_t write = sample % inst->streamDecodeCapacityFrames;
        uint32_t writable = inst->streamDecodeCapacityFrames - write;
        if (writable > target - inst->streamDecodedFrames)
            writable = target - inst->streamDecodedFrames;
        if (writable > budget) writable = budget;
        int32_t frames = WiiJob_read(inst->streamJob, sample,
            inst->streamDecodePcm + write * inst->streamChannels, writable,
            &inst->streamSampleCount);
        if (frames < 0) {
            logWarn("Wii audio: stream decode failed for sound %ld", (long)inst->soundIndex);
            inst->streamFinished = true;
            return;
        }
        if (frames == 0) break;
        inst->streamDecodedFrames += (uint32_t)frames;
        budget -= (uint32_t)frames;
    }
}

static bool parseWdsp(const uint8_t* blob, uint32_t blobSize, WiiAdpcmSound* out) {
    WdspSound parsed;
    if (!Wdsp_parse(blob, blobSize, &parsed)) return false;

    memset(out, 0, sizeof(*out));
    out->loaded = true;
    out->frames = parsed.sampleCount;
    out->sampleRate = parsed.sampleRate;
    out->channels = parsed.channelCount;

    for (uint32_t ch = 0u; ch < parsed.channelCount; ++ch) {
        const WdspChannel* source = &parsed.channels[ch];
        WiiAdpcmChannel* channel = &out->channel[ch];
        channel->size = source->size;
        channel->cachedData = blob + source->offset;
        memcpy(channel->coefs, source->coefs, sizeof(channel->coefs));
        channel->startHistory1 = (uint16_t)source->startHistory1;
        channel->startHistory2 = (uint16_t)source->startHistory2;
    }

    return true;
}

static bool loadStreamInfo(WiiAudioStream* stream, const char* path) {
    if (stream == NULL || path == NULL) return false;
    if (Audio_pathHasExtension(path, ".wdsp")) {
        FILE* file = fopen(path, "rb");
        if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
            if (file != NULL) fclose(file);
            return false;
        }
        long fileSize = ftell(file);
        uint8_t header[WDSP_HEADER_SIZE];
        WdspSound parsed;
        bool ok = fileSize >= (long)sizeof(header) && (unsigned long)fileSize <= UINT32_MAX &&
            fseek(file, 0, SEEK_SET) == 0 &&
            fread(header, 1, sizeof(header), file) == sizeof(header) &&
            Wdsp_parseHeader(header, (uint32_t)fileSize, &parsed);
        fclose(file);
        if (!ok) return false;
        stream->sampleRate = parsed.sampleRate;
        stream->sampleCount = parsed.sampleCount;
        stream->channels = parsed.channelCount;
        stream->dspAdpcm = true;
        return true;
    }
    return Audio_pathHasExtension(path, ".ogg") &&
        Audio_readVorbisStreamInfo(
            path, 0u, &stream->sampleRate, &stream->sampleCount, &stream->channels);
}

static bool loadSoundBank(WiiAudioSystem* audio) {
    if (audio == NULL) return false;
    if (audio->entries != NULL) return true;

    if (audio->fileSystem == NULL) return false;
    FileSystem* fs = audio->fileSystem;
    const char* relativePath = "audio/sound_bank.bin";
    if (!fs->vtable->fileExists(fs, relativePath)) {
        logWarn("Wii audio: missing sound bank at %s", relativePath);
        return false;
    }
    char* path = fs->vtable->resolvePath(fs, relativePath);
    if (path == NULL) return false;
    bool readOk = readFileFully(path, &audio->soundBankData, &audio->soundBankSize);
    free(path);
    if (!readOk) return false;
    const uint8_t* data = audio->soundBankData;
    SbkHeader header;
    if (!Sbk_parseHeader(data, audio->soundBankSize, &header)) {
        logWarn("Wii audio: invalid sound bank header");
        goto fail;
    }
    audio->entries = safeCalloc(header.count, sizeof(SbkEntry));
    audio->sounds = safeCalloc(header.count, sizeof(WiiAdpcmSound));
    audio->entryCount = header.count;
    if (!Sbk_parseEntries(data + 16u, &header, audio->soundBankSize, audio->entries)) {
        logWarn("Wii audio: invalid sound bank entries");
        goto fail;
    }

    for (uint32_t i = 0u; i < header.count; ++i) {
        SbkEntry* entry = &audio->entries[i];
        if (entry->size != 0u) {
            if ((entry->flags & SBK_FLAG_DSP_ADPCM) == 0u) {
                logWarn("Wii audio: sound bank entry %u is not Wii DSP ADPCM flags=0x%08x", i,
                    entry->flags);
                goto fail;
            }
        }
    }
    logInfo("Wii audio: indexed %u preprocessed DSP ADPCM sounds", header.count);
    return true;
fail:
    clearSoundBankIndex(audio);
    free(audio->soundBankData);
    audio->soundBankData = NULL;
    audio->soundBankSize = 0u;
    return false;
}

static WiiAdpcmSound* loadAdpcmSound(WiiAudioSystem* audio, int32_t soundIndex) {
    if (audio == NULL) return NULL;
    if (audio->entries == NULL && !loadSoundBank(audio)) return NULL;
    if (soundIndex < 0 || (uint32_t)soundIndex >= audio->entryCount) return NULL;
    WiiAdpcmSound* sound = &audio->sounds[soundIndex];
    if (sound->loaded) return sound;
    SbkEntry* entry = &audio->entries[soundIndex];
    if (entry->size == 0u) return NULL;
    if (!parseWdsp(audio->soundBankData + entry->offset, entry->size, sound)) {
        memset(sound, 0, sizeof(*sound));
        logWarn("Wii audio: failed to parse ADPCM sound %d", soundIndex);
        return NULL;
    }
    return sound;
}

static WiiAudioStream* streamForIndex(WiiAudioSystem* audio, int32_t streamIndex) {
    if (audio == NULL) return NULL;
    int32_t slot = streamIndex - WII_AUDIO_STREAM_INDEX_BASE;
    if (slot < 0 || slot >= WII_AUDIO_MAX_STREAMS || !audio->streams[slot].active) return NULL;
    return &audio->streams[slot];
}

static bool isStreamIndex(int32_t soundIndex) {
    return soundIndex >= WII_AUDIO_STREAM_INDEX_BASE &&
        soundIndex < WII_AUDIO_STREAM_INDEX_BASE + WII_AUDIO_MAX_STREAMS;
}

static void releaseInstance(WiiSoundInstance* inst) {
    if (inst == NULL) return;
    int16_t* pcm = inst->streamDecodePcm;
    uint32_t capacity = inst->streamDecodeCapacityFrames;
    uint8_t channels = inst->streamDecodeAllocatedChannels;
    WiiJob_release(inst->streamJob);
    memset(inst, 0, sizeof(*inst));
    inst->streamDecodePcm = pcm;
    inst->streamDecodeCapacityFrames = capacity;
    inst->streamDecodeAllocatedChannels = channels;
}

static void activateInstance(WiiSoundInstance* inst) {
    if (inst == NULL) return;
    inst->active = true;
}

static void beginStopRamp(WiiSoundInstance* inst) {
    if (inst == NULL) return;
    if (!inst->active || inst->stopping) {
        return;
    }
    if (inst->paused || (inst->streaming && inst->streamDecodedFrames == 0u) ||
        WII_AUDIO_STOP_RAMP_FRAMES == 0u) {
        inst->active = false;
        inst->streamFinished = true;
        return;
    }
    inst->stopping = true;
    inst->stopRampFrames = WII_AUDIO_STOP_RAMP_FRAMES;
    inst->stopRampTotalFrames = WII_AUDIO_STOP_RAMP_FRAMES;
}

static bool startStreamPathInstance(WiiAudioSystem* audio, WiiSoundInstance* inst, const char* path,
    uint32_t sampleRate, uint32_t sampleCount, uint8_t channels, const StreamedBankEntry* entry) {
    if (audio == NULL || inst == NULL || path == NULL || sampleRate == 0u || channels == 0u ||
        channels > 2u)
        return false;

    inst->streamJob = WiiStream_open(audio->streamWorker,
        entry != NULL ? &audio->streamedBank : NULL, entry, path,
        sampleRate, sampleCount, channels, inst->stream != NULL && inst->stream->dspAdpcm,
        inst->loop);
    if (inst->streamJob == NULL) return false;
    inst->streaming = true;
    inst->streamSampleRate = sampleRate;
    inst->streamSampleCount = sampleCount;
    inst->streamChannels = channels;
    uint8_t allocatedChannels = 2u;
    if (inst->streamDecodePcm == NULL ||
        inst->streamDecodeCapacityFrames < WII_AUDIO_STREAM_DECODE_BUFFER_FRAMES ||
        inst->streamDecodeAllocatedChannels < allocatedChannels) {
        free(inst->streamDecodePcm);
        inst->streamDecodePcm = NULL;
        inst->streamDecodeCapacityFrames = WII_AUDIO_STREAM_DECODE_BUFFER_FRAMES;
        inst->streamDecodeAllocatedChannels = allocatedChannels;
        size_t decodeBytes =
            (size_t)inst->streamDecodeCapacityFrames * allocatedChannels * sizeof(int16_t);
        inst->streamDecodePcm = memalign(32, decodeBytes);
        if (inst->streamDecodePcm == NULL) inst->streamDecodePcm = malloc(decodeBytes);
    }
    if (inst->streamDecodePcm == NULL) {
        inst->streamDecodeCapacityFrames = 0u;
        inst->streamDecodeAllocatedChannels = 0u;
        releaseInstance(inst);
        return false;
    }
    inst->streamDecodeBaseFrame = 0u;
    inst->streamDecodedFrames = 0u;
    inst->streamHoldLeft = 0;
    inst->streamHoldRight = 0;
    inst->softwarePosition = 0.0;
    inst->softwareStep = computeSoftwareStep(sampleRate, inst->pitch);
    inst->elapsed = 0.0f;
    inst->paused = false;
    inst->streamFinished = false;
    return true;
}

static bool startStreamInstance(
    WiiAudioSystem* audio, WiiSoundInstance* inst, const WiiAudioStream* stream) {
    if (stream == NULL) return false;
    inst->stream = stream;
    return startStreamPathInstance(audio, inst, stream->path, stream->sampleRate,
        stream->sampleCount, stream->channels, NULL);
}

static WiiAudioStream* reusableStreamSlot(WiiAudioSystem* audio) {
    for (int32_t i = 0; i < WII_AUDIO_MAX_STREAMS; ++i) {
        if (!audio->streams[i].active) return &audio->streams[i];
    }
    for (int32_t i = 0; i < WII_AUDIO_MAX_STREAMS; ++i) {
        WiiAudioStream* stream = &audio->streams[i];
        if (stream->refCount != 0u) continue;
        bool inUse = false;
        for (int j = 0; j < WII_AUDIO_MAX_INSTANCES; ++j) {
            if (audio->instances[j].stream == stream) {
                inUse = true;
                break;
            }
        }
        if (!inUse) {
            free(stream->path);
            memset(stream, 0, sizeof(*stream));
            return stream;
        }
    }
    return NULL;
}

static int32_t nextInstanceId(WiiAudioSystem* audio) {
    for (;;) {
        if (audio->nextInstanceCounter >= WII_AUDIO_STREAM_INDEX_BASE - WII_AUDIO_INSTANCE_BASE) {
            audio->nextInstanceCounter = 0u;
        }
        int32_t id = WII_AUDIO_INSTANCE_BASE + (int32_t)audio->nextInstanceCounter++;
        bool inUse = false;
        for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
            if (audio->instances[i].active && audio->instances[i].instanceId == id) {
                inUse = true;
                break;
            }
        }
        if (!inUse) return id;
    }
}

static int32_t playStreamSound(WiiAudioSystem* audio, const WiiAudioStream* stream,
    const StreamedBankEntry* entry, int32_t soundIndex, int32_t priority, bool loop, float gain,
    float pitch) {
    if (audio == NULL || (stream == NULL && entry == NULL)) return -1;
    int slot = findInstanceSlot(audio, priority);
    if (slot < 0) {
        logWarn("Wii audio: no free sound slots for stream %d", soundIndex);
        return -1;
    }
    WiiSoundInstance* inst = &audio->instances[slot];
    releaseInstance(inst);

    inst->loop = loop;
    inst->soundIndex = soundIndex;
    inst->instanceId = nextInstanceId(audio);
    inst->priority = priority;
    inst->gain = Audio_sanitizeGain(gain);
    inst->pitch = Audio_sanitizePitch(pitch);
    bool started = entry != NULL
        ? startStreamPathInstance(
            audio, inst, audio->streamedBank.path, entry->sampleRate, entry->sampleCount,
            (uint8_t)(entry->flags & 255u), entry)
        : startStreamInstance(audio, inst, stream);
    logEvent("audio", "stream %s sound=%ld rate=%lu frames=%lu",
        started ? "queued" : "failed", (long)soundIndex, (unsigned long)inst->streamSampleRate,
        (unsigned long)inst->streamSampleCount);
    if (!started) {
        releaseInstance(inst);
        return -1;
    }
    activateInstance(inst);
    return inst->instanceId;
}

static void init(AudioSystem* base, DataWin* dataWin, FileSystem* fileSystem) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    arrput(base->audioGroups, dataWin);
    base->dw = dataWin;
    AudioSystem_setGroupGain(base, 0, 1.0f, 0u);
    audio->fileSystem = fileSystem;
    if (!startSoftwareDma(audio)) {
        logWarn("Wii audio: failed to initialize software DMA mixer");
        return;
    }
    audio->initialized = true;
    logEvent("audio", "game-thread DMA mixer ready buffers=%u frames=%u",
        WII_AUDIO_DMA_BUFFER_COUNT, WII_AUDIO_DMA_BUFFER_FRAMES);
    loadSoundBank(audio);
    if (fileSystem != NULL && fileSystem->vtable->fileExists(fileSystem, "audio/streamed.bin")) {
        char* path = fileSystem->vtable->resolvePath(fileSystem, "audio/streamed.bin");
        if (path != NULL && !StreamedBank_load(&audio->streamedBank, path, dataWin->sond.count))
            logWarn("Wii audio: invalid streamed bank %s", path);
        free(path);
    }
    audio->streamWorker = WiiStream_create();
    if (audio->streamWorker == NULL)
        logWarn("Wii audio: failed to initialize stream decoder worker");
    else
        logEvent("audio", "stream decoder worker ready; PCM mixing stays on game thread");
}

static void destroy(AudioSystem* base) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return;
    stopSoftwareDma(audio);
    for (int32_t i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        releaseInstance(&audio->instances[i]);
        free(audio->instances[i].streamDecodePcm);
        audio->instances[i].streamDecodePcm = NULL;
        audio->instances[i].streamDecodeCapacityFrames = 0u;
        audio->instances[i].streamDecodeAllocatedChannels = 0u;
    }
    WiiStream_destroy(audio->streamWorker);
    audio->streamWorker = NULL;
    for (ptrdiff_t i = 1; i < arrlen(base->audioGroups); ++i) {
        if (base->audioGroups[i] != NULL) DataWin_free(base->audioGroups[i]);
    }
    arrfree(base->audioGroups);
    free(base->groupGains);
    free(audio->sounds);
    free(audio->entries);
    for (int32_t i = 0; i < WII_AUDIO_MAX_STREAMS; ++i)
        free(audio->streams[i].path);
    free(audio->soundBankData);
    StreamedBank_free(&audio->streamedBank);
    free(audio);
}

static void update(AudioSystem* base, float deltaTime) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return;
    if (!isfinite(deltaTime) || deltaTime < 0.0f) deltaTime = 0.0f;
    AudioSystem_updateGroupGains(base, deltaTime);
    for (int32_t i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        bool finished = inst->streamFinished;
        bool active = inst->active;
        bool paused = inst->paused;
        if (active && !paused) {
            inst->elapsed += deltaTime;
            if (inst->gainFadeDuration > 0.0f) {
                inst->gainFadeElapsed += deltaTime;
                if (inst->gainFadeElapsed >= inst->gainFadeDuration) {
                    inst->gain = inst->gainFadeTarget;
                    inst->gainFadeDuration = 0.0f;
                } else {
                    float fraction = inst->gainFadeElapsed / inst->gainFadeDuration;
                    inst->gain = inst->gainFadeStart +
                        (inst->gainFadeTarget - inst->gainFadeStart) * fraction;
                }
            }
        }
        if (finished) {
            releaseInstance(inst);
            continue;
        }
    }
    WiiAudio_pump(audio);
}

static int findInstanceSlot(WiiAudioSystem* audio, int32_t priority) {
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        if (!audio->instances[i].active) return i;
    }
    int best = -1;
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        if (audio->instances[i].priority <= priority &&
            (best < 0 || audio->instances[i].priority < audio->instances[best].priority))
            best = i;
    }
    return best;
}

static int32_t playSound(AudioSystem* base, int32_t soundIndex, int32_t priority, bool loop) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL || !audio->initialized) {
        logWarn("Wii audio: playSound before audio init for sound %d", soundIndex);
        return -1;
    }

    WiiAudioStream* stream = streamForIndex(audio, soundIndex);
    if (stream != NULL) {
        return playStreamSound(
            audio, stream, NULL, soundIndex, priority, loop, stream->gain, stream->pitch);
    }

    Sound* sond =
        (base->dw != NULL && soundIndex >= 0 && (uint32_t)soundIndex < base->dw->sond.count)
            ? &base->dw->sond.sounds[soundIndex]
            : NULL;
    const StreamedBankEntry* entry = StreamedBank_find(&audio->streamedBank, soundIndex);
    if (entry != NULL) {
        int32_t instance = playStreamSound(audio, NULL, entry, soundIndex, priority, loop,
            sond != NULL ? sond->volume : 1.0f, sond != NULL ? sond->pitch : 1.0f);
        if (instance >= 0) return instance;
        logWarn("Wii audio: failed to open archived sound %d", soundIndex);
    }
    WiiAdpcmSound* sound = loadAdpcmSound(audio, soundIndex);
    if (sound == NULL) {
        logWarn("Wii audio: no playable ADPCM bank entry for sound %d", soundIndex);
        return -1;
    }

    int slot = findInstanceSlot(audio, priority);
    if (slot < 0) {
        logWarn("Wii audio: no free sound slots for sound %d", soundIndex);
        return -1;
    }
    WiiSoundInstance* inst = &audio->instances[slot];
    releaseInstance(inst);

    inst->loop = loop;
    inst->soundIndex = soundIndex;
    inst->instanceId = nextInstanceId(audio);
    inst->priority = priority;
    inst->gain = Audio_sanitizeGain(sond != NULL ? sond->volume : 1.0f);
    inst->pitch = Audio_sanitizePitch(sond != NULL ? sond->pitch : 1.0f);
    inst->sound = sound;
    if (!startSoftwareInstance(audio, inst)) {
        releaseInstance(inst);
        return -1;
    }
    activateInstance(inst);
    return inst->instanceId;
}

typedef void (*WiiInstanceAction)(WiiAudioSystem* audio, WiiSoundInstance* inst, void* userData);

static WiiSoundInstance* findInstance(WiiAudioSystem* audio, int32_t soundOrInstance) {
    if (audio == NULL) return NULL;
    bool instanceLookup = Audio_isInstanceId(soundOrInstance);
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (!inst->active) continue;
        if (instanceLookup) {
            if (inst->instanceId == soundOrInstance) return inst;
        } else if (!inst->stopping && inst->soundIndex == soundOrInstance) {
            return inst;
        }
    }
    return NULL;
}

static void forEachInstance(
    WiiAudioSystem* audio, int32_t soundOrInstance, WiiInstanceAction action, void* userData) {
    if (audio == NULL || action == NULL) return;
    if (Audio_isInstanceId(soundOrInstance)) {
        WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
        if (inst != NULL) action(audio, inst, userData);
        return;
    }

    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (inst->active && inst->soundIndex == soundOrInstance) action(audio, inst, userData);
    }
}

static void actionStop(
    MAYBE_UNUSED WiiAudioSystem* audio, WiiSoundInstance* inst, MAYBE_UNUSED void* userData) {
    beginStopRamp(inst);
}

static void actionPause(
    MAYBE_UNUSED WiiAudioSystem* audio, WiiSoundInstance* inst, MAYBE_UNUSED void* userData) {
    if (inst == NULL) return;
    if (inst->active) inst->paused = true;
}

static void actionResume(
    MAYBE_UNUSED WiiAudioSystem* audio, WiiSoundInstance* inst, MAYBE_UNUSED void* userData) {
    if (inst == NULL) return;
    if (inst->active) inst->paused = false;
}

static void stopSound(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    forEachInstance(audio, soundOrInstance, actionStop, NULL);
}

static void stopAll(AudioSystem* base) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return;
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        beginStopRamp(&audio->instances[i]);
    }
}

static bool isPlaying(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return false;
    if (Audio_isInstanceId(soundOrInstance)) {
        WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
        return inst != NULL && !inst->paused;
    }
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (inst->active && !inst->stopping && inst->soundIndex == soundOrInstance && !inst->paused)
            return true;
    }
    return false;
}

static void pauseSound(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    forEachInstance(audio, soundOrInstance, actionPause, NULL);
}

static void resumeSound(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    forEachInstance(audio, soundOrInstance, actionResume, NULL);
}

static void pauseAll(AudioSystem* base) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return;
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        if (audio->instances[i].active) pauseSound(base, audio->instances[i].instanceId);
    }
}

static void resumeAll(AudioSystem* base) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return;
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        if (audio->instances[i].active) resumeSound(base, audio->instances[i].instanceId);
    }
}

static void applyInstanceGain(WiiAudioSystem* audio, WiiSoundInstance* inst, float gain) {
    if (audio == NULL || inst == NULL) return;
    inst->gain = Audio_sanitizeGain(gain);
    inst->gainFadeDuration = 0.0f;
}

static void fadeInstanceGain(
    WiiAudioSystem* audio, WiiSoundInstance* inst, float gain, uint32_t timeMs) {
    if (audio == NULL || inst == NULL) return;
    if (timeMs == 0u) {
        applyInstanceGain(audio, inst, gain);
        return;
    }
    inst->gainFadeStart = inst->gain;
    inst->gainFadeTarget = Audio_sanitizeGain(gain);
    inst->gainFadeDuration = (float)timeMs / 1000.0f;
    inst->gainFadeElapsed = 0.0f;
}

static void setSoundGain(AudioSystem* base, int32_t soundOrInstance, float gain, uint32_t timeMs) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    gain = Audio_sanitizeGain(gain);
    bool streamIndex = isStreamIndex(soundOrInstance);
    if (streamIndex) {
        WiiAudioStream* stream = streamForIndex(audio, soundOrInstance);
        if (stream != NULL) stream->gain = gain;
    }
    if (Audio_isInstanceId(soundOrInstance)) {
        fadeInstanceGain(audio, findInstance(audio, soundOrInstance), gain, timeMs);
        return;
    }
    if (streamIndex && !DataWin_isVersionAtLeast(base->dw, 2024, 11, 0, 0)) return;

    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (inst->active && !inst->stopping && inst->soundIndex == soundOrInstance)
            fadeInstanceGain(audio, inst, gain, timeMs);
    }
}

static float getSoundGain(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (Audio_isInstanceId(soundOrInstance)) {
        WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
        return inst != NULL ? inst->gain : 1.0f;
    }
    if (isStreamIndex(soundOrInstance)) {
        WiiAudioStream* stream = streamForIndex(audio, soundOrInstance);
        return stream != NULL ? stream->gain : 1.0f;
    }
    WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
    return inst != NULL ? inst->gain : 1.0f;
}

static void applyInstancePitch(WiiAudioSystem* audio, WiiSoundInstance* inst, float pitch) {
    if (audio == NULL || inst == NULL) return;
    inst->pitch = pitch;
    uint32_t sampleRate = inst->streaming
        ? inst->streamSampleRate
        : (inst->sound != NULL ? inst->sound->sampleRate : 0u);
    if (sampleRate != 0u) inst->softwareStep = computeSoftwareStep(sampleRate, pitch);
}

static void setSoundPitch(AudioSystem* base, int32_t soundOrInstance, float pitch) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    pitch = Audio_sanitizePitch(pitch);
    if (isStreamIndex(soundOrInstance)) {
        WiiAudioStream* stream = streamForIndex(audio, soundOrInstance);
        if (stream != NULL) stream->pitch = pitch;
    }
    if (Audio_isInstanceId(soundOrInstance)) {
        applyInstancePitch(audio, findInstance(audio, soundOrInstance), pitch);
        return;
    }
    for (int i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (inst->active && !inst->stopping && inst->soundIndex == soundOrInstance)
            applyInstancePitch(audio, inst, pitch);
    }
}

static float getSoundPitch(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (Audio_isInstanceId(soundOrInstance)) {
        WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
        return inst != NULL ? inst->pitch : 1.0f;
    }
    if (isStreamIndex(soundOrInstance)) {
        WiiAudioStream* stream = streamForIndex(audio, soundOrInstance);
        return stream != NULL ? stream->pitch : 1.0f;
    }
    WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
    return inst != NULL ? inst->pitch : 1.0f;
}

static float getTrackPosition(AudioSystem* base, int32_t soundOrInstance) {
    WiiSoundInstance* inst = findInstance((WiiAudioSystem*)base, soundOrInstance);
    if (inst == NULL) return 0.0f;
    if (inst->streaming)
        return inst->streamSampleRate > 0u
            ? (float)(inst->softwarePosition / (double)inst->streamSampleRate)
            : 0.0f;
    return inst->sound != NULL && inst->sound->sampleRate > 0u
        ? (float)(inst->softwarePosition / (double)inst->sound->sampleRate)
        : inst->elapsed;
}

static void setTrackPosition(AudioSystem* base, int32_t soundOrInstance, float positionSeconds) {
    WiiSoundInstance* inst = findInstance((WiiAudioSystem*)base, soundOrInstance);
    if (inst == NULL) return;
    if (!isfinite(positionSeconds) || positionSeconds < 0.0f) positionSeconds = 0.0f;
    uint32_t sampleRate =
        inst->streaming ? (inst->stream != NULL ? inst->stream->sampleRate : inst->streamSampleRate)
                        : (inst->sound != NULL ? inst->sound->sampleRate : 0u);
    if (sampleRate == 0u) return;
    double requestedSample = (double)positionSeconds * sampleRate;
    uint32_t totalSamples = inst->streaming ? inst->streamSampleCount : inst->sound->frames;
    uint32_t sample = requestedSample >= totalSamples ? totalSamples : (uint32_t)requestedSample;
    if (inst->streaming && inst->streamJob != NULL) {
        if (!seekStreamSample(inst, sample)) return;
        inst->softwarePosition = (double)sample;
        inst->elapsed = positionSeconds;
        inst->streamFinished = false;
    } else if (inst->sound != NULL) {
        if (sample > inst->sound->frames) sample = inst->sound->frames;
        inst->softwarePosition = (double)sample;
        inst->elapsed = (float)sample / (float)sampleRate;
        resetSoftwareDecoder(inst);
    }
}

static float getSoundLength(AudioSystem* base, int32_t soundOrInstance) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return 0.0f;
    WiiSoundInstance* inst = findInstance(audio, soundOrInstance);
    if (inst != NULL) {
        if (inst->streaming && inst->stream != NULL && inst->stream->sampleRate > 0u) {
            return (float)inst->stream->sampleCount / (float)inst->stream->sampleRate;
        }
        if (inst->streaming && inst->streamSampleRate > 0u) {
            return (float)inst->streamSampleCount / (float)inst->streamSampleRate;
        }
        if (inst->sound != NULL && inst->sound->sampleRate > 0u) {
            return (float)inst->sound->frames / (float)inst->sound->sampleRate;
        }
    }
    WiiAudioStream* stream = streamForIndex(audio, soundOrInstance);
    if (stream != NULL && stream->sampleRate > 0u) {
        return (float)stream->sampleCount / (float)stream->sampleRate;
    }
    const StreamedBankEntry* entry = StreamedBank_find(&audio->streamedBank, soundOrInstance);
    if (entry != NULL) return (float)entry->sampleCount / (float)entry->sampleRate;
    if (soundOrInstance < 0 || (uint32_t)soundOrInstance >= audio->entryCount) return 0.0f;
    WiiAdpcmSound* sound = loadAdpcmSound(audio, soundOrInstance);
    if (sound == NULL || sound->sampleRate == 0u) return 0.0f;
    return (float)sound->frames / (float)sound->sampleRate;
}

static void setMasterGain(AudioSystem* base, float gain) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL) return;
    audio->masterGain = Audio_sanitizeGain(gain);
}

static void setListenerGain(
    AudioSystem* base, float gain, MAYBE_UNUSED int32_t listenerId) {
    setMasterGain(base, gain);
}

static void setChannelCount(MAYBE_UNUSED AudioSystem* base, MAYBE_UNUSED int32_t count) {}

static void setSoundSpatial(MAYBE_UNUSED AudioSystem* base, MAYBE_UNUSED int32_t instanceId,
    MAYBE_UNUSED float x, MAYBE_UNUSED float y, MAYBE_UNUSED float z, MAYBE_UNUSED float vx,
    MAYBE_UNUSED float vy, MAYBE_UNUSED float vz, MAYBE_UNUSED float ref, MAYBE_UNUSED float max,
    MAYBE_UNUSED float factor) {}

static void setListenerPosition(MAYBE_UNUSED AudioSystem* base, MAYBE_UNUSED float x,
    MAYBE_UNUSED float y, MAYBE_UNUSED float z) {}

static void setGroupGain(AudioSystem* base, int32_t groupIndex, float gain, uint32_t timeMs) {
    AudioSystem_setGroupGain(base, groupIndex, Audio_sanitizeGain(gain), timeMs);
}

static char* resolveAudioGroupPath(AudioSystem* base, int32_t groupIndex) {
    if (base == NULL || base->dw == NULL || groupIndex <= 0) return NULL;
    if (base->dw->agrp.audioGroups != NULL && (uint32_t)groupIndex < base->dw->agrp.count) {
        AudioGroup* group = &base->dw->agrp.audioGroups[groupIndex];
        if (group->path != NULL && group->path[0] != '\0') return safeStrdup(group->path);
    }

    int length = snprintf(NULL, 0, "audiogroup%d.dat", groupIndex);
    if (length <= 0) return NULL;
    char* path = safeMalloc((size_t)length + 1u);
    snprintf(path, (size_t)length + 1u, "audiogroup%d.dat", groupIndex);
    return path;
}

static void groupLoad(AudioSystem* base, int32_t groupIndex) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL || base == NULL || base->dw == NULL || groupIndex <= 0) return;

    while (arrlen(base->audioGroups) <= groupIndex) {
        arrput(base->audioGroups, NULL);
    }
    if (base->audioGroups[groupIndex] != NULL) return;

    char* groupPath = resolveAudioGroupPath(base, groupIndex);
    if (groupPath == NULL) return;

    if (audio->fileSystem != NULL &&
        !audio->fileSystem->vtable->fileExists(audio->fileSystem, groupPath)) {
        base->audioGroups[groupIndex] = safeCalloc(1, sizeof(DataWin));
        free(groupPath);
        return;
    }

    char* resolvedPath = audio->fileSystem != NULL
        ? audio->fileSystem->vtable->resolvePath(audio->fileSystem, groupPath)
        : NULL;
    free(groupPath);
    if (resolvedPath == NULL) {
        base->audioGroups[groupIndex] = safeCalloc(1, sizeof(DataWin));
        return;
    }

    DataWinParserOptions options = {0};
    options.parseAudo = true;
    options.lazyLoadAudio = base->dw->lazyLoadAudio;
    if (base->dw->mappedFile != NULL) {
        options.loadType = DATAWINLOADTYPE_MAP_FILE;
    }

    DataWin* audioGroup = DataWin_parse(resolvedPath, options);
    free(resolvedPath);
    base->audioGroups[groupIndex] =
        audioGroup != NULL ? audioGroup : safeCalloc(1, sizeof(DataWin));
}

static bool groupIsLoaded(AudioSystem* base, int32_t groupIndex) {
    return base != NULL && groupIndex >= 0 && arrlen(base->audioGroups) > groupIndex &&
        base->audioGroups[groupIndex] != NULL;
}

static int32_t createStream(AudioSystem* base, const char* filename) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    if (audio == NULL || !audio->initialized || filename == NULL || filename[0] == '\0') return -1;

    char* path = resolveStreamPath(audio, filename);
    if (path == NULL) {
        logWarn("Wii audio: failed to resolve stream %s", filename);
        free(path);
        return -1;
    }

    for (int32_t i = 0; i < WII_AUDIO_MAX_STREAMS; ++i) {
        WiiAudioStream* stream = &audio->streams[i];
        if (stream->active && stream->path != NULL && strcmp(stream->path, path) == 0) {
            stream->refCount += 1u;
            free(path);
            return WII_AUDIO_STREAM_INDEX_BASE + i;
        }
    }

    WiiAudioStream* stream = reusableStreamSlot(audio);
    if (stream == NULL) {
        logWarn("Wii audio: exhausted stream slots for %s", path);
        free(path);
        return -1;
    }
    logEvent("audio", "stream info begin path=%s", path);
    if (!loadStreamInfo(stream, path)) {
        logWarn("Wii audio: failed to read stream info for %s", path);
        free(path);
        return -1;
    }
    logEvent("audio", "stream info ready rate=%lu frames=%lu",
        (unsigned long)stream->sampleRate, (unsigned long)stream->sampleCount);
    stream->active = true;
    stream->path = path;
    stream->refCount = 1u;
    stream->gain = 1.0f;
    stream->pitch = 1.0f;
    return WII_AUDIO_STREAM_INDEX_BASE + (int32_t)(stream - audio->streams);
}

static bool destroyStream(AudioSystem* base, int32_t streamIndex) {
    WiiAudioSystem* audio = (WiiAudioSystem*)base;
    WiiAudioStream* stream = streamForIndex(audio, streamIndex);
    if (stream == NULL || stream->refCount == 0u) return false;

    if (stream->refCount > 1u) {
        stream->refCount -= 1u;
        return true;
    }
    bool inUse = false;
    for (int32_t i = 0; i < WII_AUDIO_MAX_INSTANCES; ++i) {
        WiiSoundInstance* inst = &audio->instances[i];
        if (inst->active && inst->soundIndex == streamIndex) finishSoftwareInstance(inst);
        if (inst->stream == stream) inUse = true;
    }
    stream->refCount = 0u;
    if (!inUse) {
        free(stream->path);
        memset(stream, 0, sizeof(*stream));
    }
    return true;
}

static AudioSystemVtable vtable = {
    .init = init,
    .destroy = destroy,
    .update = update,
    .playSound = playSound,
    .setSoundSpatial = setSoundSpatial,
    .setListenerPosition = setListenerPosition,
    .stopSound = stopSound,
    .stopAll = stopAll,
    .isPlaying = isPlaying,
    .pauseSound = pauseSound,
    .resumeSound = resumeSound,
    .pauseAll = pauseAll,
    .resumeAll = resumeAll,
    .suspend = pauseAll,
    .resume = resumeAll,
    .setSoundGain = setSoundGain,
    .getSoundGain = getSoundGain,
    .setSoundPitch = setSoundPitch,
    .getSoundPitch = getSoundPitch,
    .getTrackPosition = getTrackPosition,
    .setTrackPosition = setTrackPosition,
    .getSoundLength = getSoundLength,
    .setMasterGain = setMasterGain,
    .setMasterGainForListener = setListenerGain,
    .setChannelCount = setChannelCount,
    .setGroupGain = setGroupGain,
    .groupLoad = groupLoad,
    .groupIsLoaded = groupIsLoaded,
    .createStream = createStream,
    .destroyStream = destroyStream,
};

WiiAudioSystem* WiiAudio_create(MAYBE_UNUSED const char* dataWinPath) {
    WiiAudioSystem* audio = safeCalloc(1, sizeof(WiiAudioSystem));
    audio->base.vtable = &vtable;
    audio->masterGain = 1.0f;
    return audio;
}
