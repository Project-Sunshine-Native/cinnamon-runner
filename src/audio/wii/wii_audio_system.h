#pragma once

#include "../../audio_system.h"

typedef struct WiiAudioSystem WiiAudioSystem;

WiiAudioSystem* WiiAudio_create(const char* dataWinPath);

void WiiAudio_pump(WiiAudioSystem* audio);
