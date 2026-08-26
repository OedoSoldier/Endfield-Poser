// Endfield Poser 插件壳。
// 依赖: core/base.h, core/il2cpp_api.h, core/gui_overlay.h, config.h
// 负责：DLL 入口、Applepie 插件协议导出、IL2CPP 解析与 GUI 线程启动。
// 后续阶段的冻结/角色捕获/相机等通过 GameFrameTick() 接入（见 Task 2.1+）。

#include <cstdint>

#include "core/base.h"
#include "core/il2cpp_api.h"
#include "core/gui_overlay.h"
#include "core/game_hooks.h"
#include "config.h"

// ---- Applepie 插件协议（与 {EIEM}/src/applepie_mgr.h 一致）----
#define APPLEPIE_PLUGIN_API_VERSION 1
#ifdef APPLEPIE_PLUGIN_IMPL
  #define APPLEPIE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
  #define APPLEPIE_PLUGIN_EXPORT extern "C" __declspec(dllimport)
#endif

struct AP_PluginInfo {
  int apiVersion;
  const char *id;
  const char *displayName;
  const char *description;
  const char *configFile;
  bool supportsHotDisable;
};
struct AP_HotkeyInfo {
  const char *name;
  const char *configKey;
  int currentVK;
};

static AP_PluginInfo g_info = {
    APPLEPIE_PLUGIN_API_VERSION, "poser", "Endfield Poser",
    "Game photography posing tool (FK/IK + morph keys + camera)",
    "plugin\\poser_config.txt", true};

APPLEPIE_PLUGIN_EXPORT AP_PluginInfo *AP_GetPluginInfo() { return &g_info; }
APPLEPIE_PLUGIN_EXPORT bool AP_PluginEnable() {
  StartGuiThread();
  return true;
}
APPLEPIE_PLUGIN_EXPORT bool AP_PluginDisable() {
  StopGuiThread();
  return true;
}
APPLEPIE_PLUGIN_EXPORT bool AP_ReloadConfig() { return LoadPoserConfig(); }
APPLEPIE_PLUGIN_EXPORT int AP_GetHotkeys(AP_HotkeyInfo *out, int max) {
  if (max < 2) return 2;
  out[0] = {"Toggle Poser GUI", "gui_toggle_key", g_guiToggleVK};
  out[1] = {"Screenshot", "screenshot_key", g_screenshotVK};
  return 2;
}
APPLEPIE_PLUGIN_EXPORT void AP_SetLanguage(const char *) {}

// ---- 每帧更新（占位）：后续由冻结/IK/相机模块填充 ----
static void GameFrameTick() {
  // 阶段 2+：维持冻结、IK 写回、相机控制
}

// ---- 主面板（占位）：Task 0.3 只验证窗口弹出；editor/gui.h 将替换 ----
void DrawPoserGui() {
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Endfield Poser", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    ImGui::Text("v%s", POSER_VERSION);
    ImGui::Text("GUI shell loaded. (Placeholder)");
    ImGui::Separator();
    if (ImGui::Button("Toggle Freeze (WIP)"))
      GameFrameTick();
  }
  ImGui::End();
}

static DWORD WINAPI InitThread(LPVOID) {
  LoadPoserConfig();
  Log("[POSER] Resolving IL2CPP...");
  if (!Resolve()) {
    Log("[POSER] ERROR: GameAssembly.dll not found or exports missing");
    return 0;
  }
  Log("[POSER] IL2CPP resolved. Initializing game hooks.");
  InitGameHooks(); // Task 2.1：SetMainCharacter hook → 捕获 Animator/Entity
  Log("[POSER] Starting GUI thread.");
  StartGuiThread();
  return 0;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(0);
    OpenLog("plugin\\poser_log.txt");
    Log("[POSER] === Endfield Poser v%s attached ===", POSER_VERSION);
    CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
  }
  return TRUE;
}
