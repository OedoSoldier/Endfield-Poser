#pragma once

// 精简自 {EIEM}/src/gui.h：只保留 D3D11 + DComp 透明覆盖窗 + ImGui 渲染循环。
// 业务面板由外部提供 DrawPoserGui()，本文件不关心任何游戏逻辑。

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <imm.h>
#include <tlhelp32.h>
#include <cmath>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "imm32.lib")

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "base.h"
#include "il2cpp_api.h"
#include "config.h"   // g_guiToggleVK / g_screenshotVK / 相机速度

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 由 poser.cpp / editor/gui.h 实现：每帧绘制主面板
void DrawPoserGui();
void GameFrameTick(); // poser.cpp 定义：每帧游戏逻辑（冻结维持/IK写回），隐藏时也跑

// 外部控制回调（poser.cpp 注册）：code 0=切模式 1=冻结/解冻 2=T-pose
typedef void (*ExtControlFn)(int code);
static ExtControlFn g_extControl = nullptr;
static void SetExtControl(ExtControlFn fn) { g_extControl = fn; }
// 外部轮询回调（poser.cpp 注册）：每帧调用，用于处理控制文件等外部指令
static void (*g_extPollFn)() = nullptr;
static void SetExtPollFn(void (*fn)()) { g_extPollFn = fn; }

// 退出钩子：GUI 线程结束前调用（此时仍在已 attach 到 IL2CPP 的线程上，可安全碰游戏对象）
static void (*g_guiShutdownFn)() = nullptr;
static void SetGuiShutdownFn(void (*fn)()) { g_guiShutdownFn = fn; }

static HWND g_gameHwnd = nullptr;
static HWND g_guiHwnd = nullptr;

// 图钉：锁定所有面板窗口位置（拖火柴人/滑块时窗口不会跟着动）。
// 放在这里是因为 poser.cpp 与 editor/panel_*.h 都要用它。
static bool g_pinPanels = true;

// ---- 输入路由状态 ----
// 鼠标只在「指针落在面板/旋转盘上 且 游戏光标已呼出」时由覆盖层吃掉，其余一律穿透给
// 游戏（由 WM_NCHITTEST 决定）。游戏自带的 Alt 呼出光标通过 Cursor.lockState/visible
// 读取（见 poser.cpp GameFrameTick），覆盖层不再强行改写游戏的光标状态。
static bool g_inputTakeMouse = false;  // 指针落在 ImGui 窗口内容上
static bool g_inputHoverGizmo = false; // 指针悬停在旋转盘环上
static bool g_inputDragging = false;   // gizmo 拖拽中：必须持续吃，否则松开消息丢给游戏
// 当前光标是否真的自由（lockState == None）。既包括按住 Alt 时我们强制放开的，
// 也包括游戏自己放开的情况（摄影模式、菜单等）。判定规则：光标出来了就该能点
// 面板，不必再额外按 Alt。
static bool g_cursorFreeNow = false;
static bool g_inputWantsText = false;  // 输入框聚焦中（键盘临时归覆盖层）
// 左键在我们窗口按下且尚未松开：拖拽/点选期间必须一直吃鼠标，否则松开消息会丢给
// 游戏或落进黑洞，ImGui 的 MouseDown 永远卡在按下 → 之后点哪都没反应。
static bool g_inputMouseHeld = false;
static int g_inputRouteLogged = -1;    // 路由日志去重

// click_through 模式：给窗口加/去 WS_EX_LAYERED|WS_EX_TRANSPARENT 实现真正的鼠标穿透
// （分层窗口会被系统在命中测试里整体跳过，跨进程也有效——HTTRANSPARENT 只能同线程）。
// 这个标志和 DComp 合成可能冲突（窗口可能不再显示），所以做成可选开关。
static bool g_layeredOverlay = false; // 当前是否使用分层窗口路径（见下方"分层窗口覆盖层"一节）
static void SetOverlayClickThrough(bool on) {
  static int s_ctState = -1;
  if (!g_guiHwnd || (int)on == s_ctState)
    return;
  s_ctState = (int)on;
  LONG ex = GetWindowLongW(g_guiHwnd, GWL_EXSTYLE);
  LONG nw = on ? (ex | WS_EX_LAYERED | WS_EX_TRANSPARENT)
               : (ex & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
  if (g_layeredOverlay)
    nw |= WS_EX_LAYERED; // 分层路径下 WS_EX_LAYERED 不能摘，只切 TRANSPARENT
  SetWindowLongW(g_guiHwnd, GWL_EXSTYLE, nw);
  SetWindowPos(g_guiHwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  Log("[INPUT] overlay click-through=%d", (int)on);
}
static volatile bool g_guiVisible = false;
static volatile bool g_guiRunning = false;
static bool g_xxmiDetected = false; // 进程里发现第三方 d3d11.dll（XXMI/3DMigoto）

// ---- 热键轮询线程 ----
// GetAsyncKeyState 的 bit0 是"自上次调用以来按下过"的锁存位，**进程内任何一次同键调用
// 都会把它清掉**：装了 XXMI/3DMigoto 后它们（以及游戏自己）也在轮询 F11/F12，
// 于是我们的 bit0 时有时无 —— 这正是"F11 有时有用有时没用"的根因。
// 这里改成独立线程 5ms 轮询 bit15（当前是否按下）+ 自己维护边沿，不依赖锁存位；
// 也不怕 GUI 循环被分层回读/游戏卡顿拖慢而漏掉短按。
static volatile LONG g_hotkeyToggleReq = 0;
static volatile LONG g_hotkeyFreezeReq = 0;
// 左键"短按"（不含拖动）计数：给"点空白处取消选中"用。因为覆盖层是可穿透的，
// 点在远处空白处时这次点击根本不会进我们的窗口，只能靠轮询知道它发生过。
static volatile LONG g_leftClickReq = 0;
static volatile LONG g_hotkeyPollRun = 0;
// 面板内改键：captureReq 0=未捕获 1=呼出键 2=冻结键；captureVK 0=还没按 -1=取消
static volatile LONG g_hotkeyCaptureReq = 0;
static volatile LONG g_hotkeyCaptureVK = 0;
static volatile LONG g_hotkeyCaptureCtrl = 0;

static DWORD WINAPI HotkeyPollThread(LPVOID) {
  bool prevToggle = false, prevFreeze = false;
  bool prevLBtn = false;
  static bool prevAll[256] = {};
  int lastCapture = 0;
  while (g_hotkeyPollRun) {
    // 左键短按检测：按下→300ms 内松开且位移 < 6px 才算"点击"（拖拽不算，
    // 免得转镜头/拖旋转盘被当成点空白）。
    {
      bool lbtn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
      static DWORD s_lDownTick = 0;
      static POINT s_lDownPos = {};
      if (lbtn && !prevLBtn) {
        s_lDownTick = GetTickCount();
        GetCursorPos(&s_lDownPos);
      } else if (!lbtn && prevLBtn) {
        POINT p = {};
        GetCursorPos(&p);
        DWORD dt = GetTickCount() - s_lDownTick;
        int dx = p.x - s_lDownPos.x, dy = p.y - s_lDownPos.y;
        if (dt <= 300 && (dx * dx + dy * dy) <= 36)
          InterlockedIncrement(&g_leftClickReq);
      }
      prevLBtn = lbtn;
    }
    int cap = (int)g_hotkeyCaptureReq;
    if (cap) {
      if (lastCapture == 0) {
        // 刚进入捕获：先把当前按键状态记下来，避免把"已经按着的键"当成新按键
        for (int vk = 0x08; vk <= 0xFE; vk++)
          prevAll[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        lastCapture = cap;
        Sleep(5);
        continue;
      }
      if ((int)g_hotkeyCaptureVK == 0) {
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        for (int vk = 0x08; vk <= 0xFE; vk++) {
          bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
          bool was = prevAll[vk];
          prevAll[vk] = down;
          if (!down || was)
            continue;
          // 修饰键本身不能当主键
          if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
              vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL ||
              vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU ||
              vk == VK_LWIN || vk == VK_RWIN)
            continue;
          if (vk == VK_ESCAPE) {
            InterlockedExchange(&g_hotkeyCaptureVK, -1);
            break;
          }
          InterlockedExchange(&g_hotkeyCaptureVK, vk);
          InterlockedExchange(&g_hotkeyCaptureCtrl, (ctrl || shift) ? 1 : 0);
          Log("[CFG] captured vk=0x%X (%s%s)", vk, ctrl ? "CTRL+" : "",
              shift ? "SHIFT+" : "");
          break;
        }
      }
      // 捕获期间不触发正常热键
      prevToggle = (GetAsyncKeyState(g_guiToggleVK) & 0x8000) != 0;
      prevFreeze = (GetAsyncKeyState(g_freezeVK) & 0x8000) != 0;
      Sleep(5);
      continue;
    }
    lastCapture = 0;
    // 在插件自己的输入框里打字时不响应热键（否则单键绑成字母就会边打字边触发）
    if (g_inputWantsText) {
      prevToggle = (GetAsyncKeyState(g_guiToggleVK) & 0x8000) != 0;
      prevFreeze = (GetAsyncKeyState(g_freezeVK) & 0x8000) != 0;
      Sleep(5);
      continue;
    }
    // 只有游戏窗口（或我们自己的覆盖窗）在前台时才响应热键：
    // 否则在浏览器/聊天里打字也会触发（尤其是被绑成字母的情况）
    HWND fg = GetForegroundWindow();
    bool ourFocus = (fg != nullptr) && (fg == g_gameHwnd || fg == g_guiHwnd);
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool t = ourFocus && (GetAsyncKeyState(g_guiToggleVK) & 0x8000) != 0 &&
             (!g_guiToggleCtrl || ctrl);
    bool f = ourFocus && (GetAsyncKeyState(g_freezeVK) & 0x8000) != 0 &&
             (!g_freezeCtrl || ctrl);
    if (t && !prevToggle)
      InterlockedIncrement(&g_hotkeyToggleReq);
    if (f && !prevFreeze)
      InterlockedIncrement(&g_hotkeyFreezeReq);
    prevToggle = t;
    prevFreeze = f;
    Sleep(5);
  }
  return 0;
}

static bool TakeHotkeyToggle() {
  return InterlockedExchange(&g_hotkeyToggleReq, 0) > 0;
}

static bool TakeHotkeyFreeze() {
  return InterlockedExchange(&g_hotkeyFreezeReq, 0) > 0;
}

static bool TakeLeftClick() {
  return InterlockedExchange(&g_leftClickReq, 0) > 0;
}

// 面板里的"改键"行：点按钮 → 等按键 → 应用并写回 poser_config.txt
static void DrawHotkeySetting(const char *label, const char *cfgKey, int *vkp,
                              bool *ctrlp, int captureId) {
  char name[48] = {};
  HotkeyDisplay(*vkp, *ctrlp, name, sizeof(name));
  bool capturing = ((int)g_hotkeyCaptureReq == captureId);
  ImGui::PushID(captureId);
  ImGui::Text("%s", label);
  ImGui::SameLine(110.0f);
  if (!capturing) {
    ImGui::Text("%s", name);
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"\u6539\u952e")) {
      g_hotkeyCaptureVK = 0;
      g_hotkeyCaptureCtrl = 0;
      g_hotkeyCaptureReq = captureId;
      Log("[CFG] rebind %s: waiting for a key...", cfgKey);
    }
  } else {
    int got = (int)g_hotkeyCaptureVK;
    if (got == 0) {
      ImGui::TextDisabled(
          u8"\u6309\u4e0b\u65b0\u952e\u2026\uff08\u5355\u952e\u4e5f\u884c\uff0c"
          u8"\u4f46\u5355\u5b57\u6bcd\u4f1a\u548c\u6253\u5b57\u51b2\u7a81\uff1b"
          u8"Esc \u53d6\u6d88\uff09");
    } else if (got < 0) {
      g_hotkeyCaptureReq = 0;
      g_hotkeyCaptureVK = 0;
      Log("[CFG] rebind %s: cancelled", cfgKey);
    } else {
      *vkp = got;
      *ctrlp = (g_hotkeyCaptureCtrl != 0);
      g_hotkeyCaptureReq = 0;
      g_hotkeyCaptureVK = 0;
      char nb[48] = {};
      HotkeyDisplay(*vkp, *ctrlp, nb, sizeof(nb));
      bool saved = SaveHotkeyConfig(cfgKey, *vkp, *ctrlp);
      CheckHotkeyConflicts();
      Log("[CFG] rebind %s -> %s (saved=%d)", cfgKey, nb, (int)saved);
    }
  }
  ImGui::PopID();
}

static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain1 *g_pSwapChain = nullptr;
static ID3D11RenderTargetView *g_pMainRenderTargetView = nullptr;
static IDCompositionDevice *g_pDCompDevice = nullptr;
static IDCompositionTarget *g_pDCompTarget = nullptr;
static IDCompositionVisual *g_pDCompVisual = nullptr;

// ---- 分层窗口（UpdateLayeredWindow）覆盖层 ----
// XXMI / 3DMigoto 会把自己的 d3d11.dll 注入游戏进程，并给 IDXGIFactory 的
// CreateSwapChain* 打全局 vtable 钩子。DCompositionCreateDevice 内部会走到这些钩子，
// 此时钩子拿到的不是它包装过的设备，最终在 dxgi 里访问无效地址，整个游戏进程崩掉
// （表现为黑屏卡在开屏页）。
// 故此采用 ImGui 画到离屏纹理，拷贝到 staging再由CPU 读回，最后 UpdateLayeredWindow 逐像素 alpha 渲染。
static ID3D11Texture2D *g_pLayerTex = nullptr;      // 离屏渲染目标
static ID3D11Texture2D *g_pLayerStaging = nullptr;  // CPU 可读副本（只按脏矩形大小建）
static HDC g_layerDC = nullptr;                     // 与 DIB 关联的内存 DC
static HBITMAP g_layerBmp = nullptr;                // 32bpp 顶朝下 DIB
static HGDIOBJ g_layerOldBmp = nullptr;
static void *g_layerBits = nullptr;
static int g_layerW = 0, g_layerH = 0;
static int g_blitW = 0, g_blitH = 0;                // staging/DIB 当前尺寸
static int g_prevDirtyX = 0, g_prevDirtyY = 0;      // 上一帧上传到分层表面的矩形
static int g_prevDirtyW = 0, g_prevDirtyH = 0;
// 分层路径：内容没变就不回读/不上传（面板静止时能省掉绝大部分 GPU→CPU 等待）
static bool g_layerForcePresent = true;
static unsigned long long g_layerLastHash = 0;
static int g_layerLastX = -1, g_layerLastY = -1, g_layerLastW = 0,
           g_layerLastH = 0;
static double QpcMs(LARGE_INTEGER a, LARGE_INTEGER b) {
  static double freq = 0.0;
  if (freq == 0.0) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    freq = (double)f.QuadPart;
  }
  return (double)(b.QuadPart - a.QuadPart) * 1000.0 / freq;
}

static bool DetectForeignD3D11() {
  wchar_t sysDir[MAX_PATH] = {};
  GetSystemDirectoryW(sysDir, MAX_PATH);
  size_t sysLen = wcslen(sysDir);
  bool found = false;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                         GetCurrentProcessId());
  if (snap == INVALID_HANDLE_VALUE)
    return false;
  MODULEENTRY32W me = {};
  me.dwSize = sizeof(me);
  if (Module32FirstW(snap, &me)) {
    do {
      if (_wcsicmp(me.szModule, L"d3d11.dll") != 0)
        continue;
      if (_wcsnicmp(me.szExePath, sysDir, sysLen) != 0) {
        char path[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, path, MAX_PATH, nullptr, nullptr);
        Log("[GUI] foreign d3d11.dll detected: %s", path);
        found = true;
      }
    } while (Module32NextW(snap, &me));
  }
  CloseHandle(snap);
  return found;
}

// 从 System32 取原版 D3D11CreateDevice，绕开代理 d3d11.dll 的包装设备。
static PFN_D3D11_CREATE_DEVICE GetSystemD3D11CreateDevice() {
  static PFN_D3D11_CREATE_DEVICE s_fn = nullptr;
  if (s_fn)
    return s_fn;
  wchar_t path[MAX_PATH] = {};
  GetSystemDirectoryW(path, MAX_PATH);
  wcscat_s(path, MAX_PATH, L"\\d3d11.dll");
  HMODULE m = LoadLibraryExW(path, nullptr, 0);
  if (m)
    s_fn = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(m, "D3D11CreateDevice");
  if (!s_fn)
    Log("[GUI] WARN: system d3d11.dll unavailable, falling back to the "
        "in-process import (may be a proxy!)");
  return s_fn ? s_fn : &D3D11CreateDevice;
}

static void ReleaseLayerResources() {
  if (g_pMainRenderTargetView) { g_pMainRenderTargetView->Release(); g_pMainRenderTargetView = nullptr; }
  if (g_pLayerStaging) { g_pLayerStaging->Release(); g_pLayerStaging = nullptr; }
  if (g_pLayerTex) { g_pLayerTex->Release(); g_pLayerTex = nullptr; }
  if (g_layerDC) {
    if (g_layerOldBmp) SelectObject(g_layerDC, g_layerOldBmp);
    DeleteDC(g_layerDC);
    g_layerDC = nullptr;
    g_layerOldBmp = nullptr;
  }
  if (g_layerBmp) { DeleteObject(g_layerBmp); g_layerBmp = nullptr; }
  g_layerBits = nullptr;
  g_blitW = g_blitH = 0;
  g_prevDirtyW = g_prevDirtyH = 0;
  g_layerW = g_layerH = 0;
}

// 只按"要上传的那块矩形"建 staging + DIB：整屏回读太贵（4K 每帧 33MB），
// 面板通常只占屏幕一角，这里按脏矩形回读能把开销降一到两个数量级。
static bool EnsureBlitResources(int w, int h) {
  if (w <= 0 || h <= 0 || !g_pd3dDevice)
    return false;
  if (g_pLayerStaging && g_blitW == w && g_blitH == h)
    return true;
  if (g_pLayerStaging) { g_pLayerStaging->Release(); g_pLayerStaging = nullptr; }
  g_blitW = g_blitH = 0;
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = w;
  td.Height = h;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_STAGING;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_pLayerStaging))) {
    Log("[GUI] layered: staging %dx%d failed", w, h);
    return false;
  }
  g_blitW = w;
  g_blitH = h;
  return true;
}

static bool CreateLayerResources(int w, int h) {
  ReleaseLayerResources();
  if (w <= 0) w = 1;
  if (h <= 0) h = 1;
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = w;
  td.Height = h;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET;
  if (FAILED(g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_pLayerTex)))
    return false;
  if (FAILED(g_pd3dDevice->CreateRenderTargetView(g_pLayerTex, nullptr,
                                                   &g_pMainRenderTargetView)))
    return false;
  // DIB 必须是**整窗大小**：分层窗口的位图源要覆盖整个窗口，
  // 脏矩形只通过 UpdateLayeredWindowIndirect 的 prcDirty 指定
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(nullptr);
  g_layerDC = CreateCompatibleDC(screen);
  g_layerBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &g_layerBits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!g_layerDC || !g_layerBmp || !g_layerBits)
    return false;
  g_layerOldBmp = SelectObject(g_layerDC, g_layerBmp);
  g_layerW = w;
  g_layerH = h;
  return true;
}

static bool CreateDeviceLayered(HWND hWnd) {
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[] = {D3D_FEATURE_LEVEL_11_0};
  PFN_D3D11_CREATE_DEVICE create = GetSystemD3D11CreateDevice();
  HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                      featureLevelArray, 1, D3D11_SDK_VERSION,
                      &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (hr == DXGI_ERROR_UNSUPPORTED) {
    hr = create(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevelArray, 1, D3D11_SDK_VERSION,
                &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  }
  if (FAILED(hr)) {
    Log("[GUI] layered: D3D11CreateDevice failed: 0x%08X", hr);
    return false;
  }
  RECT rc;
  GetClientRect(hWnd, &rc);
  if (!CreateLayerResources(rc.right - rc.left, rc.bottom - rc.top)) {
    Log("[GUI] layered: offscreen resources failed");
    return false;
  }
  // 分层窗口常驻 WS_EX_LAYERED 是 UpdateLayeredWindow 的前提
  LONG ex = GetWindowLongW(hWnd, GWL_EXSTYLE);
  SetWindowLongW(hWnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
  g_layeredOverlay = true;
  Log("[GUI] Layered (UpdateLayeredWindow) overlay created %dx%d", g_layerW, g_layerH);
  return true;
}

// 本帧要上传的矩形 = 本帧 ImGui 顶点包围盒 ∪ 上一帧已上传的矩形
// 内容指纹：面板静止时它不变 —— 用来跳过整轮回读+上传（给"mod 多、GPU 挤"的机器省时间）
static unsigned long long ImGuiContentHash() {
  ImDrawData *dd = ImGui::GetDrawData();
  unsigned long long h = 1469598103934665603ull;
  if (!dd)
    return h;
  for (int n = 0; n < dd->CmdListsCount; n++) {
    const ImDrawList *cl = dd->CmdLists[n];
    const unsigned char *p = (const unsigned char *)cl->VtxBuffer.Data;
    size_t words = (size_t)cl->VtxBuffer.Size * sizeof(ImDrawVert) / 8;
    const unsigned long long *q = (const unsigned long long *)p;
    for (size_t i = 0; i < words; i++) {
      h ^= q[i];
      h *= 1099511628211ull;
    }
  }
  return h;
}

// （后者必须并进来，否则"被擦掉"的像素会残留在分层表面上）
static bool LayeredDirtyRect(int &ox, int &oy, int &ow, int &oh) {
  ImDrawData *dd = ImGui::GetDrawData();
  float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
  if (dd) {
    for (int n = 0; n < dd->CmdListsCount; n++) {
      const ImDrawList *cl = dd->CmdLists[n];
      for (int v = 0; v < cl->VtxBuffer.Size; v++) {
        const ImVec2 &p = cl->VtxBuffer.Data[v].pos;
        if (p.x < minx) minx = p.x;
        if (p.y < miny) miny = p.y;
        if (p.x > maxx) maxx = p.x;
        if (p.y > maxy) maxy = p.y;
      }
    }
  }
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (maxx > minx && maxy > miny && minx < 1e29f) {
    x0 = (int)floorf(minx) - 2;
    y0 = (int)floorf(miny) - 2;
    x1 = (int)ceilf(maxx) + 2;
    y1 = (int)ceilf(maxy) + 2;
  }
  if (g_prevDirtyW > 0 && g_prevDirtyH > 0) {
    if (g_prevDirtyX < x0) x0 = g_prevDirtyX;
    if (g_prevDirtyY < y0) y0 = g_prevDirtyY;
    if (g_prevDirtyX + g_prevDirtyW > x1) x1 = g_prevDirtyX + g_prevDirtyW;
    if (g_prevDirtyY + g_prevDirtyH > y1) y1 = g_prevDirtyY + g_prevDirtyH;
  }
  // 裁到窗口范围
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > g_layerW) x1 = g_layerW;
  if (y1 > g_layerH) y1 = g_layerH;
  if (x1 - x0 <= 0 || y1 - y0 <= 0) {
    g_prevDirtyW = g_prevDirtyH = 0;
    return false;
  }
  ox = x0;
  oy = y0;
  ow = x1 - x0;
  oh = y1 - y0;
  g_prevDirtyX = ox;
  g_prevDirtyY = oy;
  g_prevDirtyW = ow;
  g_prevDirtyH = oh;
  return true;
}

static void LayeredLogTiming(double copyMs, double mapMs, double ulwMs, int w,
                             int h, int skipped) {
  static int frames = 0, skips = 0;
  static double accC = 0, accM = 0, accU = 0;
  static double maxC = 0, maxM = 0, maxU = 0;
  static ULONGLONG windowStart = 0;
  accC += copyMs;
  accM += mapMs;
  accU += ulwMs;
  if (copyMs > maxC) maxC = copyMs;
  if (mapMs > maxM) maxM = mapMs;
  if (ulwMs > maxU) maxU = ulwMs;
  frames++;
  skips += skipped;
  ULONGLONG now = GetTickCount64();
  if (windowStart == 0) windowStart = now;
  if (now - windowStart >= 1000) {
    double sec = (double)(now - windowStart) / 1000.0;
    Log("[GUI] layered present %.0f fps: rect %dx%d copy avg=%.1f max=%.1f | "
        "map avg=%.1f max=%.1f | ulw avg=%.1f max=%.1f | skipped=%d",
        frames / sec, w, h, accC / frames, maxC, accM / frames, maxM,
        accU / frames, maxU, skips);
    frames = 0;
    skips = 0;
    accC = accM = accU = 0;
    maxC = maxM = maxU = 0;
    windowStart = now;
  }
}

static void PresentLayered() {
  // 注意：g_pLayerStaging 是"按脏矩形懒创建"的（见 EnsureBlitResources），
  // 不能放在这里判空——否则它永远没机会被创建，表现为窗口显示了却什么都没有（面板看不见）。
  if (!g_layerBits || !g_pLayerTex) {
    static bool s_loggedNoRes = false;
    if (!s_loggedNoRes) {
      s_loggedNoRes = true;
      Log("[GUI] layered: resources not ready (bits=%p tex=%p) -> nothing drawn",
          g_layerBits, g_pLayerTex);
    }
    return;
  }
  int dx = 0, dy = 0, dw = 0, dh = 0;
  if (!LayeredDirtyRect(dx, dy, dw, dh)) {
    LayeredLogTiming(0, 0, 0, 0, 0, 1); // 没内容可画：不碰窗口
    return;
  }
  // 内容和矩形都没变 → 直接跳过（分层表面已经是对的），省掉这次 Map 等 GPU 的时间
  unsigned long long hash = ImGuiContentHash();
  if (!g_layerForcePresent && hash == g_layerLastHash && dx == g_layerLastX &&
      dy == g_layerLastY && dw == g_layerLastW && dh == g_layerLastH) {
    LayeredLogTiming(0, 0, 0, dw, dh, 1);
    return;
  }
  g_layerLastHash = hash;
  g_layerLastX = dx;
  g_layerLastY = dy;
  g_layerLastW = dw;
  g_layerLastH = dh;
  g_layerForcePresent = false;
  if (!EnsureBlitResources(dw, dh))
    return;
  LARGE_INTEGER t0, t1, t2, t3;
  QueryPerformanceCounter(&t0);
  D3D11_BOX box = {(UINT)dx, (UINT)dy, 0, (UINT)(dx + dw), (UINT)(dy + dh), 1};
  g_pd3dDeviceContext->CopySubresourceRegion(g_pLayerStaging, 0, 0, 0, 0,
                                             g_pLayerTex, 0, &box);
  D3D11_MAPPED_SUBRESOURCE map = {};
  if (FAILED(g_pd3dDeviceContext->Map(g_pLayerStaging, 0, D3D11_MAP_READ, 0,
                                      &map)))
    return;
  QueryPerformanceCounter(&t1);
  const size_t srcPitch = (size_t)map.RowPitch;
  const size_t dstPitch = (size_t)g_layerW * 4; // DIB 是整窗宽度
  const size_t rowBytes = (size_t)dw * 4;
  for (int y = 0; y < dh; y++)
    memcpy((char *)g_layerBits + dstPitch * (dy + y) + (size_t)dx * 4,
           (const char *)map.pData + srcPitch * y, rowBytes);
  g_pd3dDeviceContext->Unmap(g_pLayerStaging, 0);
  QueryPerformanceCounter(&t2);

  RECT wr;
  GetWindowRect(g_guiHwnd, &wr);
  POINT dst = {wr.left, wr.top};
  SIZE sz = {g_layerW, g_layerH}; // 必须是整窗尺寸：psize 语义是"窗口的新尺寸"
  POINT src = {0, 0};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  RECT dirty = {dx, dy, dx + dw, dy + dh};
  UPDATELAYEREDWINDOWINFO info = {};
  info.cbSize = sizeof(info);
  info.pptDst = &dst;
  info.psize = &sz;
  info.hdcSrc = g_layerDC;
  info.pptSrc = &src;
  info.crKey = 0;
  info.pblend = &bf;
  info.dwFlags = ULW_ALPHA;
  info.prcDirty = &dirty; // 只更新这块区域（位置/尺寸不受影响）
  UpdateLayeredWindowIndirect(g_guiHwnd, &info);
  QueryPerformanceCounter(&t3);
  LayeredLogTiming(QpcMs(t0, t1), QpcMs(t1, t2), QpcMs(t2, t3), dw, dh, 0);
}

// 覆盖层尺寸跟随游戏窗口后，分层纹理需要同步重建。
static void LayeredSyncSize() {
  RECT rc;
  GetClientRect(g_guiHwnd, &rc);
  int w = rc.right - rc.left, h = rc.bottom - rc.top;
  if (w > 0 && h > 0 && (w != g_layerW || h != g_layerH)) {
    if (!CreateLayerResources(w, h))
      Log("[GUI] layered: resize to %dx%d failed", w, h);
    g_layerForcePresent = true; // 资源重建过：下一帧必须重画/重传
  }
}

struct EnumWindowCtx { DWORD pid; HWND result; };
static BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM lParam) {
  auto *ctx = reinterpret_cast<EnumWindowCtx *>(lParam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != ctx->pid)
    return TRUE;
  // 匹配 Unity 主窗口。由于游戏进程含 Qt/CEF 等子窗口，不能取第一个。
  char cls[64] = {};
  GetClassNameA(hwnd, cls, sizeof(cls));
  if (strcmp(cls, "UnityWndClass") == 0 && IsWindowVisible(hwnd)) {
    ctx->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

static HWND FindGameHwnd() {
  EnumWindowCtx ctx = {GetCurrentProcessId(), nullptr};
  EnumWindows(EnumWindowProc, reinterpret_cast<LPARAM>(&ctx));
  return ctx.result;
}

static BOOL CALLBACK EnumAnyWindowProc(HWND hwnd, LPARAM lParam) {
  auto *ctx = reinterpret_cast<EnumWindowCtx *>(lParam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == ctx->pid) {
    ctx->result = hwnd;
    return FALSE;
  }
  return TRUE;
}

static HWND FindAnyHwnd() {
  EnumWindowCtx ctx = {GetCurrentProcessId(), nullptr};
  EnumWindows(EnumAnyWindowProc, reinterpret_cast<LPARAM>(&ctx));
  return ctx.result;
}

static void CreateRenderTarget() {
  ID3D11Texture2D *pBackBuffer = nullptr;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  if (pBackBuffer) {
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                          &g_pMainRenderTargetView);
    pBackBuffer->Release();
  }
}

static void CleanupRenderTarget() {
  if (g_pMainRenderTargetView) {
    g_pMainRenderTargetView->Release();
    g_pMainRenderTargetView = nullptr;
  }
}

static bool CreateDeviceD3DImpl(HWND hWnd) {
  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[] = {D3D_FEATURE_LEVEL_11_0};
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
      featureLevelArray, 1, D3D11_SDK_VERSION,
      &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  if (hr == DXGI_ERROR_UNSUPPORTED) {
    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
        featureLevelArray, 1, D3D11_SDK_VERSION,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
  }
  if (FAILED(hr)) return false;

  IDXGIDevice *pDxgiDevice = nullptr;
  g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice));
  IDXGIAdapter *pAdapter = nullptr;
  pDxgiDevice->GetAdapter(&pAdapter);
  IDXGIFactory2 *pFactory = nullptr;
  pAdapter->GetParent(IID_PPV_ARGS(&pFactory));

  RECT rc;
  GetClientRect(hWnd, &rc);
  DXGI_SWAP_CHAIN_DESC1 sd = {};
  sd.Width = rc.right - rc.left;
  sd.Height = rc.bottom - rc.top;
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  hr = pFactory->CreateSwapChainForComposition(g_pd3dDevice, &sd, nullptr,
                                                &g_pSwapChain);
  pFactory->Release();
  pAdapter->Release();
  if (FAILED(hr)) {
    Log("[GUI] CreateSwapChainForComposition failed: 0x%08X", hr);
    pDxgiDevice->Release();
    return false;
  }

  hr = DCompositionCreateDevice(pDxgiDevice, IID_PPV_ARGS(&g_pDCompDevice));
  pDxgiDevice->Release();
  if (FAILED(hr)) {
    Log("[GUI] DCompositionCreateDevice failed: 0x%08X", hr);
    return false;
  }
  g_pDCompDevice->CreateTargetForHwnd(hWnd, TRUE, &g_pDCompTarget);
  g_pDCompDevice->CreateVisual(&g_pDCompVisual);

  g_pDCompVisual->SetContent(g_pSwapChain);
  g_pDCompTarget->SetRoot(g_pDCompVisual);
  g_pDCompDevice->Commit();

  CreateRenderTarget();
  Log("[GUI] DComp transparent swap chain created");
  return true;
}

// 如果第三方 d3d11 钩子可能让 DComp 初始化直接访问违例，使用 SEH catch.
static bool CreateDeviceD3D(HWND hWnd) {
  __try {
    return CreateDeviceD3DImpl(hWnd);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    Log("[GUI] CreateDeviceD3D raised exception 0x%08X", GetExceptionCode());
    return false;
  }
}

static void CleanupDeviceD3D() {
  CleanupRenderTarget();
  ReleaseLayerResources();
  g_layeredOverlay = false;
  if (g_pDCompVisual) { g_pDCompVisual->Release(); g_pDCompVisual = nullptr; }
  if (g_pDCompTarget) { g_pDCompTarget->Release(); g_pDCompTarget = nullptr; }
  if (g_pDCompDevice) { g_pDCompDevice->Release(); g_pDCompDevice = nullptr; }
  if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
  if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
  if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static LRESULT CALLBACK GuiWndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
  // 命中测试：指针在面板/旋转盘上、且游戏光标已呼出时才吃鼠标；其余一律
  // HTTRANSPARENT，点击与移动直接落给游戏（面板开着也能转镜头、走位）。
  // 拖拽中强制吃：否则松开左键的消息会丢给游戏，手柄会卡在拖拽态。
  if (msg == WM_NCHITTEST) {
    bool take = g_guiVisible &&
                (g_inputDragging || g_inputMouseHeld ||
                 (g_cursorFreeNow && (g_inputTakeMouse || g_inputHoverGizmo)));
    int route = take ? 1 : 0;
    if (route != g_inputRouteLogged) {
      g_inputRouteLogged = route;
      Log("[INPUT] mouse route -> %s", take ? "overlay" : "game");
    }
    return take ? HTCLIENT : HTTRANSPARENT;
  }
  if (msg == WM_LBUTTONDOWN)
    g_inputMouseHeld = true;
  else if (msg == WM_LBUTTONUP)
    g_inputMouseHeld = false;
  bool imguiHandled =
      ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
  if (imguiHandled)
    return true;
  switch (msg) {
  case WM_SIZE:
    if (wParam == SIZE_MINIMIZED)
      return 0;
    // 分层模式没有 swapchain：这里只让分层表面跟随尺寸重建，
    // 否则 g_pSwapChain 为 nullptr 会直接访问违例（改分辨率/全屏切换时崩）
    if (g_layeredOverlay || !g_pSwapChain) {
      LayeredSyncSize();
      return 0;
    }
    if (g_pd3dDevice) {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam),
                                   (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN,
                                   0);
      CreateRenderTarget();
    }
    return 0;
  case WM_CLOSE:
    ShowWindow(hWnd, SW_HIDE);
    g_guiVisible = false;
    return 0;
  case WM_APP + 1: // 外部控制：切换面板显示（PostMessage 通道，绕过反作弊输入拦截）
    g_guiVisible = !g_guiVisible;
    Log("[CTRL] external toggle -> %d", (int)g_guiVisible);
    return 0;
  case WM_APP + 90: // 外部控制：转发业务指令（wParam = code）
    if (g_extControl)
      g_extControl((int)wParam);
    return 0;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static DWORD WINAPI GuiThread(LPVOID) {
  // 附加到 IL2CPP 域：GUI 线程每帧会经 DrawPoserGui->GameFrameTick 触碰游戏对象，
  // 不附加会让 GC 从"未知线程"收集托管对象，触发 fatal error 崩溃。
  if (il2cpp_domain_get && il2cpp_thread_attach) {
    void *domain = il2cpp_domain_get();
    if (domain) {
      il2cpp_thread_attach(domain);
      Log("[GUI] attached to IL2CPP domain");
    }
  }
  // 游戏启动较慢：先轮询等 Unity 主窗口出现（最多 60 秒），再回退任意窗口
  g_gameHwnd = nullptr;
  for (int i = 0; i < 60 && !g_gameHwnd; i++) {
    g_gameHwnd = FindGameHwnd();
    if (!g_gameHwnd)
      Sleep(1000);
  }
  if (!g_gameHwnd)
    g_gameHwnd = FindAnyHwnd();
  if (!g_gameHwnd) {
    Log("[GUI] No game hwnd, GUI thread exits");
    return 0;
  }
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = GuiWndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"EndfieldPoserOverlay";
  RegisterClassExW(&wc);

  RECT gr;
  GetWindowRect(g_gameHwnd, &gr);
  // WS_EX_NOACTIVATE：平时绝不抢游戏焦点（键盘永远归游戏）。
  // 不加 WS_EX_TRANSPARENT：该标志对非分层窗口并不能让点击穿透，反而会把整个
  // 游戏窗口的鼠标都吃掉；穿透改由 GuiWndProc 的 WM_NCHITTEST 按命中区域决定。
  // 文字输入时由主循环临时去掉 NOACTIVATE 并抢焦点，输入完立刻还给游戏（见下）。
  g_guiHwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      wc.lpszClassName, L"EndfieldPoserOverlay", WS_POPUP,
      gr.left, gr.top, gr.right - gr.left, gr.bottom - gr.top,
      nullptr, nullptr, wc.hInstance, nullptr);
  // 覆盖层窗口不关联 IME：避免面板获得输入法上下文、一按键盘就弹输入法
  ImmAssociateContext(g_guiHwnd, (HIMC)nullptr);

  // 覆盖层渲染路径选择（overlay_mode：0=auto 1=dcomp 2=layered）。
  // auto：进程里有第三方 d3d11.dll（XXMI/3DMigoto）就走分层路径，否则走 DComp；
  // DComp 重试失败后也退回分层路径。
  bool d3dOk = false;
  bool foreignD3D11 = DetectForeignD3D11();
  if (foreignD3D11)
    g_xxmiDetected = true;
  bool useLayered = (g_overlayMode == 2) ||
                    (g_overlayMode == 0 && foreignD3D11);
  if (useLayered)
    Log("[GUI] overlay path: layered (mode=%d)", g_overlayMode);
  if (!useLayered) {
    // DComp 合成在游戏刚启动时可能暂不可用（0x887A0001），重试几次
    for (int i = 0; i < 8 && !d3dOk; i++) {
      // XXMI/3DMigoto 的 d3d11.dll 若在我们之后才注入，第一次检测会漏判；
      // 每次重试都再测一遍，宁可改走分层也别去踩 DComp 的坑
      if (g_overlayMode == 0 && i > 0 && DetectForeignD3D11()) {
        Log("[GUI] foreign d3d11.dll appeared late -> switching to layered");
        g_xxmiDetected = true;
        useLayered = true;
        break;
      }
      d3dOk = CreateDeviceD3D(g_guiHwnd);
      if (!d3dOk) {
        Log("[GUI] CreateDeviceD3D attempt %d failed, retrying...", i + 1);
        CleanupDeviceD3D();
        Sleep(1000);
      }
    }
    if (!d3dOk && g_overlayMode != 1) {
      Log("[GUI] DComp path unavailable, falling back to layered overlay");
      useLayered = true;
    }
  }
  if (useLayered && !d3dOk) {
    for (int i = 0; i < 3 && !d3dOk; i++) {
      d3dOk = CreateDeviceLayered(g_guiHwnd);
      if (!d3dOk) {
        CleanupDeviceD3D();
        Sleep(500);
      }
    }
  }
  if (!d3dOk) {
    Log("[GUI] ERROR: overlay device creation failed after retries!");
    CleanupDeviceD3D();
    DestroyWindow(g_guiHwnd);
    return 0;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // 关键盘导航，避免输入框被自动聚焦
  io.MouseDrawCursor = false;
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 2.0f;
  style.FramePadding = ImVec2(8, 4);
  style.ItemSpacing = ImVec2(8, 6);
  style.WindowPadding = ImVec2(10, 6);
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 10.0f;

  {
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 1;
    fontCfg.PixelSnapH = true;
    const char *fontPath = "C:\\Windows\\Fonts\\msyh.ttc";
    bool loaded = false;
    if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES) {
      ImFont *f = io.Fonts->AddFontFromFileTTF(
          fontPath, 18.0f, &fontCfg,
          io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
      loaded = (f != nullptr);
    }
    if (!loaded) {
      io.Fonts->AddFontDefault();
      Log("[GUI] WARN: msyh.ttc not found, Chinese text may not render");
    }
  }

  ImGui_ImplWin32_Init(g_guiHwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  g_guiVisible = false;
  ShowWindow(g_guiHwnd, SW_HIDE);
  Log("[GUI] ImGui initialized, panel ready");

  MSG msg;
  ZeroMemory(&msg, sizeof(msg));
  bool s_panelShown = false;
  while (g_guiRunning) {
    if (g_extPollFn)
      g_extPollFn(); // 控制文件轮询（面板隐藏时也执行）
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT) { g_guiRunning = false; break; }
    }
    if (!g_guiRunning) break;
    if (!IsWindow(g_gameHwnd)) {
      Log("[GUI] Game window gone, shutting down");
      g_guiRunning = false;
      break;
    }

    // 快捷键：切换面板显示（由 HotkeyPollThread 边沿检测，见上方注释）
    if (TakeHotkeyToggle()) {
      g_guiVisible = !g_guiVisible;
      Log("[GUI] toggle -> visible=%d", (int)g_guiVisible);
    }

    // 只有「面板打开 且 按住 Alt」时才把覆盖层显示出来（此时它接管鼠标/键盘）。
    // 其余时间窗口直接隐藏：既不渲染也不参与命中测试，游戏拿到全部输入。
    // 原因：HTTRANSPARENT 只能把命中转给"同一线程"的下层窗口，覆盖层在本进程
    // 自建线程上、游戏窗口在主线程，跨线程放行等于把点击吞掉（实测游戏点不动）。
    bool altHeld = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    // click_through 模式：面板打开就常驻显示，靠分层穿透把鼠标让给游戏；
    // 默认模式：只有按住 Alt（或拖拽中）才显示覆盖层，其余时间整窗隐藏。
    bool shouldShow = g_guiVisible && !IsIconic(g_gameHwnd) &&
                      (g_clickThrough || altHeld || g_inputDragging);
    static int s_showLogged = -1;
    if ((int)shouldShow != s_showLogged) {
      s_showLogged = (int)shouldShow;
      Log("[GUI] overlay %s (panel=%d alt=%d)", shouldShow ? "shown" : "hidden",
          (int)g_guiVisible, (int)altHeld);
    }
    // 真穿透逐位置决定放在 DrawPoserGui 之后（见下方）—— 用本帧的 hover 状态，
    // 否则会慢一帧：环已经变色了，但这一下点击还是被当成点游戏（"点它没反应"）。
    if (shouldShow) {
      if (!s_panelShown) {
        SetWindowPos(g_guiHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_guiHwnd, SW_SHOWNOACTIVATE);
        s_panelShown = true;
        g_layerForcePresent = true; // 刚显示：下一帧必须上传一次
      }
      // 覆盖层永不抢焦点（WS_EX_NOACTIVATE 常驻）：键盘永远归游戏。
      // 不为输入框临时激活覆盖层——任何情况都不抢焦点、不弹输入法。
      // 文字输入（如姿态命名）走 WebUI，浏览器输入不依赖窗口焦点。
      // 跟随游戏窗口位置/尺寸（游戏全屏/切窗口后覆盖层仍贴合）
      RECT gr, ow;
      GetWindowRect(g_gameHwnd, &gr);
      GetWindowRect(g_guiHwnd, &ow);
      if (gr.left != ow.left || gr.top != ow.top ||
          (gr.right - gr.left) != (ow.right - ow.left) ||
          (gr.bottom - gr.top) != (ow.bottom - ow.top)) {
        SetWindowPos(g_guiHwnd, HWND_TOPMOST, gr.left, gr.top,
                     gr.right - gr.left, gr.bottom - gr.top,
                     SWP_NOACTIVATE);
      }
    } else if (s_panelShown) {
      SetWindowPos(g_guiHwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      ShowWindow(g_guiHwnd, SW_HIDE);
      s_panelShown = false;
      // 隐藏时若还停在"输入框抢焦点"状态：恢复 NOACTIVATE 并把焦点还给游戏
      if (g_inputWantsText) {
        g_inputWantsText = false;
        LONG ex = GetWindowLongW(g_guiHwnd, GWL_EXSTYLE);
        SetWindowLongW(g_guiHwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
        if (IsWindow(g_gameHwnd))
          SetForegroundWindow(g_gameHwnd);
        Log("[INPUT] overlay hidden -> keyboard back to game");
      }
    }
    if (!s_panelShown) {
      // 隐藏覆盖层时仍跑游戏逻辑（冻结维持/IK写回/控制文件）
      __try { GameFrameTick(); } __except (1) {
        Log("[GUI] hidden GameFrameTick exception");
      }
      Sleep(30);
      continue;
    }

    // 覆盖层是 NOACTIVATE，ImGui 的 Win32 后端只在"窗口获得焦点"时才轮询
    // GetCursorPos；而我们把非面板区域的鼠标消息让给了游戏，WM_MOUSEMOVE 不会
    // 再进覆盖层。必须自己每帧喂指针位置，否则 io.MousePos 会停在最后一次收到
    // 的消息上，hover/命中判定全错（表现为"点击丢失"）。
    {
      POINT mp;
      if (::GetCursorPos(&mp) && ::ScreenToClient(g_guiHwnd, &mp))
        ImGui::GetIO().AddMousePosEvent((float)mp.x, (float)mp.y);
    }
    if (g_layeredOverlay)
      LayeredSyncSize();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    __try { DrawPoserGui(); } __except (1) {
      Log("[GUI] DrawPoserGui exception code=0x%X", GetExceptionCode());
    }

    // ---- 输入路由 + 文字输入焦点 ----
    {
      ImGuiIO &io = ImGui::GetIO();
      g_inputTakeMouse = io.WantCaptureMouse; // 指针落在 ImGui 窗口内容上
      // 真穿透逐位置决定：默认穿透（鼠标全归游戏），只有当光标自由、且指针确实落在
      // 面板/关节/旋转环上（或正在拖拽）时才关掉穿透，把这次交互留给覆盖层。
      // 放在这里（DrawPoserGui 之后）是关键：用的是**本帧**的 hover 状态，
      // 快一帧都不行 —— 否则快速移到旋转环上立刻点击，那一下会被判成点游戏。
      if (g_clickThrough) {
        bool overInteractive = g_inputTakeMouse || g_inputHoverGizmo ||
                               g_inputDragging || g_inputMouseHeld;
        SetOverlayClickThrough(!(g_cursorFreeNow && overInteractive));
      }
      bool wantText = io.WantTextInput;
      if (wantText != g_inputWantsText) {
        g_inputWantsText = wantText;
        LONG ex = GetWindowLongW(g_guiHwnd, GWL_EXSTYLE);
        if (wantText) {
          // 输入框聚焦：临时允许激活并抢焦点，让 WM_CHAR/WM_KEYDOWN 进 ImGui
          SetWindowLongW(g_guiHwnd, GWL_EXSTYLE, ex & ~WS_EX_NOACTIVATE);
          SetForegroundWindow(g_guiHwnd);
          SetFocus(g_guiHwnd);
          Log("[INPUT] text field focused -> keyboard to overlay");
        } else {
          SetWindowLongW(g_guiHwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
          if (IsWindow(g_gameHwnd))
            SetForegroundWindow(g_gameHwnd);
          Log("[INPUT] text field blurred -> keyboard back to game");
        }
      }
    }

    ImGui::Render();
    const float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRenderTargetView,
                                             nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_pMainRenderTargetView,
                                                clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    if (g_layeredOverlay) {
      PresentLayered();
      // 分层路径没有垂直同步：按 overlay_fps 限帧（0=不限）。
      // mod 多的机器上，回读等待本来就长，帧率越低对游戏干扰越小。
      DWORD waitMs = 8;
      if (g_overlayFps > 0) {
        int ms = 1000 / g_overlayFps;
        waitMs = (DWORD)(ms < 1 ? 1 : ms);
      }
      Sleep(waitMs);
    } else {
      g_pSwapChain->Present(0, 0);
    }
  }

  Log("[GUI] Shutting down...");
  if (g_guiShutdownFn) {
    __try {
      g_guiShutdownFn(); // 解冻 + 恢复物理/表情，避免禁用插件后布料一直僵着
    } __except (1) {
      Log("[GUI] shutdown hook exception");
    }
  }
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  CleanupDeviceD3D();
  DestroyWindow(g_guiHwnd);
  return 0;
}

static void StartGuiThread() {
  if (g_guiRunning) return;
  g_guiRunning = true;
  if (!g_hotkeyPollRun) {
    g_hotkeyPollRun = 1;
    CreateThread(nullptr, 0, HotkeyPollThread, nullptr, 0, nullptr);
  }
  CreateThread(nullptr, 0, GuiThread, nullptr, 0, nullptr);
}

static void StopGuiThread() {
  g_guiRunning = false;
  g_hotkeyPollRun = 0;
}
