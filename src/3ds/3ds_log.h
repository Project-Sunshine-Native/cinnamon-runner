#pragma once

#include "../log.h"

void logSetEnabled(bool enabled);
void logInit(void);
void logClose(void);
void logEvent(const char* tag, const char* format, ...);
