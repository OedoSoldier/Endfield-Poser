#pragma once

// Task 5.1-5.2：相机面板 + 自由相机接管。
// - 接管：冻结/勾选时禁用主相机 GameObject 上的 CinemachineBrain（防游戏每帧
//   覆盖我们写的位姿），记录原始 FOV；解冻时恢复。
// - FOV 滑条（20-90°）实时写 Camera.set_fieldOfView。
// - 机位预设：记录/返回一组相机位姿。
// 相机移动（WSAD/右键/滚轮）由 panel_mode.h 的 ApplyFreeCamera 驱动。

#include "imgui.h"
#include "core/game_hooks.h"
#include "math/quat_math.h"
#include "editor/panel_mode.h"
#include "config.h"

#include <cstring>

static void *g_mainCamera = nullptr;
static void *g_mainCamTransform = nullptr;
static void *s_cinemachineBrain = nullptr; // 主相机上的 CinemachineBrain（若有）
static float g_origFov = 60.0f;
static bool g_camTakeover = false;
static float g_camFov = 60.0f; // 面板 FOV 值

static Vec3 g_camPresetPos{0, 0, 0};
static Quat g_camPresetRot{0, 0, 0, 1};
static bool g_camPresetValid = false;

// 解析主相机 + 查找 CinemachineBrain（角色/相机切换后懒加载）
static void ResolveMainCamera() {
  g_mainCamera = nullptr;
  g_mainCamTransform = nullptr;
  s_cinemachineBrain = nullptr;
  if (!g_camera_get_main || !g_component_get_transform)
    return;
  __try {
    g_mainCamera = Invoke(g_camera_get_main, nullptr);
    if (!g_mainCamera)
      return;
    g_mainCamTransform = Invoke(g_component_get_transform, g_mainCamera);

    void *camGO = g_component_get_gameObject
                      ? Invoke(g_component_get_gameObject, g_mainCamera)
                      : nullptr;
    if (!camGO || !g_gameObject_GetComponents || !g_componentClass)
      return;
    void *compType = il2cpp_class_get_type(g_componentClass);
    void *typeObj = compType ? il2cpp_type_get_object(compType) : nullptr;
    if (!typeObj)
      return;
    void *args[] = {typeObj};
    void *arr = Invoke(g_gameObject_GetComponents, camGO, args);
    if (arr) {
      int cnt = *(int *)((char *)arr + 24);
      void **data = (void **)((char *)arr + 32);
      for (int i = 0; i < cnt; i++) {
        if (!data[i])
          continue;
        void *cls = il2cpp_object_get_class(data[i]);
        const char *cn = cls ? il2cpp_class_get_name(cls) : "";
        if (cn && strcmp(cn, "CinemachineBrain") == 0) {
          s_cinemachineBrain = data[i];
          break;
        }
      }
    }
    Log("[POSER] Camera resolved: cam=%p brain=%p", g_mainCamera,
        s_cinemachineBrain);
  } __except (1) {
  }
}

// 接管/释放相机：禁用/启用 CinemachineBrain，记录或还原 FOV
static void CameraTakeover(bool on) {
  if (on && !g_camTakeover)
    ResolveMainCamera();
  if (on && !g_camTakeover && g_mainCamera && g_camera_get_fieldOfView) {
    __try {
      void *fovBox = Invoke(g_camera_get_fieldOfView, g_mainCamera);
      if (fovBox)
        g_origFov = *(float *)((char *)fovBox + 16);
      g_camFov = g_origFov;
    } __except (1) {
    }
  }
  if (s_cinemachineBrain && g_animator_set_enabled) {
    __try {
      int v = on ? 0 : 1;
      void *params[] = {&v};
      Invoke(g_animator_set_enabled, s_cinemachineBrain, params);
    } __except (1) {
    }
  }
  if (!on && g_mainCamera && g_camera_set_fieldOfView) {
    __try {
      void *params[] = {&g_origFov};
      Invoke(g_camera_set_fieldOfView, g_mainCamera, params);
      g_camFov = g_origFov;
    } __except (1) {
    }
  }
  g_camTakeover = on;
  Log("[POSER] Camera takeover=%s (origFov=%.2f)", on ? "ON" : "OFF",
      g_origFov);
}

static void DrawCameraPanel() {
  bool t = g_camTakeover;
  if (ImGui::Checkbox(u8"\u63a5\u7ba1\u76f8\u673a\uff08\u7981 CinemachineBrain\uff09", &t))
    CameraTakeover(t);

  ImGui::Separator();

  // FOV 滑条 + 还原
  float fov = g_camFov;
  bool fovChanged = false;
  if (ImGui::SliderFloat(u8"FOV", &fov, 20.0f, 90.0f, "%.0f"))
    fovChanged = true;
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u8fd8\u539f##fov")) {
    fov = g_origFov;
    fovChanged = true;
  }
  if (fovChanged) {
    g_camFov = fov;
    if (g_mainCamera && g_camera_set_fieldOfView) {
      __try {
        void *params[] = {&fov};
        Invoke(g_camera_set_fieldOfView, g_mainCamera, params);
      } __except (1) {
      }
    }
  }

  ImGui::SliderFloat(u8"\u76f8\u673a\u901f\u5ea6", &g_cameraSpeed, 0.5f, 50.0f,
                     "%.1f");

  ImGui::Separator();

  // 机位预设：记录 / 返回
  void *ct = GetMainCamTransform();
  if (ImGui::SmallButton(u8"\u8bb0\u5f55\u673a\u4f4d")) {
    if (ct) {
      g_camPresetPos = GetBoneWorldPos(ct);
      g_camPresetRot = GetBoneWorldRot(ct);
      g_camPresetValid = true;
      Log("[POSER] Camera preset saved");
    }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u8fd4\u56de\u673a\u4f4d") && g_camPresetValid && ct) {
    SetBoneWorldPos(ct, g_camPresetPos);
    SetBoneWorldRot(ct, g_camPresetRot);
    Log("[POSER] Camera preset restored");
  }
  ImGui::SameLine();
  ImGui::TextDisabled(u8"相机位姿: (%.1f, %.1f, %.1f)", ct ? GetBoneWorldPos(ct).x : 0,
                      ct ? GetBoneWorldPos(ct).y : 0, ct ? GetBoneWorldPos(ct).z : 0);
}
