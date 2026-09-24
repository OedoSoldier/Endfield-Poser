> **AI Agent（Codex 等）请先读 [AGENT.md](AGENT.md)。**

# Endfield Poser

《明日方舟：终末地》的游戏内摆姿插件：把角色冻结在当前姿态，用 3D 旋转盘和参数面板直接摆姿势，
保存 / 载入姿态预设，方便游戏内取景与后续参考。

- 下载：[Releases · v0.3.1](https://github.com/honxi1/Endfield-Poser/releases/tag/v0.3.1)
- 依赖全部自包含在 `deps/`，不依赖 EIEM 的构建产物（注入链路的思路参考 EIEM，AGPL-3.0）

## 下载与安装

从 [Releases](https://github.com/honxi1/Endfield-Poser/releases) 下载 `EndfieldPoser-v0.3.1.zip`，解压后按目录对应放置（也可以直接双击包里的 `安全安装.bat` 走向导）：

| 包内文件 | 放到 |
|---|---|
| `d3dcompiler_47.dll` | 游戏根目录（**先备份游戏自带的那份**） |
| `vulkan-1.dll`（可选） | 游戏根目录——DX/Vulkan 代理放一个或都放均可 |
| `plugin\poser.dll` | 游戏根目录的 `plugin\` |
| `plugin\poser_config.txt` | 游戏根目录的 `plugin\` |

> 📁 **注意是 `plugin\`（单数，插件目录），不是游戏自带的 `plugins\`（复数）**。
> 后者是游戏的 Qt 插件目录（里面是 `imageformats/`、`platforms/` 这些），放进去不会生效。
> 如果游戏目录下没有 `plugin\` 文件夹，自己新建一个。

> ⚠️ **必须用游戏启动器启动**（Hypergryph Launcher）。直接运行 `Endfield.exe` 会在 IL2CPP
> 运行时初始化完成前 attach，触发 Unity GC 致命错误并卡死；判据是 `plugin\poser_log.txt`
> 停在 `[POSER] Resolving IL2CPP...`，同时游戏根目录的 `Endfield.gc.log` 里会出现
> `Threads explicit registering is not previously enabled` / `Collecting from unknown thread`。

渲染 API 说明：插件与游戏用的 API 无关（面板是自建的 D3D11 + DirectComposition 透明窗口）。
**DX11 与 Vulkan 两种模式都已实测可用**；用 Vulkan 时请确保 `vulkan-1.dll`（本包的代理）也在
游戏根目录，并使用「窗口化 / 无边框全屏」——独占全屏会绕过 DWM 合成，面板会看不见。
日志里会标明插件由哪个代理拉起：`[PROXY] plugins loaded via d3dcompiler_47.dll (DX path)`
或 `[PROXY] plugins loaded via vulkan-1.dll (Vulkan path)`。

## 使用

> 完整图文流程见 **[docs/tutorial.md](docs/tutorial.md)**（安装 → 冻结 → 摆姿 → 表情 → 姿态库 → 排查）。

| 操作 | 说明 |
|---|---|
| `L` | 呼出 / 隐藏面板（主面板 `快捷键（可改）` 一键改键，或改 `poser_config.txt`） |
| `P` | 冻结 / 解冻 |
| 按住 `Alt` | 光标归面板（游戏自己放开光标时——例如摄影模式——直接点即可） |

> **为什么不用 F11/F12（连 Ctrl+F12 也不行）**：XXMI / 3DMigoto 是直接轮询 F11/F12 的
> 按键状态，你按 `Ctrl+F12` 它们照样会触发自己的动作 —— 只有完全不碰 F 键才躲得掉，所以默认用 `L` / `P`。
> 代价是游戏内文本框/聊天里打字可能误触发（插件自己面板的输入框已屏蔽）。想换键：点
> `快捷键（可改）` → `改键` → 直接按（自动写回 `poser_config.txt`，`Esc` 取消）。
> 单键（含字母）也允许绑，但游戏内打字会误触发——绑了单键主面板会提醒，建议用带 Ctrl 的组合。

典型流程：进游戏 → `Ctrl+F11` 冻结 → 在 3D 视图里点选骨骼（勾「全量骨骼(微调)」可点到从骨与手指）→
拖旋转盘或调参数 → 命名并保存姿态。

- 姿态文件：`<游戏目录>\plugin\poses\*.poser.json`（含 humanoid 骨、从骨与面部形态键）
- 日志：`<游戏目录>\plugin\poser_log.txt` —— **排查问题先看这里**
- WebUI：插件启动后监听 `http://127.0.0.1:18923`

## 功能

- **角色冻结**：关闭 Animator 并抑制 FinalIK / 布料等写者，每帧维持；可选"冻结飘带/裙子/头发"（默认开），取消勾选则从骨保持实时演算。
- **多角色**：冻结状态按角色记忆——切到没冻过的角色时它保持默认（正常动），切回冻过的角色会**恢复你离开时的姿势**；已经在后台的冻结角色不会被游戏重新启用。当前只有"当前角色"可编辑。
- **摆姿编辑**：3D 点选骨骼 + ImGuizmo 旋转盘；旋转 / 位置参数支持滑条、数值输入、± 步进与复位。
- **人物位置**：Root XYZ 的滑条、精确输入，以及可调步长的 ± 步进。
- **从骨控制**：头发、裙子、飘带等从骨随冻结钉住；手动编辑会同步冻结基线，不会被每帧回写打回。
- **姿态预设**：命名保存 / 覆盖 / 加载 / 删除，格式含从骨与形态键，旧格式文件仍可读。
- **形态键与表情**：面部 BlendShape 面板 + 游戏原生 SMC 表情。滑条值恒为**相对"中性默认脸"**的权重
  （默认脸等价于面部 A-pose，自动采样，不是冻结那一刻的脸）；**冻结会保持当前表情**（把此刻的脸
  反解成滑条值），`全部归零` 回到默认脸，`读入当前表情` 把游戏当前表情读进滑条。
- **输入路由**：覆盖层常驻并真穿透（`click_through=1`），只有指针落在面板 / 关节上且光标可用时才接管；点击输入框可直接打字，失焦后键盘立刻还给游戏。

## 配置（`plugin\poser_config.txt`）

```
gui_toggle_key=L          # 支持 L / CTRL+L / VK_F12 / 0x7B 这类写法
freeze_key=P              # 冻结 / 解冻（写法同上）
click_through=1           # 1=覆盖层常驻并真穿透（推荐）；0=按住 Alt 才显示面板
overlay_mode=0            # 0=自动（检测到 XXMI/3DMigoto 时改用分层窗口）；1=强制 DComp；2=强制分层窗口
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
- **表情滑条只覆盖游戏自带的那 5 个口型 + 19 个表情**：角色表情里用到的其它 morph 滑条表示不了，
  但会被内部基准保住（脸不会跳变、不会错位）。若「读入当前表情」在面板上显示
  `no usable source`，把日志里 `[SMC] weight source probe` 那几行发出来即可继续适配。
- 相机参数不随姿态文件保存。
- 撤销 / 重做、骨骼层级树、IK 控制器、骨骼镜像、外置姿态导出均未包含在本版（做过但实测有问题，代码保留在 git 历史）。

## 参考与致谢

- **[Sasye/EIEM](https://github.com/Sasye/EIEM)**（AGPL-3.0）：本项目的注入链路脱胎于此——
  Applepie 插件协议（`AP_*` 导出）、IL2CPP 运行时解析、MinHook 挂点、D3D11 + DirectComposition
  透明覆盖层。`src/core/` 下的 `base.h`、`il2cpp_api.h`、`proxy_d3dcompiler.cpp`、
  `gui_overlay.h`、`game_hooks.h` 都标注了"精简自 EIEM"，改这些文件时请保留来源声明。
- **[Sasye/ApplepieManager](https://github.com/Sasye/ApplepieManager)**（AGPL-3.0）：插件宿主/管理器，
  负责枚举拉起 `plugin\*.dll` 并提供控制面板。
- **第三方依赖**（全部自包含在 `deps/`，不联网）：imgui（MIT）、ImGuizmo 1.83（MIT）、
  MinHook（BSD-2-Clause）、nlohmann/json（MIT）；其中 imgui 与 MinHook 的源码/静态库取自 EIEM 仓库。

## 交流 / 反馈

非官方粉丝项目与交流群，与 Hypergryph 无关。

- QQ 群：**终末地影棚爱好者**（群号 1126684901）
- 扫码入群：

![QQ 群](docs/qq-group.jpg)

遇到问题欢迎带上 `plugin\poser_log.txt` 与复现步骤，在 issue 或群里反馈。

## 许可

本仓库整体构成 **AGPL-3.0** 衍生作品（`src/core/` 的注入层参考 AGPL-3.0 的 EIEM），
公开分发需按 AGPL 提供源码；不可改为 MIT / 专有许可发布。修改 `src/core/` 时请保留来源声明。
