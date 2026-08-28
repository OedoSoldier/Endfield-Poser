#pragma once

// 精简自 {EIEM}/src/gui.h：只保留 D3D11 + DComp 透明覆盖窗 + ImGui 渲染循环。
// 业务面板由外部提供 DrawPoserGui()，本文件不关心任何游戏逻辑。

#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <dcomp.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dcomp.lib")

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
  bool imguiHandled =
      ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
  if (msg == WM_LBUTTONDOWN) {
    POINT p = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
    ImGuiIO &dio = ImGui::GetIO();
    Log("[MOUSE] overlay LBUTTONDOWN (%d,%d) capture=%p fg=%p wantCap=%d "
        "mousePos=(%.0f,%.0f)",
        p.x, p.y, GetCapture(), (void *)GetForegroundWindow(),
        (int)dio.WantCaptureMouse, dio.MousePos.x, dio.MousePos.y);
  }
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
  // WS_EX_NOACTIVATE：点击面板不抢游戏焦点（否则游戏失焦暂停、点击失效）。
  // 文字输入由主循环临时取消该标志并激活覆盖层（见下），输入完恢复。
  g_guiHwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      wc.lpszClassName, L"EndfieldPoserOverlay", WS_POPUP,
      gr.left, gr.top, gr.right - gr.left, gr.bottom - gr.top,
      nullptr, nullptr, wc.hInstance, nullptr);

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
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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

    bool shouldShow = g_guiVisible && !IsIconic(g_gameHwnd);
    if (shouldShow) {
      if (!s_panelShown) {
        // 面板模式：覆盖层激活并持有焦点（鼠标键盘都归面板，游戏不响应）
        SetWindowPos(g_guiHwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(g_guiHwnd, SW_SHOWNOACTIVATE);
        LONG_PTR ex = GetWindowLongPtrW(g_guiHwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(g_guiHwnd, GWL_EXSTYLE, ex & ~WS_EX_NOACTIVATE);
        SetForegroundWindow(g_guiHwnd);
        s_panelShown = true;
      }
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
      // 恢复 NOACTIVATE 并把焦点还给游戏（游戏完全恢复控制）
      LONG_PTR ex = GetWindowLongPtrW(g_guiHwnd, GWL_EXSTYLE);
      SetWindowLongPtrW(g_guiHwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
      if (GetForegroundWindow() == g_guiHwnd)
        SetForegroundWindow(g_gameHwnd);
      s_panelShown = false;
    }
    if (!s_panelShown) {
      // 隐藏覆盖层时仍跑游戏逻辑（冻结维持/IK写回/控制文件）
      __try { GameFrameTick(); } __except (1) {
        Log("[GUI] hidden GameFrameTick exception");
      }
      Sleep(30);
      continue;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    __try { DrawPoserGui(); } __except (1) {
      Log("[GUI] DrawPoserGui exception caught");
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
