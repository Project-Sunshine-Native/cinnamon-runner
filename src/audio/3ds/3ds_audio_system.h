#pragma once

#include "audio_system.h"

typedef struct N3DSAudioSystem N3DSAudioSystem;

N3DSAudioSystem* n3ds_audioCreate(void);
void n3ds_audioSetOutputGain(N3DSAudioSystem* audio, float gain);
bool n3ds_audioIsReady(const N3DSAudioSystem* audio);
const char* n3ds_audioGetStartupError(const N3DSAudioSystem* audio);
