#include "vorbis_probe.h"

#include <stdio.h>
#include <string.h>
#include <tremor/ivorbisfile.h>

bool Audio_readVorbisStreamInfo(
    const char* path, size_t fileBufferSize, uint32_t* rate, uint32_t* samples, uint8_t* channels) {
    if (path == NULL || rate == NULL || samples == NULL || channels == NULL) return false;

    FILE* file = fopen(path, "rb");
    if (file == NULL) return false;
    if (fileBufferSize > 0u) setvbuf(file, NULL, _IOFBF, fileBufferSize);

    OggVorbis_File vorbisFile;
    memset(&vorbisFile, 0, sizeof(vorbisFile));
    if (ov_open(file, &vorbisFile, NULL, 0) < 0) {
        fclose(file);
        return false;
    }

    vorbis_info* info = ov_info(&vorbisFile, -1);
    ogg_int64_t totalSamples = ov_pcm_total(&vorbisFile, -1);
    bool valid = info != NULL && info->rate > 0 && info->channels > 0 && info->channels <= 2 &&
        totalSamples > 0 && totalSamples <= UINT32_MAX;
    if (valid) {
        *rate = (uint32_t)info->rate;
        *samples = (uint32_t)totalSamples;
        *channels = (uint8_t)info->channels;
    }
    ov_clear(&vorbisFile);
    return valid;
}
