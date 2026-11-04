#include "sound_bank.h"
#include "audio_util.h"

#include <limits.h>
#include <string.h>

bool Sbk_parseHeader(const uint8_t header[16], uint32_t bankSize, SbkHeader* out) {
    if (header == NULL || out == NULL || bankSize < 16u || Audio_readLe32(header) != SBK_MAGIC ||
        Audio_readLe32(header + 4u) != SBK_VERSION)
        return false;

    uint32_t count = Audio_readLe32(header + 8u);
    uint32_t dataOffset = Audio_readLe32(header + 12u);
    if (count > (UINT32_MAX - 16u) / SBK_ENTRY_STRIDE || dataOffset < 16u ||
        dataOffset > bankSize || count * SBK_ENTRY_STRIDE > dataOffset - 16u)
        return false;

    out->count = count;
    out->dataOffset = dataOffset;
    return true;
}

bool Sbk_parseEntries(
    const uint8_t* table, const SbkHeader* header, uint32_t bankSize, SbkEntry* out) {
    if (header == NULL || (header->count > 0u && (table == NULL || out == NULL))) return false;
    for (uint32_t i = 0u; i < header->count; ++i) {
        const uint8_t* row = table + i * SBK_ENTRY_STRIDE;
        SbkEntry* entry = &out[i];
        entry->offset = Audio_readLe32(row);
        entry->size = Audio_readLe32(row + 4u);
        entry->sampleRate = Audio_readLe32(row + 8u);
        entry->sampleCount = Audio_readLe32(row + 12u);
        entry->flags = Audio_readLe32(row + 16u);
        if (entry->size > 0u &&
            (entry->offset < header->dataOffset || entry->offset >= bankSize ||
                entry->size > bankSize - entry->offset))
            return false;
    }
    return true;
}

bool Wdsp_parseHeader(const uint8_t header[WDSP_HEADER_SIZE], uint32_t size, WdspSound* out) {
    if (header == NULL || out == NULL || size < WDSP_HEADER_SIZE ||
        Audio_readLe32(header) != WDSP_MAGIC || Audio_readLe32(header + 4u) != WDSP_VERSION)
        return false;

    uint32_t channels = Audio_readLe32(header + 8u);
    uint32_t sampleRate = Audio_readLe32(header + 12u);
    uint32_t sampleCount = Audio_readLe32(header + 16u);
    uint32_t encodedSamples = Audio_readLe32(header + 20u);
    uint32_t headerSize = Audio_readLe32(header + 24u);
    if (channels == 0u || channels > 2u || sampleRate == 0u || sampleCount == 0u ||
        encodedSamples < sampleCount || encodedSamples % 14u != 0u ||
        headerSize < WDSP_HEADER_SIZE || headerSize > size ||
        32u + channels * WDSP_CHANNEL_ROW_SIZE > headerSize)
        return false;

    uint32_t frames = encodedSamples / 14u;
    if (frames > UINT32_MAX / 8u) return false;

    memset(out, 0, sizeof(*out));
    out->sampleRate = sampleRate;
    out->sampleCount = sampleCount;
    out->channelCount = (uint8_t)channels;
    for (uint32_t i = 0u; i < channels; ++i) {
        const uint8_t* row = header + 32u + i * WDSP_CHANNEL_ROW_SIZE;
        WdspChannel* channel = &out->channels[i];
        channel->offset = Audio_readLe32(row);
        channel->size = Audio_readLe32(row + 4u);
        if (channel->offset > size || channel->size > size - channel->offset ||
            channel->size < frames * 8u)
            return false;
        for (uint32_t j = 0u; j < 16u; ++j)
            channel->coefs[j] = Audio_readLe16(row + 8u + j * 2u);
        channel->startPredictorScale = Audio_readLe16(row + 40u);
        channel->startHistory1 = (int16_t)Audio_readLe16(row + 42u);
        channel->startHistory2 = (int16_t)Audio_readLe16(row + 44u);
        channel->loopPredictorScale = Audio_readLe16(row + 46u);
        channel->loopHistory1 = (int16_t)Audio_readLe16(row + 48u);
        channel->loopHistory2 = (int16_t)Audio_readLe16(row + 50u);
    }
    return true;
}

bool Wdsp_parse(const uint8_t* blob, uint32_t size, WdspSound* out) {
    return Wdsp_parseHeader(blob, size, out);
}
