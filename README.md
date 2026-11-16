# 音符碰撞

C++17 / Qt 6.8.3 四轨节奏游戏，包含单人和局域网双人模式。

## 开发环境

使用 VS Code 的 **程序设计** Profile。工具路径沿用已有配置：Qt 位于 `D:/Qt/6.8.3/mingw_64`，MinGW 位于 `D:/Qt/Tools/mingw1310_64`，CMake 3.31.10 和 Ninja 使用已安装版本。

当前工作入口为 `C:/Users/Administrator/Desktop/progdesign/code`。由于当前桌面应用仍占用原工作目录，`progdesign` 暂为指向原目录的 Windows 目录联接，两种路径访问同一份文件。关闭占用原目录的应用后，可从桌面的另一个 PowerShell 窗口执行 `tools/Rename-Workspace.ps1` 完成物理改名；脚本验证路径并在失败时恢复联接，不删除项目内容。

```powershell
pwsh -NoProfile -File .\tools\Development.ps1 -Action Open
pwsh -NoProfile -File .\tools\Development.ps1 -Action Build
pwsh -NoProfile -File .\tools\Development.ps1 -Action Test
pwsh -NoProfile -File .\tools\Development.ps1 -Action Run
pwsh -NoProfile -File .\tools\Development.ps1 -Action Package
```

也可在该 Profile 中打开项目，使用 `Ctrl+Shift+B` 构建、`F5` 调试。构建目录必须使用英文路径；本机 Qt moc 对中文输出路径不能正确处理。脚本从英文工作入口配置 CMake Presets，构建产物保存在 `build/qt-debug` 或 `build/qt-release`，可携带文件输出到 `out/NoteCollision`。

## 使用

选择曲目和 Easy / Normal / Hard，按 D、F、J、K 对应四条轨道。单点一次击打，长按保持至尾部，连锁节点与同时按键分别判定。单人按 Esc 暂停，继续时重新按住活动长按；重开和返回选曲不保存未完成局。

双人模式在同一局域网的两台电脑运行相同程序及资源。一端创建房间，另一端输入房主显示的 IPv4 和端口。双方资源预加载并校验一致后准备，由房主开始。联机不暂停，失焦、退出或断线中止未确认局。最终同分为平局，双方结果确认后保存各自成绩。

成绩与设置保存到 Qt 的用户数据目录（Windows 为 `%APPDATA%/NoteCollision/音符碰撞`），通过 QSaveFile 原子写入；不同模式、谱面、难度和规则分榜，同分并列。损坏或未知版本成绩文件保留备份并阻止覆盖。

演示曲《霓虹脉冲》为本项目原创合成的 36 秒 WAV，三个难度分别包含 68、84、136 个判定单元。资源及再生成说明见 [assets/songs/neon-pulse/README.md](assets/songs/neon-pulse/README.md)。

## 开发资料

- 需求依据：[音符碰撞总体开发文档](docs/音符碰撞总体开发文档.md)。该文件由另一个 agent 维护，本次实现不覆盖其编辑。
- 实际进度与验证：[实现与验证记录](docs/实现与验证记录.md)。

当前自动化测试包含确定性判定、真实文件存储、网络协议、原始 socket 异常输入以及真实媒体的单人和本机双客户端流程。两台实体电脑的声画同步和真实键盘双押/三押仍需设备实测。
