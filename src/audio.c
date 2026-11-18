#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "nc.h"
#include <mmsystem.h>
#include <stdio.h>
static bool opened;
/**
 * @brief 向 Windows MCI 发送一条宽字符音频命令。
 * 参数：text 为完整 UTF-16 MCI 命令；error 可为空，否则至少提供 NC_ERROR 字节。
 * 返回：MCI 返回成功时为 true；出错为 false，并将系统错误文字转成 UTF-8。
 * 说明：不改变 opened 标志，由具体调用函数负责；该模块在主线程串行调用，共享别名
 * nc_music，不能同时播放两首背景音乐。
 */
static bool command(const wchar_t *text, char *error) {
    MCIERROR rc = mciSendStringW(text, NULL, 0, NULL);
    if (!rc)
        return true;
    wchar_t message[256];
    char utf[NC_ERROR];
    mciGetErrorStringW(rc, message, 256);
    WideCharToMultiByte(CP_UTF8, 0, message, -1, utf, sizeof(utf), NULL, NULL);
    return nc_error(error, "音频错误：%s", utf);
}
/**
 * @brief 关闭当前音乐设备并清除已打开标志。
 * 参数：无参数；作用于模块内部的 nc_music 别名。
 * 返回：无返回值。
 * 说明：仅在 opened 为真时发 close；随后始终清标志。关闭错误被忽略，适合重开或退出时重复清理。
 */
void audio_close(void) {
    if (opened)
        mciSendStringW(L"close nc_music", NULL, 0, NULL);
    opened = false;
}
/**
 * @brief 预加载歌曲，为随后的计划起播准备音频设备。
 * 参数：path 为 UTF-8 路径；volume 为调用者保证的 0—100 音量；duration
 * 为谱面毫秒时长；error 可为空。
 * 返回：设备打开且报告时长在谱面时长至谱面时长加 100ms 之间时返回 true，否则返回 false。
 * 说明：先关闭旧设备，用 mpegvideo MCI 打开、设置毫秒格式并核对时长，再设置音量，不立即播放。设置时间格式或时长
 * 检查失败时关闭本次设备；音频至少覆盖最后一个音符可能的结算时刻。音量命令本身为尽力设置，不决定本函数返回值。
 */
bool audio_open(const char *path, int volume, int duration, char *error) {
    audio_close();
    wchar_t w[NC_PATH], cmd[NC_PATH + 64];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, w, NC_PATH);
    swprintf(cmd, NC_PATH + 64, L"open \"%ls\" type mpegvideo alias nc_music", w);
    if (!command(cmd, error))
        return false;
    opened = true;
    if (!command(L"set nc_music time format milliseconds", error)) {
        audio_close();
        return false;
    }
    wchar_t value[64];
    int length = mciSendStringW(L"status nc_music length", value, 64, NULL) ? -1 : _wtoi(value);
    if (length < duration || length > duration + 100) {
        audio_close();
        return nc_error(error, "音频实际长度与谱面不一致");
    }
    /* MCI 使用 0—1000 的音量范围；命令失败时仍可播放。 */
    swprintf(cmd, NC_PATH + 64, L"setaudio nc_music volume to %d", volume * 10);
    mciSendStringW(cmd, NULL, 0, NULL);
    return true;
}
/**
 * @brief 从音乐零毫秒处开始播放。
 * 参数：e 是可空错误缓冲区，含义与 command 的 error 相同。
 * 返回：设备已打开且 play 命令成功返回 true，否则返回 false。
 * 说明：用于新局起播；若尚未打开则短路返回 false，不额外填错误文字，已有设备则从头播放而非从暂停处继续。
 */
bool audio_play(char *e) { return opened && command(L"play nc_music from 0", e); }
/**
 * @brief 暂停当前音乐，保留播放位置。
 * 参数：e 为可空错误缓冲区。
 * 返回：设备已打开且暂停命令成功返回 true，否则返回 false。
 * 说明：只暂停音频，不冻结引擎；应用层需同时调用 engine_pause，防止显示时间与判定状态不一致。
 */
bool audio_pause(char *e) { return opened && command(L"pause nc_music", e); }
/**
 * @brief 继续已暂停的音乐设备。
 * 参数：e 为可空错误缓冲区。
 * 返回：设备已打开且 resume 命令成功返回 true，否则返回 false。
 * 说明：继续原有位置，不从零开始；应在 engine_resume 已确认长按恢复条件之后调用。
 */
bool audio_resume(char *e) { return opened && command(L"resume nc_music", e); }
/**
 * @brief 读取 MCI 报告的当前音乐位置。
 * 参数：无参数，查询模块内部设备。
 * 返回：成功返回非负毫秒位置；设备未打开或查询失败返回 -1。
 * 说明：游戏以该位置为时间源而非靠逐帧累加。设备位置可能短暂不变，应用层负责防止时间倒退及检测播放停滞。
 */
int audio_position(void) {
    wchar_t value[64];
    if (!opened || mciSendStringW(L"status nc_music position", value, 64, NULL))
        return -1;
    return _wtoi(value);
}
