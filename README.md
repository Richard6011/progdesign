# 音符碰撞 · C17 + Qt 薄接口

这是从 main 的 C17 + EasyX 版改出的 Qt 窗口版本。课设以能运行、便于解释为目标：保留现有 C 业务，不额外引入 QML、信号槽业务层、多线程或通用框架。

## 代码边界

- `src/qt_bridge.cpp` 是唯一的 C++ 应用文件，只用 QWidget、QImage、QPainter 转发窗口、绘图和输入，以 `extern "C"` 提供 `gfx_*` 接口。
- 页面布局、注册登录、账号切换、游戏判定、计分、音频与联机仍在 `.c` 中，按 C17 编译。不能表述为“全部纯 C”。
- 音乐继续使用 Windows MCI，网络继续使用 Winsock；换 Qt 图形层不改变联机协议。
- 不再编译或链接 EasyX。`third_party/easyx` 仅保留仓库原有文件，避免不必要删除。

答辩时沿着 `main.c -> graphics.h -> qt_bridge.cpp` 解释即可：C 决定画什么、做什么，Qt 负责画出来和收集事件。保留原有检查，不再增加复杂的鲁棒性设计。

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
- 命令面板“任务: 运行任务”中提供构建、测试、运行与 Release 打包入口。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Build
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Test -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Run -TemporaryData
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Package -Configuration Release
```

也可以用 `.local/cmake/bin/cmake.exe --preset c-debug` 与 `--build --preset c-debug`；Release 对应 `c-release`。系统 PATH 里有旧 CMake 时，请使用这些入口。

## 运行与玩法

完整解压 `out/NoteCollision-Qt-Windows-x64.zip`，运行 `NoteCollision.exe`。Qt DLL、`platforms`、`assets` 等目录必须一并保留，无需在运行电脑安装 Qt SDK。

首次注册本机账号，然后输入密码登录。支持账号切换、《小星星》《霓虹脉冲》各三档谱面、D/F/J/K 四轨单点/长按/连锁/同时按键、成绩和本地排行榜。单人 Esc 暂停、Enter 继续；未完成退出不保存本局。账号、设置、成绩默认仍在 `%APPDATA%/NoteCollisionC`。

界面使用浅灰配色，主页入口居中竖排。排行榜按曲目和难度比较不同账号的最佳单人成绩，同分并列；不显示联机榜。设置与操作说明页面已移除。

## 局域网双人

两台电脑使用相同提交构建的同一发行包，连接相同局域网。房主创建房间，客人填写房主的 IPv4 地址和端口；双人双方准备后由房主开始。跨电脑时不能填 `127.0.0.1`，它只指向本机。

本版虽用 Qt 画窗口，但仍使用 C 版协议 2，不能和旧 Qt 业务版本混联。账号仅在本机保存，不通过局域网交换密码。Windows 网络访问提示由使用者依据所在网络处理。

## 两台电脑交替开发

第一次换电脑：克隆 main、安装匹配工具、运行 ConfigureLocal.ps1，打开“程序设计”Profile。不要复制 build 或 .local。

每次开始前确认上一台电脑已提交并推送，且本机工作区干净：

```powershell
cd D:/progdesign
git status
git pull --ff-only origin main
# 开发后构建、测试
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Development.ps1 -Action Test
git diff
git add src tools .vscode CMakeLists.txt CMakePresets.json README.md docs .gitignore third_party/NOTICE.md
git commit -m "说明本次修改"
git push origin main
```

下一台电脑先 pull，再开发。若有未提交工作，先 commit，或用 `git stash push -u` 暂存，pull 后再 `git stash pop`；发生冲突先解决并测试，不能用强制推送覆盖另一台的工作。Qt 改造和界面精简随本次提交同步，第二台电脑拉取后需重新构建。

本机实际验证结果见 [本机部署验证](docs/本机部署验证.md)。`docs/验证记录.md` 和代码规模文档是上游 EasyX 版的历史记录。
