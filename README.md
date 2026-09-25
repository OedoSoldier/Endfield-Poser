# Endfield Poser

《明日方舟：终末地》的游戏内摄影摆姿与 MMD 动作播放器。支持角色冻结、骨骼编辑、姿态库、口型表情、VMD 动作和可选音乐同步，无需 Blender。

本仓库是 [honxi1/Endfield-Poser](https://github.com/honxi1/Endfield-Poser) 的功能分支，当前版本：**0.4.16（预发布）**。下载本分支的 [Windows x64 预编译安装包](https://github.com/OedoSoldier/Endfield-Poser/releases/tag/v0.4.16)，无需安装编译工具；更新内容和详细安装方法见 [0.4.16 发布说明](docs/releases/v0.4.16.md)。[上游 Releases](https://github.com/honxi1/Endfield-Poser/releases) 的版本和功能可能与本分支不同。

> 本项目仅供学习与技术交流。使用前请阅读下方[免责声明](#免责声明)。插件与游戏版本相关，开发版仍需游戏内兼容性验证。

## 安装、更新与卸载

需要 Windows x64。在 Release 页的 **Assets** 中下载 **`Endfield-Poser-v0.4.16-win64.zip`**（`source.zip` 和 GitHub 的 `Source code` 是源码，需要自行构建）。先退出游戏，将安装包完整解压，再双击 **`安全安装.bat`**，选择包含 `Endfield.exe` 和 `GameAssembly.dll` 的游戏根目录。不要在压缩包内直接运行脚本。

- **安装 / 更新**：直接覆盖更新，无需先卸载。修改前备份 DLL，保留已有配置、窗口布局、校准和姿态；只补充缺少的适配预设。
- **卸载**：移除 `poser.dll`，按安装记录恢复本工具管理的代理 DLL。用户数据和备份保留；发现其他插件时保留共用代理。旧版没有安装记录时，仅移除已识别的 `poser.dll`。
- 游戏运行中脚本会拒绝操作。安装后必须经 **Hypergryph Launcher** 启动，不要直接运行 `Endfield.exe`。

仓库源码需要先运行 `build.bat` 生成 DLL，再运行安装向导。向导支持源码构建和发布包两种目录布局；不要只复制 `.bat` 文件，须保留 `tools/deploy.ps1`。

也可从 PowerShell 部署（将路径替换成实际游戏目录）：

```powershell
# 只显示计划，不修改文件
powershell -NoProfile -ExecutionPolicy Bypass -File tools/deploy.ps1 -GameDir "D:\Games\Arknights Endfield" -WhatIf

# 安装或更新
powershell -NoProfile -ExecutionPolicy Bypass -File tools/deploy.ps1 -GameDir "D:\Games\Arknights Endfield"

# 卸载，保留用户数据
powershell -NoProfile -ExecutionPolicy Bypass -File tools/deploy.ps1 -GameDir "D:\Games\Arknights Endfield" -Action Uninstall
```

手动安装时，先备份同名 DLL，再按下表放置；不要把整个本机构建输出或他人的配置复制进游戏。

| 文件 | 游戏内位置 |
|---|---|
| `d3dcompiler_47.dll` | 游戏根目录 |
| `vulkan-1.dll`（可选，Vulkan 模式需要） | 游戏根目录 |
| `poser.dll` | `plugin/poser.dll` |
| `presets/mmd/*.mmdrig.json` | `plugin/mmd/rig-presets/`，保留已有同名文件 |

插件目录是 **`plugin`（单数）**；游戏自带的 `plugins` 不是安装位置。配置由插件首次启动生成。建议使用窗口化或无边框全屏，以便显示覆盖层面板。

## 快速使用

进入可操作角色的场景后按 **L** 打开面板。按 **P** 冻结，用骨骼面板或 3D 旋转盘摆姿，再在姿态库保存。拖动窗口标题栏调整位置；取消 **锁定窗口** 后可自由移动，**重排窗口** 恢复初始布局。

| 默认快捷键 | 操作 |
|---|---|
| `L` | 显示 / 隐藏面板 |
| `P` | 冻结 / 解冻；MMD 占用时先退出播放 |
| 按住 `Alt` | 将光标交给面板；摄影模式通常可直接点击 |
| `Ctrl+F5` | MMD 播放 / 继续 |
| `Ctrl+F6` | 暂停并保持当前姿态 |
| `Ctrl+F7` | 停止并恢复播放前状态 |
| `Ctrl+F8` | 回到动作首帧并暂停 |

快捷键可在主面板修改，MMD 面板显示当前绑定。已有配置优先于默认值。隐藏面板后，动作和音乐继续播放；避免绑定与其他插件冲突的 F11/F12。

## MMD 播放器

勾选 **MMD 播放器** → **打开 VMD** → 完成角色校准 → **播放**。完整操作见 [MMD 播放指南](docs/mmd-player.md)。

- **动作与表情**：身体、手指、眼神、口型和表情；支持追加独立口型 / 表情 / 眼神文件，同名轨道由后追加文件替换。
- **校准与参考骨架**：自动读取游戏绑定姿态，失败时提供手动 T 姿预览；可选 PMX 2.0/2.1 骨架参考，不导入模型或材质。
- **播放控制**：暂停、逐帧、拖动、0.25–2 倍速、循环、原地模式、位移比例和高度修正；自然结束保持末帧。
- **手动适配**：A/T 源姿态、IK 跟随 / 强制开 / 强制关、準標準骨补全、自定义骨和两层映射，可保存适配预设。不会读取动作说明或按文件名自动选择设置。
- **动作幅度**：全身与各部位分别调节，支持左右联动和复位。全身系数与部位系数相乘，默认均为 100%。这不是自动碰撞检测。
- **音乐同步**：手动选择系统可解码的 WAV、MP3、M4A 等音频，支持偏移和音量，跟随播放、暂停、拖动与循环。变速会改变音高。

一次控制当前角色。校准和播放期间隐藏已识别的专用道具节点，停止后恢复；默认保留游戏头发、衣物物理，也可冻结。换人会结束当前播放，保留已打开的动作和各角色校准。

## 姿态、表情与保存位置

- 支持骨骼旋转 / 位移、从骨调整、角色根位置、姿态保存与载入。
- 冻结姿态按角色在本次运行中记忆；切走释放旧实例控制，切回重新捕获并恢复。
- 口型与表情映射到游戏已有 SMC 通道，未匹配轨道可手动绑定和调整强度。没有对应通道的材质类表情无法复现。
- 姿态库默认不保存 / 套用面部骨骼，避免跨角色错脸；暂停 MMD 时可保存身体姿态。

| 内容 | 相对于游戏目录的路径 |
|---|---|
| 设置 / 快捷键 | `plugin/poser_config.txt` |
| 窗口布局 | `plugin/poser_layout.ini` |
| 姿态库 | `plugin/poses/*.poser.json` |
| 校准、表情映射、适配预设 | `plugin/mmd/` |
| 日志 | `plugin/poser_log.txt` |
| 安装记录 / 备份 | `plugin/poser-install.json` / `plugin/poser-backups/` |

本机 WebUI：`http://127.0.0.1:18923`。MMD 播放 / 暂停期间，冲突的姿态写入接口返回 `409 Conflict`。

## 构建源码

安装 Visual Studio / Build Tools 的 **MSVC x64 C++ 工具链**和 **Windows SDK**，然后运行：

```bat
build.bat
```

也可直接执行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_msvc.ps1`。脚本自动寻找 MSVC，优先使用系统 SDK；仅在没有系统 SDK 时可用 `tools/setup_winsdk.ps1` 下载 SDK 回退依赖。产物为 `plugin/poser.dll`、`plugin/d3dcompiler_47.dll`、`plugin/vulkan-1.dll`。

支持 CMake + MSVC，例如 VS 2022 x64：

```powershell
cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64
cmake --build build-cmake --config Release
```

公开源码构建不依赖本机测试、实验记录或媒体素材。`src/core/version.h` 是版本号来源；保留 `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`，以兼容游戏自带的旧 MSVC 运行库。

## 当前限制与排查

- 不包含 MMD 相机、灯光、模型导入、多角色同步或 MMD 物理模拟。特殊骨、道具和表情可能需要手动映射，未支持项显示在导入报告中。
- 动作幅度不能保证消除穿模，也可能改变脚底接触；腿部表现需结合原始动作、源骨架和 IK 模式调整。
- 动作更新与面板绘制已分离，但游戏帧回调可能不可用并回退到独立计时。面板显示实际驱动来源，当前不能保证所有渲染管线均与背景帧同步。
- 0.4.13 调整了冻结换人的对象恢复与引用管理；复杂角色切换、连续播放及其他模组共存仍需游戏内验证。
- 0.4.16 修正表情任务回调的返回值与参数转发，处理首次加载角色时的闪退；安装钩子前校验接口，游戏接口不兼容时停用表情接管。
- 游戏更新可能改变骨架与运行时接口。捕获不到角色时先等待模型加载，再尝试 **刷新骨骼**；校准失效时重新校准。
- 面板不出现时检查窗口模式、快捷键和日志。覆盖层卡顿可降低 `overlay_fps`；无法拖动先检查 **锁定窗口**。

反馈请提供插件版本、复现步骤和必要日志片段，并先检查其中的个人路径等信息。

## 参考与交流

注入层参考 [Sasye/EIEM](https://github.com/Sasye/EIEM)，兼容 [ApplepieManager](https://github.com/Sasye/ApplepieManager) 插件协议；Poser 可由自带代理加载，无需另装管理器。依赖位于 `deps/`：Dear ImGui、ImGuizmo、MinHook、nlohmann/json，保留各自许可与来源声明。

非官方粉丝项目，与 Hypergryph 无关。

- [上游问题反馈](https://github.com/honxi1/Endfield-Poser/issues)；本分支新增功能的问题请在本分支提交页面讨论。
- QQ 群：**终末地影棚爱好者**（1126684901）
- 邮箱：**king_time@foxmail.com**（版权 / 内容下架等问题优先邮件）

![QQ 群](docs/qq-group.jpg)

## 免责声明

> **本项目仅供学习与技术交流，请勿用于任何商业用途。**

- 本项目是**非官方第三方工具**，与《明日方舟：终末地》的开发商 / 发行商（Hypergryph、鹰角网络）
  **没有任何隶属、合作或授权关系**，也未获得其认可。
- 本工具会**注入并修改游戏客户端进程**。这类行为**可能违反游戏的用户协议**，存在
  **账号被限制或封禁**的风险；请自行评估风险，并**自行承担全部后果**。
- 请**仅在单人 / 摄影模式**下使用；**不要在多人联机、竞技或任何会影响他人游戏体验的场景使用**。
- 请勿使用本工具**修改、绕过或传播任何付费内容与游戏资源**（模型、贴图、音频等）；
  姿态文件只应包含你自己摆出的骨骼数据。
- 作者不对使用本工具造成的任何损失负责（包括但不限于账号封禁、数据丢失、设备异常）。
- 如相关权利人、游戏官方或平台认为本仓库 / 发布包中的任何内容不妥，请通过
  **king_time@foxmail.com**（或 [issue](https://github.com/honxi1/Endfield-Poser/issues) /
  文末交流群）联系作者，**收到通知后会第一时间处理（包括删除相关内容、停止分发）**。
- **如果你不接受以上任何一条，请立即停止使用并删除本工具**：用包内 `安全安装.bat` 卸载，
  卸载保留配置和姿态；手动处理代理 DLL 前请确认归属并备份，避免影响其他插件。

### 内容合规与行为约束

本插件本身不包含任何游戏美术资产。用户知悉并同意，《明日方舟：终末地》游戏内置的官方动画、
场景、模型等资产其版权完全隶属于鹰角网络，并不适用 AGPL-3.0 协议。您不应该且不得利用本插件，
或利用游戏内置的官方动画、场景、模型等游戏资产，制作、播放或传播任何不合适的动作 / 动画
（包括但不限于色情、暴力、政治敏感等违反法律法规或引起社区不适的内容）。

## 许可

本仓库整体构成 **AGPL-3.0** 衍生作品（`src/core/` 的注入层参考 AGPL-3.0 的 EIEM），
公开分发需按 AGPL 提供源码；不可改为 MIT / 专有许可发布。修改 `src/core/` 时请保留来源声明。完整许可见 [LICENSE](LICENSE)，依赖声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。每个预编译 Release 同时提供对应版本源码。
