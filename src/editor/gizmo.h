#pragma once

// Task 3.1：ImGuizmo 集成。
// 负责把选中骨的世界矩阵喂给 ImGuizmo::Manipulate，并把返回的 delta
// 矩阵（LOCAL 模式 = 骨局部坐标系下的增量）换算回骨 localRotation/localPosition
// 写回。还负责从主相机构建 view/projection 矩阵，让手柄与 3D 视口对齐。
//
// 约定：矩阵全部为列主序 float[16]（ImGuizmo/glm 布局）。
// [in-game] 说明：Unity 的 Transform.get_rotation/get_position 是世界位姿，
// 相机主矩阵用它们构建；若游戏主相机被 Cinemachine 接管（Task 5.1），本模块
// 会自然跟随其当前位姿。

#include "imgui.h"
#include "ImGuizmo.h"
#include "math/quat_math.h"
#include "core/game_hooks.h"

#include <cmath>
#include <cstring>

// ---- 列主序 4x4 基础 ----
static void Mat4Identity(float m[16]) {
  for (int i = 0; i < 16; i++)
    m[i] = 0;
  m[0] = m[5] = m[10] = m[15] = 1;
}

// pos + quat -> 列主序矩阵（TR）
static void Mat4Compose(const Vec3 &pos, const Quat &q, float m[16]) {
  float x = q.x, y = q.y, z = q.z, w = q.w;
  float x2 = x + x, y2 = y + y, z2 = z + z;
  float xx = x * x2, xy = x * y2, xz = x * z2;
  float yy = y * y2, yz = y * z2, zz = z * z2;
  float wx = w * x2, wy = w * y2, wz = w * z2;
  m[0] = 1 - (yy + zz); m[1] = xy + wz;     m[2] = xz - wy;     m[3] = 0;
  m[4] = xy - wz;       m[5] = 1 - (xx + zz); m[6] = yz + wx;   m[7] = 0;
  m[8] = xz + wy;       m[9] = yz - wx;     m[10] = 1 - (xx + yy); m[11] = 0;
  m[12] = pos.x;        m[13] = pos.y;      m[14] = pos.z;     m[15] = 1;
}

// 从矩阵提取 pos + quat（标准四元数-from-矩阵）
static void Mat4Decompose(const float m[16], Vec3 &pos, Quat &q) {
  pos = {m[12], m[13], m[14]};
  float tr = m[0] + m[5] + m[10];
  float w, x, y, z;
  if (tr > 0.0f) {
    float s = std::sqrt(tr + 1.0f) * 2.0f;
    w = 0.25f * s;
    x = (m[6] - m[9]) / s;
    y = (m[8] - m[2]) / s;
    z = (m[1] - m[4]) / s;
  } else if (m[0] > m[5] && m[0] > m[10]) {
    float s = std::sqrt(1.0f + m[0] - m[5] - m[10]) * 2.0f;
    w = (m[6] - m[9]) / s;
    x = 0.25f * s;
    y = (m[1] + m[4]) / s;
    z = (m[8] + m[2]) / s;
  } else if (m[5] > m[10]) {
    float s = std::sqrt(1.0f + m[5] - m[0] - m[10]) * 2.0f;
    w = (m[8] - m[2]) / s;
    x = (m[1] + m[4]) / s;
    y = 0.25f * s;
    z = (m[6] + m[9]) / s;
  } else {
    float s = std::sqrt(1.0f + m[10] - m[0] - m[5]) * 2.0f;
    w = (m[1] - m[4]) / s;
    x = (m[8] + m[2]) / s;
    y = (m[6] + m[9]) / s;
    z = 0.25f * s;
  }
  q = NormQ(Quat{x, y, z, w});
}

static void Mat4Mul(const float a[16], const float b[16], float out[16]) {
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++)
      out[c * 4 + r] =
          a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] +
          a[2 * 4 + r] * b[c * 4 + 2] + a[3 * 4 + r] * b[c * 4 + 3];
}

// 沿父链自根向下组合出该骨的世界矩阵（local pos/rot 逐级相乘）
static bool GetBoneWorldMatrix(void *transform, float out[16]) {
  if (!transform || !g_transform_get_parent)
    return false;
  __try {
    void *chain[32];
    int n = 0;
    void *cur = transform;
    while (cur && n < 32) {
      chain[n++] = cur;
      cur = Invoke(g_transform_get_parent, cur);
    }
    Mat4Identity(out);
    for (int i = n - 1; i >= 0; i--) {
      float m[16], tmp[16];
      Mat4Compose(GetBoneLocalPos(chain[i]), GetBoneLocalRot(chain[i]), m);
      Mat4Mul(out, m, tmp);
      memcpy(out, tmp, sizeof(tmp));
    }
    return true;
  } __except (1) {
    return false;
  }
}

// 从主相机构建 view + projection（列主序）。返回 false 表示相机不可用。
static bool GetCameraViewProj(float view[16], float proj[16]) {
  if (!g_camera_get_main || !g_component_get_transform)
    return false;
  __try {
    void *cam = Invoke(g_camera_get_main, nullptr);
    if (!cam)
      return false;
    void *ct = Invoke(g_component_get_transform, cam);
    if (!ct)
      return false;
    Vec3 pos = GetBoneWorldPos(ct);
    Quat rot = GetBoneWorldRot(ct);

    // view = inverse(TR)：转置旋转部分，平移取负
    float wm[16];
    Mat4Compose(pos, rot, wm);
    view[0] = wm[0];  view[1] = wm[4];  view[2] = wm[8];  view[3] = 0;
    view[4] = wm[1];  view[5] = wm[5];  view[6] = wm[9];  view[7] = 0;
    view[8] = wm[2];  view[9] = wm[6];  view[10] = wm[10]; view[11] = 0;
    view[12] = -(wm[0] * pos.x + wm[1] * pos.y + wm[2] * pos.z);
    view[13] = -(wm[4] * pos.x + wm[5] * pos.y + wm[6] * pos.z);
    view[14] = -(wm[8] * pos.x + wm[9] * pos.y + wm[10] * pos.z);
    view[15] = 1;

    float fov = 60.0f;
    if (g_camera_get_fieldOfView) {
      void *boxed = Invoke(g_camera_get_fieldOfView, cam);
      if (boxed)
        fov = *(float *)((char *)boxed + 16);
    }
    ImGuiIO &io = ImGui::GetIO();
    float aspect = io.DisplaySize.y > 1.0f ? io.DisplaySize.x / io.DisplaySize.y
                                           : 1.0f;
    const float nearP = 0.01f, farP = 1000.0f;
    float f = 1.0f / std::tan(fov * 0.5f * 3.14159265358979f / 180.0f);
    proj[0] = f / aspect; proj[1] = 0; proj[2] = 0; proj[3] = 0;
    proj[4] = 0; proj[5] = f; proj[6] = 0; proj[7] = 0;
    proj[8] = 0; proj[9] = 0;
    proj[10] = (farP + nearP) / (nearP - farP); proj[11] = -1.0f;
    proj[12] = 0; proj[13] = 0;
    proj[14] = (2.0f * farP * nearP) / (nearP - farP); proj[15] = 0;
    return true;
  } __except (1) {
    return false;
  }
}

// 状态：gizmo 开关与当前操作（在 panel_pose.h 里由 UI 切换）
static bool g_gizmoEnabled = true;
static ImGuizmo::OPERATION g_gizmoOp = ImGuizmo::ROTATE;
static ImGuizmo::MODE g_gizmoMode = ImGuizmo::LOCAL;
static float g_gizmoSize = 0.12f;

// 对指定骨绘制 3D 手柄；拖拽时把 delta 写回 localRotation/localPosition。
// LOCAL 模式下 delta 为骨局部系增量：local' = local * deltaRot（右乘），
// localPos' = localPos + deltaPos。
static bool DrawBoneGizmo(void *transform) {
  if (!transform)
    return false;
  float view[16], proj[16];
  if (!GetCameraViewProj(view, proj))
    return false;
  float obj[16], delta[16];
  if (!GetBoneWorldMatrix(transform, obj))
    return false;
  Mat4Identity(delta);

  ImGuiIO &io = ImGui::GetIO();
  ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
  ImGuizmo::SetGizmoSizeClipSpace(g_gizmoSize);
  bool used = ImGuizmo::Manipulate(view, proj, g_gizmoOp, g_gizmoMode, obj,
                                   delta);
  if (used) {
    Vec3 dPos;
    Quat dRot;
    Mat4Decompose(delta, dPos, dRot);
    Quat cur = GetBoneLocalRot(transform);
    SetBoneLocalRot(transform, NormQ(cur * dRot));
    Vec3 p = GetBoneLocalPos(transform);
    SetBoneLocalPos(transform, p + dPos);
  }
  return used;
}
