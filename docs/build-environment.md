# 构建环境与 EIEM 复用说明（Endfield Poser）

## 本机现状（2026-09-20 更新）

这台 Windows 机器上：

- 已安装 **Visual Studio 18 Insiders**（含 MSVC C++ 工具链，`vcvars64.bat` 位于
  `C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\`）
- **没有安装 cmake**（`build.bat` 原版无法直接跑）
- **没有安装系统 Windows SDK**（缺少 `windows.h`、`crtdbg.h`、`d3d11.lib` 等）
- 当前 shell 用户非管理员，无法通过 VS Installer 装 SDK

解决方案：从 NuGet 拉取 Windows SDK 头文件/库到 `deps/`，用 `tools/build_msvc.ps1` 直接调用 MSVC 编译，绕开 cmake 与系统 SDK。

`tools/build_msvc.ps1` 会自动探测 `vcvars64.bat`（遍历各 VS 版本目录，含 Insiders / BuildTools，
再退回 vswhere），不再硬编码 2022 Community 路径。

### 换机器后的初始化（两步）

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_winsdk.ps1   # 需联网，约 200MB，只写 deps/
powershell -ExecutionPolicy Bypass -File tools\build_msvc.ps1      # 编译 plugin/ 并跑三个数学单测
```

`deps/winsdk`、`deps/winsdkcpp` 与 `plugin/` 都在 `.gitignore` 里，因此每次换机器/换 clone 都要先跑第一步。

## 参考项目：EIEM（已跑通的原型）

本项目的注入链路脱胎于 [Sasye/EIEM](https://github.com/Sasye/EIEM)（AGPL-3.0），已克隆到
`D:\PROJECTS\EIEM`；插件宿主 [Sasye/ApplepieManager](https://github.com/Sasye/ApplepieManager)（AGPL-3.0）
已克隆到 `D:\PROJECTS\ApplepieManager`。

复用情况：

| 组件 | 状态 |
|------|------|
| `il2cpp_api.h`（IL2CPP 运行时解析 + FindMethod/FindFieldInHierarchy/Dump） | ✅ 与 EIEM 同文件，已完整复用 |
| Applepie 插件协议（`AP_*` 导出） | ✅ 同构，管理器可直接识别 poser.dll |
| 动态字段偏移解析（`FindFieldInHierarchy` + `SafeOff` 回退） | ✅ 已采用 |
| SetMainCharacter hook（Entity → ComplexAnimationComponent → Animator） | ✅ 已采用同款解析链 |
| DComp + D3D11 + ImGui 覆盖层 | ✅ 同构 |
| `d3dcompiler_47.dll` 代理 | ✅ 自建（同机制） |
| `vulkan-1.dll` 代理 | ✅ 已从 EIEM 移植到 `src/core/proxy_vulkan_full.cpp` |
| ApplepieManager 宿主 | ✅ 用 `tools/build_applepie.ps1` 编出 `plugin\applepie_manager.dll` |
| 原生 BipedIK/FinalIK 钩子（SolverManager.LateUpdate / BipedIK.UpdateSolver / IKSolverTrigonometric.OnUpdate + 求解器偏移） | ⏳ 已定位，待接入 ik_driver（见下） |

**下一步（原生 IK 复用）**：EIEM 的 `init.h` 里有一套验证过的 FinalIK 钩子与偏移：
`OFF_BIPEDIK_SOLVERS 0x40`、`OFF_SOLVERS_LEFT_FOOT 0x10` 等，以及三个 hook 点的 RVA 回退
（SolverManager.LateUpdate `0x035BD200`、BipedIK.UpdateSolver `0x0326A380`、
IKSolverTrigonometric.OnUpdate `0x032759E0`）。这些是接我们 `ik_driver.h` 的
"预留原生 BipedIK 接入点" 的现成答案，待基础注入验证后再搬。

## 依赖的 SDK 包（已放入 deps/）

| 目录 | NuGet 包 | 内容 |
|------|----------|------|
| `deps/winsdk` | `Microsoft.Windows.SDK.CPP` | `c\Include\10.0.28000.0\{um,shared,ucrt}` 头文件 |
| `deps/winsdkcpp` | `Microsoft.Windows.SDK.CPP.x64` | `c\um\x64`、`c\ucrt\x64` 库文件 |

这两个目录体积约 200MB，已加入 `.gitignore`，**不入库**。换机器时运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools\setup_winsdk.ps1
```

（需要联网；它会重新下载并解压上述两个包。）

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_msvc.ps1
```

或直接 `build.bat`（会自动检测 cmake，没有则回退到 `tools\build_msvc.ps1`）。

产物：

- `plugin\poser.dll` —— 主插件（自启动：DllMain → InitThread → IL2CPP → hooks → GUI）
- `plugin\d3dcompiler_47.dll` —— DX 代理加载器（转发到 `C:\Windows\System32\d3dcompiler_47.dll`）
- `plugin\vulkan-1.dll` —— Vulkan 代理加载器（转发到 System32，游戏走 Vulkan 时用）
- `plugin\applepie_manager.dll` —— 插件宿主/管理器（`tools/build_applepie.ps1` 产出）
- `plugin\poser_config.txt` —— 默认配置

同时会用 cl 编译并运行 `tests\test_quat / test_ik / test_pose_file` 三个数学单测。

## 部署（游戏侧）

1. 备份游戏目录原有的 `d3dcompiler_47.dll`
2. 把 `plugin\d3dcompiler_47.dll` 复制到 `D:\Endfield Game\`（覆盖游戏自带的）
3. （可选）把 `plugin\vulkan-1.dll` 也复制到 `D:\Endfield Game\`——DX/Vulkan 代理放一个或都放均可
4. 把 `plugin\poser.dll`、`plugin\poser_config.txt` 复制到 `D:\Endfield Game\plugin\`
5. （可选）把 `plugin\applepie_manager.dll`、`plugin\applepie_manager_config.txt` 也放进 `plugin\`
6. 启动游戏；代理 DLL 枚举加载 `plugin\*.dll`，poser 与 manager 都会被拉起；
   日志写在 `D:\Endfield Game\plugin\poser_log.txt`（poser）与 manager 的日志

> **必须用启动器启动游戏（本机实测 2026-09-20）**：直接运行 `Endfield.exe` 会在 IL2CPP
> 运行时初始化完成前 attach，触发 Unity GC 致命错误
> （`Threads explicit registering is not previously enabled` / `Collecting from unknown thread`）。
> 本机游戏在 `E:\Hypergryph Launcher\games\Arknights Endfield\`，用
> `E:\Hypergryph Launcher\Launcher.exe` 启动即正常。

> 代理与插件可共存：EIEM / ApplepieManager / poser 的代理加载器机制相同，
> 谁先部署谁生效，重复放置代理无害（README 说明"无需重复放置"指的是二选一/全放均可）。

## 已知问题 / 备注

- **ImGuizmo 1.83 与 imgui 1.92.9 不兼容**，已对 `deps\imguizmo\ImGuizmo.cpp` 打了最小补丁：
  `AddPolyline` 的 `bool closed` → `ImDrawFlags_None/Closed`（厚度 `2`→`2.0f`），
  `ImGui::CaptureMouseFromApp()` → `ImGui::SetNextFrameWantCaptureMouse(true)`。
- **config 的 VK 写法**：`ParseVK` 只认 `VK_` 后跟数字（如 `VK_0x2D`）或纯数字（`0x2D`）；
  README 示例里的 `VK_INSERT` 会被解析成 0。默认配置已用 `0x2D`/`0x77`。
- **MinHook 静态 CRT 混用**：`libMinHook.x64.lib` 是 /MT 编译的，主 DLL 用 /MD，
  已加 `/NODEFAULTLIB:LIBCMT` 消除冲突警告（正常可用）。
- **反作弊**：游戏目录带 AntiCheatExpert，代理 DLL 注入有被拦截/风控的风险，尚未实测。
