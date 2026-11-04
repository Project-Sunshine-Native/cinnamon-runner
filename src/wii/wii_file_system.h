#pragma once

#include "../file_system.h"

typedef struct {
    FileSystem base;
    char* basePath;
} WiiFileSystem;

WiiFileSystem* WiiFS_create(const char* dataWinPath);
void WiiFS_destroy(WiiFileSystem* fs);

char* WiiFS_readText(const char* path);
bool WiiFS_pathHasScheme(const char* path);
void WiiFS_normalizeSeparators(char* path);
char* WiiFS_getParentDir(const char* path);
bool WiiFS_pathIsFile(const char* path);
bool WiiFS_dirExists(const char* path);
char* WiiFS_joinPath(const char* base, const char* child);
char* WiiFS_resolveSettingsPath(int argc, char** argv, const char* fallbackPath);
char** WiiFS_resolveDataWins(
    int argc, char** argv, const char* configuredRoot, const char* defaultRoot);
char* WiiFS_resolveGamePath(
    const char* currentDataWinPath, const char* workingDirectory, char** launchArgs);
