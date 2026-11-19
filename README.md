# 音符碰撞 · C17 + Qt 薄接口

音符碰撞使用 C17 实现游戏逻辑，使用 Qt Widgets 提供窗口、绘图和输入。

## 代码边界

- `src/qt_bridge.cpp` 是唯一的 C++ 应用文件，只用 QWidget、QImage、QPainter 转发窗口、绘图和输入，以 `extern "C"` 提供 `gfx_*` 接口。
- 页面布局、注册登录、账号切换、游戏判定、计分、音频与联机仍在 `.c` 中，按 C17 编译。不能表述为“全部纯 C”。
- 音乐由 Windows MCI 播放，联机由 Winsock 处理。

答辩时可沿着 `main.c -> graphics.h -> qt_bridge.cpp` 讲解。C 负责游戏逻辑和绘制内容，Qt 负责渲染和收集事件。游戏按选曲和难度加载对应谱面。

## 开发环境

Windows x64；Qt 6.8.0 MinGW 64 位；GCC/G++ 13.1.0（MSVCRT、POSIX、SEH）；配套 GDB；CMake 3.31.10；Ninja。

首次配置本机工具（常用 D:/Qt 安装布局可以直接运行）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ConfigureLocal.ps1
```

其他安装位置可显式传入：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/ConfigureLocal.ps1 -MinGWRoot C:/Qt/Tools/mingw1310_64 -QtRoot C:/Qt/6.8.0/mingw_64 -CMakeRoot C:/Tools/cmake-3.31.10 -NinjaRoot C:/Qt/Tools/Ninja
```

脚本在 `.local` 建立四个本机目录链接。CMake 预设、脚本、VS Code 编译器、终端、GDB 均引用这些链接；Git 忽略 `.local`，不会把用户名、工具安装目录或临时账号带到另一台电脑。没有使用 CMakeUserPresets；该文件也已预先忽略。

在 VS Code 的“程序设计”Profile 中打开仓库根目录，安装 `ms-vscode.cpptools`、`ms-vscode.cmake-tools`、`twxs.cmake`，选择 `c-debug`。UTF-8、C17/C++17、`.clang-format` 和 CMake Tools 配置提供器已配置。

- Ctrl+Shift+B：默认 Debug 构建任务。
- F5：先构建，再启动 GDB；在 `src/main.c` 的 `main` 行按 F9 设置断点。
- F5 默认使用 `.local/debug-data`，与真实用户数据隔离。正常运行可用“运行.bat”。
- 命令面板“任务: 运行任务”中提供构建、运行与 Release 打包入口。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Build
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Run
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Package -Configuration Release
```

也可以用 `.local/cmake/bin/cmake.exe --preset c-debug` 与 `--build --preset c-debug`；Release 对应 `c-release`。系统 PATH 里有旧 CMake 时，请使用这些入口。

## 运行与玩法

完整解压 `out/NoteCollision-Qt-Windows-x64.zip`，运行 `NoteCollision.exe`。Qt DLL、`platforms`、`assets` 等目录必须一并保留，无需在运行电脑安装 Qt SDK。

首次注册本机账号，然后输入密码登录。支持账号切换、《小星星》《霓虹脉冲》各三档谱面、D/F/J/K 四轨单点/长按/连锁/同时按键、成绩和本地排行榜。单人 Esc 暂停、Enter 继续；未完成退出不保存本局。

运行工程的 `build/debug` 或 `build/release` 版本时，`accounts.json`、`records.json` 和可选的 `settings.json` 默认位于工程根目录。独立发行包默认使用 `%APPDATA%/NoteCollisionC`。`settings.json` 只在启动时读取，缺失时使用默认设置。可以用 `--data-dir` 指定其他数据目录。

界面使用浅灰配色，主页入口居中竖排。排行榜按曲目和难度比较不同账号的最佳单人成绩，同分并列；不显示联机榜。设置与操作说明页面已移除。

## 局域网双人

两台电脑使用同一新版发行包（联机协议 5），并连到同一局域网。房主创建房间，客人填写房主的局域网 IPv4 地址和端口后加入。默认端口为 `9527`。连接后双方能看到对方的昵称。

房主选择曲目和难度，客人自动加载该选择。选曲由房主控制。客人加载失败时会看到错误提示，无法开始。双方各点自己的“开始”，各自播放音乐和处理按键。任一方开始后，房主的选曲会锁定。

演奏中会显示对方的累计分。自己完赛后可在结果页继续看对方的比分，双方完赛后显示各自的最终分并保存本机成绩。可以随时退出，断线时会有提示。下一局需要重新建房和加入。

单机开两个窗口时，客人可填 `127.0.0.1`，两个实例分别用 `--data-dir` 指定数据目录。已有账号、设置和成绩文件可继续使用。
