#include "wii_file_system.h"
#include "../utils.h"
#include "stb_ds.h"
#include "../log.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fsMkdir(const char* path) {
    return mkdir(path, 0777);
}

char* WiiFS_readText(const char* path) {
    if (path == NULL || path[0] == '\0') return NULL;

    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char* data = safeMalloc((size_t)size + 1u);
    size_t readSize = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (readSize != (size_t)size) {
        free(data);
        return NULL;
    }
    data[readSize] = '\0';
    return data;
}

bool WiiFS_pathHasScheme(const char* path) {
    if (path == NULL) return false;
    const char* colon = strchr(path, ':');
    return colon != NULL && (colon[1] == '/' || colon[1] == '\\');
}

void WiiFS_normalizeSeparators(char* path) {
    if (path == NULL) return;
    for (char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') *cursor = '/';
    }
}

char* WiiFS_getParentDir(const char* path) {
    if (path == NULL || path[0] == '\0') return safeStrdup(".");

    char* parent = safeStrdup(path);
    WiiFS_normalizeSeparators(parent);
    char* separator = strrchr(parent, '/');
    if (separator != NULL && separator != parent) {
        *separator = '\0';
    } else {
        parent[0] = '.';
        parent[1] = '\0';
    }
    return parent;
}

bool WiiFS_pathIsFile(const char* path) {
    if (path == NULL || path[0] == '\0') return false;

    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool WiiFS_dirExists(const char* path) {
    if (path == NULL || path[0] == '\0') return false;

    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

char* WiiFS_joinPath(const char* base, const char* child) {
    if (base == NULL || base[0] == '\0') return safeStrdup(child != NULL ? child : "");
    if (child == NULL || child[0] == '\0') return safeStrdup(base);

    size_t baseLen = strlen(base);
    size_t childLen = strlen(child);
    bool needSlash = base[baseLen - 1] != '/' && base[baseLen - 1] != '\\';
    size_t pathSize = baseLen + childLen + (needSlash ? 1 : 0) + 1;
    char* path = safeMalloc(pathSize);
    snprintf(path, pathSize, "%s%s%s", base, needSlash ? "/" : "", child);
    WiiFS_normalizeSeparators(path);
    return path;
}

char* WiiFS_resolveSettingsPath(int argc, char** argv, const char* fallbackPath) {
    if (argc > 0 && argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
        char* launchDir = WiiFS_getParentDir(argv[0]);
        char* settingsPath = WiiFS_joinPath(launchDir, "cinnamon_settings.json");
        free(launchDir);
        return settingsPath;
    }
    return safeStrdup(fallbackPath != NULL ? fallbackPath : "cinnamon_settings.json");
}

static int fsCompareGamePaths(const void* left, const void* right) {
    const char* a = *(const char* const*)left;
    const char* b = *(const char* const*)right;
    size_t aLength = (size_t)(strrchr(a, '/') - a);
    size_t bLength = (size_t)(strrchr(b, '/') - b);
    int order = strncmp(a, b, aLength < bLength ? aLength : bLength);
    return order != 0 ? order : (aLength > bLength) - (aLength < bLength);
}

static char** fsFindDataWins(const char* root) {
    if (root == NULL || root[0] == '\0') return NULL;
    char** paths = NULL;
    char* candidate = WiiFS_joinPath(root, "data.win");
    if (WiiFS_pathIsFile(candidate)) {
        arrput(paths, candidate);
        return paths;
    }
    free(candidate);

    DIR* directory = opendir(root);
    if (directory == NULL) return NULL;

    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char* gameDir = WiiFS_joinPath(root, entry->d_name);
        if (!WiiFS_dirExists(gameDir)) {
            free(gameDir);
            continue;
        }

        candidate = WiiFS_joinPath(gameDir, "data.win");
        free(gameDir);
        if (WiiFS_pathIsFile(candidate)) {
            arrput(paths, candidate);
        } else {
            free(candidate);
        }
    }

    closedir(directory);
    if (arrlen(paths) > 1) qsort(paths, arrlen(paths), sizeof(*paths), fsCompareGamePaths);
    return paths;
}

char** WiiFS_resolveDataWins(
    int argc, char** argv, const char* configuredRoot, const char* defaultRoot) {
    char** paths = NULL;
    if (argc > 1 && argv != NULL && argv[1] != NULL && argv[1][0] != '\0') {
        char* explicitPath = safeStrdup(argv[1]);
        WiiFS_normalizeSeparators(explicitPath);
        if (!WiiFS_pathHasScheme(explicitPath) && explicitPath[0] != '/' && argc > 0 &&
            argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
            char* launchDir = WiiFS_getParentDir(argv[0]);
            char* resolvedPath = WiiFS_joinPath(launchDir, explicitPath);
            free(launchDir);
            free(explicitPath);
            arrput(paths, resolvedPath);
            return paths;
        }
        arrput(paths, explicitPath);
        return paths;
    }

    if (argc > 0 && argv != NULL && argv[0] != NULL && argv[0][0] != '\0') {
        char* launchDir = WiiFS_getParentDir(argv[0]);
        char* launchDataWin = WiiFS_joinPath(launchDir, "data.win");
        free(launchDir);
        if (WiiFS_pathIsFile(launchDataWin)) {
            arrput(paths, launchDataWin);
            return paths;
        }
        free(launchDataWin);
    }

    const char* root =
        configuredRoot != NULL && configuredRoot[0] != '\0' ? configuredRoot : defaultRoot;
    paths = fsFindDataWins(root);
    if (arrlen(paths) > 0) return paths;

    if (defaultRoot == NULL || strcmp(root, defaultRoot) == 0) {
        return NULL;
    }

    return fsFindDataWins(defaultRoot);
}

char* WiiFS_resolveGamePath(
    const char* currentDataWinPath, const char* workingDirectory, char** launchArgs) {
    const char* dataWinFilename = NULL;
    size_t argCount = arrlen(launchArgs);
    repeat(argCount, i) {
        if (strcmp(launchArgs[i], "-game") == 0 && i + 1 < argCount) {
            dataWinFilename = launchArgs[i + 1];
            break;
        }
    }

    if (dataWinFilename == NULL || dataWinFilename[0] == '\0') {
        logError("Wii: game_change launch parameters are missing '-game <file>'.\n");
        return NULL;
    }
    if (WiiFS_pathHasScheme(dataWinFilename)) {
        char* directPath = safeStrdup(dataWinFilename);
        WiiFS_normalizeSeparators(directPath);
        return directPath;
    }

    char* parentDir = WiiFS_getParentDir(currentDataWinPath);
    char* work = safeStrdup(workingDirectory != NULL ? workingDirectory : "");
    WiiFS_normalizeSeparators(work);
    const char* relativeWork = work;
    while (*relativeWork == '/')
        ++relativeWork;
    char* gameDir = WiiFS_pathHasScheme(work)
        ? safeStrdup(work)
        : WiiFS_joinPath(parentDir, relativeWork);
    const char* relativeFile = dataWinFilename;
    while (*relativeFile == '/' || *relativeFile == '\\')
        ++relativeFile;
    char* path = WiiFS_joinPath(gameDir, relativeFile);
    free(gameDir);
    free(work);
    free(parentDir);
    return path;
}

static char* fsNormalizePath(const char* path) {
    if (path == NULL) return NULL;
    char* normalized = safeStrdup(path);
    WiiFS_normalizeSeparators(normalized);
    return normalized;
}

static char* fsJoin(const char* basePath, const char* relativePath) {
    if (relativePath == NULL) return NULL;
    if (WiiFS_pathHasScheme(relativePath) || relativePath[0] == '/') {
        return fsNormalizePath(relativePath);
    }

    char* normalizedRelativePath = fsNormalizePath(relativePath);
    const char* relativeStart = normalizedRelativePath;
    while (*relativeStart == '/')
        ++relativeStart;

    char* fullPath = WiiFS_joinPath(basePath, relativeStart);
    free(normalizedRelativePath);
    return fullPath;
}

static char* fsBuildFullPath(WiiFileSystem* fs, const char* relativePath) {
    return fsJoin(fs->basePath, relativePath);
}

static char* fsGetFullPath(FileSystem* fs, const char* path) {
    if (WiiFS_pathHasScheme(path)) return fsNormalizePath(path);
    return fsBuildFullPath((WiiFileSystem*)fs, path);
}

static void fsEnsureParentDir(const char* fullPath) {
    if (fullPath == NULL) return;

    char buf[1024];
    size_t len = strlen(fullPath);
    if (len >= sizeof(buf)) return;
    memcpy(buf, fullPath, len + 1);

    char* lastSlash = strrchr(buf, '/');
    if (lastSlash == NULL || lastSlash == buf) return;
    *lastSlash = '\0';

    size_t start = 1u;
    char* scheme = strstr(buf, ":/");
    if (scheme != NULL) start = (size_t)(scheme - buf) + 2u;
    for (size_t i = start; buf[i] != '\0'; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            fsMkdir(buf);
            buf[i] = '/';
        }
    }
    fsMkdir(buf);
}

static char* fsResolvePath(FileSystem* fs, const char* relativePath) {
    return fsBuildFullPath((WiiFileSystem*)fs, relativePath);
}

static bool fsFileExists(FileSystem* fs, const char* relativePath) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    struct stat st;
    bool exists = fullPath != NULL && stat(fullPath, &st) == 0 && S_ISREG(st.st_mode);
    free(fullPath);
    return exists;
}

static bool fsDirectoryExists(FileSystem* fs, const char* relativePath) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    struct stat st;
    bool exists = fullPath != NULL && stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode);
    free(fullPath);
    return exists;
}

static char* fsReadFileText(FileSystem* fs, const char* relativePath) {
    char* path = fsGetFullPath(fs, relativePath);
    char* text = WiiFS_readText(path);
    free(path);
    return text;
}

static bool fsWriteFileText(
    FileSystem* fs, const char* relativePath, const char* contents) {
    return contents != NULL && fs->vtable->writeFileBinary(
        fs, relativePath, (const uint8_t*)contents, (int32_t)strlen(contents));
}

static bool fsDeleteFile(FileSystem* fs, const char* relativePath) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    int result = fullPath != NULL ? remove(fullPath) : -1;
    free(fullPath);
    return result == 0;
}

static bool fsRenameFile(
    FileSystem* fs, const char* oldRelativePath, const char* newRelativePath) {
    char* oldPath = fsGetFullPath(fs, oldRelativePath);
    char* newPath = fsGetFullPath(fs, newRelativePath);
    if (oldPath == NULL || newPath == NULL) {
        free(oldPath);
        free(newPath);
        return false;
    }
    fsEnsureParentDir(newPath);
    bool ok = rename(oldPath, newPath) == 0;
    free(oldPath);
    free(newPath);
    return ok;
}

static bool fsReadFileBinary(
    FileSystem* fs, const char* relativePath, uint8_t** outData, int32_t* outSize) {
    if (outData == NULL || outSize == NULL) return false;
    *outData = NULL;
    *outSize = 0;

    char* fullPath = fsGetFullPath(fs, relativePath);
    FILE* file = fullPath != NULL ? fopen(fullPath, "rb") : NULL;
    free(fullPath);
    if (file == NULL) return false;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0 || size > INT32_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    uint8_t* data = safeMalloc((size_t)size + (size == 0 ? 1 : 0));
    size_t bytesRead = fread(data, 1, (size_t)size, file);
    bool ok = bytesRead == (size_t)size;
    fclose(file);
    if (!ok) {
        free(data);
        return false;
    }

    *outData = data;
    *outSize = (int32_t)size;
    return true;
}

static bool fsWriteFileBinary(
    FileSystem* fs, const char* relativePath, const uint8_t* data, int32_t size) {
    if (data == NULL || size < 0) return false;
    char* fullPath = fsGetFullPath(fs, relativePath);
    fsEnsureParentDir(fullPath);
    FILE* file = fullPath != NULL ? fopen(fullPath, "wb") : NULL;
    free(fullPath);
    if (file == NULL) return false;
    bool ok = fwrite(data, 1, (size_t)size, file) == (size_t)size;
    if (fclose(file) != 0) ok = false;
    return ok;
}

typedef struct {
    FILE* file;
    char* fullPath;
} WiiBinaryHandle;

static void* fsBinaryOpen(FileSystem* fs, const char* relativePath, int32_t mode) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    if (fullPath == NULL) return NULL;

    const char* modeString =
        mode == GML_FILE_BIN_READ ? "rb" : (mode == GML_FILE_BIN_WRITE ? "wb+" : "rb+");
    if (mode != GML_FILE_BIN_READ) fsEnsureParentDir(fullPath);
    FILE* file = fopen(fullPath, modeString);
    if (file == NULL && mode == GML_FILE_BIN_READWRITE) file = fopen(fullPath, "wb+");
    if (file == NULL) {
        free(fullPath);
        return NULL;
    }

    WiiBinaryHandle* handle = safeCalloc(1, sizeof(WiiBinaryHandle));
    handle->file = file;
    handle->fullPath = fullPath;
    return handle;
}

static void fsBinaryClose(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file != NULL) fclose(h->file);
    free(h->fullPath);
    free(h);
}

static int32_t fsBinaryRead(
    MAYBE_UNUSED FileSystem* fs, void* handle, void* dst, int32_t size) {
    if (handle == NULL || dst == NULL || size <= 0) return 0;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file == NULL) return 0;
    return (int32_t)fread(dst, 1, (size_t)size, h->file);
}

static int32_t fsBinaryWrite(
    MAYBE_UNUSED FileSystem* fs, void* handle, const void* src, int32_t size) {
    if (handle == NULL || src == NULL || size <= 0) return 0;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file == NULL) return 0;
    return (int32_t)fwrite(src, 1, (size_t)size, h->file);
}

static int32_t fsBinaryTell(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return 0;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file == NULL) return 0;
    long pos = ftell(h->file);
    return pos < 0 ? 0 : (int32_t)pos;
}

static bool fsBinarySeek(MAYBE_UNUSED FileSystem* fs, void* handle, int32_t position) {
    if (handle == NULL) return false;
    if (position < 0) position = 0;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file == NULL) return false;
    return fseek(h->file, position, SEEK_SET) == 0;
}

static int32_t fsBinarySize(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return 0;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file == NULL) return 0;
    FILE* file = h->file;
    long current = ftell(file);
    if (current < 0) current = 0;
    if (fseek(file, 0, SEEK_END) != 0) return 0;
    long size = ftell(file);
    fseek(file, current, SEEK_SET);
    return size < 0 ? 0 : (int32_t)size;
}

static void fsBinaryRewrite(MAYBE_UNUSED FileSystem* fs, void* handle) {
    if (handle == NULL) return;
    WiiBinaryHandle* h = (WiiBinaryHandle*)handle;
    if (h->file != NULL) fclose(h->file);
    h->file = fopen(h->fullPath, "wb+");
}

static bool fsCreateDirectory(FileSystem* fs, const char* relativePath) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    fsEnsureParentDir(fullPath);
    int result = fullPath != NULL ? fsMkdir(fullPath) : -1;
    bool ok = result == 0 || (errno == EEXIST && WiiFS_dirExists(fullPath));
    free(fullPath);
    return ok;
}

static bool fsDeleteDirectory(FileSystem* fs, const char* relativePath) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    int result = fullPath != NULL ? rmdir(fullPath) : -1;
    free(fullPath);
    return result == 0;
}

static FileSystemDirEntry* fsListDirectory(FileSystem* fs, const char* relativePath) {
    char* fullPath = fsGetFullPath(fs, relativePath);
    DIR* dir = fullPath != NULL ? opendir(fullPath) : NULL;
    if (dir == NULL) {
        free(fullPath);
        return NULL;
    }

    FileSystemDirEntry* entries = NULL;
    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        bool isDirectory = false;
        char* childPath = fsJoin(fullPath, entry->d_name);
        struct stat st;
        if (childPath != NULL && stat(childPath, &st) == 0) isDirectory = S_ISDIR(st.st_mode);
        free(childPath);
        arrput(entries,
            ((FileSystemDirEntry){.name = safeStrdup(entry->d_name), .isDirectory = isDirectory}));
    }

    closedir(dir);
    free(fullPath);
    return entries;
}

static FileSystemVtable WiiFileSystemVtable = {
    .resolvePath = fsResolvePath,
    .fileExists = fsFileExists,
    .readFileText = fsReadFileText,
    .writeFileText = fsWriteFileText,
    .deleteFile = fsDeleteFile,
    .readFileBinary = fsReadFileBinary,
    .writeFileBinary = fsWriteFileBinary,
    .renameFile = fsRenameFile,
    .binaryOpen = fsBinaryOpen,
    .binaryClose = fsBinaryClose,
    .binaryRead = fsBinaryRead,
    .binaryWrite = fsBinaryWrite,
    .binaryTell = fsBinaryTell,
    .binarySeek = fsBinarySeek,
    .binarySize = fsBinarySize,
    .binaryRewrite = fsBinaryRewrite,
    .directoryExists = fsDirectoryExists,
    .createDirectory = fsCreateDirectory,
    .deleteDirectory = fsDeleteDirectory,
    .listDirectory = fsListDirectory,
};

WiiFileSystem* WiiFS_create(const char* dataWinPath) {
    WiiFileSystem* fs = safeCalloc(1, sizeof(WiiFileSystem));
    fs->base.vtable = &WiiFileSystemVtable;

    char* normalized = fsNormalizePath(dataWinPath);
    const char* lastSlash = normalized != NULL ? strrchr(normalized, '/') : NULL;
    if (lastSlash != NULL) {
        size_t dirLen = (size_t)(lastSlash - normalized + 1);
        fs->basePath = safeMalloc(dirLen + 1);
        memcpy(fs->basePath, normalized, dirLen);
        fs->basePath[dirLen] = '\0';
    } else {
        fs->basePath = safeStrdup("./");
    }
    free(normalized);
    return fs;
}

void WiiFS_destroy(WiiFileSystem* fs) {
    if (fs == NULL) return;
    free(fs->basePath);
    free(fs);
}
