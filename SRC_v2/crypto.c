#define WIN32_LEAN_AND_MEAN /* 减少 Windows 头文件引入的内容。 */
#include <windows.h> /* 使用 UTF-8 字符转换与系统类型。 */
#include <bcrypt.h> /* 使用系统随机数和 SHA-256 接口。 */
#include <stdio.h> /* 使用受长度限制的文本格式化。 */
#include <string.h> /* 取得字符串字节数。 */
#include "game.h" /* 引入账号校验接口。 */

int text_valid(const char *text, int maximum) /* 检查 UTF-8 编码、字符数和控制字符。 */
{ /* 函数开始。 */
    wchar_t wide[258]; /* 保存转换后的 UTF-16 字符。 */
    int length; /* 保存含结束符的字符数量。 */
    int i; /* 遍历转换后的字符。 */
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, 258); /* 严格转换编码。 */
    if (length < 2 || length - 1 > maximum) /* 文本不能为空，也不能超过规定上限。 */
        return 0; /* 拒绝非法编码和不合适的字符数量。 */
    for (i = 0; i < length - 1; i++) /* 逐个检查字符。 */
    { /* 遍历开始。 */
        if (wide[i] < 32 || wide[i] == 127) /* 拒绝制表符、换行等控制字符。 */
            return 0; /* 避免破坏按行保存的数据格式。 */
    } /* 遍历结束。 */
    return 1; /* 文本满足要求。 */
} /* 函数结束。 */

int make_salt(char salt[33]) /* 为每次注册生成不同的随机盐。 */
{ /* 函数开始。 */
    unsigned char bytes[16]; /* 保存十六字节系统随机数。 */
    int i; /* 将每字节转换为两个十六进制字符。 */
    if (BCryptGenRandom(NULL, bytes, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) /* 使用系统随机源。 */
        return 0; /* 随机数生成失败时禁止保存账号。 */
    for (i = 0; i < 16; i++) /* 逐字节编码。 */
        sprintf(salt + i * 2, "%02x", bytes[i]); /* 输出两位小写十六进制文本。 */
    salt[32] = 0; /* 补上字符串结束符。 */
    return 1; /* 随机盐生成成功。 */
} /* 函数结束。 */

int password_hash(const char *salt, const char *password, char hash[65]) /* 计算盐和密码的 SHA-256。 */
{ /* 函数开始。 */
    BCRYPT_ALG_HANDLE algorithm = NULL; /* 保存系统算法句柄。 */
    unsigned char bytes[32]; /* SHA-256 的结果固定为三十二字节。 */
    char text[300]; /* 保存盐、冒号和原始密码，不写入文件。 */
    NTSTATUS status; /* 保存系统接口结果。 */
    int i; /* 编码摘要字节。 */
    int length; /* 保存需要参与摘要的文本长度。 */
    length = snprintf(text, sizeof(text), "%s:%s", salt, password); /* 组成与验证时一致的输入。 */
    if (length < 0 || length >= (int)sizeof(text)) /* 禁止使用被截断的输入。 */
        return 0; /* 输入超出缓冲区。 */
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0); /* 打开 SHA-256。 */
    if (status != 0) /* 系统无法提供算法时不继续。 */
        return 0; /* 告知调用方失败。 */
    status = BCryptHash(algorithm, NULL, 0, (PUCHAR)text, (ULONG)length, bytes, 32); /* 一次性计算摘要。 */
    BCryptCloseAlgorithmProvider(algorithm, 0); /* 释放系统算法句柄。 */
    SecureZeroMemory(text, sizeof(text)); /* 尽早清除临时密码组合文本。 */
    if (status != 0) /* 摘要计算失败时没有可用结果。 */
        return 0; /* 告知调用方失败。 */
    for (i = 0; i < 32; i++) /* 将摘要转成便于文本保存的格式。 */
        sprintf(hash + i * 2, "%02x", bytes[i]); /* 每字节转换成两位十六进制字符。 */
    hash[64] = 0; /* 保存摘要文本结束符。 */
    return 1; /* 摘要计算完成。 */
} /* 函数结束。 */
