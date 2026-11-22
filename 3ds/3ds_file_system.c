#include "3ds_file_system.h"
#include "../utils.h"
#include <3ds.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stb/ds/stb_ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define N3DS_FILE_BUFFER_SIZE (128u * 1024u)
#define N3DS_SDMC_LEGACY_ROOT "sdmc:/3ds/cinnamon"

typedef struct N3DSFileSystemResolvedPathEntry {
    char* key;
    char* value;
} N3DSFileSystemResolvedPathEntry;

static bool n3ds_fsPathHasScheme(const char* path) {
    return path != NULL && strstr(path, ":/") != NULL;
}

static char* n3ds_fsNormalizePath(const char* path) {
    if (path == NULL) return NULL;

    char* normalized = safeStrdup(path);
    for (char* cursor = normalized; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') *cursor = '/';
    }
    return normalized;
}

static bool n3ds_fsMakeParentDirectories(const char* path) {
    char* parent = safeStrdup(path);
    char* scheme = strstr(parent, ":/");
    char* start = scheme != NULL ? scheme + 2 : parent;

    for (char* slash = strchr(start, '/'); slash != NULL; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        bool ok = mkdir(parent, 0777) == 0 || errno == EEXIST;
        *slash = '/';
        if (!ok) {
            free(parent);
            return false;
        }
    }
    free(parent);
    return true;
}

static void n3ds_fsSetFileBuffer(FILE* file) {
    if (file == NULL) return;
    setvbuf(file, NULL, _IOFBF, N3DS_FILE_BUFFER_SIZE);
}

bool n3ds_fsPathExists(const char* path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0;
}

bool n3ds_fsPathIsDirectory(const char* path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool n3ds_fsGetParentDir(const char* path, char* outDir, size_t outDirSize) {
    if (path == NULL || outDir == NULL || outDirSize == 0) return false;

    const char* lastSlash = strrchr(path, '/');
    const char* lastBackslash = strrchr(path, '\\');
    const char* separator = lastSlash;
    if (lastBackslash != NULL && (separator == NULL || lastBackslash > separator)) {
        separator = lastBackslash;
    }
    if (separator == NULL) return false;

    size_t length = (size_t)(separator - path);
    if (length == 0 || length >= outDirSize) return false;
    memcpy(outDir, path, length);
    outDir[length] = '\0';
    return true;
}

static char* n3ds_fsJoin(const char* base, const char* relativePath) {
    if (relativePath == NULL) return NULL;
    if (n3ds_fsPathHasScheme(relativePath))
        return n3ds_fsNormalizePath(relativePath);

    char* normalizedRelativePath = n3ds_fsNormalizePath(relativePath);
    relativePath = normalizedRelativePath;

    size_t baseLen = strlen(base);
    size_t relLen = strlen(relativePath);
    if (relLen == 0) {
        bool needSlash = baseLen > 0 && base[baseLen - 1] != '/';
        char* result = safeMalloc(baseLen + (needSlash ? 2 : 1));
        memcpy(result, base, baseLen);
        if (needSlash) result[baseLen++] = '/';
        result[baseLen] = '\0';
        free(normalizedRelativePath);
        return result;
    }

    bool needSlash =
        baseLen > 0 && base[baseLen - 1] != '/' && relLen > 0 && relativePath[0] != '/';
    char* result = safeMalloc(baseLen + relLen + (needSlash ? 2 : 1));
    memcpy(result, base, baseLen);
    size_t cursor = baseLen;
    if (needSlash) result[cursor++] = '/';
    memcpy(result + cursor, relativePath, relLen);
    result[cursor + relLen] = '\0';
    free(normalizedRelativePath);
    return result;
}

static bool n3ds_fsIsSaveFile(const char* path) {
    if (path == NULL || path[0] == '\0' || strchr(path, '/') != NULL || strchr(path, '\\') != NULL)
        return false;

    if (strncmp(path, "filech", 6) == 0 && isdigit((unsigned char)path[6])) return true;
    if (strncmp(path, "file", 4) == 0 && isdigit((unsigned char)path[4])) return true;
    if (strncmp(path, "save", 4) == 0) return true;

    const char* extension = strrchr(path, '.');
    return extension != NULL && (strcmp(extension, ".ini") == 0 ||
        strcmp(extension, ".sav") == 0 || strcmp(extension, ".save") == 0);
}

static bool n3ds_fsIsChapterDirectory(const char* name) {
    char lower[256];
    size_t length = strlen(name);
    if (length >= sizeof(lower)) return false;
    for (size_t i = 0; i < length; ++i)
        lower[i] = (char)tolower((unsigned char)name[i]);
    lower[length] = '\0';

    return strstr(lower, "deltarune") != NULL || strstr(lower, "chapter") != NULL ||
        (length > 2 && lower[0] == 'c' && lower[1] == 'h' &&
            isdigit((unsigned char)lower[2]));
}

static void n3ds_fsConsiderLegacySave(const char* root, const char* name,
    char** selectedPath, time_t* modified) {
    char* candidate = n3ds_fsJoin(root, name);
    struct stat st;
    if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode) &&
        (*selectedPath == NULL || st.st_mtime > *modified)) {
        free(*selectedPath);
        *selectedPath = candidate;
        *modified = st.st_mtime;
    } else {
        free(candidate);
    }
}

static char* n3ds_fsFindLegacySave(N3DSFileSystem* fs, const char* name) {
    if (!n3ds_fsIsSaveFile(name) ||
        strncmp(fs->saveBasePath, N3DS_SDMC_LEGACY_ROOT "/saves/",
            sizeof(N3DS_SDMC_LEGACY_ROOT "/saves/") - 1) != 0)
        return NULL;

    char* selected = NULL;
    time_t modified = 0;
    n3ds_fsConsiderLegacySave(fs->gameBasePath, name, &selected, &modified);
    n3ds_fsConsiderLegacySave(N3DS_SDMC_LEGACY_ROOT, name, &selected, &modified);

    if (strcmp(fs->saveBasePath, N3DS_SDMC_LEGACY_ROOT "/saves/DELTARUNE") == 0) {
        DIR* dir = opendir(N3DS_SDMC_LEGACY_ROOT);
        if (dir != NULL) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (!n3ds_fsIsChapterDirectory(entry->d_name)) continue;
                char* chapter = n3ds_fsJoin(N3DS_SDMC_LEGACY_ROOT, entry->d_name);
                if (n3ds_fsPathIsDirectory(chapter))
                    n3ds_fsConsiderLegacySave(chapter, name, &selected, &modified);
                free(chapter);
            }
            closedir(dir);
        }
    }
    return selected;
}

static bool n3ds_fsCopyLegacySave(const char* source, const char* destination) {
    if (!n3ds_fsMakeParentDirectories(destination)) return false;

    FILE* input = fopen(source, "rb");
    if (input == NULL) return false;

    size_t length = strlen(destination);
    char* temp = safeMalloc(length + sizeof(".importing"));
    memcpy(temp, destination, length);
    memcpy(temp + length, ".importing", sizeof(".importing"));
    FILE* output = fopen(temp, "wb");
    if (output == NULL) {
        fclose(input);
        free(temp);
        return false;
    }

    uint8_t buffer[8192];
    bool ok = true;
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            ok = false;
            break;
        }
    }
    if (ferror(input)) ok = false;
    if (fclose(output) != 0) ok = false;
    fclose(input);

    if (ok && !n3ds_fsPathExists(destination))
        ok = rename(temp, destination) == 0;
    if (!ok || n3ds_fsPathExists(temp)) remove(temp);
    free(temp);
    return ok;
}

static bool gN3DSFileSystemGameRootDidScan = false;
static char gN3DSFileSystemGameRoot[512] = N3DS_SDMC_LEGACY_ROOT;

void n3ds_fsSetGameRoot(const char* root) {
    const char* selected = root != NULL && root[0] != '\0' ? root : N3DS_SDMC_LEGACY_ROOT;
    size_t length = strlen(selected);
    while (length > 1u && selected[length - 1u] == '/')
        length--;
    if (length >= sizeof(gN3DSFileSystemGameRoot)) length = sizeof(gN3DSFileSystemGameRoot) - 1u;
    memcpy(gN3DSFileSystemGameRoot, selected, length);
    gN3DSFileSystemGameRoot[length] = '\0';
    gN3DSFileSystemGameRootDidScan = true;
}

const char* n3ds_fsGetGameRoot(void) {
    if (gN3DSFileSystemGameRootDidScan) return gN3DSFileSystemGameRoot;
    gN3DSFileSystemGameRootDidScan = true;

    char candidate[512];
    snprintf(candidate, sizeof(candidate), "%s/data.win", N3DS_SDMC_LEGACY_ROOT);
    if (n3ds_fsPathExists(candidate)) return gN3DSFileSystemGameRoot;

    DIR* dir = opendir(N3DS_SDMC_LEGACY_ROOT);
    if (dir == NULL) return gN3DSFileSystemGameRoot;

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        const char* name = entry->d_name;
        if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        snprintf(candidate, sizeof(candidate), "%s/%s", N3DS_SDMC_LEGACY_ROOT, name);
        if (!n3ds_fsPathIsDirectory(candidate)) continue;
        char dataWinPath[512];
        snprintf(dataWinPath, sizeof(dataWinPath), "%s/data.win", candidate);
        if (!n3ds_fsPathExists(dataWinPath)) continue;
        snprintf(gN3DSFileSystemGameRoot, sizeof(gN3DSFileSystemGameRoot), "%s", candidate);
        break;
    }

    closedir(dir);
    return gN3DSFileSystemGameRoot;
}

static const char* n3ds_fsGetGameGfxRoot(void) {
    static char gfxRoot[512];
    snprintf(gfxRoot, sizeof(gfxRoot), "%s/gfx", n3ds_fsGetGameRoot());
    return gfxRoot;
}

static const char* n3ds_fsPickGfxPath(const char* relativePath) {
    if (relativePath == NULL || relativePath[0] == '\0') return NULL;
    static char sdPath[512];
    snprintf(sdPath, sizeof(sdPath), "%s/%s", n3ds_fsGetGameGfxRoot(), relativePath);
    return n3ds_fsPathExists(sdPath) ? sdPath : NULL;
}

FILE* n3ds_fsOpenReadPath(const char* path) {
    FILE* file = path != NULL ? fopen(path, "rb") : NULL;
    if (file != NULL) n3ds_fsSetFileBuffer(file);

    return file;
}

FILE* n3ds_fsOpenGfxAsset(const char* relativePath, char* outPath, size_t outPathSize) {
    const char* path = n3ds_fsPickGfxPath(relativePath);
    if (outPath != NULL && outPathSize > 0u)
        snprintf(outPath, outPathSize, "%s", path != NULL ? path : "");
    return n3ds_fsOpenReadPath(path);
}

size_t n3ds_fsReadFileBytes(FILE* file, uint8_t* dst, size_t size) {
    if (file == NULL || dst == NULL || size == 0u) return 0u;
    size_t total = 0u;
    while (total < size) {
        size_t chunk = size - total;
        if (chunk > 256u * 1024u) chunk = 256u * 1024u;
        size_t got = fread(dst + total, 1, chunk, file);
        if (got == 0u) break;
        total += got;
    }
    return total;
}

static char* n3ds_fsGetCachedPath(N3DSFileSystem* n3ds, const char* relativePath) {
    if (n3ds == NULL || relativePath == NULL) return NULL;
    ptrdiff_t idx = shgeti(n3ds->resolvedPathCache, relativePath);
    if (idx < 0) return NULL;
    return safeStrdup(n3ds->resolvedPathCache[idx].value);
}

static void n3ds_fsSetCachedPath(
    N3DSFileSystem* n3ds, const char* relativePath, const char* resolvedPath) {
    if (n3ds == NULL || relativePath == NULL || resolvedPath == NULL) return;
    ptrdiff_t idx = shgeti(n3ds->resolvedPathCache, relativePath);
    if (idx >= 0) {
        free(n3ds->resolvedPathCache[idx].value);
        n3ds->resolvedPathCache[idx].value = safeStrdup(resolvedPath);
        return;
    }

    shput(n3ds->resolvedPathCache, relativePath, safeStrdup(resolvedPath));
}

static char* n3ds_fsResolvePath(FileSystem* fs, const char* relativePath) {
    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    if (relativePath == NULL) return NULL;
    if (n3ds_fsPathHasScheme(relativePath))
        return n3ds_fsNormalizePath(relativePath);
    if (relativePath[0] == '\0')
        return n3ds_fsJoin(n3ds->gameBasePath, relativePath);

    char* normalizedRelativePath = n3ds_fsNormalizePath(relativePath);
    relativePath = normalizedRelativePath;

    char* cachedPath = n3ds_fsGetCachedPath(n3ds, relativePath);
    if (cachedPath != NULL) {
        free(normalizedRelativePath);
        return cachedPath;
    }

    char* savePath = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (!n3ds_fsPathExists(savePath)) {
        char* legacy = n3ds_fsFindLegacySave(n3ds, relativePath);
        if (legacy != NULL) {
            if (!n3ds_fsCopyLegacySave(legacy, savePath)) {
                free(savePath);
                free(normalizedRelativePath);
                return legacy;
            }
            free(legacy);
        }
    }
    if (n3ds_fsPathExists(savePath)) {
        n3ds_fsSetCachedPath(n3ds, relativePath, savePath);
        free(normalizedRelativePath);
        return savePath;
    }

    char* gamePath = n3ds_fsJoin(n3ds->gameBasePath, relativePath);
    if (n3ds_fsPathExists(gamePath)) {
        n3ds_fsSetCachedPath(n3ds, relativePath, gamePath);
        free(savePath);
        free(normalizedRelativePath);
        return gamePath;
    }

    n3ds_fsSetCachedPath(n3ds, relativePath, savePath);
    free(gamePath);
    free(normalizedRelativePath);
    return savePath;
}

static bool n3ds_fsFileExists(FileSystem* fs, const char* relativePath) {
    char* path = n3ds_fsResolvePath(fs, relativePath);
    struct stat st;
    bool ok = path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);

    free(path);
    return ok;
}

static bool n3ds_fsDirectoryExists(FileSystem* fs, const char* relativePath) {
    char* path = n3ds_fsResolvePath(fs, relativePath);
    struct stat st;
    bool ok = path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);

    free(path);
    return ok;
}

static char* n3ds_fsReadFileText(FileSystem* fs, const char* relativePath) {
    char* path = n3ds_fsResolvePath(fs, relativePath);
    if (path == NULL) return NULL;

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        free(path);
        return NULL;
    }
    n3ds_fsSetFileBuffer(file);

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        free(path);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        free(path);
        return NULL;
    }

    char* text = safeMalloc((size_t)size + 1);
    size_t bytesRead = fread(text, 1, (size_t)size, file);
    bool readOk = bytesRead == (size_t)size;
    fclose(file);

    free(path);
    if (!readOk) {
        free(text);
        return NULL;
    }
    text[bytesRead] = '\0';
    return text;
}

static bool n3ds_fsWriteFileText(
    FileSystem* fs, const char* relativePath, const char* contents) {
    if (contents == NULL) return false;

    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    char* path = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (path == NULL) return false;
    if (!n3ds_fsMakeParentDirectories(path)) {
        free(path);
        return false;
    }

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        free(path);
        return false;
    }
    n3ds_fsSetFileBuffer(file);

    size_t length = strlen(contents);
    bool ok = fwrite(contents, 1, length, file) == length;
    if (fclose(file) != 0) ok = false;

    if (ok) {
        n3ds_fsSetCachedPath(n3ds, relativePath, path);
    }
    free(path);
    return ok;
}

static bool n3ds_fsDeleteFile(FileSystem* fs, const char* relativePath) {
    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    char* path = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (path == NULL) return false;
    int rc = remove(path);

    if (rc == 0) {
        char* gamePath = n3ds_fsJoin(n3ds->gameBasePath, relativePath);
        n3ds_fsSetCachedPath(n3ds, relativePath, gamePath);
        free(gamePath);
    }
    free(path);
    return rc == 0;
}

static bool n3ds_fsCreateDirectory(FileSystem* fs, const char* relativePath) {
    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    char* path = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (path == NULL) return false;
    if (!n3ds_fsMakeParentDirectories(path)) {
        free(path);
        return false;
    }
    int rc = mkdir(path, 0777);
    free(path);
    return rc == 0;
}

static bool n3ds_fsDeleteDirectory(FileSystem* fs, const char* relativePath) {
    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    char* path = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (path == NULL) return false;
    int rc = rmdir(path);
    free(path);
    return rc == 0;
}

static void n3ds_fsAppendDirectory(FileSystemDirEntry** entries, const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) return;

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        bool duplicate = false;
        for (size_t i = 0; i < arrlenu(*entries); ++i) {
            if (strcmp((*entries)[i].name, entry->d_name) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        bool isDirectory = false;
#ifdef DT_DIR
        if (entry->d_type != DT_UNKNOWN) {
            isDirectory = entry->d_type == DT_DIR;
        } else
#endif
        {
            char* childPath = n3ds_fsJoin(path, entry->d_name);
            struct stat st;
            isDirectory = childPath != NULL && stat(childPath, &st) == 0 && S_ISDIR(st.st_mode);
            free(childPath);
        }

        arrput(*entries, ((FileSystemDirEntry){
                           .name = safeStrdup(entry->d_name),
                           .isDirectory = isDirectory,
                       }));
    }
    closedir(dir);
}

static FileSystemDirEntry* n3ds_fsListDirectory(FileSystem* fs, const char* relativePath) {
    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    if (relativePath == NULL) return NULL;

    FileSystemDirEntry* entries = NULL;
    if (n3ds_fsPathHasScheme(relativePath)) {
        char* path = n3ds_fsNormalizePath(relativePath);
        n3ds_fsAppendDirectory(&entries, path);
        free(path);
    } else {
        char* savePath = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
        char* gamePath = n3ds_fsJoin(n3ds->gameBasePath, relativePath);
        n3ds_fsAppendDirectory(&entries, savePath);
        if (strcmp(savePath, gamePath) != 0) n3ds_fsAppendDirectory(&entries, gamePath);
        free(savePath);
        free(gamePath);
    }
    return entries;
}

static bool n3ds_fsReadFileBinary(
    FileSystem* fs, const char* relativePath, uint8_t** outData, int32_t* outSize) {
    if (outData == NULL || outSize == NULL) return false;
    *outData = NULL;
    *outSize = 0;

    char* path = n3ds_fsResolvePath(fs, relativePath);
    if (path == NULL) return false;

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        free(path);
        return false;
    }
    n3ds_fsSetFileBuffer(file);

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        free(path);
        return false;
    }
    long size = ftell(file);
    if (size < 0 || size > INT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        free(path);
        return false;
    }

    uint8_t* data = safeMalloc(size > 0 ? (size_t)size : 1u);
    size_t bytesRead = fread(data, 1, (size_t)size, file);
    bool readOk = bytesRead == (size_t)size;
    fclose(file);

    free(path);
    if (!readOk) {
        free(data);
        return false;
    }
    *outData = data;
    *outSize = (int32_t)size;
    return readOk;
}

static bool n3ds_fsWriteFileBinary(
    FileSystem* fs, const char* relativePath, const uint8_t* data, int32_t size) {
    if (data == NULL || size < 0) return false;

    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    char* path = n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (path == NULL) return false;
    if (!n3ds_fsMakeParentDirectories(path)) {
        free(path);
        return false;
    }

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        free(path);
        return false;
    }
    n3ds_fsSetFileBuffer(file);

    bool ok = fwrite(data, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file) != 0) ok = false;

    if (ok) {
        n3ds_fsSetCachedPath(n3ds, relativePath, path);
    }
    free(path);
    return ok;
}

static const char* n3ds_fsBinaryModeString(int32_t mode) {
    if (mode == GML_FILE_BIN_READ) return "rb";
    if (mode == GML_FILE_BIN_WRITE) return "wb+";
    return "rb+";
}

static void* n3ds_fsBinaryOpen(FileSystem* fs, const char* relativePath, int32_t mode) {
    N3DSFileSystem* n3ds = (N3DSFileSystem*)fs;
    char* path = mode == GML_FILE_BIN_READ
        ? n3ds_fsResolvePath(fs, relativePath)
        : n3ds_fsJoin(n3ds->saveBasePath, relativePath);
    if (path == NULL) return NULL;
    if (mode == GML_FILE_BIN_READWRITE && !n3ds_fsPathExists(path)) {
        char* legacy = n3ds_fsFindLegacySave(n3ds, relativePath);
        if (legacy != NULL) {
            bool copied = n3ds_fsCopyLegacySave(legacy, path);
            free(legacy);
            if (!copied) {
                free(path);
                return NULL;
            }
        }
    }
    if (mode != GML_FILE_BIN_READ && !n3ds_fsMakeParentDirectories(path)) {
        free(path);
        return NULL;
    }

    FILE* file = fopen(path, n3ds_fsBinaryModeString(mode));
    if (file == NULL && mode == GML_FILE_BIN_READWRITE) {
        file = fopen(path, "wb+");
    }
    if (file != NULL) {
        n3ds_fsSetFileBuffer(file);
        n3ds_fsSetCachedPath(n3ds, relativePath, path);
    }
    free(path);
    return file;
}

static void n3ds_fsBinaryClose(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle != NULL) fclose((FILE*)handle);
}

static int32_t n3ds_fsBinaryTell(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return 0;
    long pos = ftell((FILE*)handle);
    return pos < 0 ? 0 : (int32_t)pos;
}

static int32_t n3ds_fsBinarySize(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return 0;
    FILE* file = (FILE*)handle;
    long current = ftell(file);
    if (current < 0) current = 0;
    if (fseek(file, 0, SEEK_END) != 0) return 0;
    long size = ftell(file);
    fseek(file, current, SEEK_SET);
    return size < 0 ? 0 : (int32_t)size;
}

static bool n3ds_fsBinarySeek(MAYBE_UNUSED FileSystem* fs, void* handle, int32_t position) {
    if (handle == NULL) return false;
    if (position < 0) position = 0;
    return fseek((FILE*)handle, position, SEEK_SET) == 0;
}

static int32_t n3ds_fsBinaryRead(
    MAYBE_UNUSED FileSystem* fs, void* handle, void* outData, int32_t size) {
    if (handle == NULL || outData == NULL || size <= 0) return 0;
    return (int32_t)fread(outData, 1, (size_t)size, (FILE*)handle);
}

static int32_t n3ds_fsBinaryWrite(
    MAYBE_UNUSED FileSystem* fs, void* handle, const void* data, int32_t size) {
    if (handle == NULL || data == NULL || size <= 0) return 0;
    return (int32_t)fwrite(data, 1, (size_t)size, (FILE*)handle);
}

static void n3ds_fsBinaryRewrite(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return;
    FILE* file = (FILE*)handle;
    fflush(file);
    freopen(NULL, "wb+", file);
}

static FileSystemVtable N3DSFileSystem_vtable = {
    .resolvePath = n3ds_fsResolvePath,
    .fileExists = n3ds_fsFileExists,
    .directoryExists = n3ds_fsDirectoryExists,
    .readFileText = n3ds_fsReadFileText,
    .writeFileText = n3ds_fsWriteFileText,
    .deleteFile = n3ds_fsDeleteFile,
    .createDirectory = n3ds_fsCreateDirectory,
    .deleteDirectory = n3ds_fsDeleteDirectory,
    .listDirectory = n3ds_fsListDirectory,
    .readFileBinary = n3ds_fsReadFileBinary,
    .writeFileBinary = n3ds_fsWriteFileBinary,
    .binaryOpen = n3ds_fsBinaryOpen,
    .binaryClose = n3ds_fsBinaryClose,
    .binaryTell = n3ds_fsBinaryTell,
    .binarySize = n3ds_fsBinarySize,
    .binarySeek = n3ds_fsBinarySeek,
    .binaryRead = n3ds_fsBinaryRead,
    .binaryWrite = n3ds_fsBinaryWrite,
    .binaryRewrite = n3ds_fsBinaryRewrite,
};

N3DSFileSystem* n3ds_fsCreate(const char* gameBasePath, const char* saveBasePath) {
    N3DSFileSystem* fs = safeCalloc(1, sizeof(N3DSFileSystem));
    fs->base.vtable = &N3DSFileSystem_vtable;
    fs->gameBasePath = safeStrdup(gameBasePath != NULL ? gameBasePath : "sdmc:/3ds/cinnamon/");
    fs->saveBasePath = safeStrdup(saveBasePath != NULL ? saveBasePath : "sdmc:/3ds/cinnamon/");
    fs->resolvedPathCache = NULL;
    sh_new_strdup(fs->resolvedPathCache);
    return fs;
}

void n3ds_fsDestroy(N3DSFileSystem* fs) {
    if (fs == NULL) return;
    free(fs->gameBasePath);
    free(fs->saveBasePath);
    repeat(shlen(fs->resolvedPathCache), i) {
        free(fs->resolvedPathCache[i].value);
    }
    shfree(fs->resolvedPathCache);
    free(fs);
}
