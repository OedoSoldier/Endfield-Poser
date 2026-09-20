# AGENT.md — Endfield Poser 开发手册（AI Agent 自读）

> 本文件是给 AI Agent（如 Codex）的全局手册：读完它即可接手本项目——知道架构、知道怎么改、怎么编、怎么部署、怎么取反馈、怎么排查崩溃。每次开始工作前先读本文件与 `README.md`。

## 0. 项目是什么（一句话）

《明日方舟：终末地》的**游戏内摄影摆姿插件**：注入游戏进程，冻结角色动画，让用户用 FK / 2-bone IK 摆姿势、调面部形态键（BlendShape）、切双模式（摆姿/镜头）自由取景并截图。仓库独立、依赖自包含、不依赖 EIEM 的构建产物。

## 1. 目录结构

```
endfield-poser/
├── AGENT.md                 # 本文件
├── README.md                # 面向人的说明（特性/构建/依赖/合并摘要）
├── CMakeLists.txt           # Windows: 插件DLL+代理DLL；tests: 数学单测(跨平台)
├── build.bat                # Windows 一键构建 → plugin/
├── plugin/                  # 运行时输出(构建产物, gitignore)：poser.dll + d3dcompiler_47.dll + poses/ + poser_config.txt + poser_log.txt
├── debug/                   # 运行时调试产物(截图 snap.png 等, gitignore)
├── deps/                    # 自包含第三方：imgui / imguizmo(1.83) / minhook_lib / json
├── src/
│   ├── poser.cpp            # DLL入口 + Applepie 插件协议导出 + 每帧调度 + 主面板窗口
│   ├── config.h             # plugin/poser_config.txt 读写、快捷键(VK)
│   ├── core/
│   │   ├── base.h           # 版本宏、线程安全日志(Log)、IL2CPP对象布局常量、字段偏移回退
│   │   ├── il2cpp_api.h     # IL2CPP 运行时解析 + FindClass/FindMethod/Invoke + Dump 工具
│   │   ├── proxy_d3dcompiler.cpp  # d3dcompiler_47 代理DLL(转发系统DLL导出)
│   │   ├── game_hooks.h     # MinHook: SetMainCharacter hook → 捕获 Animator/Entity；Humanoid 55骨句柄
│   │   └── gui_overlay.h    # D3D11+DComp 透明覆盖窗 + ImGui 渲染循环（不关心游戏逻辑）
│   ├── math/                # 纯C++可单测：quat_math / ik_two_bone / pose_file
│   ├── game/                # skeleton(骨骼/快照/镜像/T-pose) / accessory(从骨) / freeze(冻结) / ik_driver / morph(BlendShape)
│   └── editor/              # gizmo(ImGuizmo) / panel_pose / panel_library / panel_mode / panel_morph / panel_camera
├── docs/superpowers/plans/2026-08-26-endfield-poser.md  # 分阶段实现计划(任务清单)
├── tools/screenshot.ps1     # PowerShell 截游戏窗口 → debug/snap.png（AI 视觉反馈入口）
└── tests/                   # 数学层单测(g++ 可跑, CTest)
```

**关键调用链**：`DllMain(poser.cpp)` → `InitThread` → `Resolve()`(IL2CPP) → `InitGameHooks()` → `StartGuiThread()`(覆盖窗) → 每帧 `GuiThread` 调 `DrawPoserGui()`(poser.cpp) → `GameFrameTick()`(角色重建/IK写回/模式相机)。

## 2. 构建

### Windows 插件（MSVC，唯一能产出可注入 DLL 的方式）
```bat
build.bat
```
等价于：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` + `cmake --build build --config Release`。
产物自动部署到 `plugin/poser.dll` + `plugin/d3dcompiler_47.dll` + `plugin/vulkan-1.dll`。

**本机没有 cmake，也没有系统 Windows SDK**：若 `cmake` 不在 PATH，`build.bat` 会自动回退到
`tools\build_msvc.ps1`（cl 直接编译 + 跑三个数学单测）；该脚本自动探测 `vcvars64.bat`
（含 VS18 Insiders / BuildTools 等目录）。新机器首次使用先补齐 SDK 依赖：
```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_winsdk.ps1   # 需联网，约 200MB，落到 deps\（gitignore）
powershell -ExecutionPolicy Bypass -File tools\build_msvc.ps1
```

### Linux 数学单测（沙箱/CI 可用，验证 math/ 层正确性）
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```
测试：`test_quat` / `test_ik` / `test_pose_file`。**AI 在无 Windows 环境时，改 math/ 层代码必须跑这套确认不回归。**

## 3. 部署与注入

1. 把整个 `plugin/` 文件夹复制到游戏可执行文件**同级目录**（与 `GameAssembly.dll` 同级）。
2. 启动游戏：代理 `d3dcompiler_47.dll` 被游戏加载链拉起，进而加载 `poser.dll`。
3. 由宿主 Applepie 插件系统启用 `Endfield Poser`（或自动加载），`DllMain` 启动初始化线程。
4. 默认热键：`VK_INSERT` 呼出/隐藏 GUI，`VK_F8` 截图（可在 `plugin/poser_config.txt` 改）。

> **必须经启动器启动，不要直接运行 `Endfield.exe`**（2026-09-20 本机实测）：
> 直启会绕过 Hypergryph 启动器 / ACE 的初始化，插件会在 IL2CPP 运行时尚未初始化完成时
> 调用 `il2cpp_thread_attach()`，触发 Unity GC 致命错误并卡死，弹窗内容为
> `Threads explicit registering is not previously enabled` 与 `Collecting from unknown thread`
> （同样两行会写进游戏根目录的 `Endfield.gc.log`）。同一份 `poser.dll` 走启动器启动正常。
> 直启崩了的判据：`plugin\poser_log.txt` 停在 `[POSER] Resolving IL2CPP...`、没有
> `[POSER] IL2CPP resolved.`。用启动器重开即可，不必重装插件。

本机部署路径（2026-09-20）：

- 游戏根目录 `E:\Hypergryph Launcher\games\Arknights Endfield\`（启动器 `E:\Hypergryph Launcher\Launcher.exe`）
- `d3dcompiler_47.dll`、`vulkan-1.dll` 放游戏根目录（覆盖游戏自带的需要先备份）
- `poser.dll`、`poser_config.txt` 放 `<游戏根目录>\plugin\`

## 4. 运行时产物（排查的第一现场）

| 产物 | 路径 | 说明 |
|------|------|------|
| 日志 | `plugin/poser_log.txt` | 全局唯一日志，`Log()` 写入。**一切 in-game 问题先看这里** |
| 配置 | `plugin/poser_config.txt` | key=value：`gui_toggle_key`/`screenshot_key`/`camera_speed`/`default_pose_dir` |
| 姿态预设 | `plugin/poses/*.poser.json` | 姿态库面板读写 |
| 截图 | `debug/snap.png` | `tools/screenshot.ps1` 产出（AI 视觉反馈） |

## 5. 视觉反馈（AI 闭环的关键）

AI 在终端里**看不见游戏画面**。取画面靠截图：
```powershell
powershell -ExecutionPolicy Bypass -File tools\screenshot.ps1 -ProcessName <游戏进程名>
# 或自动探测窗口标题含 明日方舟/终末地/Arknights/Endfield 的窗口
```
输出到 `debug/snap.png`，AI 自行读图判断画面状态（UI 布局、姿势、手柄、形态效果）。

## 6. 调试方法

### 6.1 读日志
`plugin/poser_log.txt` 按 `[模块]` 前缀区分：`[POSER]`(启动/IL2CPP) `[GUI]`(覆盖窗/DComp) `[OK]`(hook成功) `[WARN]`(回退/降级)。启动后应依次看到：attached → IL2CPP resolved → hooked → GUI initialized。

### 6.2 IL2CPP 类/字段侦察
`il2cpp_api.h` 内置 `DumpClassMethods` / `DumpClassFields` / `DumpFieldsHierarchy`，日志输出到 `poser_log.txt`。**游戏版本更新导致字段偏移失效时，用这些 Dump 工具重查布局，更新 `base.h` 中的偏移表。**

### 6.3 崩溃转储
游戏崩溃时先看 Windows 事件查看器：`eventvwr.msc` → Windows 日志 → 应用程序，找 `Application Error`/`AppModel Runtime`，记下 `Faulting module` 与异常码（常见 `0xC0000005` 访问违例）。
建议配置 WER 迷你转储（注册表 `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`）以便拿到 `.dmp` 分析。
代码中所有对游戏对象的直接读写都包了 SEH(`__try/__except`)，崩溃大概率出现在非包裹区（如 IL2CPP 调用链）、偏移错误或线程问题。

### 6.4 常见问题定位速查
- **GUI 不显示**：看 `[GUI]` 日志（DComp/CreateSwapChainForComposition 失败 → 显卡驱动/DWM 问题）；确认游戏窗口存在；确认 `g_guiVisible` 已置位（热键）。
- **骨骼数为 0**：`SetMainCharacter` hook 未命中（游戏更新改了方法签名/参数个数）→ 用 Dump 工具重新定位，更新 `game_hooks.h`。
- **冻结无效**：Animator 关闭未生效或仍有写者 → 检查 `freeze.h` 抑制逻辑、从骨物理。
- **BlendShape 不生效**：网格采集失败或写入时机 → 看 `morph.h`，结合日志确认采集数量。
- **中文乱码**：`msyh.ttc` 加载失败（见 `[GUI]` WARN）→ 确认系统有微软雅黑。

## 7. 标准迭代循环（AI 工作流）

对每个 in-game 问题/需求，按此循环执行：

1. **读上下文**：先看 `plugin/poser_log.txt` 最新段 + 可选截图 `debug/snap.png`；必要时用 Dump 工具侦察 IL2CPP 布局。
2. **改码**：在 `src/` 下修改（改动尽量小而聚焦）。
3. **纯数学层改动** → 跑 Linux 单测确认（`ctest`）。
4. **编译**：`build.bat`（Windows）→ 确认无编译错误。
5. **部署**：把新 `plugin\poser.dll`(+`d3dcompiler_47.dll`) 拷到游戏目录覆盖。
6. **重启游戏 + 复现**：让用户/宿主重开游戏，触发对应功能。
7. **取反馈**：读 `plugin/poser_log.txt`；需要画面则 `tools\screenshot.ps1` 截图并读图。
8. **判断**：命中预期 → 结束；否则回到 2（每次只改一个变量）。

**重要**：注入类改动无法在当前沙箱验证，AI 必须把"可复现步骤 + 期望日志 + 判据"写清楚，交给 Windows 真机执行；不要假装验证过。

## 8. 技术要点与约束

- **IL2CPP**：所有游戏对象访问走 `il2cpp_api.h` 解析出的函数指针 + `base.h` 的布局常量。版本敏感——偏移优先用 Dump 自纠。
- **注入链路**：代理 `d3dcompiler_47.dll` → Applepie 插件宿主 → `poser.dll`（`extern "C" __declspec(dllexport)` 的 `AP_*` 协议）。
- **Hook**：MinHook（`deps/minhook_lib`），当前仅 `SetMainCharacter`。
- **UI**：D3D11 + DComp 透明覆盖窗 + ImGui（dear imgui 旧版 API，勿用 1.90+ 新 API）；ImGuizmo 1.83（全局 API 风格）。
- **跨平台约束**：`math/` 与 `tests/` 必须是纯 C++（无 Windows 头），保证 g++ 可编；其余层依赖 Windows。
- **C++ 标准**：C++17，header-only 为主（`.h` 内 static 实现），CMake 源文件列表要同步新增头文件。

## 9. 依赖与许可证

- 依赖（全在 `deps/`，自包含）：imgui=MIT、imguizmo=MIT、nlohmann/json=MIT、minhook_lib=BSD。
- **AGPL 注意**：`src/core/` 中 base/il2cpp_api/proxy_d3dcompiler/gui_overlay/game_hooks 的头文件标注"摘自/精简自 EIEM"，EIEM 为 **AGPL-3.0**。本项目整体构成 AGPL 衍生作品：自用无义务，公开分发必须以 AGPL 提供源码；不能改成 MIT/专有发布。**改 core/ 层时保留来源声明。**

## 10. 实现状态速查

详细任务清单见 `docs/superpowers/plans/2026-08-26-endfield-poser.md`。当前状态：
- ✅ 已完成实现：冻结/解冻、骨骼列表+快照/恢复、从骨(链/物理/锁定)、FK+ImGuizmo、2-bone IK 驱动、双模式状态机+相机固定、BlendShape 采集/读写、相机接管、FOV。
- ⏳ 待 in-game 验证：所有标记 `[in-game]` 的步骤（插件注入、冻结、FK/IK、形态、相机、姿态库、截图）。
- ⬜ 未实现：姿态操作(镜像等细化)、景深、截图功能(插件内)、config 热键热生效、全局 SEH 包裹与优雅退出。
- 🧪 已由单测覆盖：quat / ik / pose_file（Linux g++ 3/3 通过）。
