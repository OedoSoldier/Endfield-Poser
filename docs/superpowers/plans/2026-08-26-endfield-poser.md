# Endfield Poser 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为《明日方舟：终末地》开发一个独立游戏内摄影摆姿插件（Poser）：**让角色进入静止可摆姿状态——抑制动画/IK/形态/布料对骨骼的覆盖，保证摆好的姿势稳定、不被动画打回** → 用 FK/IK 摆姿势（含从骨：头发/配饰，可关物理、可锁定）→ 调形态键（面部 BlendShape + 身体骨骼形态）→ **双模式（摆姿模式/镜头模式）**自由取景 → 截图出片。全局时间冻结为可选增强，延后实施。

**Architecture:** 沿用 EIEM 的成熟注入链路（代理 DLL + Applepie 插件宿主 + IL2CPP 运行时解析 + MinHook），但**不引入 MMD 重定向/动画解析**——直接对游戏自身骨骼（Animator Humanoid 骨骼 + 原生 FinalIK）做读写。整体分四层：`core`（注入/IL2CPP/hook 基础）、`math`（纯 C++：四元数、2-bone IK、姿态文件，可单测）、`game`（冻结/角色捕获/形态键）、`editor`（ImGui + ImGuizmo 界面与摄影）。

**Tech Stack:** C++17 / MSVC（Windows 构建）、CMake + build.bat、ImGui + ImGuizmo（D3D11）、MinHook、IL2CPP API、nlohmann/json（单头，姿态文件）、stb_image_write（截图 PNG）。纯数学模块用 g++（Linux 沙箱）跑单测。

**参考来源:** EIEM 项目 `/workspace/src/`（本文以 `{EIEM}` 指代）。

**环境约束（重要）:** 插件只能在 Windows + 游戏内验证（MSVC 编译、D3D11 注入）。本沙箱为 Linux，仅可对 `math` 层做编译+单测。所有"验证"步骤若标注 `[in-game]` 需在 Windows 游戏环境执行。

---

## 范围决策（已与用户确认）

- 目标游戏：明日方舟：终末地（复用 EIEM 的 IL2CPP/hook 基建）
- 形态：独立新项目（本目录 `endfield-poser/`），不动 EIEM，依赖自包含
- 形态键：面部 BlendShape + 身体 SkeletalMorph 都要
- 摆姿交互：FK + 2-bone IK + 手指 FK（不需要物理模拟/运动合成）
- 从骨（头发/配饰/衣角等动态骨骼）：可自由摆姿、**可逐链/逐骨禁用物理**（DynamicBone/Cloth 类组件）、**可锁定**（锁定的骨不受 FK/快照影响）
- 双模式：**摆姿模式**（镜头固定、类似 Blender 姿态模式，也可切到镜头视角移动）↔ **镜头模式**（自由相机），一键切换
- 摄影：冻结、自由相机、FOV、景深、隐藏 UI 截图

---

## 项目结构

```
endfield-poser/
├── CMakeLists.txt               # Windows: 插件 DLL + 代理 DLL；tests: 数学单测（跨平台）
├── build.bat                    # Windows 一键构建（产出 plugin/）
├── plugin/                      # 输出布局（放入游戏目录）
│   ├── d3dcompiler_47.dll       # 代理 DLL（本地编译自 src/core/proxy_d3dcompiler.cpp）
│   ├── vulkan-1.dll             # 代理 DLL（可选）
│   └── poser.dll                # 本插件
├── deps/                        # 自包含第三方：imgui、imgui_impl_dx11/win32、imguizmo、minhook_lib、json.hpp、stb_image_write.h
├── src/
│   ├── poser.cpp                # DLL 入口 + Applepie 插件协议 + 版本信息
│   ├── config.h                 # poser_config.txt 读写、快捷键
│   ├── core/
│   │   ├── base.h               # Log / SafeOff / IL2CPP 布局常量（从 EIEM 精简）
│   │   ├── il2cpp_api.h         # IL2CPP Resolve + FindClass/FindMethod/Invoke（复制 EIEM）
│   │   ├── proxy_d3dcompiler.cpp# 代理 DLL 加载器（复制 EIEM）
│   │   ├── game_hooks.h         # SetMainCharacter hook → 捕获 Animator/Entity
│   │   └── gui_overlay.h        # ImGui + D3D11 覆盖层（复制 EIEM gui.h 思路）
│   ├── math/
│   │   ├── quat_math.h          # Vec3/Quat、Slerp、Euler 换算（纯 C++）
│   │   ├── ik_two_bone.h        # 解析式 2-bone IK（纯 C++）
│   │   └── pose_file.h          # 姿态文件 JSON 序列化（纯 C++）
│   ├── game/
│   │   ├── skeleton.h           # 骨骼列表（Humanoid 55 根 + 从骨）、拾取、快照/恢复
│   │   ├── accessory.h          # 从骨链（头发/配饰）：枚举、物理开关、锁定
│   │   ├── freeze.h             # 时间/动画/物理冻结 + 固化当前姿势
│   │   ├── ik_driver.h          # 摆姿 IK（写求解结果 或 驱动原生 BipedIK）
│   │   └── morph.h              # 形态键：BlendShape + SkeletalMorph 权重读写
│   └── editor/
│       ├── gui.h                # 主窗口调度、子面板注册、渲染循环
│       ├── panel_mode.h         # 模式切换条：摆姿模式 ↔ 镜头模式（含镜头锁定/解锁）
│       ├── gizmo.h              # ImGuizmo 手柄（旋转/移动）
│       ├── panel_pose.h         # FK/IK 面板（骨骼树、滑条）
│       ├── panel_accessory.h    # 从骨面板（链列表、物理开关、锁定、FK 滑条）
│       ├── panel_morph.h        # 形态键面板（面部+身体滑条）
│       ├── panel_camera.h       # 相机面板（FOV、景深、预设机位）
│       ├── panel_photo.h        # 截图面板（隐藏 UI、出图）
│       └── panel_library.h      # 姿态预设库（网格 + 存取）
└── tests/                       # math 层单测（g++ 可跑）
    ├── test_quat.cpp
    ├── test_ik.cpp
    └── test_pose_file.cpp
```

**分解原则:** `math/` 零依赖游戏，独立可测；`core/` 从 EIEM 复制后只做"删掉用不到的"，不改逻辑；`game/` 承载逆向与冻结逻辑；`editor/` 只做 UI。改动文件按职责聚合。

---

## 阶段 0：项目骨架与可复用基础层

### Task 0.1：搭目录 + 构建脚本

**Files:**
- Create: `poser/CMakeLists.txt`
- Create: `poser/build.bat`

- [ ] **Step 1: 写 CMakeLists.txt（插件 + 单测双目标）**

```cmake
cmake_minimum_required(VERSION 3.16)
project(endfield_poser CXX)
set(CMAKE_CXX_STANDARD 17)

# 插件 DLL（Windows/MSVC）
add_library(poser SHARED
  src/poser.cpp
  src/config.h
  src/core/proxy_d3dcompiler.cpp
  src/core/game_hooks.h
  src/core/gui_overlay.h
  src/game/skeleton.h src/game/freeze.h src/game/ik_driver.h src/game/morph.h
  src/editor/gui.h src/editor/gizmo.h src/editor/panel_pose.h src/editor/panel_morph.h
  src/editor/panel_camera.h src/editor/panel_photo.h src/editor/panel_library.h
)
target_include_directories(poser PRIVATE deps deps/imgui deps/imguizmo deps/minhook/include deps/json deps/stb)
target_compile_definitions(poser PRIVATE APPLEPIE_PLUGIN_IMPL _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
target_link_libraries(poser PRIVATE d3d11 dxgi d3dcompiler dwmapi ole32)

# 数学单测（跨平台，Linux g++ 也可编）
add_executable(poser_tests tests/test_quat.cpp tests/test_ik.cpp tests/test_pose_file.cpp src/math/quat_math.h src/math/ik_two_bone.h src/math/pose_file.h)
target_include_directories(poser_tests PRIVATE src deps/json)
enable_testing()
add_test(NAME poser_tests COMMAND poser_tests)
```

- [ ] **Step 2: 写 build.bat（Windows 一键构建 + 复制依赖）**

```bat
@echo off
setlocal
cd /d %~dp0
if not exist build mkdir build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release || exit /b 1
if not exist plugin mkdir plugin
copy /y build\Release\poser.dll plugin\poser.dll
copy /y {EIEM}\build\Release\d3dcompiler_47.dll plugin\d3dcompiler_47.dll
echo Build OK. Copy plugin\ folder next to the game executable.
```

- [ ] **Step 3: 验证单测目标可编可跑**

Run（沙箱）: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
Expected: `poser_tests` 编译通过（目前是空的 main），ctest 0 失败。

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt build.bat
git commit -m "chore: scaffold poser project with cmake + build script"
```

### Task 0.2：复制并精简 EIEM 基础件

**Files:**
- Create: `src/core/il2cpp_api.h` ← 复制 `{EIEM}/src/il2cpp_api.h`（整体）
- Create: `src/core/base.h` ← 精简 `{EIEM}/src/globals.h` 的 `Log()`、`SafeOff()`、IL2CPP 布局宏（`IL2CPP_STR_LEN` 等）
- Create: `src/core/proxy_d3dcompiler.cpp` ← 复制 `{EIEM}/src/proxy_d3dcompiler.cpp`
- Create: `src/core/gui_overlay.h` ← 精简 `{EIEM}/src/gui.h`（只保留 D3D11 设备/交换链/合成目标/ImGui 初值）

- [ ] **Step 1: 复制 il2cpp_api.h**

Run: `cp {EIEM}/src/il2cpp_api.h src/core/il2cpp_api.h`
说明：该文件是纯 API 封装（`Resolve()`、`FindClass`、`FindMethod`、`Invoke`、`il2cpp_resolve_icall`），与 EIEM 业务无关，可直接整体复用。

- [ ] **Step 2: 从 globals.h 抽取 base.h**

在 `base.h` 中放：`Log()`（含 `g_logHandle`/`g_logLock`/打开 `poser_log.txt`）、`SafeOff()`、`IL2CPP_*` 布局宏。**不要**带入任何动画/相机/形态业务全局。

- [ ] **Step 3: 复制 proxy_d3dcompiler.cpp**

Run: `cp {EIEM}/src/proxy_d3dcompiler.cpp src/core/proxy_d3dcompiler.cpp`
说明：加载 `plugin\*.dll` 的逻辑里有个对 `applepie_manager.dll` 的硬编码，保留即可（我们的插件也由它或直读配置加载）。

- [ ] **Step 4: 精简 gui.h → gui_overlay.h**

保留：`CreateGuiOverlay(hwnd)`（D3D11CreateDevice + swapchain + `CreateCompositionTarget` + `SetVisual`）与 ImGui `ImGui_ImplWin32_Init`/`ImGui_ImplDX11_Init`。删除 EIEM 特有面板代码。

- [ ] **Step 5: Commit**

```bash
git add src/core
git commit -m "chore: port base infra (il2cpp api, proxy dll, gui overlay) from eiem"
```

### Task 0.3：插件壳 poser.cpp（Applepie 协议 + 注入启动）

**Files:**
- Create: `src/poser.cpp`

- [ ] **Step 1: 写插件壳**

```cpp
// 依赖: core/base.h, core/il2cpp_api.h, core/gui_overlay.h, core/game_hooks.h
#include "core/base.h"
#include "core/il2cpp_api.h"
#include "core/gui_overlay.h"
#include "core/game_hooks.h"
#include "config.h"

static AP_PluginInfo g_info = {
    APPLEPIE_PLUGIN_API_VERSION, "poser", "Endfield Poser",
    "Game photography posing tool (FK/IK + morph keys + camera)", "plugin\\poser_config.txt", true};

extern "C" __declspec(dllexport) AP_PluginInfo* AP_GetPluginInfo() { return &g_info; }
extern "C" __declspec(dllexport) bool AP_PluginEnable()   { g_pluginActive = true;  StartPoser(); return true; }
extern "C" __declspec(dllexport) bool AP_PluginDisable()  { g_pluginActive = false; StopPoser();  return true; }
extern "C" __declspec(dllexport) bool AP_ReloadConfig()   { return LoadPoserConfig(); }
extern "C" __declspec(dllexport) int  AP_GetHotkeys(AP_HotkeyInfo* out, int max) {
    if (max < 1) return 1;
    out[0] = {"Toggle Poser GUI", "gui_toggle_key", g_guiToggleVK}; return 1;
}
extern "C" __declspec(dllexport) void AP_SetLanguage(const char*) {}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(0);
        OpenLog("plugin\\poser_log.txt");
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr); // 见 Task 2.1
    }
    return TRUE;
}
```

- [ ] **Step 2: 实现 StartPoser/StopPoser 与主循环骨架**

`StartPoser()` 启动 `GuiThread`（调 `CreateGuiOverlay` + ImGui 主循环，挂 `DrawPoserGui()`）；`StopPoser()` 通知线程退出。主循环里每帧调 `GameFrameTick()`（后续阶段填充：冻结维持、IK 写回、相机）。

- [ ] **Step 3: 编译**

Run（Windows）: `build.bat`
Expected: `plugin/poser.dll` 生成，无编译错误。

- [ ] **Step 4: `[in-game]` 验证注入与 GUI**

将 `plugin/` 复制到游戏目录，启动游戏。Expected: 按 `Insert`（或配置键）呼出 ImGui 窗口（标题 "Endfield Poser"），控制台无崩溃。

- [ ] **Step 5: Commit**

```bash
git add src/poser.cpp src/config.h
git commit -m "feat: poser plugin shell with applepie protocol + gui thread"
```

---

## 阶段 1：纯数学核心（沙箱可单测）

> 此阶段全部为平台无关 C++，在本沙箱用 g++ 完成 TDD。

### Task 1.1：quat_math.h —— 姿态数学

**Files:**
- Create: `src/math/quat_math.h`
- Test: `tests/test_quat.cpp`

- [ ] **Step 1: 写失败测试**

```cpp
#include "math/quat_math.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fails++; std::printf("FAIL: %s\n", msg); } } while (0)

int main() {
    Quat q(0.0f, 0.0f, 0.0f, 1.0f);            // identity
    Quat q90 = Quat::AxisAngle({0,1,0}, 1.5707963f); // 90° around Y
    Vec3 fwd = q90 * Vec3(0,0,1);               // 前向(0,0,1) 绕Y+90° → (1,0,0)
    CHECK(fabsf(fwd.x - 1.0f) < 1e-4f && fabsf(fwd.z) < 1e-4f, "axis-angle rotate forward");

    Quat a = Quat::AxisAngle({1,0,0}, 0.5f);
    Quat b = Quat::AxisAngle({1,0,0}, 1.5f);
    Quat mid = Quat::Slerp(a, b, 0.5f);
    Vec3 va = a * Vec3(0,1,0), vb = b * Vec3(0,1,0), vm = mid * Vec3(0,1,0);
    CHECK(vm.y > va.y && vm.y < vb.y, "slerp stays between endpoints");

    Quat d = Quat::Delta(a, b);                 // b = d * a
    Vec3 vd = d * va;
    CHECK(fabsf(vd.x - vb.x) < 1e-3f && fabsf(vd.y - vb.y) < 1e-3f, "delta composition");

    Vec3 e = q90.ToEulerDeg();
    CHECK(fabsf(e.y - 90.0f) < 0.5f, "euler round trip");
    Quat back = Quat::FromEulerDeg(e);
    CHECK(fabsf(Quat::Angle(q90, back)) < 1e-2f, "euler->quat round trip");

    std::printf(fails ? "%d FAILURES\n" : "quat_math OK\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `g++ -std=c++17 -Isrc tests/test_quat.cpp -o /tmp/tq && /tmp/tq`
Expected: 编译失败（`quat_math.h` 不存在）。

- [ ] **Step 3: 实现 quat_math.h**

```cpp
#pragma once
#include <cmath>

struct Vec3 { float x=0,y=0,z=0; };
inline Vec3 operator+(Vec3 a, Vec3 b){return{a.x+b.x,a.y+b.y,a.z+b.z};}
inline Vec3 operator-(Vec3 a, Vec3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}
inline Vec3 operator*(Vec3 a, float s){return{a.x*s,a.y*s,a.z*s};}
inline float Dot(Vec3 a, Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a, Vec3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Len(Vec3 a){return std::sqrt(Dot(a,a));}
inline Vec3 Norm(Vec3 a){float l=Len(a);return l>1e-6f?a*(1.0f/l):Vec3{};}

struct Quat { float x=0,y=0,z=0,w=1; };
inline Quat operator*(Quat a, Quat b){
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Vec3 operator*(Quat q, Vec3 v){
    Vec3 u{q.x,q.y,q.z};
    return u*(2.0f*Dot(u,v)) + v*(q.w*q.w-Dot(u,u)) + Cross(u,v)*(2.0f*q.w);
}
inline Quat Conj(Quat q){return {-q.x,-q.y,-q.z,q.w};}
inline float QuatLen(Quat q){return std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);}
inline Quat NormQ(Quat q){float l=QuatLen(q); return l>1e-6f?Quat{q.x/l,q.y/l,q.z/l,q.w/l}:Quat{};}
inline float DotQ(Quat a, Quat b){return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;}

struct Quat {
    static Quat AxisAngle(Vec3 axis, float rad){
        float h=rad*0.5f, s=std::sin(h); Vec3 n=Norm(axis);
        return {n.x*s,n.y*s,n.z*s,std::cos(h)};
    }
    static Quat FromEulerDeg(Vec3 deg){   // YXZ 顺序（Unity 惯例）
        const float D2R=3.14159265358979f/180.0f;
        Vec3 r{deg.x*D2R*0.5f, deg.y*D2R*0.5f, deg.z*D2R*0.5f};
        float cx=std::cos(r.x),sx=std::sin(r.x),cy=std::cos(r.y),sy=std::sin(r.y),cz=std::cos(r.z),sz=std::sin(r.z);
        return {sx*cy*cz+cx*sy*sz, cx*sy*cz-sx*cy*sz, cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz};
    }
    Vec3 ToEulerDeg() const {              // 与 FromEulerDeg 互逆（YXZ）
        const float R2D=180.0f/3.14159265358979f;
        float sp=2.0f*(w*y - x*z); sp = sp>1.0f?1.0f:(sp<-1.0f?-1.0f:sp);
        float rx=std::atan2(2.0f*(w*x+y*z), 1.0f-2.0f*(x*x+y*y));
        float ry=std::asin(sp);
        float rz=std::atan2(2.0f*(w*z+x*y), 1.0f-2.0f*(y*y+z*z));
        return {rx*R2D, ry*R2D, rz*R2D};
    }
    static Quat Slerp(Quat a, Quat b, float t){
        float d=DotQ(a,b); if (d<0){b={-b.x,-b.y,-b.z,-b.w}; d=-d;}
        if (d>0.9995f) { Quat r{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t}; return NormQ(r); }
        float th=std::acos(d), sth=std::sin(th);
        float s0=std::sin((1-t)*th)/sth, s1=std::sin(t*th)/sth;
        return {a.x*s0+b.x*s1, a.y*s0+b.y*s1, a.z*s0+b.z*s1, a.w*s0+b.w*s1};
    }
    static Quat Delta(Quat from, Quat to){ return NormQ(Conj(from) * to); } // to = Delta(from,to) * from
    static float Angle(Quat a, Quat b){ float d=std::fabs(DotQ(a,b)); if(d>1)d=1; return 2.0f*std::acos(d); }
};
```

> 注：上面的 `struct Quat` 出现两次会冲突——实现时只保留第二个（含静态方法）定义即可；测试只依赖公开接口。

- [ ] **Step 4: 运行确认通过**

Run: `g++ -std=c++17 -Isrc tests/test_quat.cpp -o /tmp/tq && /tmp/tq`
Expected: `quat_math OK`，退出码 0。

- [ ] **Step 5: Commit**

```bash
git add src/math/quat_math.h tests/test_quat.cpp
git commit -m "feat(math): quaternion + vec3 with slerp/euler, TDD"
```

### Task 1.2：ik_two_bone.h —— 解析式 2-bone IK

**Files:**
- Create: `src/math/ik_two_bone.h`
- Test: `tests/test_ik.cpp`

- [ ] **Step 1: 写失败测试**

```cpp
#include "math/ik_two_bone.h"
#include <cmath>
#include <cstdio>
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){fails++;std::printf("FAIL: %s\n",m);} }while(0)

int main(){
    // 上臂/前臂各 0.5，平伸目标 (1,0,0)
    Vec3 a{0,0,0}, b{0.5f,0,0}, c{1.0f,0,0};
    Vec3 target{0.7f, 0.6f, 0.0f};           // 目标点距原点 0.92 < 1.0，可解
    SolveTwoBone(a,b,c,target,{0,0,1}, true);
    float err = Len(c - target);
    CHECK(err < 1e-3f, "end effector reaches target");
    CHECK(b.x>=0.0f && fabsf(b.y)<1e-4f, "bend direction respected (pole)");

    // 不可达目标（比两段还长）→ 应伸直
    Vec3 far{3.0f,0,0};
    SolveTwoBone(a,b,c,far,{0,0,1}, true);
    CHECK(Len(c-far) < 1e-2f, "unreachable: fully extended toward target");

    std::printf(fails?"%d FAILURES\n":"ik_two_bone OK\n", fails);
    return fails?1:0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `g++ -std=c++17 -Isrc tests/test_ik.cpp -o /tmp/ti && /tmp/ti`
Expected: 编译失败（头不存在）。

- [ ] **Step 3: 实现 ik_two_bone.h**

```cpp
#pragma once
#include "math/quat_math.h"
#include <cmath>

// 标准解析式 2-bone IK。a=根, b=中间(肘), c=末端。target=期望末端位置。pole=弯曲方向（上/外）。
// 直接原地改写 a/b/c 的坐标（供测试）；游戏内由调用方转成每根骨 localRotation。
inline void SolveTwoBone(Vec3& a, Vec3& b, Vec3& c, Vec3 target, Vec3 pole, bool enforcePole){
    Vec3 ab = Norm(b-a), bc = Norm(c-b);
    float lab = Len(b-a), lbc = Len(c-b);
    Vec3 at = Norm(target-a);
    float d = std::fmin(Len(target-a), lab+lbc-1e-4f);

    // 弯折角（余弦定理）
    float cos1 = (lab*lab + d*d - lbc*lbc) / (2.0f*lab*d);
    cos1 = cos1>1.0f?1.0f:(cos1<-1.0f?-1.0f:cos1);
    float ang1 = std::acos(cos1);            // 根关节需弯多少
    float cos2 = (lab*lab + lbc*lbc - d*d) / (2.0f*lab*lbc);
    cos2 = cos2>1.0f?1.0f:(cos2<-1.0f?-1.0f:cos2);
    float ang2 = std::acos(cos2);            // 肘关节需弯多少

    // 以目标方向为基准轴
    Vec3 baseAxis = Norm(Cross(Vec3{0,0,1}, at));   // 默认上方向为 pole
    Vec3 poleDir  = Norm(pole - a);
    Vec3 bendAxis = Norm(Cross(at, poleDir));       // 由 pole 决定弯向
    if (Len(bendAxis) < 1e-4f) bendAxis = baseAxis;
    if (enforcePole && Len(Cross(at, poleDir)) < 1e-3f) { /* degenerate: keep current plane */ }

    Quat rootRot = Quat::AxisAngle(bendAxis, ang1);
    b = a + rootRot * (ab * lab);

    Quat elbowRot = Quat::AxisAngle(bendAxis, ang2);
    c = b + (elbowRot * bc) * lbc;
}
```

- [ ] **Step 4: 运行确认通过**

Run: `g++ -std=c++17 -Isrc tests/test_ik.cpp -o /tmp/ti && /tmp/ti`
Expected: `ik_two_bone OK`，退出码 0。

- [ ] **Step 5: Commit**

```bash
git add src/math/ik_two_bone.h tests/test_ik.cpp
git commit -m "feat(math): analytic two-bone IK solver, TDD"
```

### Task 1.3：pose_file.h —— 姿态文件 JSON 序列化

**Files:**
- Create: `src/math/pose_file.h`
- Test: `tests/test_pose_file.cpp`
- Dep: `deps/json/json.hpp`（nlohmann 单头，从 `https://github.com/nlohmann/json/releases` 下载 v3.11.x 放入）

- [ ] **Step 1: 写失败测试**

```cpp
#include "math/pose_file.h"
#include <cstdio>
#include <string>
static int fails=0;
#define CHECK(c,m) do{ if(!(c)){fails++;std::printf("FAIL: %s\n",m);} }while(0)

int main(){
    PoseDoc doc;
    doc.name="test_pose";
    doc.morphs.push_back({"Mouth_A", 0.6f});
    doc.bones.push_back({"spine_01", {0,0,0}, {0,0,0,1}});
    doc.bones.push_back({"arm_R_01", {0.2f,1,0}, {0,0,0.707f,0.707f}});

    std::string json = PoseToJson(doc);
    PoseDoc back = PoseFromJson(json);
    CHECK(back.name==doc.name, "name round trip");
    CHECK(back.bones.size()==2, "bone count round trip");
    CHECK(back.bones[1].name=="arm_R_01", "bone name round trip");
    CHECK(fabsf(back.bones[1].rot.z-0.707f)<1e-3f, "rotation round trip");
    CHECK(back.morphs[0].value==0.6f, "morph round trip");
    std::printf(fails?"%d FAILURES\n":"pose_file OK\n", fails);
    return fails?1:0;
}
```

- [ ] **Step 2: 运行确认失败**

Run: `g++ -std=c++17 -Isrc -Ideps/json tests/test_pose_file.cpp -o /tmp/tp && /tmp/tp`
Expected: 编译失败（`pose_file.h` 不存在）。

- [ ] **Step 3: 实现 pose_file.h**

```cpp
#pragma once
#include "math/quat_math.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct PoseBone { std::string name; Vec3 pos; Quat rot; };
struct PoseMorph { std::string name; float value; };

struct PoseDoc {
    std::string name;
    std::vector<PoseBone> bones;
    std::vector<PoseMorph> morphs;
};

inline std::string PoseToJson(const PoseDoc& d){
    nlohmann::json j;
    j["name"] = d.name;
    for (auto& b : d.bones)
        j["bones"].push_back({{"n",b.name},{"p",{b.pos.x,b.pos.y,b.pos.z}},
                              {"r",{b.rot.x,b.rot.y,b.rot.z,b.rot.w}}});
    for (auto& m : d.morphs)
        j["morphs"].push_back({{"n",m.name},{"v",m.value}});
    return j.dump(2);
}

inline PoseDoc PoseFromJson(const std::string& s){
    PoseDoc d;
    auto j = nlohmann::json::parse(s);
    d.name = j.value("name", "");
    if (j.contains("bones"))
        for (auto& e : j["bones"]) {
            PoseBone b; b.name = e["n"].get<std::string>();
            b.pos = {e["p"][0].get<float>(), e["p"][1].get<float>(), e["p"][2].get<float>()};
            b.rot = {e["r"][0].get<float>(), e["r"][1].get<float>(), e["r"][2].get<float>(), e["r"][3].get<float>()};
            d.bones.push_back(b);
        }
    if (j.contains("morphs"))
        for (auto& e : j["morphs"])
            d.morphs.push_back({e["n"].get<std::string>(), e["v"].get<float>()});
    return d;
}
```

- [ ] **Step 4: 运行确认通过**

Run: `g++ -std=c++17 -Isrc -Ideps/json tests/test_pose_file.cpp -o /tmp/tp && /tmp/tp`
Expected: `pose_file OK`，退出码 0。

- [ ] **Step 5: Commit**

```bash
git add src/math/pose_file.h tests/test_pose_file.cpp deps/json/json.hpp
git commit -m "feat(math): pose file json serialization, TDD"
```

- [ ] **Step 6: `[in-game]` 附带验证**：阶段 3 的存取复用此格式，无需单独验证。

---

## 阶段 2：角色捕获与时间冻结

### Task 2.1：game_hooks.h —— 捕获角色 Animator/Entity

**Files:**
- Create: `src/core/game_hooks.h`
- Reference: `{EIEM}/src/init.h` 中 `PlayerController.SetMainCharacter` hook（约 L985-L1121）

- [x] **Step 1: 移植 SetMainCharacter hook**

结构：`ResolveGameApi()`（解析 `g_animator_*`、`g_transform_*`、`g_smr_*`、`g_camera_*` 等，参照 `{EIEM}` 的 init 段）→ MinHook 挂 `PlayerController.SetMainCharacter`（或当版本变化的等价方法）→ hook 里从参数提取 Entity，经 `OFF_entityComplexAnim`/`OFF_complexAnimAnimator` 拿到 `Animator`，存入 `g_charAnimator`，触发 `g_charChanged=true`。**已完成**：`src/core/game_hooks.h` 实现 `ResolveGameApi()`（Animator/Transform/Object/Component 方法）、`InstallSetMainCharacterHook()`（含 `TryCaptureFromPlayerController()` 补捞已就绪角色）、`ResolveEntityOffsets()` 懒解析两级偏移。

- [x] **Step 2: 写骨骼句柄封装（供 FK 层用）**

在 `game_hooks.h` 暴露：
```cpp
void* GetHumanoidBone(HumanBodyBones bone);          // g_animator_GetBoneTransform(g_charAnimator, bone)
Quat  GetBoneLocalRot(void* t);                      // SafeGetLocalRotation
void  SetBoneLocalRot(void* t, Quat q);              // SafeSetLocalRotation
Vec3  GetBoneLocalPos(void* t);
void  SetBoneLocalPos(void* t, Vec3 p);
```
（另含 `SafeGetComponentTransform`/`GetCharRootTransform`/`GetBoneName`；`HumanBodyBones` 枚举与 55 根名称表已内建。）

- [ ] **Step 3: `[in-game]` 验证捕获**

启动游戏 → 加载角色/进入战斗场景 → 日志出现 `[POSER] CharAnimator=%p`。切换角色时该指针随变化。

- [ ] **Step 4: Commit**

```bash
git add src/core/game_hooks.h
git commit -m "feat(game): capture character animator via SetMainCharacter hook"
```

### Task 2.2：freeze.h —— 冻结系统

**Files:**
- Create: `src/game/freeze.h`

- [x] **Step 1: 实现基础冻结**

```cpp
static bool g_frozen=false;
static bool g_animatorWasEnabled=false;

void FreezeCharacter(){
    if (!g_charAnimator || g_frozen) return;
    g_animatorWasEnabled = g_animator_get_enabled ? Invoke(g_animator_get_enabled, g_charAnimator) != nullptr : true;
    if (g_animator_set_enabled) { int v=0; Invoke(g_animator_set_enabled, g_charAnimator, {&v}); }
    // 记录一帧全骨骼 local 到 g_poseSnapshot（skeleton.h 提供），保证后续编辑基于当前帧
    CapturePoseSnapshot();
    g_frozen=true; Log("[POSER] Frozen");
}
void UnfreezeCharacter(){
    if (!g_frozen) return;
    if (g_animator_set_enabled && g_animatorWasEnabled) { int v=1; Invoke(g_animator_set_enabled, g_charAnimator, {&v}); }
    RestoreGameState();   // 恢复动画/时间/相机（Task 5 相机接管会注册恢复回调）
    g_frozen=false; Log("[POSER] Unfrozen");
}
```
**已完成**：`src/game/freeze.h` 实现 `FreezeCharacter()`/`UnfreezeCharacter()`（记录并关闭/恢复 Animator.enabled + `PinCurrentPose()` 固化基线），并留出 `SuppressPoseWriters()`/`RestorePoseWriters()` 供后续 Task（IK/形态/布料）追加抑制逻辑。**关键设计**：冻结不做每帧快照重写，否则 FK 拖骨会被打回——直接关 Animator 即让姿势稳定。

- [ ] **Step 2: 调研并补全"场景级冻结"（探针任务）**

`[in-game]` 用 `Probe`（参照 `{EIEM}` 的 `DumpClassFields`/`ListComponentsOnGameObject` 套路）逐项确认并补全冻结目标，每项写入日志验证：
1. `Time.timeScale = 0`（`UnityEngine.Time` icall）——影响全局时间、物理与动画。注意测试是否会冻结 UI 交互/渲染。
2. 角色根 GameObject 上的 `Animator`、`SkeletalMorphCore`（写 `m_allMorphBoneDirty` 为 false 并跳过 Update）、`RootMotion.FinalIK` 组件（`BipedIK`/`Grounder`/`LookAt`——把 `IKSolver` 的 weight 写 0 或禁用组件，参照 `{EIEM}` `s_bipedIK`/`s_grounderIK` 收集逻辑）。
3. 布料 `BuildAndRun`/`SetTimeScale`（参照 `{EIEM}` `s_bbc_*` 钩子，把时间缩放写 0）；从骨动态骨骼（DynamicBone/Cloth 等组件）的禁用联动见 Task 2.4。
4. 粒子/特效：若角色挂 `ParticleSystem`，暂停主模块（`ParticleSystem.Pause`）。
5. 其他 NPC/背景：整场景冻结靠 `Time.timeScale=0` 覆盖；若它不可行，退化为只冻角色 + 文档说明局限。

- [ ] **Step 3: `[in-game]` 验证冻结完整**

冻结后：角色完全静止（含布料、表情、衣角）、背景 NPC 可选静止、截图无模糊/抖动。逐项在日志核对上述 1-5 命中情况。

- [ ] **Step 4: Commit**

```bash
git add src/game/freeze.h
git commit -m "feat(game): character freeze (animator + time + ik + cloth)"
```

### Task 2.3：骨架快照与固化姿势

**Files:**
- Create: `src/game/skeleton.h`

- [x] **Step 1: 实现骨骼列表 + 快照/恢复**

- 枚举 `HumanBodyBones`（55 根）经 `GetHumanoidBone` 收集到 `s_bones[]`（name + transform）。**已完成**：`src/game/skeleton.h` 定义 `BoneHandle`（humanBone/transform/name/locked/localPos/localRot）与 `s_humanBones[]`；`RebuildHumanBones()` 在角色切换时重建（含锁定标记，Task 2.4 用）；`CapturePoseSnapshot()`/`ApplyPoseSnapshot()`（跳过锁定骨）/`PinCurrentPose()`/`ApplyTPose()`/`SkeletonFrameTick()`/`FindHumanBoneIndex()` 均已实现。

- [ ] **Step 2: `[in-game]` 验证**

冻结后 `PinCurrentPose()` → 日志打印 55 根骨名称与数值；T-pose 角色应显示 0 旋转。

- [ ] **Step 3: Commit**

```bash
git add src/game/skeleton.h
git commit -m "feat(game): skeleton bone list + pose snapshot/restore"
```

### Task 2.4：从骨（头发/配饰）采集 + 物理开关 + 锁定

**Files:**
- Create: `src/game/accessory.h`
- Modify: `src/game/freeze.h`（物理禁用联动）
- Modify: `src/editor/panel_accessory.h`

> 从骨 = 除 Humanoid 55 根外、角色根下其余所有骨骼（头发/帽饰/飘带/衣角/挂件等），通常是 DynamicBone / 程序化布料驱动的链。

- [x] **Step 1: 枚举从骨链**

从 `g_charAnimator` 根 Transform 递归 `GetChild` 遍历全部子骨；对每条**非 Humanoid** 分支按"父子连续链"分组（如 `hair_01 → hair_02 → …`）。每条链记录：根骨、链上全部 bone name、所在 GameObject 上挂的物理组件（`DynamicBone`/`CommonDynamicBone`/`Cloth`/`MagicaCloth` 等，探针确认实际组件类名）。输出到日志 `[ACCESSORY]` 与面板列表。**已完成**：`src/game/accessory.h` 实现 `RebuildAccessories()`（`WalkTransforms` 递归遍历非 Humanoid 子骨 → 按父链分组为 `s_accessoryChains`/`s_accessoryBones`，`CollectPhysicsComponents` 用 `GameObject.GetComponents(Component)` 枚举类名命中 `kPhysicsClassSubstrings` 的组件）。

- [x] **Step 2: 逐链/逐骨物理开关**

对每条链（或单骨）提供 `SetPhysicsEnabled(chain, bool)`：
- 方案 A（首选）：`Component.enabled = false` 禁用 DynamicBone/Cloth 组件（探针确认字段/方法），物理不再每帧写骨。
- 方案 B（兜底）：每帧在冻结逻辑里把该链骨 local rot/pos 钉在快照值（等价"物理禁写"）。
开关状态随冻结态生效；解冻时恢复原 enabled。**已完成**：`SetPhysicsEnabled()`/`SetAllPhysicsEnabled()` 用 `Behaviour.set_enabled` 开关链上各骨的物理组件；`freeze.h` 的 `SuppressPoseWriters()`/`RestorePoseWriters()` 分别调 `SetAllPhysicsEnabled(false/true)` 联动。

- [x] **Step 3: 锁定**

每个从骨提供 `locked` 标记：锁定时 `ApplyPoseSnapshot`/FK/IK/镜像等**跳过该骨**，且物理开关置"禁用写回"。默认行为：锁定的骨永远保持冻结帧姿态。**已完成**：`SetAccessoryBoneLocked()`（锁定瞬间 `localPos/localRot` 钉住当前值）/`SetChainLocked()`（整链锁定），`ApplyAccessorySnapshot()` 跳过锁定骨。

- [ ] **Step 4: `[in-game]` 验证**

关物理后头发/飘带不再摆动、可被 FK 自由摆弄；解锁后解冻恢复物理；锁定骨不被姿态操作改动。

- [ ] **Step 5: Commit**

```bash
git add src/game/accessory.h src/game/freeze.h src/editor/panel_accessory.h
git commit -m "feat(game): accessory bone chains with physics toggle + lock"
```

---

## 阶段 3：FK/IK 姿态编辑

### Task 3.1：FK 编辑（ImGuizmo 手柄 + 滑条）

**Files:**
- Create: `src/editor/gizmo.h`（ImGuizmo 集成）
- Modify: `src/editor/panel_pose.h`（骨骼树 + 旋转滑条）
- Dep: `deps/imguizmo/`（从 https://github.com/CedricGuillemet/ImGuizmo 拉取 `ImGuizmo.h/.cpp`，与 imgui 同目录编译）

- [x] **Step 1: 集成 ImGuizmo**

`gizmo.h` 提供 `DrawGizmo(drawList, viewProj, boneWorldMat)`：对当前选中骨用 `ImGuizmo::Manipulate` 旋转/移动，把 delta 转成 `SetBoneLocalRot`（用 `Quat::Delta(旧, 新)` 得到相对旋转，叠加到局部旋转）。**已完成**：`src/editor/gizmo.h` 提供列主序矩阵辅助（`Mat4Compose`/`Mat4Decompose`/`Mat4Mul`，沙箱已单测往返）、`GetBoneWorldMatrix()`（沿父链组合世界矩阵）、`GetCameraViewProj()`（由主相机 `Transform.get_position/get_rotation` + `Camera.get_fieldOfView` 构建 view/proj，`game_hooks.h` 已补 `g_transform_get_rotation`/`g_camera_get_main`/`g_camera_get_fieldOfView`）、`DrawBoneGizmo()`（LOCAL 模式，`local' = local * deltaRot` 右乘叠加）。`game_hooks.h` 另补世界位姿读取 `GetBoneWorldPos/GetBoneWorldRot`（IK 驱动也依赖）。

- [x] **Step 2: FK 面板**

`panel_pose.h`：骨骼树（按 HumanBodyBones 分组：身体/头/左臂/右臂/左腿/右腿/手指）+ 选中项的三个 Euler 滑条（`ToEulerDeg`/`FromEulerDeg` 实时写回）。手指用细分滑条页。**已完成**：`src/editor/panel_pose.h` 实现 `DrawPosePanel()`（`kBoneGroups` 八组树 + 选中骨 X/Y/Z Euler 滑条实时回读、局部位置滑条、逐骨锁定复选框）+ `DrawPoseGizmoOverlay()`（冻结时 gizmo 叠加层）。`poser.cpp` 主面板新增"姿态 (FK)"窗口并接入 gizmo 覆盖层；`CMakeLists.txt` 加入 editor 头与可选的 `deps/imguizmo/ImGuizmo.cpp`。

- [ ] **Step 3: `[in-game]` 验证**

冻结后：点骨骼 → 拖手柄可转；滑条可微调；手指 30 根均可动；其余骨不抖动。

- [ ] **Step 4: Commit**

```bash
git add src/editor/gizmo.h src/editor/panel_pose.h deps/imguizmo
git commit -m "feat(editor): fk editing via imguizmo + sliders"
```

### Task 3.2：IK 编辑（两种驱动方式）

**Files:**
- Create: `src/game/ik_driver.h`
- Modify: `src/editor/panel_pose.h`

- [x] **Step 1: 实现 2-bone IK 驱动**

对选中"手/脚"端点：`ik_driver.h` 收集 根→中→末端 三根骨的世界坐标（`GetBoneLocalPos` + 父级链相乘，或直接用 `Transform.GetPosition`/`GetRotation` icall），调 `SolveTwoBone`，再把 a/b 的旋转差写回各自局部旋转（`Quat::Delta(当前局部, 目标局部)`）。**已完成**：`src/game/ik_driver.h` 定义四肢链（`kIkChains`：左/右臂 = UpperArm→LowerArm→Hand，左/右腿 = UpperLeg→LowerLeg→Foot）+ 状态（`g_ikActive`/`g_ikNative`/`g_ikChain`/`g_ikTarget`）。`SolveTwoBoneChain()` 每帧读取世界坐标 → `SolveTwoBone`（pole 取当前肘/膝，保证弯折方向不翻转）→ 用 `Quat::FromTo`（`quat_math.h` 新增，已单测）求根/中骨方向差 → `ApplyWorldRotDelta()` 把世界旋转增量经 `localDelta = conj(parentWorld)*d*parentWorld` 折算写回局部旋转。`IkFrameTick()` 冻结态每帧驱动（根/中骨被锁定则跳过）。`panel_pose.h` 新增 IK 区（链下拉 + 启用/原生开关 + 目标读数 + "还原目标"），IK 模式下 gizmo 改拖目标点（`DrawIkTargetGizmo`，WORLD 平移）。`poser.cpp` 的 `GameFrameTick` 每帧调 `IkFrameTick`，角色切换时置 `g_ikTargetValid=false`。数学层已用纯 C++ 模拟驱动链路单测通过（test_ik：deltas rotate chain to reach target）。

- [x] **Step 2: 驱动原生 BipedIK（可选增强，复用 EIEM 偏移表）**

利用 `{EIEM}/globals.h` 的 `OFF_BIPEDIK_SOLVERS`/`OFF_IKSOLVER_IKPOS`/`OFF_IKTRIG_TARGET` 偏移，把末端目标 transform 拖到 gizmo 位置、weight 写 1，让游戏自带求解器接力。作为 mode 开关（自研解算 / 原生解算）。**已完成（骨架）**：`ik_driver.h` 的 `CollectBipedIK()`（角色根 `GetComponents(Component)` 按类名含 "BipedIK" 匹配，解析 `solvers` 字段）、`ResolveNativeLimbSolver()`（按当前链解析 `leftArm/rightArm/leftLeg/rightLeg` 的 `IKPosition`/`IKPositionWeight` 偏移，`SafeOff` 兜底）、`NativeIkFrameTick()`（把 `g_ikTarget` 写入 `IKPosition`、权重置 1）。偏移与类名依赖 `[in-game]` 探针收敛（计划已在自评标注此风险）。

- [ ] **Step 3: `[in-game]` 验证**

选中右手 → 拖 gizmo → 手臂整体跟随、肘部自然弯曲；双腿可独立"落位"（站姿/坐姿）。

- [ ] **Step 4: Commit**

```bash
git add src/game/ik_driver.h src/editor/panel_pose.h src/math/quat_math.h tests/test_quat.cpp tests/test_ik.cpp
git commit -m "feat(editor): ik posing via two-bone solver (+ native bipedik mode)"
```

### Task 3.3：姿态操作与存取

**Files:**
- Modify: `src/editor/panel_pose.h`
- Modify: `src/editor/panel_library.h`

- [ ] **Step 1: 实现姿态操作**

- 重置：`ApplyPoseSnapshot()`（回到冻结帧）
- T-pose：所有骨 local rot 置 0
- 复制/粘贴：当前选中骨或全骨 → `PoseDoc`
- 左右镜像：对 `arm_R_01`↔`arm_L_01` 等对称对，`rot` 取镜像（x/y/z 符号翻转，用 `Quat::Delta` 相对对称轴换算）

- [ ] **Step 2: 预设库面板**

`panel_library.h`：网格展示 `plugin/poses/*.poser.json`，点选加载/保存/覆盖/删除（复用 `pose_file.h`）。

- [ ] **Step 3: `[in-game]` 验证**

摆好姿势 → 保存 → 重置 → 加载 → 完全还原；镜像后左右对称。

- [ ] **Step 4: Commit**

```bash
git add src/editor/panel_library.h
git commit -m "feat(editor): pose operations (reset/tpose/copy/mirror) + library"
```

### Task 3.4：双模式——摆姿模式 ↔ 镜头模式

**Files:**
- Create: `src/editor/panel_mode.h`（模式状态机 + 顶栏切换条）
- Modify: `src/editor/gui.h`（按模式调度输入/面板/相机）
- Modify: `src/editor/panel_camera.h`（镜头锁定/解锁）
- Modify: `src/editor/panel_pose.h`（摆姿模式下相机固定）

> 类比 Blender：Pose Mode 里相机保持固定、专注摆角色；也可临时切到相机视角微调取景。两个模式一键互切。

- [x] **Step 1: 模式状态机**

```cpp
enum class Mode { Pose, Camera };
static Mode g_mode = Mode::Pose;
// Pose 模式：相机锁定在当前位置（不响应 WSAD/鼠标转向），鼠标/手柄全用于选骨与摆姿
// Camera 模式：自由相机接管（WSAD + 鼠标转向 + 滚轮缩放），面板显示 FOV/景深/机位
```
顶栏 `panel_mode.h` 放两个按钮/热键（默认 `Tab` 或配置键）切换，`g_mode` 变化时通知 freeze/相机模块。**已完成**：`src/editor/panel_mode.h` 定义 `PoserMode{Pose,Camera}`/`g_mode`/`DrawModeBar()`（顶栏双按钮 + Tab 边沿触发）、`SetMode()`（切 Camera 记忆机位，切回 Pose 恢复并锁定）。

- [x] **Step 2: 摆姿模式相机固定**

Pose 模式下每帧 `ApplyFreeCamera` 跳过（相机 transform 不被写），仅在用户显式"解锁镜头"（拖相机 gizmo 或切到 Camera 模式）时才写。切回 Pose 时记忆上次相机位姿。**已完成**：`ModeFrameTick()` 仅在 `Camera` 或 `Pose&&!g_camLocked` 时调 `ApplyFreeCamera`；Pose 模式默认 `g_camLocked=true`，顶栏提供"锁定镜头"开关；切回 Pose 时 `SetMode` 恢复 `g_camSavedPos/Rot`。

- [x] **Step 3: Camera 模式角色不动**

Camera 模式下 gizmo 隐藏/停用（避免误改骨骼），输入全给相机；角色保持冻结帧姿势。**已完成**：`panel_pose.h` 的 `DrawPoseGizmoOverlay()` 开头 `if (!InPoseMode()) return;`。

- [ ] **Step 4: `[in-game]` 验证**

Pose 模式拖骨时相机纹丝不动；切 Camera 模式飞行取景角色不动；再切回 Pose 继续摆姿、相机回到原位。

- [ ] **Step 5: Commit**

```bash
git add src/editor/panel_mode.h src/editor/gui.h src/editor/panel_camera.h src/editor/panel_pose.h
git commit -m "feat(editor): pose/camera dual modes with camera lock"
```

---

## 阶段 4：形态键

### Task 4.1：面部 BlendShape 面板

**Files:**
- Create: `src/game/morph.h`（BlendShape 部分）
- Modify: `src/editor/panel_morph.h`

- [x] **Step 1: 枚举并读写 BlendShape**

从 `g_charAnimator` 根下找 `SkinnedMeshRenderer`（复用 `{EIEM}` 的 `g_smr_get_sharedMesh`/`g_mesh_get_blendShapeCount`/`g_mesh_GetBlendShapeName`/`g_smr_SetBlendShapeWeight`），枚举全部名称到面板，滑条 0-100 实时 `SetBlendShapeWeight`。冻结态下直接写生效（不受动画覆盖）。**已完成**：`game_hooks.h` 补 `SkinnedMeshRenderer/Mesh` 方法解析；`src/game/morph.h` 实现 `WalkForBlendShapes()`（递归收集全角色 SMR 的 BlendShape，含原始权重）+ `SetBlendShapeWeight()`/`RestoreBlendShapes()`；`src/editor/panel_morph.h` 按网格分组滑条 + 搜索过滤 + 恢复原始；`poser.cpp` 角色切换时 `RebuildBlendShapes()`、解冻时恢复。

- [ ] **Step 2: `[in-game]` 验证**

面板列出 AIUEO/眨眼/眉毛等滑条，拖动立即生效。

- [ ] **Step 3: Commit**

```bash
git add src/game/morph.h src/editor/panel_morph.h
git commit -m "feat(morph): blend shape panel (face expressions)"
```

### Task 4.2：身体骨骼形态键（SkeletalMorph）

**Files:**
- Modify: `src/game/morph.h`
- Reference: `{EIEM}/src/smc_face.h` 的偏移解析 + `{EIEM}/globals.h` 的 `OFF_*` 表

- [ ] **Step 1: 复用 SMC 结构解析（探针）**

复用 `ResolveSMCOffsets`/`ResolveMouthShapes` 的 hashmap 解析思路，遍历 `m_allMorphs` 与 `morphMappingNames`，把**全部**形态（含身体：胸/腰/身高类）的名称 dump 出来（`{EIEM}` 已在 `[MORPH-DUMP]` 打出带骨骼 delta 的形态列表）。

- [ ] **Step 2: 实现形态权重写回（研究任务，逐步收敛）**

目标：找到"按 nameHash 设置权重 → 触发 morph-to-bone job 重算"的入口。候选路径按顺序验证：
1. `m_morphNameHashToMorphData`（`OFF_nativeHashMap`）value 结构里找权重 float 字段，写入后置 `m_allMorphBoneDirty=true`。
2. 若不行，找 `SkeletalMorphCore` 暴露的 `SetMorphWeight(int morphId, float)` 或 `ApplyMorph(int,float)` 方法（`FindMethod`）。
3. 若都没有，退化为"复用 EIEM 的大表覆盖法"：捕获全量 `MorphBoneEntry`，按形态比例叠加 delta（代价高、精确度低，仅作兜底）。
每步以 `[MORPH]` 日志 + 游戏内肉眼确认，收敛到能稳定生效的方案。

- [ ] **Step 3: `[in-game]` 验证**

身体形态滑条（例如胸部/腰围/身高）拖动后角色体型实时变化；与面部 BlendShape 互不干扰；解冻后恢复游戏原状。

- [ ] **Step 4: Commit**

```bash
git add src/game/morph.h
git commit -m "feat(morph): skeletal morph panel (body shape)"
```

---

## 阶段 5：摄影模块

### Task 5.1：自由相机

**Files:**
- Create: `src/editor/panel_camera.h`
- Reference: `{EIEM}/src/camera_control.h`（`ResolveMainCamera`/`CaptureAndDisableCinemachine`/`RestoreCinemachine`）

- [x] **Step 1: 相机接管**

复用 `camera_control.h` 的接管逻辑：禁用 `CinemachineBrain`，记录原 FOV。新增自由相机模式：WSAD 平移 + 鼠标转向/滚轮缩放（每帧 `ApplyFreeCamera()` 直接写主相机 transform，参照其 `g_nativeSetPos/SetRot` 手法；配合 `Transform` icall 钩子防止游戏覆盖，见 `{EIEM}` `Hook_SetPos` 等）。**已完成**：`game_hooks.h` 补 `Transform.set_position/set_rotation` 与 `Camera.set_fieldOfView`；`src/editor/panel_camera.h` 实现 `ResolveMainCamera()`（找主相机 + 枚举 GameObject 组件识别 `CinemachineBrain`）、`CameraTakeover(bool)`（禁用/启用 brain、记录/还原 FOV）；`poser.cpp` 冻结→`CameraTakeover(true)`、解冻→`false`。自由相机移动在 `panel_mode.h` 的 `ApplyFreeCamera()`。

- [ ] **Step 2: `[in-game]` 验证**

冻结后相机自由飞行不被打回；解冻后相机恢复原状态（`RestoreCinemachine`）。

- [ ] **Step 3: Commit**

```bash
git add src/editor/panel_camera.h
git commit -m "feat(photo): free camera with game takeover"
```

### Task 5.2：FOV 与景深

- [x] **Step 1: FOV 滑条**

复用 `g_camera_set_fieldOfView`（`{EIEM}` `globals.h`），滑条 20-90°。**已完成**：`panel_camera.h` 的 `DrawCameraPanel()` 提供 FOV 滑条（20-90°，实时写）+ 还原按钮 + 相机速度滑条 + 机位记录/返回。

- [ ] **Step 2: 景深（探针）**

`[in-game]` 探测主相机上的 DOF 组件（`PostProcessing`/`URP` Volume 的 `DepthOfField` 或引擎自研组件），用 `FindMethod`/字段偏移写 `focusDistance`/`aperture`。若为 URP Volume，可找 `Volume.weight` 或参数覆盖。找不到则此特性标注"受限"并跳过，不阻塞其他功能。

- [ ] **Step 3: Commit**

```bash
git add src/editor/panel_camera.h
git commit -m "feat(photo): fov slider + depth of field probe"
```

### Task 5.3：截图（隐藏 UI + 出图）

**Files:**
- Create: `src/editor/panel_photo.h`
- Dep: `deps/stb/stb_image_write.h`

- [ ] **Step 1: 实现截图**

`[in-game]` 流程：隐藏本插件 GUI（`g_guiVisible=false`）→ 等 1 帧（`WM_MMD_GUI_*` 式消息或直接 `Sleep`+`Present` 前拦截）→ 读 swapchain backbuffer（`GetBuffer`/`Map`）→ `stbi_write_png` 写出 `plugin/screenshots/yyyymmdd_hhmmss.png`。D3D11 设备/交换链句柄由 `gui_overlay.h` 提供。

- [ ] **Step 2: `[in-game]` 验证**

截图不含 GUI、分辨率与窗口一致、PNG 可正常打开。

- [ ] **Step 3: Commit**

```bash
git add src/editor/panel_photo.h deps/stb/stb_image_write.h
git commit -m "feat(photo): screenshot with ui-hide + backbuffer png"
```

---

## 阶段 6：打磨

### Task 6.1：配置与快捷键

- [ ] **Step 1: `config.h` 读写 `poser_config.txt`**

键：`gui_toggle_key`（默认 `VK_INSERT`）、`screenshot_key`（默认 `VK_F8`）、`camera_speed`、`default_pose_dir`。参照 `{EIEM}/src/eiem_config.h` 的解析套路，`AP_ReloadConfig` 触发重载。

- [ ] **Step 2: `[in-game]` 验证**：改配置热键后立即生效。

- [ ] **Step 3: Commit**

```bash
git add src/config.h
git commit -m "feat: config file + hotkeys"
```

### Task 6.2：健壮性与兼容性

- [ ] **Step 1: 全部游戏对象读写包裹 SEH**

沿用 `{EIEM}` 的 `__try/__except` 惯例；所有字段偏移经 `SafeOff()` 兜底；每阶段关键日志带 `[POSER]` 前缀写入 `plugin/poser_log.txt`。

- [ ] **Step 2: 优雅退出**

`AP_PluginDisable` 或卸载时：解冻角色、恢复相机、恢复动画/时间/形态，`MinHook_Disable` 全部钩子。

- [ ] **Step 3: `[in-game]` 全流程回归**

冻结→摆姿→形态→相机→截图→解冻 全链路无崩溃；热禁用/重启用正常。

- [ ] **Step 4: Commit**

```bash
git add .
git commit -m "chore: robustness pass (seh, safe fallbacks, clean disable)"
```

---

## 自评（对 spec 的覆盖）

- 冻结角色 → Task 2.2 ✔
- FK 摆姿 → Task 3.1 ✔
- IK 摆姿（含 IK/FK 够用问题：2-bone + 原生 BipedIK 双模式）→ Task 3.2 ✔
- 形态键（面部 BlendShape + 身体骨骼形态）→ Task 4.1 / 4.2 ✔
- Rig 支持 → 直接利用游戏 Humanoid 骨骼 + 原生 FinalIK，无自建重定向 ✔
- 从骨（头发/配饰）自由摆姿 + 物理开关 + 锁定 → Task 2.4 ✔
- 双模式（摆姿模式 ↔ 镜头模式，镜头可固定/解锁）→ Task 3.4 ✔
- 摄影（自由相机 / FOV / 景深 / 截图）→ Task 5.1-5.3 ✔
- 姿态预设/镜像/存取 → Task 3.3 ✔
- 已知风险（写计划时无法在沙箱消除，需 `[in-game]` 收敛）：
  1. 场景级冻结完整性（Task 2.2 Step 2 探针）
  2. 从骨物理组件类名与禁用方式（Task 2.4 Step 2 探针）
  3. 身体形态键权重写回入口（Task 4.2 Step 2 探针）
  4. 景深组件定位（Task 5.2 Step 2 探针）
- 无占位符：每步均含可执行代码或明确的复用/探针指令。

---

## 执行交接

计划已保存。两种执行方式：

1. **子代理驱动（推荐）**——每个 Task 派一个全新子代理实现 + 我逐任务审查，迭代快、上下文干净。
2. **当前会话内执行**——按任务批量执行、设检查点暂停供你确认。

> 注意：阶段 1（math 层）可在本沙箱立即 TDD 跑通；其余阶段需在 Windows + 游戏内验证（`[in-game]` 步骤只能由你在游戏环境执行或确认）。
