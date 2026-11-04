#include "3ds_log.h"

#include <3ds.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>

#define N3DS_LOG_PATH "sdmc:/3ds/cinnamon/runner_log.txt"
#define N3DS_LOG_BUFFER_SIZE 1024

static FILE* gLogFile;
static u64 gLogStartTick;
static uint32_t gLogEventCount;
static bool gLogEnabled;
static char gLogBuffer[N3DS_LOG_BUFFER_SIZE];

static double n3ds_logTicksToMs(u64 ticks) {
    return (double)ticks * 1000.0 / (double)SYSCLOCK_ARM11;
}

static void n3ds_logOpenIfNeeded(bool truncate) {
    if (gLogStartTick == 0) gLogStartTick = svcGetSystemTick();
    if (gLogFile != NULL) return;

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/cinnamon", 0777);
    gLogFile = fopen(N3DS_LOG_PATH, truncate ? "w" : "a");
    if (gLogFile != NULL) {
        setvbuf(gLogFile, gLogBuffer, _IOFBF, sizeof(gLogBuffer));
    }
}

void logSetEnabled(bool enabled) {
    gLogEnabled = enabled;
}

void logInit(void) {
    if (!gLogEnabled) return;
    n3ds_logOpenIfNeeded(true);
}

void logClose(void) {
    if (gLogFile == NULL) return;
    fflush(gLogFile);
    fclose(gLogFile);
    gLogFile = NULL;
}

void logEvent(const char* tag, const char* format, ...) {
    if (!gLogEnabled) return;
    n3ds_logOpenIfNeeded(false);

    char message[160];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format != NULL ? format : "", args);
    va_end(args);

    double elapsedMs = n3ds_logTicksToMs(svcGetSystemTick() - gLogStartTick);
    uint32_t eventCount = ++gLogEventCount;
    if (gLogFile != NULL) {
        fprintf(gLogFile, "[%10.2f ms] #%06lu %-10s %s\n", elapsedMs, (unsigned long)eventCount,
            tag != NULL ? tag : "event", message);
    }
}

void platformLog(const logType type, const char* format, va_list va) {
    if (!gLogEnabled) return;
    va_list copy;
    va_copy(copy, va);
    char message[160];
    vsnprintf(message, sizeof(message), format != NULL ? format : "", copy);
    va_end(copy);
    logEvent(type == LOG_TYPE_ERROR
            ? "error"
            : type == LOG_TYPE_WARNING
                ? "warning"
                : "runtime",
        "%s", message);
}
