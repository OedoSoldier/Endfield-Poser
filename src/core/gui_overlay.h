#pragma once

// 精简自 {EIEM}/src/gui.h：只保留 D3D11 + DComp 透明覆盖窗 + ImGui 渲染循环。
// 业务面板由外部提供 DrawPoserGui()，本文件不关心任何游戏逻辑。

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <dcomp.h>
#include <imm.h>
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
static void SetOverlayClickThrough(bool on) {
  static int s_ctState = -1;
  if (!g_guiHwnd || (int)on == s_ctState)
    return;
  s_ctState = (int)on;
  LONG ex = GetWindowLongW(g_guiHwnd, GWL_EXSTYLE);
  LONG nw = on ? (ex | WS_EX_LAYERED | WS_EX_TRANSPARENT)
               : (ex & ~(WS_EX_LAYERED | WS_EX_TRANSPARENT));
  SetWindowLongW(g_guiHwnd, GWL_EXSTYLE, nw);
  SetWindowPos(g_guiHwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  Log("[INPUT] overlay click-through=%d", (int)on);
}
static volatile bool g_guiVisible = false;
static volatile bool g_guiRunning = false;

static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain1 *g_pSwapChain = nullptr;
static ID3D11RenderTargetView *g_pMainRenderTargetView = nullptr;
static IDCompositionDevice *g_pDCompDevice = nullptr;
static IDCompositionTarget *g_pDCompTarget = nullptr;
static IDCompositionVisual *g_pDCompVisual = nullptr;

struct EnumWindowCtx { DWORD pid; HWND result; };
static BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM lParam) {
  auto *ctx = reinterpret_cast<EnumWindowCtx *>(lParam);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != ctx->pid)
    return TRUE;
  // 精确匹配 Unity 主窗口（游戏进程含 Qt/CEF 等子窗口，不能取第一个）
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

static bool CreateDeviceD3D(HWND hWnd) {
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

static void CleanupDeviceD3D() {
  CleanupRenderTarget();
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
    if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
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

  // DComp 合成在游戏刚启动时可能暂不可用（0x887A0001），重试几次
  bool d3dOk = false;
  for (int i = 0; i < 8 && !d3dOk; i++) {
    d3dOk = CreateDeviceD3D(g_guiHwnd);
    if (!d3dOk) {
      Log("[GUI] CreateDeviceD3D attempt %d failed, retrying...", i + 1);
      Sleep(1000);
    }
  }
  if (!d3dOk) {
    Log("[GUI] ERROR: CreateDeviceD3D failed after retries!");
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

    // 快捷键：切换面板显示
    // GetAsyncKeyState 只能调用一次：bit0(按下边沿)会被调用消费掉，
    // 同一表达式调两次会让第二次永远为 0，热键失效。
    if (GetAsyncKeyState(g_guiToggleVK) & 1) {
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
    // 真穿透逐位置决定：默认穿透（鼠标全归游戏），只有当光标自由、且指针确实落在
    // 面板/关节/旋转环上（或正在拖拽）时才关掉穿透，把这次交互留给覆盖层。这样
    // 摄影模式里点游戏 UI 也照样有效，不再依赖跨线程 HTTRANSPARENT。
    if (g_clickThrough) {
      bool overInteractive = g_inputTakeMouse || g_inputHoverGizmo ||
                             g_inputDragging || g_inputMouseHeld;
      SetOverlayClickThrough(!(g_cursorFreeNow && overInteractive));
    }
    if (shouldShow) {
      if (!s_panelShown) {
        SetWindowPos(g_guiHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_guiHwnd, SW_SHOWNOACTIVATE);
        s_panelShown = true;
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
    g_pSwapChain->Present(0, 0);
  }

  Log("[GUI] Shutting down...");
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
  CreateThread(nullptr, 0, GuiThread, nullptr, 0, nullptr);
}

static void StopGuiThread() {
  g_guiRunning = false;
}
