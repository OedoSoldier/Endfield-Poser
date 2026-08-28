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
#include "game/smc_morph.h"
#include "editor/panel_pose.h"
#include "editor/panel_library.h"
#include "editor/panel_morph.h"
#include "config.h"
#include "core/web_server.h"

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
void GameFrameTick() {
  __try {
    // 光标管理：面板显示时解锁鼠标（方便拖面板），隐藏时恢复锁定
    static bool s_cursorManaged = false;
    if (g_guiVisible) {
      if (!s_cursorManaged && g_cursor_set_lockState && g_cursor_set_visible) {
        int v0 = 0; void *p0[] = {&v0};
        Invoke(g_cursor_set_lockState, nullptr, p0);
        int v1 = 1; void *p1[] = {&v1};
        Invoke(g_cursor_set_visible, nullptr, p1);
        s_cursorManaged = true;
      }
    } else if (s_cursorManaged) {
      int v = 1; void *p[] = {&v};
      Invoke(g_cursor_set_lockState, nullptr, p);
      s_cursorManaged = false;
    }
    // 角色切换 → 统一重建 Humanoid + 从骨列表（单一消费点，避免双消费）
    if (g_charChanged) {
    g_charChanged = false;
    RebuildHumanBones();
    RebuildAllBones();
    RebuildAccessories();
      RebuildBlendShapes(); // Task 4.1：形态键列表随角色重建
      ResetSMCState();      // Task 4.2：SMC 表情状态随角色重置
      for (int i = 0; i < kIkChainCount; i++)
        g_ikTargetValidChain[i] = false; // 角色切换 → IK 目标失效，按新末端重建
    }
    // 冻结态维持：每帧强制关闭 Animator/动画组件/IK 组件（游戏会重新启用）
    MaintainFreeze();
  } __except (1) {
    Log("[POSER] GameFrameTick SEH exception caught");
  }
}

// ---- 主面板：控制（冻结）+ 姿态编辑（Task 3.1）----
// 图钉：锁定全部面板窗口位置，防止拖火柴人/滑块时窗口跟着动
static bool g_pinPanels = true;

void DrawPoserGui() {
  __try { GameFrameTick(); } __except (1) {
    Log("[POSER] GameFrameTick SEH exception caught");
  }

  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 180), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Endfield Poser", nullptr,
                   ImGuiWindowFlags_NoCollapse |
                       (g_pinPanels ? ImGuiWindowFlags_NoMove : 0))) {
    ImGui::Text("v%s", POSER_VERSION);
    ImGui::SameLine();
    ImGui::Checkbox(u8"\u56fe\u9489", &g_pinPanels);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(u8"\u9501\u5b9a\u9762\u677f\u4f4d\u7f6e\uff1a\u62d6\u706b\u67f4\u4eba\u65f6\u7a97\u53e3\u4e0d\u8ddf\u7740\u52a8\uff1b\u53d6\u6d88\u540e\u53ef\u62d6\u6807\u9898\u79fb\u52a8");
    ImGui::Separator();
    ImGui::Text("Animator=%p  Bones=%d", g_charAnimator, s_humanBoneCount);
    ImGui::Separator();
    if (ImGui::Button(g_frozen ? "Unfreeze" : "Freeze Character")) {
      if (g_frozen) {
        UnfreezeCharacter();
        RestoreBlendShapes();     // 形态键恢复冻结前原始值
      } else {
        FreezeCharacter();
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
                   ImGuiWindowFlags_NoCollapse |
                       (g_pinPanels ? ImGuiWindowFlags_NoMove : 0))) {
    DrawPosePanel();
  }
  ImGui::End();

  // 姿态预设库（独立窗口）
  ImGui::SetNextWindowPos(ImVec2(340, 500), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(u8"\u59ff\u6001\u5e93", nullptr,
                   ImGuiWindowFlags_NoCollapse |
                       (g_pinPanels ? ImGuiWindowFlags_NoMove : 0))) {
    DrawLibraryPanel();
  }
  ImGui::End();

  // 形态键面板（面部 BlendShape，Task 4.1）
  ImGui::SetNextWindowPos(ImVec2(680, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 300), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(u8"\u5f62\u6001\u952e", nullptr,
                   ImGuiWindowFlags_NoCollapse |
                       (g_pinPanels ? ImGuiWindowFlags_NoMove : 0))) {
    DrawMorphPanel();
  }
  ImGui::End();

}

// 外部控制（PostMessage WM_APP+90 触发，绕过反作弊输入拦截）：
// 1=冻结/解冻 2=T-pose
static void ExtControl(int code) {
  switch (code) {
  case 1:
    if (g_frozen) {
      UnfreezeCharacter();
      RestoreBlendShapes();
    } else {
      FreezeCharacter();
    }
    break;
  case 2:
    ApplyTPose();
    break;
  default:
    break;
  }
}

// 控制文件通道：外部（Codex）往 plugin\poser_control.txt 写命令，每帧执行后清空。
// 命令：toggle / freeze / tpose / reset
static void ProcessControlFile() {
  FILE *f = fopen("plugin\\poser_control.txt", "r");
  if (!f)
    return;
  char line[64];
  while (fgets(line, sizeof(line), f)) {
    char *e = line + strlen(line) - 1;
    while (e > line && (*e == '\n' || *e == '\r' || *e == ' '))
      *e-- = 0;
    if (!*line)
      continue;
    if (strcmp(line, "toggle") == 0) {
      g_guiVisible = !g_guiVisible;
      Log("[CTRL] file toggle -> %d", (int)g_guiVisible);
    } else if (strcmp(line, "freeze") == 0) {
      if (g_frozen) {
        UnfreezeCharacter();
        RestoreBlendShapes();
      } else {
        FreezeCharacter();
      }
    } else if (strcmp(line, "tpose") == 0) {
      ApplyTPose();
    } else if (strcmp(line, "reset") == 0) {
      ApplyPoseSnapshot();
    } else if (strncmp(line, "select ", 7) == 0) {
      const char *boneName = line + 7;
      if (SelectBoneByName(boneName))
        Log("[CTRL] selected bone '%s' -> idx %d", boneName, g_selectedBone);
      else
        Log("[CTRL] bone not found: '%s'", boneName);
    } else if (strcmp(line, "bones") == 0) {
      Log("[CTRL] %d human bones:", s_humanBoneCount);
      for (int i = 0; i < s_humanBoneCount; i++)
        Log("[CTRL]   [%d] name='%s' human=%s", i, s_humanBones[i].name,
            HumanBoneName(s_humanBones[i].humanBone));
    } else {
      Log("[CTRL] unknown command: %s", line);
    }
  }
  fclose(f);
  remove("plugin\\poser_control.txt");
}

static DWORD WINAPI InitThread(LPVOID) {
  LoadPoserConfig();
  // 注册外部控制回调：PostMessage 通道（绕过反作弊对合成输入的拦截）
  SetExtControl(ExtControl);
  SetExtPollFn(ProcessControlFile);
  // 等待 GameAssembly.dll 加载并让 IL2CPP 域初始化（参照 {EIEM}/src/init.h）
  while (!GetModuleHandleW(L"GameAssembly.dll"))
    Sleep(500);
  Sleep(3000);
  Log("[POSER] Resolving IL2CPP...");
  if (!Resolve()) {
    Log("[POSER] ERROR: GameAssembly.dll not found or exports missing");
    return 0;
  }
  // 附加到 IL2CPP 域（域内方法/对象操作必需）
  void *domain = il2cpp_domain_get();
  if (domain)
    il2cpp_thread_attach(domain);
  // MinHook 初始化（MH_CreateHook 前置）
  if (MH_Initialize() != MH_OK)
    Log("[POSER] WARN: MH_Initialize failed");
  Log("[POSER] IL2CPP resolved. Initializing game hooks.");
  InitGameHooks(); // Task 2.1：SetMainCharacter hook → 捕获 Animator/Entity
  InstallSMCFaceHooks(); // Task 4.2：SkeletalMorph 表情 hook（参照 EIEM smc_face.h）
  StartWebServer(); // 独立 UI：localhost HTTP 服务器（浏览器打开控制窗口）
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
