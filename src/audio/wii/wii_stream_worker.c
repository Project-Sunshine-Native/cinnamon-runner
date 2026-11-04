#include "wii_stream_worker.h"

#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STREAM_WORKER_MAX_JOBS 128u
#define STREAM_WORKER_RING_FRAMES 8192u
#define STREAM_WORKER_CHUNK_FRAMES 2048u
#define STREAM_WORKER_MAX_CHUNKS 32u
#define STREAM_WORKER_STACK_BYTES (64u * 1024u)
#define STREAM_WORKER_PRIORITY 50u

struct WiiStreamJob {
    WiiStreamWorker* worker;
    WiiStreamJob* next;
    const StreamedBank* bank;
    const StreamedBankEntry* entry;
    char* path;
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint8_t channels;
    bool dspAdpcm;
    bool loop;

    bool cancelled;
    bool failed;
    uint32_t generation;
    uint32_t baseFrame;
    uint32_t bufferedFrames;
    int16_t* pcm;

    StreamedAudio* decoder;
    uint32_t decoderGeneration;
};

struct WiiStreamWorker {
    lwp_t thread;
    mutex_t mutex;
    sem_t wake;
    bool stopping;
    uint32_t jobCount;
    WiiStreamJob* jobs;
    int16_t scratch[STREAM_WORKER_CHUNK_FRAMES * 2u];
};

static void freeJob(WiiStreamJob* job) {
    StreamedAudio_close(job->decoder);
    free(job->pcm);
    free(job->path);
    free(job);
}

static StreamedAudio* openDecoder(WiiStreamJob* job) {
    if (job->entry != NULL) return StreamedAudio_open(job->bank, job->entry);

    FILE* file = fopen(job->path, "rb");
    if (file == NULL) return NULL;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    fclose(file);
    if (size <= 0 || (uint64_t)size > UINT32_MAX) return NULL;
    StreamedBank bank = {.path = job->path};
    StreamedBankEntry entry = {
        .size = (uint32_t)size,
        .sampleRate = job->sampleRate,
        .sampleCount = job->sampleCount,
        .flags = job->channels | (job->dspAdpcm ? STREAMED_BANK_DSP : STREAMED_BANK_VORBIS),
    };
    return StreamedAudio_open(&bank, &entry);
}

static void fillJob(WiiStreamJob* job) {
    WiiStreamWorker* worker = job->worker;
    uint32_t budget = STREAM_WORKER_RING_FRAMES;
    for (uint32_t chunk = 0; chunk < STREAM_WORKER_MAX_CHUNKS && budget != 0u; ++chunk) {
        LWP_MutexLock(worker->mutex);
        bool done = worker->stopping || job->cancelled || job->failed;
        uint32_t generation = job->generation;
        uint32_t base = job->baseFrame;
        uint32_t writable = STREAM_WORKER_RING_FRAMES - job->bufferedFrames;
        LWP_MutexUnlock(worker->mutex);
        if (done || writable == 0u) return;

        if (job->decoder == NULL) {
            job->decoder = openDecoder(job);
            job->pcm = malloc(STREAM_WORKER_RING_FRAMES * job->channels * sizeof(int16_t));
            if (job->decoder == NULL || job->pcm == NULL) {
                LWP_MutexLock(worker->mutex);
                job->failed = true;
                LWP_MutexUnlock(worker->mutex);
                return;
            }
            LWP_MutexLock(worker->mutex);
            job->sampleCount = StreamedAudio_sampleCount(job->decoder);
            LWP_MutexUnlock(worker->mutex);
        }
        if (job->decoderGeneration != generation) {
            uint32_t sample = job->loop ? base % job->sampleCount : base;
            bool ok = StreamedAudio_seek(job->decoder, sample);
            job->decoderGeneration = generation;
            if (!ok) {
                LWP_MutexLock(worker->mutex);
                if (job->generation == generation) job->failed = true;
                LWP_MutexUnlock(worker->mutex);
                return;
            }
        }
        uint32_t sample = StreamedAudio_tell(job->decoder);
        if (sample >= job->sampleCount) {
            if (!job->loop) return;
            if (!StreamedAudio_seek(job->decoder, 0u)) {
                LWP_MutexLock(worker->mutex);
                if (job->generation == generation) job->failed = true;
                LWP_MutexUnlock(worker->mutex);
                return;
            }
            sample = 0u;
        }
        uint32_t frames = job->sampleCount - sample;
        if (frames > writable) frames = writable;
        if (frames > budget) frames = budget;
        if (frames > STREAM_WORKER_CHUNK_FRAMES) frames = STREAM_WORKER_CHUNK_FRAMES;
        int32_t decoded = StreamedAudio_read(job->decoder, worker->scratch, frames);

        LWP_MutexLock(worker->mutex);
        if (job->generation != generation || job->cancelled || worker->stopping) {
            LWP_MutexUnlock(worker->mutex);
            return;
        }
        if (decoded == 0 && sample != 0u) {
            job->sampleCount = sample;
            LWP_MutexUnlock(worker->mutex);
            continue;
        }
        if (decoded <= 0 || (uint32_t)decoded > frames) {
            job->failed = true;
            LWP_MutexUnlock(worker->mutex);
            return;
        }
        uint32_t write = (job->baseFrame + job->bufferedFrames) % STREAM_WORKER_RING_FRAMES;
        uint32_t first = STREAM_WORKER_RING_FRAMES - write;
        if (first > (uint32_t)decoded) first = (uint32_t)decoded;
        memcpy(job->pcm + write * job->channels, worker->scratch,
            first * job->channels * sizeof(int16_t));
        memcpy(job->pcm, worker->scratch + first * job->channels,
            ((uint32_t)decoded - first) * job->channels * sizeof(int16_t));
        job->bufferedFrames += (uint32_t)decoded;
        budget -= (uint32_t)decoded;
        LWP_MutexUnlock(worker->mutex);
    }
}

static void* streamWorkerThread(void* context) {
    WiiStreamWorker* worker = context;
    for (;;) {
        LWP_SemWait(worker->wake);
        LWP_MutexLock(worker->mutex);
        bool stopping = worker->stopping;
        WiiStreamJob* job = worker->jobs;
        LWP_MutexUnlock(worker->mutex);
        if (stopping) break;

        while (job != NULL) {
            fillJob(job);
            LWP_MutexLock(worker->mutex);
            WiiStreamJob* next = job->next;
            bool cancelled = job->cancelled;
            if (cancelled) {
                WiiStreamJob** link = &worker->jobs;
                while (*link != job) link = &(*link)->next;
                *link = next;
                --worker->jobCount;
            }
            stopping = worker->stopping;
            LWP_MutexUnlock(worker->mutex);
            if (cancelled) freeJob(job);
            if (stopping) break;
            job = next;
        }
    }
    while (worker->jobs != NULL) {
        WiiStreamJob* job = worker->jobs;
        worker->jobs = job->next;
        freeJob(job);
    }
    return NULL;
}

WiiStreamWorker* WiiStream_create(void) {
    WiiStreamWorker* worker = calloc(1, sizeof(*worker));
    if (worker == NULL) return NULL;
    worker->thread = LWP_THREAD_NULL;
    worker->mutex = LWP_MUTEX_NULL;
    worker->wake = LWP_SEM_NULL;
    if (LWP_MutexInit(&worker->mutex, false) < 0 ||
        LWP_SemInit(&worker->wake, 0u, 1u) < 0 ||
        LWP_CreateThread(&worker->thread, streamWorkerThread, worker, NULL,
            STREAM_WORKER_STACK_BYTES, STREAM_WORKER_PRIORITY) < 0) {
        WiiStream_destroy(worker);
        return NULL;
    }
    return worker;
}

void WiiStream_destroy(WiiStreamWorker* worker) {
    if (worker == NULL) return;
    if (worker->thread != LWP_THREAD_NULL) {
        LWP_MutexLock(worker->mutex);
        worker->stopping = true;
        LWP_MutexUnlock(worker->mutex);
        LWP_SemPost(worker->wake);
        LWP_JoinThread(worker->thread, NULL);
    }
    if (worker->wake != LWP_SEM_NULL) LWP_SemDestroy(worker->wake);
    if (worker->mutex != LWP_MUTEX_NULL) LWP_MutexDestroy(worker->mutex);
    free(worker);
}

void WiiStream_wake(WiiStreamWorker* worker) {
    if (worker != NULL) LWP_SemPost(worker->wake);
}

WiiStreamJob* WiiStream_open(WiiStreamWorker* worker, const StreamedBank* bank,
    const StreamedBankEntry* entry, const char* path, uint32_t sampleRate,
    uint32_t sampleCount, uint8_t channels, bool dspAdpcm, bool loop) {
    if (worker == NULL || sampleRate == 0u || sampleCount == 0u ||
        channels == 0u || channels > 2u || (entry == NULL && path == NULL)) return NULL;
    WiiStreamJob* job = calloc(1, sizeof(*job));
    if (job == NULL) return NULL;
    if (path != NULL) {
        job->path = malloc(strlen(path) + 1u);
        if (job->path == NULL) { free(job); return NULL; }
        strcpy(job->path, path);
    }
    job->worker = worker;
    job->bank = bank;
    job->entry = entry;
    job->sampleRate = sampleRate;
    job->sampleCount = sampleCount;
    job->channels = channels;
    job->dspAdpcm = dspAdpcm;
    job->loop = loop;
    job->generation = 1u;
    LWP_MutexLock(worker->mutex);
    if (worker->stopping || worker->jobCount >= STREAM_WORKER_MAX_JOBS) {
        LWP_MutexUnlock(worker->mutex);
        free(job->path);
        free(job);
        return NULL;
    }
    job->next = worker->jobs;
    worker->jobs = job;
    ++worker->jobCount;
    LWP_MutexUnlock(worker->mutex);
    WiiStream_wake(worker);
    return job;
}

void WiiJob_release(WiiStreamJob* job) {
    if (job == NULL) return;
    WiiStreamWorker* worker = job->worker;
    LWP_MutexLock(worker->mutex);
    job->cancelled = true;
    LWP_MutexUnlock(worker->mutex);
    WiiStream_wake(worker);
}

void WiiJob_seek(WiiStreamJob* job, uint32_t sample) {
    WiiStreamWorker* worker = job->worker;
    LWP_MutexLock(worker->mutex);
    ++job->generation;
    job->baseFrame = sample;
    job->bufferedFrames = 0u;
    job->failed = false;
    LWP_MutexUnlock(worker->mutex);
    WiiStream_wake(worker);
}

int32_t WiiJob_read(WiiStreamJob* job, uint32_t sample, int16_t* pcm,
    uint32_t maxFrames, uint32_t* sampleCount) {
    WiiStreamWorker* worker = job->worker;
    if (LWP_MutexTryLock(worker->mutex) != 0) return 0;
    *sampleCount = job->sampleCount;
    int32_t frames = 0;
    if (sample == job->baseFrame && job->bufferedFrames != 0u) {
        uint32_t read = sample % STREAM_WORKER_RING_FRAMES;
        uint32_t available = STREAM_WORKER_RING_FRAMES - read;
        if (available > job->bufferedFrames) available = job->bufferedFrames;
        if (available > maxFrames) available = maxFrames;
        memcpy(pcm, job->pcm + read * job->channels, available * job->channels * sizeof(int16_t));
        job->baseFrame += available;
        job->bufferedFrames -= available;
        frames = (int32_t)available;
    } else if (job->failed) {
        frames = -1;
    }
    LWP_MutexUnlock(worker->mutex);
    return frames;
}
