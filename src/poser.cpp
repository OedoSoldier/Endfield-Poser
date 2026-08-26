// Endfield Poser 插件壳。
// 依赖: core/base.h, core/il2cpp_api.h, core/gui_overlay.h, config.h
// 负责：DLL 入口、Applepie 插件协议导出、IL2CPP 解析与 GUI 线程启动。
// 后续阶段的冻结/角色捕获/相机等通过 GameFrameTick() 接入（见 Task 2.1+）。

#include <cstdint>

#include "core/base.h"
#include "core/il2cpp_api.h"
#include "core/gui_overlay.h"
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "game/accessory.h"
#include "game/freeze.h"
#include "game/ik_driver.h"
#include "game/morph.h"
#include "editor/panel_pose.h"
#include "editor/panel_library.h"
#include "editor/panel_mode.h"
#include "editor/panel_morph.h"
#include "editor/panel_camera.h"
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

// ---- 每帧更新（阶段 2+：冻结维持、骨骼列表维护、IK 写回、相机）----
static void GameFrameTick() {
  // 角色切换 → 统一重建 Humanoid + 从骨列表（单一消费点，避免双消费）
  if (g_charChanged) {
    g_charChanged = false;
    RebuildHumanBones();
    RebuildAccessories();
    RebuildBlendShapes(); // Task 4.1：形态键列表随角色重建
    g_ikTargetValid = false; // 角色切换 → IK 目标失效，按新末端重建
  }
  // 冻结态下的 IK 写回（Task 3.2）
  IkFrameTick();
  // 双模式：Tab 切换 + 按模式驱动相机（Task 3.4）
  ModeFrameTick();
  // 阶段 3+：姿态操作、相机控制
}

// ---- 主面板：控制（冻结）+ 姿态编辑（Task 3.1）----
void DrawPoserGui() {
  GameFrameTick(); // 每帧：骨骼列表维护、冻结维持、IK 写回、相机控制

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Endfield Poser", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    ImGui::Text("v%s", POSER_VERSION);
    DrawModeBar(); // 摆姿/镜头双模式切换（Task 3.4）
    ImGui::Separator();
    ImGui::Text("Animator=%p  Bones=%d", g_charAnimator, s_humanBoneCount);
    ImGui::Separator();
    if (ImGui::Button(g_frozen ? "Unfreeze" : "Freeze Character")) {
      if (g_frozen) {
        UnfreezeCharacter();
        RestoreBlendShapes();     // 形态键恢复冻结前原始值
        CameraTakeover(false);    // 释放相机接管（Task 5.1）
      } else {
        FreezeCharacter();
        CameraTakeover(true);     // 接管相机，防游戏覆盖（Task 5.1）
      }
    }
    if (!g_frozen)
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                         u8"\u8bf7\u5148\u51bb\u7ed3\u89d2\u8272\u518d\u6446\u59ff");
  }
  ImGui::End();

  // 姿态编辑面板（独立窗口，可拖到一侧）
  ImGui::SetNextWindowPos(ImVec2(340, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(560, 480), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(u8"\u59ff\u6001 (FK)", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    DrawPosePanel();
  }
  ImGui::End();

  // 姿态预设库（独立窗口）
  ImGui::SetNextWindowPos(ImVec2(340, 500), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(u8"\u59ff\u6001\u5e93", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    DrawLibraryPanel();
  }
  ImGui::End();

  // 形态键面板（面部 BlendShape，Task 4.1）
  ImGui::SetNextWindowPos(ImVec2(680, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(u8"\u5f62\u6001\u952e", nullptr,
                   ImGuiWindowFlags_NoCollapse)) {
    DrawMorphPanel();
  }
  ImGui::End();

  // 相机面板（接管 / FOV / 机位预设，Task 5.1-5.2）
  ImGui::SetNextWindowPos(ImVec2(680, 320), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(u8"\u76f8\u673a", nullptr, ImGuiWindowFlags_NoCollapse)) {
    DrawCameraPanel();
  }
  ImGui::End();

  // 3D 手柄覆盖整个视口
  DrawPoseGizmoOverlay();
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
