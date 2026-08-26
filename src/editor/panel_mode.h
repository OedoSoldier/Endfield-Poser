#pragma once

// Task 3.4：双模式——摆姿模式 ↔ 镜头模式。
// 类比 Blender：Pose Mode 里相机保持固定（专注摆角色）；Camera Mode 自由相机取景。
// 顶栏一键互切（默认 Tab 键）。切到 Camera 时记住摆姿机位，切回 Pose 时恢复并锁定；
// Pose 模式下也可勾选"锁定镜头"解锁，用同一套自由相机键位（不抢左键选骨）。
//
// 依赖 game_hooks.h（主相机 Transform + 世界位姿读写）、config.h（相机速度）。

#include "imgui.h"
#include "core/game_hooks.h"
#include "math/quat_math.h"
#include "config.h"

#include <cmath>

enum class PoserMode { Pose, Camera };
static PoserMode g_mode = PoserMode::Pose;
static bool g_camLocked = true;     // Pose 模式下镜头是否锁定（不响应移动）
static bool g_camMouseLook = false; // Camera 模式下是否正在鼠标转向（按住右键）
static bool g_camSavedValid = false;
static Vec3 g_camSavedPos{0, 0, 0};
static Quat g_camSavedRot{0, 0, 0, 1};

// 主相机 Transform（nullptr = 不可用）
static void *GetMainCamTransform() {
  if (!g_camera_get_main || !g_component_get_transform)
    return nullptr;
  __try {
    void *cam = Invoke(g_camera_get_main, nullptr);
    if (!cam)
      return nullptr;
    return Invoke(g_component_get_transform, cam);
  } __except (1) {
    return nullptr;
  }
}

static void SetMode(PoserMode m) {
  if (m == g_mode)
    return;
  void *ct = GetMainCamTransform();
  if (m == PoserMode::Camera) {
    // 切到镜头模式：记住摆姿机位，之后自由移动
    if (ct) {
      g_camSavedPos = GetBoneWorldPos(ct);
      g_camSavedRot = GetBoneWorldRot(ct);
      g_camSavedValid = true;
    }
  } else {
    // 切回摆姿模式：相机恢复到记忆机位并锁定，便于继续摆姿
    if (ct && g_camSavedValid) {
      SetBoneWorldPos(ct, g_camSavedPos);
      SetBoneWorldRot(ct, g_camSavedRot);
    }
  }
  g_mode = m;
  Log("[POSER] Mode -> %s", m == PoserMode::Camera ? "Camera" : "Pose");
}

// 顶栏切换条（放在主窗口顶部）
static void DrawModeBar() {
  const bool isCam = (g_mode == PoserMode::Camera);
  ImGui::TextDisabled(u8"\u6a21\u5f0f"); // 模式
  ImGui::SameLine();
  if (ImGui::RadioButton(u8"\u6446\u59ff", !isCam))
    SetMode(PoserMode::Pose);
  ImGui::SameLine();
  if (ImGui::RadioButton(u8"\u955c\u5934", isCam))
    SetMode(PoserMode::Camera);
  ImGui::SameLine();
  ImGui::TextDisabled("(Tab)");
  if (!isCam) {
    ImGui::SameLine();
    ImGui::Checkbox(u8"\u9501\u5b9a\u955c\u5934", &g_camLocked);
  }
  if (isCam)
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                       u8"WSAD \u79fb\u52a8 / \u53f3\u952e\u8f6c\u5411 / \u6eda\u8f6e\u63a8\u62c9 / "
                       u8"Shift\u52a0\u901f / Space\u4e0a / Ctrl\u4e0b");
}

// ---- 自由相机（Camera 模式或 Pose 模式解锁镜头时）----
static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static void ApplyFreeCamera() {
  void *ct = GetMainCamTransform();
  if (!ct)
    return;
  ImGuiIO &io = ImGui::GetIO();

  g_camMouseLook = KeyDown(VK_RBUTTON);
  if (g_camMouseLook) {
    io.WantCaptureMouse = true; // 抢走鼠标，避免拖拽/点击 ImGui 窗口
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
  }

  Vec3 pos = GetBoneWorldPos(ct);
  Quat rot = GetBoneWorldRot(ct);

  // 滚轮推拉（沿前向）
  if (fabsf(io.MouseWheel) > 0.001f) {
    Vec3 fwd = Norm(rot * Vec3{0, 0, 1});
    pos = pos + fwd * (io.MouseWheel * g_cameraSpeed);
  }

  // 右键转向（水平 yaw 世界系 + 垂直 pitch 局部系）
  if (g_camMouseLook) {
    const float sens = 0.0025f;
    Quat qYaw = Quat::AxisAngle({0, 1, 0}, -io.MouseDelta.x * sens);
    Quat qPitch = Quat::AxisAngle({1, 0, 0}, -io.MouseDelta.y * sens);
    rot = NormQ(qYaw * rot * qPitch);
  }

  // WASD 平移（相对相机朝向）+ Shift 加速 + Space/Ctrl 升降
  Vec3 fwd = Norm(rot * Vec3{0, 0, 1});
  Vec3 right = Norm(rot * Vec3{1, 0, 0});
  Vec3 up = Norm(rot * Vec3{0, 1, 0});
  float speed = g_cameraSpeed * (KeyDown(VK_SHIFT) ? 3.0f : 1.0f);
  Vec3 move{0, 0, 0};
  if (KeyDown('W')) move = move + fwd;
  if (KeyDown('S')) move = move - fwd;
  if (KeyDown('D')) move = move + right;
  if (KeyDown('A')) move = move - right;
  if (KeyDown(VK_SPACE)) move = move + up;
  if (KeyDown(VK_CONTROL)) move = move - up;
  float l = Len(move);
  if (l > 0.01f) {
    move = move * (1.0f / l);
    pos = pos + move * (speed * (io.DeltaTime > 0.0f ? io.DeltaTime : 0.016f));
  }

  SetBoneWorldPos(ct, pos);
  SetBoneWorldRot(ct, rot);
}

// 每帧调度：Tab 切换 + 按模式驱动相机
static void ModeFrameTick() {
  static bool s_tabPrev = false;
  bool tab = KeyDown(VK_TAB) && !ImGui::GetIO().WantTextInput;
  if (tab && !s_tabPrev)
    SetMode(g_mode == PoserMode::Pose ? PoserMode::Camera : PoserMode::Pose);
  s_tabPrev = tab;

  if (g_mode == PoserMode::Camera || (g_mode == PoserMode::Pose && !g_camLocked))
    ApplyFreeCamera();
}

// 是否摆姿（非镜头）模式——gizmo 只在该模式下显示
static bool InPoseMode() { return g_mode == PoserMode::Pose; }
