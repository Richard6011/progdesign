#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "nc.h"
#include <mmsystem.h>
#include <stdio.h>
static bool opened;
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
void audio_close(void) {
    if (opened)
        mciSendStringW(L"close nc_music", NULL, 0, NULL);
    opened = false;
}
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
    swprintf(cmd, NC_PATH + 64, L"setaudio nc_music volume to %d", volume * 10);
    mciSendStringW(cmd, NULL, 0, NULL);
    return true;
}
bool audio_play(char *e) { return opened && command(L"play nc_music from 0", e); }
bool audio_pause(char *e) { return opened && command(L"pause nc_music", e); }
bool audio_resume(char *e) { return opened && command(L"resume nc_music", e); }
int audio_position(void) {
    wchar_t value[64];
    if (!opened || mciSendStringW(L"status nc_music position", value, 64, NULL))
        return -1;
    return _wtoi(value);
}
