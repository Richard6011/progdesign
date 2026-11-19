#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "nc.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char *nc_difficulties[3] = {"Easy", "Normal", "Hard"};
static void wide(const char *s, wchar_t *w, int n) {
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    for (wchar_t *p = w; *p; p++)
        if (*p == L'/')
            *p = L'\\';
}
bool nc_error(char *error, const char *fmt, ...) {
    if (error) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error, NC_ERROR, fmt, args);
        va_end(args);
    }
    return false;
}
void nc_copy(char *out, size_t cap, const char *s) {
    if (cap)
        snprintf(out, cap, "%s", s ? s : "");
}
int64_t nc_now(void) {
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return value.QuadPart / frequency.QuadPart * 1000 +
           value.QuadPart % frequency.QuadPart * 1000 / frequency.QuadPart;
}
void nc_uuid(char out[65]) {
    unsigned char bytes[16];
    if (BCryptGenRandom(NULL, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes, &(int64_t){nc_now()}, sizeof(int64_t));
        DWORD pid = GetCurrentProcessId();
        memcpy(bytes + 8, &pid, sizeof(pid));
    }
    for (int i = 0; i < 16; i++)
        sprintf(out + i * 2, "%02x", bytes[i]);
}
bool nc_read(const char *path, char **out, size_t *size, size_t limit, char *error) {
    wchar_t w[NC_PATH];
    wide(path, w, NC_PATH);
    FILE *f = _wfopen(w, L"rb");
    *out = NULL;
    if (!f)
        return nc_error(error, "无法读取文件：%s", path);
    int64_t len;
    if (_fseeki64(f, 0, SEEK_END) || (len = _ftelli64(f)) < 0 || (size_t)len > limit) {
        fclose(f);
        return nc_error(error, "文件过大或长度无效：%s", path);
    }
    rewind(f);
    char *p = malloc((size_t)len + 1);
    if (!p) {
        fclose(f);
        return nc_error(error, "内存不足");
    }
    size_t got = fread(p, 1, (size_t)len, f);
    bool ok = got == (size_t)len && !ferror(f);
    fclose(f);
    if (!ok) {
        free(p);
        return nc_error(error, "文件读取不完整：%s", path);
    }
    p[len] = 0;
    *out = p;
    if (size)
        *size = (size_t)len;
    return true;
}
bool nc_atomic(const char *path, const char *data, size_t size, char *error) {
    char tmp[NC_PATH], id[65];
    wchar_t wt[NC_PATH], wp[NC_PATH];
    nc_uuid(id);
    if (snprintf(tmp, sizeof(tmp), "%s.%s.tmp", path, id) >= (int)sizeof(tmp))
        return nc_error(error, "路径太长");
    wide(tmp, wt, NC_PATH);
    wide(path, wp, NC_PATH);
    HANDLE f = CreateFileW(wt, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return nc_error(error, "无法创建临时文件：%s", path);
    DWORD wrote = 0;
    bool ok = WriteFile(f, data, (DWORD)size, &wrote, NULL) && wrote == size && FlushFileBuffers(f);
    CloseHandle(f);
    if (ok)
        ok = MoveFileExW(wt, wp, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        DeleteFileW(wt);
        return nc_error(error, "保存失败，原文件已保留：%s", path);
    }
    return true;
}
bool nc_exists(const char *path) {
    wchar_t w[NC_PATH];
    wide(path, w, NC_PATH);
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}
bool nc_mkdir(const char *path, char *error) {
    wchar_t w[NC_PATH];
    wide(path, w, NC_PATH);
    int rc = SHCreateDirectoryExW(NULL, w, NULL);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS ||
           nc_error(error, "无法创建目录：%s", path);
}
void nc_exe_dir(char out[NC_PATH]) {
    wchar_t w[NC_PATH];
    GetModuleFileNameW(NULL, w, NC_PATH);
    wchar_t *p = wcsrchr(w, L'\\');
    if (p)
        *p = 0;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, NC_PATH, NULL, NULL);
}
void nc_data_dir(const char *exe, char out[NC_PATH]) {
    char root[NC_PATH];
    nc_copy(root, sizeof(root), exe);
    char *configuration = strrchr(root, '\\');
    if (configuration && (!_stricmp(configuration + 1, "debug") ||
                          !_stricmp(configuration + 1, "release"))) {
        *configuration = 0;
        char *build = strrchr(root, '\\');
        if (build && !_stricmp(build + 1, "build")) {
            *build = 0;
            char presets[NC_PATH];
            if (snprintf(presets, sizeof(presets), "%s/CMakePresets.json", root) <
                    (int)sizeof(presets) &&
                nc_exists(presets)) {
                nc_copy(out, NC_PATH, root);
                return;
            }
        }
    }
    wchar_t w[NC_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, w);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, root, NC_PATH, NULL, NULL);
    snprintf(out, NC_PATH, "%s/NoteCollisionC", root);
}
bool nc_hash(const void *data, size_t len, char out[65]) {
    unsigned char bytes[32];
    DWORD size = sizeof(bytes);
    if (len > UINT32_MAX || !CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, NULL, data,
                                                  (DWORD)len, bytes, &size))
        return false;
    for (int i = 0; i < 32; i++)
        sprintf(out + i * 2, "%02x", bytes[i]);
    return true;
}
bool nc_hash_file(const char *path, char out[65], char *error) {
    char *data;
    size_t size;
    if (!nc_read(path, &data, &size, UINT32_MAX, error))
        return false;
    bool ok = nc_hash(data, size, out);
    free(data);
    return ok || nc_error(error, "SHA-256 计算失败");
}
