# 小星星 / Twinkle Twinkle Little Star

旋律采用 18 世纪流传至今的公版曲调；本项目重新合成了 WAV 伴奏和演奏音色，没有使用第三方录音、采样或受限音频文件。录音和三份谱面均可由 `tools/generate_twinkle.py` 确定性重建。

- 时长：约 33.8 秒，100 BPM，4/4 拍。
- 格式：44.1kHz、16-bit、立体声 PCM WAV。
- 前奏约 3 秒，随后按 C C G G A A G / F F E E D D C 的六句旋律演奏。
- Easy / Normal / Hard 使用 2400 / 2000 / 1700ms 接近时间；难度通过额外节拍、双押/三押和谱面密度区分。
- 三档均包含单点、长按、连锁和同时按键；长按尾部和最后一个目标后保留判定余量。
- 判定单元数量依次为 43 / 57 / 66。Easy 以主旋律为主，Normal 加入少量半拍伴奏，Hard 增加半拍和三押。

在仓库根目录运行：

```powershell
python tools/generate_twinkle.py
```
