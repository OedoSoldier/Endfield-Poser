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
#include "game/morph.h"
#include "game/smc_morph.h"
#include "editor/selection.h"
#include "editor/rig_gizmo.h"
#include "editor/panel_library.h"
#include "editor/panel_morph.h"
#include "config.h"

// 手动刷新骨骼（面板按钮 / WebUI /api/refresh 共用）
static void RefreshCharacterBones();

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

// ---- 光标状态：首次打开面板时记录游戏原始值，关闭时原样恢复 ----
static bool s_cursorStateSaved = false;
static int s_origLockState = 0;
static bool s_origVisible = true;

static int CursorLockState() {
  if (!g_cursor_get_lockState)
    return 0;
  __try {
    void *boxed = Invoke(g_cursor_get_lockState, nullptr);
    return boxed ? *(int *)((char *)boxed + 16) : 0;
  } __except (1) {
    return 0;
  }
}

static bool CursorVisible() {
  if (!g_cursor_get_visible)
    return true;
  __try {
    void *boxed = Invoke(g_cursor_get_visible, nullptr);
    return boxed ? *(bool *)((char *)boxed + 16) : true;
  } __except (1) {
    return true;
  }
}

// ---- 每帧更新（阶段 2+：冻结维持、骨骼列表维护、IK 写回、相机）----
void GameFrameTick() {
  __try {
    // 光标管理：面板显示时隐藏 Unity 光标、由 ImGui 画唯一光标（避免双光标）；
    // 隐藏时恢复游戏原始光标状态（锁定/隐藏）。
    static bool s_cursorManaged = false;
    if (g_guiVisible) {
      if (!s_cursorManaged) {
        if (!s_cursorStateSaved) {
          s_origLockState = CursorLockState();
          s_origVisible = CursorVisible();
          s_cursorStateSaved = true;
          Log("[POSER] Cursor saved: lock=%d visible=%d", s_origLockState,
              (int)s_origVisible);
        }
        if (g_cursor_set_lockState) {
          int v0 = 0;
          void *p0[] = {&v0};
          Invoke(g_cursor_set_lockState, nullptr, p0);
        }
        if (g_cursor_set_visible) {
          int v1 = 0; // 隐藏 Unity 光标，避免和覆盖层光标形成"双光标"
          void *p1[] = {&v1};
          Invoke(g_cursor_set_visible, nullptr, p1);
        }
        ImGui::GetIO().MouseDrawCursor = true; // 覆盖层绘制唯一光标
        s_cursorManaged = true;
        // 释放游戏窗口可能持有的鼠标捕获，否则点击会被游戏窗口截走，
        // 覆盖层（ImGui 面板）收不到鼠标消息。
        SetCapture(nullptr);
        ReleaseCapture();
      }
    } else if (s_cursorManaged) {
      ImGui::GetIO().MouseDrawCursor = false;
      if (g_cursor_set_lockState) {
        int v = s_origLockState;
        void *p[] = {&v};
        Invoke(g_cursor_set_lockState, nullptr, p);
      }
      if (g_cursor_set_visible) {
        int v = s_origVisible ? 1 : 0;
        void *p[] = {&v};
        Invoke(g_cursor_set_visible, nullptr, p);
      }
      s_cursorManaged = false;
      Log("[POSER] Cursor restored: lock=%d visible=%d", s_origLockState,
          (int)s_origVisible);
    }
    // 角色捕获自愈：SetMainCharacter hook 漏触发/时机错过时，
    // 周期性从 PlayerController 补捞当前角色（约每 2 秒一次）。
    // 骨骼数为 0 也要补捞：角色切换/场景变化后 g_charAnimator 可能残留
    // 失效指针（非空），此时重建出来是 0 根骨，必须强制重新捕获。
    static int s_captureRetry = 0;
    if (!g_charAnimator || s_humanBoneCount == 0) {
      if (++s_captureRetry >= 60) {
        s_captureRetry = 0;
        TryCaptureFromPlayerController();
        // 实体/动画器未变化但骨骼仍为 0：强制重建一次，等角色恢复后接上
        if (s_humanBoneCount == 0) {
          RebuildAllBones();   // 先刷全骨列表：humanoid 缺失骨按名回退依赖它
          RebuildHumanBones();
          Log("[POSER] Re-capture retry: animator=%p bones=%d",
              g_charAnimator, s_humanBoneCount);
        }
      }
    } else {
      s_captureRetry = 0;
    }
    // 冻结热键 F9：面板按钮万一点不到时的可靠通道（隐藏面板时也生效）
    if (GetAsyncKeyState(VK_F9) & 1) {
      Log("[CTRL] F9 hotkey -> toggle freeze");
      if (g_frozen) {
        UnfreezeCharacter();
        RestoreBlendShapes();
      } else {
        FreezeCharacter();
      }
    }
    // 角色切换 → 统一重建 Humanoid + 从骨列表（单一消费点，避免双消费）
    if (g_charChanged) {
    g_charChanged = false;
    s_restCaptured = false; // 新角色：A-pose 基线作废，下次重建时重捕
    RebuildAllBones();   // 先刷全骨列表：humanoid 缺失骨按名回退依赖它
    RebuildHumanBones();
    RebuildAccessories();
      RebuildBlendShapes(); // Task 4.1：形态键列表随角色重建
      ResetSMCState();      // Task 4.2：SMC 表情状态随角色重置
      ResetSkirtState();    // 裙子碰撞：清空旧角色布料采集
      if (!s_restCaptured)
        CaptureRestPose();  // 角色最初姿态 = A-pose 基线
    }
    // 冻结态维持：每帧强制关闭 Animator/动画组件/IK 组件（游戏会重新启用）
    MaintainFreeze();
  } __except (1) {
    Log("[POSER] GameFrameTick SEH exception caught");
  }
}

// 手动刷新：重跑角色骨骼/从骨/形态键重建链（某些场景无法切换角色时用）
static void RefreshCharacterBones() {
  if (!g_charAnimator || s_humanBoneCount == 0)
    TryCaptureFromPlayerController();
  bool newChar = g_charChanged;
  g_charChanged = false;
  if (newChar)
    s_restCaptured = false; // 新角色：重捕 A-pose 基线
  RebuildAllBones(); // 先刷全骨列表：humanoid 缺失骨按名回退依赖它
  RebuildHumanBones();
  RebuildAccessories();
  RebuildBlendShapes();
  ResetSMCState();
  ResetSkirtState();
  if (!s_restCaptured)
    CaptureRestPose();
  Log("[POSER] Manual bone refresh: human=%d", s_humanBoneCount);
}

// ---- 主面板：控制（冻结）+ 姿态编辑（Task 3.1）----
// 图钉：锁定全部面板窗口位置，防止拖火柴人/滑块时窗口跟着动
static bool g_pinPanels = true;

void DrawPoserGui() {
  __try { GameFrameTick(); } __except (1) {
    Log("[POSER] GameFrameTick exception code=0x%X", GetExceptionCode());
  }
  ImGuizmo::BeginFrame(); // ImGuizmo 每帧初始化（draw list / 内部窗口），否则轮盘不绘制
  __try {
    DrawSkeletonOverlay();
  } __except (1) {
    Log("[POSER] DrawSkeletonOverlay exception code=0x%X", GetExceptionCode());
  }
  __try {
    HandleRigClick();
  } __except (1) {
    Log("[POSER] HandleRigClick exception code=0x%X", GetExceptionCode());
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
    ImGui::Checkbox(u8"\u663e\u793a\u9aa8\u9abc", &g_showBones);
    ImGui::SameLine();
    ImGui::TextDisabled(
        g_selectedName[0] ? g_selectedName : u8"\u672a\u9009\u4e2d");
    ImGui::Text("Bones=%d  Overlay: %s", s_humanBoneCount, g_overlayStatus);
    ImGui::Checkbox(u8"\u5168\u91cf\u9aa8\u9abc(\u5fae\u8c03)", &g_fullBones);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(u8"\u9ed8\u8ba4\u53ea\u663e\u793a\u4e3b\u8981\u9aa8\u9abc\uff1b\u52fe\u9009\u540e\u53e0\u52a0\u5c42\u5c55\u793a/\u53ef\u62fe\u53d6\u6240\u6709\u9aa8\u9abc\uff08\u542b\u624b\u6307\u7b49\uff09\uff0c\u7528\u4e8e\u7cbe\u7ec6\u5fae\u8c03\u3002\u5168\u91cf\u6536\u96c6\u59cb\u7ec8\u8fdb\u884c\u3002");
    if (g_fullBones && s_allBones.empty())
      RebuildAllBones();
    if (!g_fullBones && FindTransformIndex(g_selectedTransform) < 0)
      SelectTransform(nullptr, nullptr);
    ImGui::Separator();
    if (ImGui::Button(g_frozen ? "Unfreeze" : "Freeze Character")) {
      Log("[GUI] Freeze button clicked (frozen=%d animator=%p bones=%d)",
          (int)g_frozen, g_charAnimator, s_humanBoneCount);
      if (g_frozen) {
        UnfreezeCharacter();
        RestoreBlendShapes();     // 形态键恢复冻结前原始值
      } else {
        FreezeCharacter();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"\u5237\u65b0\u9aa8\u9abc")) // 刷新骨骼
      RefreshCharacterBones();
    bool accPrev = g_freezeAccessories;
    ImGui::Checkbox(u8"\u51bb\u7ed3\u98d8\u5e26/\u88d9\u5b50/\u5934\u53d1",
                    &g_freezeAccessories);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(u8"\u9ed8\u8ba4\u5173\uff1a\u51bb\u7ed3\u540e\u4ece\u9aa8\u4fdd\u6301\u5b9e\u65f6\u6f14\u7b97\uff1b\u52fe\u9009\u540e\u8fde\u540c\u4e00\u8d77\u51bb\u7ed3");
    if (g_frozen && accPrev != g_freezeAccessories) {
      if (g_freezeAccessories) {
        if (s_accessoryChains.empty())
          RebuildAccessories();
        SetAllPhysicsEnabled(false);
      } else {
        SetAllPhysicsEnabled(true);
      }
    }
    // 根骨骼位置微调（整体位移；冻结态直接写回）
    void *rootT = nullptr;
    for (size_t i = 0; i < s_allBones.size(); i++)
      if (s_allBones[i].parentIdx < 0) {
        rootT = s_allBones[i].transform;
        break;
      }
    if (!rootT && s_humanBoneCount > 0)
      rootT = s_humanBones[0].transform; // 回退：Hips
    if (g_frozen && rootT) {
      Vec3 lp = GetBoneLocalPos(rootT);
      float vx = lp.x, vy = lp.y, vz = lp.z;
      bool changed = false;
      changed |= ImGui::SliderFloat(u8"##rootpx", &vx, -10.0f, 10.0f,
                                    "Root X %.2f");
      changed |= ImGui::SliderFloat(u8"##rootpy", &vy, -10.0f, 10.0f,
                                    "Root Y %.2f");
      changed |= ImGui::SliderFloat(u8"##rootpz", &vz, -10.0f, 10.0f,
                                    "Root Z %.2f");
      if (changed)
        SetBoneLocalPos(rootT, Vec3{vx, vy, vz});
    }
  }
  ImGui::End();

  // 旋转盘：全屏无交互窗口内绘制，避免被小窗口裁剪
  {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin(u8"##rig_gizmo_layer", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs)) {
      __try {
        DrawBoneRotationGizmo();
      } __except (1) {
        Log("[POSER] DrawBoneRotationGizmo exception code=0x%X",
            GetExceptionCode());
      }
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }

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
