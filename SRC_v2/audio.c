#define WIN32_LEAN_AND_MEAN /* 减少 Windows 头文件内容。 */
#include <windows.h> /* 使用系统音频接口所需类型。 */
#include <mmsystem.h> /* 使用 Windows 自带的 MCI 播放 WAV。 */
#include <stdio.h> /* 使用错误文本格式化。 */
#include <stdlib.h> /* 使用数值转换函数。 */
#include <wchar.h> /* 使用宽字符文本函数。 */
#include "game.h" /* 引入音乐播放接口声明。 */

static int opened = 0; /* 记录当前是否打开了音频设备。 */

static int music_command(const wchar_t *command, char *error) /* 执行一条 MCI 命令。 */
{ /* 函数开始。 */
    MCIERROR code = mciSendStringW(command, NULL, 0, NULL); /* 使用系统音频播放器。 */
    wchar_t message[200]; /* 保存系统提供的宽字符错误。 */
    if (code == 0) /* 系统命令已经接受。 */
        return 1; /* 返回成功。 */
    mciGetErrorStringW(code, message, 200); /* 取得具体错误文字。 */
    WideCharToMultiByte(CP_UTF8, 0, message, -1, error, MESSAGE_SIZE, NULL, NULL); /* 转为统一 UTF-8。 */
    return 0; /* 返回播放接口失败。 */
} /* 函数结束。 */

void music_close(void) /* 离开本局或重新选歌时释放当前音频。 */
{ /* 函数开始。 */
    if (opened) /* 只关闭本程序自己打开的别名。 */
        mciSendStringW(L"close basic_song", NULL, 0, NULL); /* 同时停止播放并释放设备。 */
    opened = 0; /* 清除已打开标记。 */
} /* 函数结束。 */

int music_open(int song, int volume, char *error) /* 按固定路径打开歌曲。 */
{ /* 函数开始。 */
    wchar_t path[200]; /* 保存音频宽字符相对路径。 */
    wchar_t command[300]; /* 保存将要发送的 MCI 命令。 */
    music_close(); /* 先释放上一首歌。 */
    if (song < 0 || song > 1 || volume < 0 || volume > 100) /* 校验所有输入范围。 */
    { /* 输入错误分支开始。 */
        snprintf(error, MESSAGE_SIZE, "歌曲或音量设置无效。"); /* 输出明确提示。 */
        return 0; /* 不调用设备。 */
    } /* 输入错误分支结束。 */
    MultiByteToWideChar(CP_UTF8, 0, audio_path(song), -1, path, 200); /* 转换固定资源路径。 */
    swprintf(command, 300, L"open \"%ls\" type mpegvideo alias basic_song", path); /* 使用支持音量设置的系统媒体驱动播放 WAV。 */
    if (!music_command(command, error)) /* 音频文件缺失或无法播放。 */
        return 0; /* 不允许继续开局。 */
    opened = 1; /* 从此处开始需要负责关闭设备。 */
    if (!music_command(L"set basic_song time format milliseconds", error)) /* 统一计时单位为毫秒。 */
    { /* 配置失败分支开始。 */
        music_close(); /* 释放已经打开的设备。 */
        return 0; /* 返回失败。 */
    } /* 配置失败分支结束。 */
    swprintf(command, 300, L"setaudio basic_song volume to %d", volume * 10); /* 转成 MCI 的千分制音量。 */
    if (!music_command(command, error)) /* 音量设置失败时不忽略错误。 */
    { /* 音量失败分支开始。 */
        music_close(); /* 清理设备。 */
        return 0; /* 返回失败。 */
    } /* 音量失败分支结束。 */
    return 1; /* 预加载完成。 */
} /* 函数结束。 */

int music_play(char *error) /* 新的一局从音乐零位置开始。 */
{ /* 函数开始。 */
    return music_command(L"play basic_song from 0", error); /* 发送异步播放命令。 */
} /* 函数结束。 */

int music_pause(char *error) /* 同时暂停音乐与游戏判定。 */
{ /* 函数开始。 */
    return music_command(L"pause basic_song", error); /* 保留当前播放位置。 */
} /* 函数结束。 */

int music_resume(char *error) /* 在活动长按重新按住后恢复音乐。 */
{ /* 函数开始。 */
    return music_command(L"resume basic_song", error); /* 从暂停处继续播放。 */
} /* 函数结束。 */

int music_position(void) /* 以实际音频位置作为游戏时间。 */
{ /* 函数开始。 */
    wchar_t position[32]; /* 保存系统返回的位置文本。 */
    if (!opened || mciSendStringW(L"status basic_song position", position, 32, NULL) != 0) /* 查询实际位置。 */
        return -1; /* 读取失败，禁止用错误时间推进判定。 */
    return (int)wcstol(position, NULL, 10); /* 将毫秒文本转成整数。 */
} /* 函数结束。 */

int music_stopped(void) /* 区分正常播放与设备意外停止。 */
{ /* 函数开始。 */
    wchar_t mode[32]; /* 保存当前播放状态。 */
    if (!opened || mciSendStringW(L"status basic_song mode", mode, 32, NULL) != 0) /* 查询当前状态。 */
        return 1; /* 无有效设备时视为已停止。 */
    return wcscmp(mode, L"stopped") == 0; /* 只接受明确的停止状态。 */
} /* 函数结束。 */
