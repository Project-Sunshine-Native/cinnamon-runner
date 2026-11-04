#pragma once

#include "../file_system.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct N3DSFileSystemResolvedPathEntry N3DSFileSystemResolvedPathEntry;

typedef struct {
    FileSystem base;
    char* gameBasePath;
    char* saveBasePath;
    N3DSFileSystemResolvedPathEntry* resolvedPathCache;
} N3DSFileSystem;

N3DSFileSystem* n3ds_fsCreate(const char* gameBasePath, const char* saveBasePath);
void n3ds_fsDestroy(N3DSFileSystem* fs);
void n3ds_fsSetGameRoot(const char* root);
const char* n3ds_fsGetGameRoot(void);
bool n3ds_fsPathExists(const char* path);
bool n3ds_fsPathIsDirectory(const char* path);
bool n3ds_fsGetParentDir(const char* path, char* outDir, size_t outDirSize);
FILE* n3ds_fsOpenReadPath(const char* path);
FILE* n3ds_fsOpenGfxAsset(const char* relativePath, char* outPath, size_t outPathSize);
size_t n3ds_fsReadFileBytes(FILE* file, uint8_t* dst, size_t size);
