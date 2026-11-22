#pragma once

#include <citro2d.h>
#include "../audio_system.h"

bool n3ds_menuIntro(C3D_RenderTarget* top, C3D_RenderTarget* bottom,
    AudioSystem* audio, int volume, bool enabled, bool (*loadNext)(void*), void* context);
