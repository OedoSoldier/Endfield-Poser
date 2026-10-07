#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <atomic>
#include <algorithm>
#include <cwchar>
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"gdi32.lib")

// Display only. Export reads Unity's texture, never this window. No screen
// readback, image upload, or additional game render is needed for this cover.
namespace recording_display {
static std::atomic<bool> requested{false},ready{false},failed{false},restoring{false};
static std::atomic<unsigned> done{0},total{0};
static HWND window=nullptr; // All HWND/GDI lifetime belongs to the GUI thread.
static HMODULE windowModule=nullptr;
static ULONGLONG nextTick=0;
static unsigned paintedDone=~0u,paintedTotal=~0u;
static bool paintedRestore=false;
inline void Begin(bool enabled) {
  ready=false;failed=false;restoring=false;done=total=0;requested=enabled;
}
inline void End(){requested=false;ready=false;}
inline LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
  if(msg==WM_NCHITTEST)return HTTRANSPARENT;
  if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
  if(msg==WM_ERASEBKGND)return 1;
  if(msg==WM_PAINT) {
    PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);RECT rect{};GetClientRect(hwnd,&rect);
    auto brush=CreateSolidBrush(RGB(22,27,37));FillRect(dc,&rect,brush);DeleteObject(brush);
    const int h=(std::max)(18,(std::min)(36,int(rect.bottom/36)));
    auto font=CreateFontW(-h,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(226,232,244));
    wchar_t text[256];const auto n=done.load(),count=total.load();
    swprintf_s(text,L"%s\n\n已保存 %u / %u 帧\n\n分层中间画面已隐藏，导出图像不受影响。\n可在 Poser 录制面板停止。",
      restoring.load()?L"正在恢复游戏画面…":count?L"正在录制 MMD":L"正在准备录制…",n,count);
    RECT lines=rect;DrawTextW(dc,text,-1,&lines,DT_CENTER|DT_CALCRECT|DT_NOPREFIX);
    rect.top=(std::max)(0L,(rect.bottom-(lines.bottom-lines.top))/2);
    DrawTextW(dc,text,-1,&rect,DT_CENTER|DT_NOPREFIX);
    SelectObject(dc,old);if(font)DeleteObject(font);EndPaint(hwnd,&ps);return 0;
  }
  return DefWindowProcW(hwnd,msg,w,l);
}
inline void Shutdown() {
  End();if(window){DestroyWindow(window);window=nullptr;}
  if(windowModule){UnregisterClassW(L"EndfieldPoserRecordingDisplay",windowModule);windowModule=nullptr;}nextTick=0;
}
inline void Tick(HWND game,HWND panels) {
  if(!requested.load()) {if(window||windowModule)Shutdown();return;}
  if(failed.load())return;
  const auto now=GetTickCount64();if(now<nextTick)return;nextTick=now+16;
  if(!IsWindow(game)){failed=true;return;}
  const auto fg=GetForegroundWindow();const bool show=fg&&!IsIconic(game)&&(fg==game||fg==panels);
  if(!show){if(window)ShowWindow(window,SW_HIDE);return;}
  RECT rect{};POINT origin{};
  if(!GetClientRect(game,&rect)||!ClientToScreen(game,&origin)||rect.right<=0||rect.bottom<=0) {failed=true;return;}
  if(!window) {
    // Use this DLL's module, so WndProc never points at unloaded executable code.
    HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
      reinterpret_cast<LPCWSTR>(&WndProc),&module);
    WNDCLASSW cls{};cls.hInstance=module;cls.lpfnWndProc=WndProc;cls.lpszClassName=L"EndfieldPoserRecordingDisplay";
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS){failed=true;return;}
    windowModule=module;
    window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,
      cls.lpszClassName,L"Endfield Poser recording",WS_POPUP,origin.x,origin.y,rect.right,rect.bottom,game,nullptr,module,nullptr);
    if(!window||!SetLayeredWindowAttributes(window,0,255,LWA_ALPHA)){failed=true;if(window){DestroyWindow(window);window=nullptr;}return;}
    paintedDone=paintedTotal=~0u;
  }
  RECT current{};GetWindowRect(window,&current);
  const bool moved=current.left!=origin.x||current.top!=origin.y||current.right-current.left!=rect.right||current.bottom-current.top!=rect.bottom;
  const bool wasVisible=IsWindowVisible(window)!=FALSE;
  // Keep the interactive Poser panels above the input-transparent cover.
  HWND behind=IsWindowVisible(panels)?panels:HWND_TOPMOST;
  const bool orderChanged=behind==HWND_TOPMOST?!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST):GetWindow(window,GW_HWNDPREV)!=behind;
  if(moved||!wasVisible||orderChanged) {
    if(!SetWindowPos(window,behind,origin.x,origin.y,rect.right,rect.bottom,SWP_NOACTIVATE|SWP_SHOWWINDOW)) {failed=true;return;}
  }
  if(!wasVisible||moved||paintedDone!=done.load()||paintedTotal!=total.load()||paintedRestore!=restoring.load()) {
    paintedDone=done;paintedTotal=total;paintedRestore=restoring;
    InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
  }
  if(!ready.load()) {
    GdiFlush();
    if(FAILED(DwmFlush())){failed=true;return;}
    if(requested.load())ready=true; // Producer waits for the first painted cover.
  }
}
}
