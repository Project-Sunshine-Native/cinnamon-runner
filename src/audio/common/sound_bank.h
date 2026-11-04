#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SBK_MAGIC 0x314B4253u
#define SBK_VERSION 2u
#define SBK_ENTRY_STRIDE 20u
#define SBK_FLAG_DSP_ADPCM 0x00000800u
#define WDSP_MAGIC 0x50534457u
#define WDSP_VERSION 2u
#define WDSP_HEADER_SIZE 160u
#define WDSP_CHANNEL_ROW_SIZE 56u

typedef struct {
    uint32_t count;
    uint32_t dataOffset;
} SbkHeader;

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint32_t flags;
} SbkEntry;

typedef struct {
    uint16_t coefs[16];
    uint32_t offset;
    uint32_t size;
    uint16_t startPredictorScale;
    int16_t startHistory1;
    int16_t startHistory2;
    uint16_t loopPredictorScale;
    int16_t loopHistory1;
    int16_t loopHistory2;
} WdspChannel;

typedef struct {
    uint32_t sampleRate;
    uint32_t sampleCount;
    uint8_t channelCount;
    WdspChannel channels[2];
} WdspSound;

bool Sbk_parseHeader(const uint8_t header[16], uint32_t bankSize, SbkHeader* out);
bool Sbk_parseEntries(const uint8_t* table, const SbkHeader* header, uint32_t bankSize, SbkEntry* out);
bool Wdsp_parseHeader(const uint8_t header[WDSP_HEADER_SIZE], uint32_t size, WdspSound* out);
bool Wdsp_parse(const uint8_t* blob, uint32_t size, WdspSound* out);
