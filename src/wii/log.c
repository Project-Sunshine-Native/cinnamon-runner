#include "wii_log.h"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WII_LOG_PATH_SIZE 512
#define WII_LOG_BUFFER_SIZE 1024

static FILE* gLogFile;
static uint64_t gLogStartTicks;
static uint32_t gLogEventCount;
static bool gLogEnabled;
static char gLogPath[WII_LOG_PATH_SIZE] = "runner_log.txt";
static char gLogBuffer[WII_LOG_BUFFER_SIZE];

static double logTicksToMs(uint64_t ticks) {
    return (double)ticks_to_microsecs(ticks) / 1000.0;
}

static void logOpenIfNeeded(bool truncate) {
    if (gLogStartTicks == 0) gLogStartTicks = gettime();
    if (gLogFile != NULL) return;

    gLogFile = fopen(gLogPath, truncate ? "w" : "a");
    if (gLogFile != NULL) {
        setvbuf(gLogFile, gLogBuffer, _IOFBF, sizeof(gLogBuffer));
    }
}

void logSetEnabled(bool enabled) {
    gLogEnabled = enabled;
}

void logSetDirectory(const char* directory) {
    if (directory == NULL || directory[0] == '\0') return;
    size_t len = strlen(directory);
    snprintf(gLogPath, sizeof(gLogPath), "%s%srunner_log.txt", directory,
        directory[len - 1u] == '/' ? "" : "/");
}

void logInit(void) {
    if (!gLogEnabled) return;
    logOpenIfNeeded(true);
    logEvent("boot", "log initialized build=%s %s", __DATE__, __TIME__);
}

void logClose(void) {
    if (gLogFile == NULL) return;
    fflush(gLogFile);
    fclose(gLogFile);
    gLogFile = NULL;
}

void logEvent(const char* tag, const char* format, ...) {
    if (!gLogEnabled) return;
    logOpenIfNeeded(false);

    char message[160];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format != NULL ? format : "", args);
    va_end(args);

    uint64_t nowTicks = gettime();
    double elapsedMs = logTicksToMs(nowTicks - gLogStartTicks);
    uint32_t eventCount = ++gLogEventCount;
    if (gLogFile != NULL) {
        fprintf(gLogFile, "[%10.2f ms] #%06lu %-10s %s\n", elapsedMs, (unsigned long)eventCount,
            tag != NULL ? tag : "event", message);
        if (fflush(gLogFile) == 0) fsync(fileno(gLogFile));
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
