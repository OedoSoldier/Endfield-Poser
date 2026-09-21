> **AI Agent（Codex 等）请先读 [AGENT.md](AGENT.md)。**

# Endfield Poser

《明日方舟：终末地》的游戏内摆姿插件：把角色冻结在当前姿态，用 3D 旋转盘和参数面板直接摆姿势，
保存 / 载入姿态预设，方便游戏内取景与后续参考。

- 下载：[Releases · v0.2.0](https://github.com/honxi1/Endfield-Poser/releases/tag/v0.2.0)
- 依赖全部自包含在 `deps/`，不依赖 EIEM 的构建产物（注入链路的思路参考 EIEM，AGPL-3.0）

## 下载与安装

从 [Releases](https://github.com/honxi1/Endfield-Poser/releases) 下载 `EndfieldPoser-v0.2.0.zip`，解压后按目录对应放置：

| 包内文件 | 放到 |
|---|---|
| `d3dcompiler_47.dll` | 游戏根目录（**先备份游戏自带的那份**） |
| `vulkan-1.dll`（可选） | 游戏根目录——DX/Vulkan 代理放一个或都放均可 |
| `plugin\poser.dll` | 游戏根目录的 `plugin\` |
| `plugin\poser_config.txt` | 游戏根目录的 `plugin\` |

> ⚠️ **必须用游戏启动器启动**（Hypergryph Launcher）。直接运行 `Endfield.exe` 会在 IL2CPP
> 运行时初始化完成前 attach，触发 Unity GC 致命错误并卡死；判据是 `plugin\poser_log.txt`
> 停在 `[POSER] Resolving IL2CPP...`，同时游戏根目录的 `Endfield.gc.log` 里会出现
> `Threads explicit registering is not previously enabled` / `Collecting from unknown thread`。

## 使用

| 操作 | 说明 |
|---|---|
| `F12` | 呼出 / 隐藏面板（可在 `poser_config.txt` 改） |
| `F11` | 冻结 / 解冻 |
| 按住 `Alt` | 光标归面板（游戏自己放开光标时——例如摄影模式——直接点即可） |

典型流程：进游戏 → `F11` 冻结 → 在 3D 视图里点选骨骼（勾「全量骨骼(微调)」可点到从骨与手指）→
拖旋转盘或调参数 → 命名并保存姿态。

- 姿态文件：`<游戏目录>\plugin\poses\*.poser.json`（含 humanoid 骨、从骨与面部形态键）
- 日志：`<游戏目录>\plugin\poser_log.txt` —— **排查问题先看这里**
- WebUI：插件启动后监听 `http://127.0.0.1:18923`

## 功能

- **角色冻结**：关闭 Animator 并抑制 FinalIK / 布料等写者，每帧维持；可选"冻结飘带/裙子/头发"（默认开），取消勾选则从骨保持实时演算。
- **摆姿编辑**：3D 点选骨骼 + ImGuizmo 旋转盘；旋转 / 位置参数支持滑条、数值输入、± 步进与复位。
- **人物位置**：Root XYZ 的滑条、精确输入，以及可调步长的 ± 步进。
- **从骨控制**：头发、裙子、飘带等从骨随冻结钉住；手动编辑会同步冻结基线，不会被每帧回写打回。
- **姿态预设**：命名保存 / 覆盖 / 加载 / 删除，格式含从骨与形态键，旧格式文件仍可读。
- **形态键与表情**：面部 BlendShape 面板，以及游戏原生 SMC 表情。
- **输入路由**：覆盖层常驻并真穿透（`click_through=1`），只有指针落在面板 / 关节上且光标可用时才接管；点击输入框可直接打字，失焦后键盘立刻还给游戏。

## 配置（`plugin\poser_config.txt`）

```
gui_toggle_key=VK_F12     # 支持 VK_F12 / F12 / 0x7B 三种写法
click_through=1           # 1=覆盖层常驻并真穿透（推荐）；0=按住 Alt 才显示面板
default_pose_dir=plugin\poses
```

`default_pose_dir` 支持绝对路径，或相对游戏根目录的路径；面板底部会显示当前保存位置。

## 构建

### Windows（插件本体，MSVC）

```powershell
# 本机（VS 18 Insiders、无 cmake、无系统 Windows SDK）：
powershell -ExecutionPolicy Bypass -File tools\setup_winsdk.ps1   # 首次：拉取 Windows SDK 到 deps/（需联网）
powershell -ExecutionPolicy Bypass -File tools\build_msvc.ps1     # 编译 plugin/ 并跑三个数学单测

# 有 cmake + VS 工具链时：
build.bat
```

两种方式产物都落在 `plugin/`：`poser.dll`（插件）、`d3dcompiler_47.dll`、`vulkan-1.dll`（代理）。

### Linux / 沙箱（数学层单测）

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

测试覆盖 `math/` 层（`test_quat` / `test_ik` / `test_pose_file`）；插件本体只能在 Windows + 游戏内验证。

## 目录结构

```
endfield-poser/
├── AGENT.md              # 开发手册（架构 / 调试 / 迭代流程，改代码前先读）
├── CMakeLists.txt        # Windows: 插件 DLL + 代理 DLL；tests: 数学单测
├── build.bat             # 有 cmake 时的一键构建（否则回退 build_msvc.ps1）
├── deps/                 # 自包含第三方：imgui / imguizmo / minhook_lib / json
├── tools/                # build_msvc.ps1、setup_winsdk.ps1、screenshot.ps1 等
├── src/
│   ├── poser.cpp         # DLL 入口 + Applepie 插件协议 + 每帧调度 + 主面板
│   ├── config.h          # poser_config.txt 读写、热键解析
│   ├── core/             # base / il2cpp_api / game_hooks / gui_overlay / web_server / 代理 DLL
│   ├── math/             # quat_math / ik_two_bone / pose_file（纯 C++，可单测）
│   ├── game/             # skeleton / accessory / freeze / cloth / morph / smc_morph
│   └── editor/           # selection / rig_gizmo / panel_bones / panel_library / panel_morph
├── tests/                # math 层单测（g++ 亦可跑）
└── docs/                 # task-state.md（进度与已知问题）、构建环境、研究笔记
```

## 已知问题

- **必须经启动器启动**（原因见上）。
- **表情不能跨角色通用**：本作面部由 SMC（骨骼变形）驱动，网格上没有 BlendShape 目标，
  而 SMC 权重目前不写入姿态文件——所以保存下来的姿态只含骨骼与从骨，表情需要在新角色上重调；
  强行套用其它角色的面部骨数据会得到错位或夸张的脸（姿态面板默认勾选「不保存/不套用表情」，
  姿态文件里不再包含眼/下巴等表情骨；旧文件里若带这类数据，载入时也会被跳过）。
- 相机参数不随姿态文件保存。
- 撤销 / 重做、骨骼层级树、IK 控制器、外置姿态导出均未包含在本版（做过但实测有问题，代码保留在 git 历史）。

## 许可

本仓库整体构成 **AGPL-3.0** 衍生作品（`src/core/` 的注入层参考 AGPL-3.0 的 EIEM），
公开分发需按 AGPL 提供源码；不可改为 MIT / 专有许可发布。修改 `src/core/` 时请保留来源声明。
