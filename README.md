# 音符碰撞 · C17 + EasyX

原 Qt 项目的独立重写版，文件全部位于 `C:/Users/Administrator/Desktop/predesign_2`。原项目没有改动。

## 代码规模与函数说明

应用源码及头文件注释前为 3090 行，补充注释后为 4446 行；加上测试，C/C++ 共 4949 行。排除空白和纯注释后，应用有效代码为 3077 行，加测试为 3486 行。自编的 143 个应用函数、14 个测试函数和 17 个 Python 生成器函数均有中文详细说明，头文件接口也有注释。完整分文件统计和函数索引见 [代码规模与函数注释](docs/代码规模与函数注释.md)。

## 直接运行

完整解压 `out/NoteCollision-C-Windows-x64.zip`，双击其中的 `NoteCollision.exe`。保留同级 `assets` 文件夹。

首次进入点击“没有账号，注册一个”，创建本机账号，再输入密码登录。默认不创建通用密码或演示账号。两台电脑可以使用不同账号。

## VS Code 编译与调试

沿用你的 **程序设计** Profile 的工具链：

- GCC / G++ / GDB：`D:/Qt/Tools/mingw1310_64/bin`（GCC 13.1，x64，MSVCRT）。
- CMake：`C:/Users/Administrator/AppData/Local/Programs/CMake/cmake-3.31.10-windows-x86_64/bin/cmake.exe`。
- Ninja：`D:/Qt/Tools/Ninja/ninja.exe`。

在“程序设计”Profile 打开本目录，选择 `c-debug` preset。`Ctrl+Shift+B` 构建，`F5` 调试。项目级配置已经覆盖原 Profile 的 Qt kit，不需要修改你的全局 Profile。也可以双击根目录的“构建.bat”“运行.bat”“测试.bat”。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/Development.ps1 -Action Build
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/Development.ps1 -Action Test
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/Development.ps1 -Action Run
powershell -NoProfile -ExecutionPolicy Bypass -File ./tools/Development.ps1 -Action Package -Configuration Release
```

## C 与 EasyX 的边界

游戏判定、计分、界面布局与交互、账号、文件存储、音频、网络全部由 `.c` 文件按 C17 编译。EasyX 官方头文件含 `#error EasyX is only for C++`，因此保留 `src/easyx_bridge.cpp` 一个薄接口文件：只转发绘图、窗口事件和 UTF-8 文本，不包含游戏规则或网络逻辑。它不能称为“所有源文件均为纯 C”。

不使用 Qt，不需要 Qt DLL。EasyX 26.10.25 的头文件与匹配的官方 MinGW MSVCRT x64 库已随源码放在 `third_party/easyx`，无需额外安装 EasyX。JSON 使用 MIT 许可的 cJSON 1.7.19。发行程序静态链接编译器运行库，只依赖 Windows 系统 DLL。

## 功能与操作

- 登录 / 注册、本机账号切换。
- 《小星星》《霓虹脉冲》，各有 Easy / Normal / Hard 三档谱面。
- D / F / J / K 四轨，单点、长按、连锁与同时按键。
- 判定窗口 ±90 / ±150 / ±220 ms；分数、准确率、连击、S/A/B/C/D、Full Combo、All Perfect。
- 音乐位置作为游戏时间。单人 Esc 暂停，Enter 继续；活动长按需重新按住；重开与退出不保存未完成局。
- 音乐音量与 -200 至 +200 ms 输入校准保存到本机。
- 单人 / 局域网排行榜按谱面、难度、规则分榜，分页显示，同分并列。
- 局域网双人：创建或输入 IPv4 / 端口加入、资源 SHA-256 校验、准备、时钟同步、倒计时、对手比分、最终成绩确认、同分平局、断线中止。
- 联机结算后由房主发起“再来一局”，保留 TCP 连接并重新准备；单人直接复用启动流程重开。

## 局域网

两台电脑运行相同发行包。房主在“局域网双人”中创建房间，将大厅显示的 IPv4 与端口告知客人。客人输入房主 IPv4（不能填自己电脑的 `127.0.0.1`），点击加入。房主选择曲目和难度，双方资源加载并校验后各自准备，由房主开始。Windows 若询问网络访问，请由使用者按所在网络自行处理。

C 版使用协议版本 2，不与旧 Qt 版混联。音乐和谱面直接计算文件 SHA-256，因此联机双方使用完全相同文件。联网不交换账号密码，账号仅在各自本机保存。

## 数据与资源

账号、设置、成绩在 `%APPDATA%/NoteCollisionC`，与旧 Qt 版独立，不迁移或覆盖旧版文件。账号文件存储随机盐与 SHA-256 摘要。文件使用 UTF-8 JSON 和临时文件原子替换；损坏、未知版本的成绩文件保留且禁止覆盖。损坏设置使用默认值并阻止覆盖。

歌曲与谱面保留原版资源及资源说明。生成脚本在 `tools/generate_demo.py` 和 `tools/generate_twinkle.py`。谱面检查轨道、ID、时间、同轨重叠、连锁 / 同时按键分组、WAV 时长和资源路径。

## 结构

| 文件 | 职责 |
| --- | --- |
| `src/main.c` | 页面跳转、输入分发、演奏生命周期 |
| `src/ui.c` | EasyX 页面、按钮、四轨绘制 |
| `src/engine.c` | 确定性判定与分数统计 |
| `src/assets.c` | 歌曲索引、谱面与 WAV 校验 |
| `src/storage.c` | 账号、设置、成绩、排行榜 |
| `src/audio.c` | Windows MCI 播放、暂停、音量、位置 |
| `src/network.c` | Winsock 非阻塞 TCP、同步与结果确认 |
| `src/platform.c` | UTF-8 路径、SHA-256、时钟、原子写入 |
| `src/json.c` | 带范围检查的 JSON 访问 |
| `src/easyx_bridge.cpp` | EasyX 的 C ABI 转发 |
| `tests/test_all.c` | 判定、资源、文件与真实 TCP 回归 |

测试与实际验证范围见 `docs/验证记录.md`。两台实体电脑的播放延迟、不同声卡以及键盘多键冲突仍需实测。
