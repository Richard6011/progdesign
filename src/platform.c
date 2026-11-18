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
/**
 * @brief 把 UTF-8 文件路径转为 Windows 宽字符路径。
 * 参数：s 为有效且能装入目标的 UTF-8 字符串；w 为输出数组；n 为 wchar_t 元素容量。
 * 返回：无返回值；调用方需保证转换成功所需的输入有效性和容量。
 * 说明：转换包含结尾零，随后把 / 统一为反斜杠，以满足 SHCreateDirectoryEx
 * 等路径接口。本函数不返回转换错误，也不创建文件。
 */
static void wide(const char *s, wchar_t *w, int n) {
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n);
    for (wchar_t *p = w; *p; p++)
        if (*p == L'/')
            *p = L'\\';
}
/**
 * @brief 格式化一条错误消息，并统一返回失败。
 * 参数：error 可为 NULL；非空时至少 NC_ERROR 字节；fmt 与其后变参遵循 printf 格式。
 * 返回：始终返回 false，便于在条件分支中直接 return nc_error(...)。
 * 说明：使用 vsnprintf 限制输出长度；error 为空时只返回 false，不输出到控制台，也不弹窗。
 */
bool nc_error(char *error, const char *fmt, ...) {
    if (error) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error, NC_ERROR, fmt, args);
        va_end(args);
    }
    return false;
}
/**
 * @brief 将可空字符串以零结尾形式复制到定长缓冲区。
 * 参数：out 为容量 cap 的输出数组；s 为来源，允许为 NULL；来源不能与目标发生不允许的重叠。
 * 返回：无返回值；cap 大于零时按容量保留结尾零，超长文本被截断。
 * 说明：NULL 来源按空字符串处理，cap 为零不写内存。该工具不报告截断，要求完整字段的模块应先检查长度。
 */
void nc_copy(char *out, size_t cap, const char *s) {
    if (cap)
        snprintf(out, cap, "%s", s ? s : "");
}
/**
 * @brief 获取用于计划开局和超时的单调毫秒时钟。
 * 参数：无参数。
 * 返回：返回 QueryPerformanceCounter 换算的 int64_t 毫秒值；该值不是日期或 Unix 时间。
 * 说明：用商和余数拆开换算，避免直接乘 1000 的中间值过大。不同电脑的原点无共同含义，联机必须通过 offset
 * 映射，记录日期另用 GetSystemTime。
 */
int64_t nc_now(void) {
    LARGE_INTEGER value, frequency;
    QueryPerformanceCounter(&value);
    QueryPerformanceFrequency(&frequency);
    return value.QuadPart / frequency.QuadPart * 1000 +
           value.QuadPart % frequency.QuadPart * 1000 / frequency.QuadPart;
}
/**
 * @brief 生成会话、对局及临时文件使用的标识文本。
 * 参数：out 至少容纳 65 字节，本实现写入 32 位十六进制字符及结尾零。
 * 返回：无返回值；输出由调用方持有。
 * 说明：优先使用系统 BCrypt 随机字节；失败时使用时钟和进程 ID 回退，该回退不能保证多次调用唯一，也不是标准带连字符
 * UUID，不应视为认证凭证。
 */
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
/**
 * @brief 限长读取整个文件，并额外补上文本结尾零。
 * 参数：path 为 UTF-8 路径；out 为非空 char** 输出；size 可空；limit
 * 为允许字节上限；error 可空。
 * 返回：成功返回 true，*out 为 malloc 内存；失败返回 false，*out 为 NULL。
 * 说明：以二进制模式读取并校验长度、分配和完整读取；size 不含补的零。调用者必须
 * free(*out)，文件句柄由函数在所有已打开路径关闭。
 */
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
/**
 * @brief 用同目录临时文件提交一份新文件内容。
 * 参数：path 为目标路径；data 为至少 size 字节数据；本项目 size 应不超过 DWORD 范围；error
 * 可空。
 * 返回：完整写入、刷新并替换成功返回 true，否则返回 false。
 * 说明：创建带标识的独占临时文件，FlushFileBuffers 后用 MoveFileExW
 * 替换目标；失败尽力删除临时文件并保留原目标。父目录必须已存在，不在这里创建。
 */
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
/**
 * @brief 检查 Windows 能否取得路径的文件属性。
 * 参数：path 为有效且不超 NC_PATH 的 UTF-8 路径。
 * 返回：GetFileAttributesW 成功返回 true，否则返回 false。
 * 说明：true 既可能是文件也可能是目录；false 可能源于不存在、权限或其他系统错误，不单独区分原因。
 */
bool nc_exists(const char *path) {
    wchar_t w[NC_PATH];
    wide(path, w, NC_PATH);
    return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}
/**
 * @brief 创建目标目录及需要的父目录。
 * 参数：path 为本项目使用的绝对 UTF-8 路径；error 可空。
 * 返回：创建成功或系统报告路径已存在时返回 true，否则返回 false。
 * 说明：统一宽字符路径后调用 SHCreateDirectoryExW；不删除或覆盖现有内容。已存在的返回码在此按可继续处理，
 * 后续文件操作仍会检查实际可写性。
 */
bool nc_mkdir(const char *path, char *error) {
    wchar_t w[NC_PATH];
    wide(path, w, NC_PATH);
    int rc = SHCreateDirectoryExW(NULL, w, NULL);
    return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS ||
           nc_error(error, "无法创建目录：%s", path);
}
/**
 * @brief 取得当前可执行文件所在目录。
 * 参数：out 至少 NC_PATH 字节，运行环境路径需能装入相应宽字符缓冲区。
 * 返回：无返回值，输出 UTF-8 目录路径，不含最后的可执行文件名。
 * 说明：使用 GetModuleFileNameW 而非当前工作目录，因此双击和调试器运行都可以从程序旁找到
 * assets；本接口没有单独的系统错误返回通道。
 */
void nc_exe_dir(char out[NC_PATH]) {
    wchar_t w[NC_PATH];
    GetModuleFileNameW(NULL, w, NC_PATH);
    wchar_t *p = wcsrchr(w, L'\\');
    if (p)
        *p = 0;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, NC_PATH, NULL, NULL);
}
/**
 * @brief 构造当前用户的独立 C 版数据目录。
 * 参数：out 至少 NC_PATH 字节。
 * 返回：无返回值，正常情况下输出 AppData/Roaming 下的 NoteCollisionC 路径。
 * 说明：先查询 CSIDL_APPDATA 再转换为 UTF-8。仅构造路径，不创建目录，不读取或迁移 Qt
 * 版数据；写入模块负责在需要时建目录。
 */
void nc_data_dir(char out[NC_PATH]) {
    wchar_t w[NC_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, w);
    char root[NC_PATH];
    WideCharToMultiByte(CP_UTF8, 0, w, -1, root, NC_PATH, NULL, NULL);
    snprintf(out, NC_PATH, "%s/NoteCollisionC", root);
}
/**
 * @brief 计算一段内存的 SHA-256 文本摘要。
 * 参数：data 为可读内存，len 为字节数且不超过 UINT32_MAX；out 至少 65 字节。
 * 返回：成功返回 true 并填写摘要；失败返回 false，输出不能继续使用。
 * 说明：使用 Windows 提供的一次性 SHA-256 接口，输出格式与既有账号和谱面摘要相同。
 */
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
/**
 * @brief 计算资源文件的 SHA-256。
 * 参数：path 为 UTF-8 文件路径；out 至少 65 字节；error 可空。
 * 返回：成功返回 true；打开、读取或哈希失败返回 false 并写原因。
 * 说明：读入原始字节后与内存摘要共用 nc_hash。谱面与音频计算完即释放临时缓冲，文件格式无需改变。
 */
bool nc_hash_file(const char *path, char out[65], char *error) {
    char *data;
    size_t size;
    if (!nc_read(path, &data, &size, UINT32_MAX, error))
        return false;
    bool ok = nc_hash(data, size, out);
    free(data);
    return ok || nc_error(error, "SHA-256 计算失败");
}
