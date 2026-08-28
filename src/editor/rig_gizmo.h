#pragma once

// 游戏内简易摆姿（UE 风格旋转盘）：
//   冻结后角色上直接渲染骨骼线/关节点；点击关节点选中；选中骨上显示
//   ImGuizmo ROTATE 旋转盘，拖动 = FK 旋转该骨（localRotation 写回）。
// 只做旋转、不做 IK；仅冻结态可用；与 Blender 控制 Rig 并存（都走 /api/pose 语义）。
// 相机/矩阵代码沿用旧 gizmo.h（git a28d143^），简化去掉平移与诊断。

#include "imgui.h"
#include "ImGuizmo.h"
#include "math/quat_math.h"
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "game/freeze.h"
#include "editor/selection.h"

#include <cmath>
#include <cstring>

// 显示开关（主窗口复选框）
static bool g_showBones = true;

// 叠加层状态（面板直接显示，方便排查）
static char g_overlayStatus[128] = "off";

// 上一帧叠加层投影的关节屏幕坐标缓存（供 WM_NCHITTEST 命中测试，overlay 客户端坐标）
static const int kMaxJointCache = 128;
static float g_jointSx[kMaxJointCache] = {};
static float g_jointSy[kMaxJointCache] = {};
static int g_jointCount = 0;

// ---- 列主序 4x4 基础 ----
static void Mat4Identity(float m[16]) {
  for (int i = 0; i < 16; i++)
    m[i] = 0;
  m[0] = m[5] = m[10] = m[15] = 1;
}

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

// 沿父链自根向下组合世界矩阵（local pos/rot 逐级相乘）
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

// 从主相机构建 view + projection（列主序）。Unity 矩阵优先，手动回退。
static bool GetCameraViewProj(float view[16], float proj[16]) {
  static bool s_camDiagLogged = false;
  auto CamLogOnce = [&](const char *why) {
    if (!s_camDiagLogged) {
      s_camDiagLogged = true;
      Log("[RIG] camera unavailable: %s (main=%p transform=%p findOfType=%p)",
          why, g_camera_get_main, g_component_get_transform,
          g_object_FindObjectOfType);
    }
  };
  if (!g_component_get_transform) {
    CamLogOnce("no get_transform");
    return false;
  }
  __try {
    void *cam = Invoke(g_camera_get_main, nullptr);
    if (!cam && g_object_FindObjectOfType && g_cameraClass) {
      // get_main 拿不到时兜底：FindObjectOfType<Camera>() 找任意相机
      void *type = il2cpp_class_get_type(g_cameraClass);
      void *typeObj = type ? il2cpp_type_get_object(type) : nullptr;
      if (typeObj) {
        void *args[] = {typeObj};
        cam = Invoke(g_object_FindObjectOfType, nullptr, args);
        if (cam)
          Log("[RIG] camera fallback FindObjectOfType -> %p", cam);
      }
    }
    if (!cam) {
      CamLogOnce("Camera.get_main returned null and no fallback");
      return false;
    }
    if (g_camera_get_worldToCameraMatrix && g_camera_get_projectionMatrix) {
      void *vbox = Invoke(g_camera_get_worldToCameraMatrix, cam);
      void *pbox = Invoke(g_camera_get_projectionMatrix, cam);
      if (vbox && pbox) {
        memcpy(view, (char *)vbox + 16, 16 * sizeof(float));
        memcpy(proj, (char *)pbox + 16, 16 * sizeof(float));
        return true;
      }
      CamLogOnce("camera matrices returned null");
    }
    void *ct = Invoke(g_component_get_transform, cam);
    if (!ct) {
      CamLogOnce("camera has no transform");
      return false;
    }
    Vec3 pos = GetBoneWorldPos(ct);
    Quat rot = GetBoneWorldRot(ct);
    float wm[16];
    Mat4Compose(pos, rot, wm);
    view[0] = wm[0];  view[1] = wm[4];  view[2] = wm[8];  view[3] = 0;
    view[4] = wm[1];  view[5] = wm[5];  view[6] = wm[9];  view[7] = 0;
    view[8] = wm[2];  view[9] = wm[6];  view[10] = wm[10]; view[11] = 0;
    view[12] = -(wm[0] * pos.x + wm[4] * pos.y + wm[8] * pos.z);
    view[13] = -(wm[1] * pos.x + wm[5] * pos.y + wm[9] * pos.z);
    view[14] = -(wm[2] * pos.x + wm[6] * pos.y + wm[10] * pos.z);
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

// 世界点投影到屏幕；返回 false = 在相机后方或超出视口
static bool ProjectToScreen(const float view[16], const float proj[16],
                            const Vec3 &w, float &sx, float &sy) {
  float vx = view[0] * w.x + view[4] * w.y + view[8] * w.z + view[12];
  float vy = view[1] * w.x + view[5] * w.y + view[9] * w.z + view[13];
  float vz = view[2] * w.x + view[6] * w.y + view[10] * w.z + view[14];
  float vw = view[3] * w.x + view[7] * w.y + view[11] * w.z + view[15];
  float cx = proj[0] * vx + proj[4] * vy + proj[8] * vz + proj[12] * vw;
  float cy = proj[1] * vx + proj[5] * vy + proj[9] * vz + proj[13] * vw;
  float cw = proj[3] * vx + proj[7] * vy + proj[11] * vz + proj[15] * vw;
  if (fabsf(cw) < 1e-6f)
    return false;
  float nx = cx / cw, ny = cy / cw;
  if (nx < -1.5f || nx > 1.5f || ny < -1.5f || ny > 1.5f)
    return false;
  ImGuiIO &io = ImGui::GetIO();
  sx = (nx + 1.0f) * 0.5f * io.DisplaySize.x;
  sy = (1.0f - ny) * 0.5f * io.DisplaySize.y;
  return true;
}

// ---- 投影上下文：相机优先，失败回退正交前视图（保证骨骼一定能画出来）----
static float g_viewM[16], g_projM[16];
static bool g_useCamera = false;
static float g_fbMinX = 0, g_fbMaxX = 1, g_fbMinY = 0, g_fbMaxY = 1;

static bool ComputeProjection() {
  g_useCamera = GetCameraViewProj(g_viewM, g_projM);
  if (g_useCamera)
    return true;
  static bool s_fbLogged = false;
  g_fbMinX = g_fbMinY = 1e9f;
  g_fbMaxX = g_fbMaxY = -1e9f;
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (!s_humanBones[i].transform)
      continue;
    Vec3 w = GetBoneWorldPos(s_humanBones[i].transform);
    if (w.x < g_fbMinX) g_fbMinX = w.x;
    if (w.x > g_fbMaxX) g_fbMaxX = w.x;
    if (w.y < g_fbMinY) g_fbMinY = w.y;
    if (w.y > g_fbMaxY) g_fbMaxY = w.y;
  }
  if (g_fbMaxX - g_fbMinX < 1e-4f || g_fbMaxY - g_fbMinY < 1e-4f)
    return false;
  // 兜底投影矩阵（正交前视图，与 ProjectBone 的屏幕映射一致）：
  // view = 单位矩阵（相机在原点朝 -Z 看）；proj = 正交 [minX,maxX]x[minY,maxY]
  Mat4Identity(g_viewM);
  // 相机放在角色前方，保证骨点在相机前方（view z < 0），否则 ImGuizmo 不画
  {
    float zSum = 0.0f;
    int zN = 0;
    for (int i = 0; i < s_humanBoneCount; i++) {
      if (!s_humanBones[i].transform)
        continue;
      zSum += GetBoneWorldPos(s_humanBones[i].transform).z;
      zN++;
    }
    if (zN > 0)
      g_viewM[14] = -(zSum / zN + 10.0f);
  }
  Mat4Identity(g_projM);
  float l = g_fbMinX, r = g_fbMaxX, b = g_fbMinY, t = g_fbMaxY;
  const float n = -1000.0f, f = 1000.0f;
  g_projM[0] = 2.0f / (r - l);
  g_projM[5] = 2.0f / (t - b);
  g_projM[10] = -2.0f / (f - n);
  g_projM[12] = -(r + l) / (r - l);
  g_projM[13] = -(t + b) / (t - b);
  g_projM[14] = -(f + n) / (f - n);
  if (!s_fbLogged) {
    s_fbLogged = true;
    Log("[RIG] camera unavailable, using ortho front-view fallback");
  }
  return true;
}

static bool ProjectBone(const Vec3 &w, float &sx, float &sy) {
  if (g_useCamera)
    return ProjectToScreen(g_viewM, g_projM, w, sx, sy);
  ImGuiIO &io = ImGui::GetIO();
  float ww = g_fbMaxX - g_fbMinX, hh = g_fbMaxY - g_fbMinY;
  if (ww < 1e-4f || hh < 1e-4f || io.DisplaySize.x < 1 ||
      io.DisplaySize.y < 1)
    return false;
  sx = (w.x - g_fbMinX) / ww * io.DisplaySize.x;
  sy = (g_fbMaxY - w.y) / hh * io.DisplaySize.y;
  return true;
}

// 渲染骨骼线 + 关节点（世界 → 屏幕，画在背景层）
static void DrawSkeletonOverlay() {
  if (!g_showBones) {
    snprintf(g_overlayStatus, sizeof(g_overlayStatus), "off (checkbox)");
    return;
  }
  static bool s_noBonesLogged = false;
  if (s_humanBoneCount <= 0) {
    snprintf(g_overlayStatus, sizeof(g_overlayStatus),
             "no bones (capture=%d)", s_humanBoneCount);
    if (!s_noBonesLogged) {
      s_noBonesLogged = true;
      Log("[RIG] overlay: no human bones captured yet");
    }
    return;
  }
  g_jointCount = 0;
  static bool s_okLogged = false;
  if (!s_okLogged) {
    s_okLogged = true;
    Log("[RIG] overlay active: bones=%d", s_humanBoneCount);
  }
  if (!ComputeProjection()) {
    snprintf(g_overlayStatus, sizeof(g_overlayStatus), "projection failed");
    return;
  }
  snprintf(g_overlayStatus, sizeof(g_overlayStatus),
           g_useCamera ? "camera ok (bones=%d)" : "ortho fallback (bones=%d)",
           s_humanBoneCount);
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  // 父子连线（父关节 → 子关节）
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (!s_humanBones[i].transform)
      continue;
    void *parent = g_transform_get_parent
                       ? Invoke(g_transform_get_parent, s_humanBones[i].transform)
                       : nullptr;
    if (!parent)
      continue;
    int pi = FindTransformIndex(parent);
    if (pi < 0)
      continue;
    float a[2], b[2];
    if (!ProjectBone(GetBoneWorldPos(s_humanBones[pi].transform), a[0], a[1]))
      continue;
    if (!ProjectBone(GetBoneWorldPos(s_humanBones[i].transform), b[0], b[1]))
      continue;
    dl->AddLine(ImVec2(a[0], a[1]), ImVec2(b[0], b[1]),
                IM_COL32(170, 190, 220, 200), 1.6f);
  }
  // 关节点（黄=选中，红=锁定，蓝=普通）
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (!s_humanBones[i].transform)
      continue;
    float sx, sy;
    if (!ProjectBone(GetBoneWorldPos(s_humanBones[i].transform), sx, sy))
      continue;
    bool sel = (i == g_selectedBone);
    ImU32 col = sel ? IM_COL32(255, 200, 60, 255)
                    : (s_humanBones[i].locked ? IM_COL32(255, 90, 90, 255)
                                              : IM_COL32(120, 200, 255, 255));
    dl->AddCircleFilled(ImVec2(sx, sy), sel ? 6.0f : 4.0f, col);
    if (g_jointCount < kMaxJointCache) {
      g_jointSx[g_jointCount] = sx;
      g_jointSy[g_jointCount] = sy;
      g_jointCount++;
    }
  }
  // 悬停高亮：冻结态下光标附近的关节画白色外圈，提示可点击
  if (g_frozen) {
    ImGuiIO &io = ImGui::GetIO();
    int hover = -1;
    float hd = 18.0f;
    for (int i = 0; i < g_jointCount; i++) {
      float dx = g_jointSx[i] - io.MousePos.x, dy = g_jointSy[i] - io.MousePos.y;
      float d = std::sqrt(dx * dx + dy * dy);
      if (d < hd) {
        hd = d;
        hover = i;
      }
    }
    if (hover >= 0)
      dl->AddCircle(ImVec2(g_jointSx[hover], g_jointSy[hover]), 10.0f,
                    IM_COL32(255, 255, 255, 230), 0, 1.5f);
  }
}

// 命中测试：屏幕坐标（overlay 客户端）是否落在任一关节附近（供点击穿透判断）
static bool RigGizmoHitTest(float sx, float sy) {
  if (!g_frozen || !g_showBones)
    return false;
  for (int i = 0; i < g_jointCount; i++) {
    float dx = g_jointSx[i] - sx, dy = g_jointSy[i] - sy;
    if (dx * dx + dy * dy < 14.0f * 14.0f)
      return true;
  }
  return false;
}

// 点击拾取：冻结态 + 未悬停 ImGui 窗口时，最近的关节点
static void HandleRigClick() {
  if (!g_frozen || !g_showBones)
    return;
  if (!ImGui::IsMouseClicked(0))
    return;
  if (ImGui::GetIO().WantCaptureMouse)
    return;
  if (!ComputeProjection())
    return;
  ImGuiIO &io = ImGui::GetIO();
  int best = -1;
  float bd = 14.0f;
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (!s_humanBones[i].transform)
      continue;
    float sx, sy;
    if (!ProjectBone(GetBoneWorldPos(s_humanBones[i].transform), sx, sy))
      continue;
    float dx = sx - io.MousePos.x, dy = sy - io.MousePos.y;
    float d = std::sqrt(dx * dx + dy * dy);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  g_selectedBone = best; // 点空处取消
}

// 选中骨上的旋转盘（ImGuizmo ROTATE / LOCAL）；拖拽 = FK 旋转写回
static bool DrawBoneRotationGizmo() {
  if (!g_frozen || g_selectedBone < 0 || g_selectedBone >= s_humanBoneCount)
    return false;
  void *t = s_humanBones[g_selectedBone].transform;
  if (!t)
    return false;
  // 与骨架叠加层共用投影上下文：相机可用用相机，否则用正交前视图兜底，
  // 保证旋转盘在相机不通时也能显示（位置与骨架一致）。
  if (!ComputeProjection())
    return false;
  float obj[16], delta[16];
  if (!GetBoneWorldMatrix(t, obj))
    return false;
  Mat4Identity(delta);
  ImGuiIO &io = ImGui::GetIO();
  ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
  ImGuizmo::SetGizmoSizeClipSpace(0.15f);
  bool used = ImGuizmo::Manipulate(g_viewM, g_projM, ImGuizmo::ROTATE,
                                   ImGuizmo::LOCAL, obj, delta);
  if (used) {
    Vec3 dPos;
    Quat dRot;
    Mat4Decompose(delta, dPos, dRot);
    (void)dPos; // 只做旋转，平移忽略
    if (fabsf(dRot.x) + fabsf(dRot.y) + fabsf(dRot.z) +
            fabsf(dRot.w - 1.0f) >
        1e-5f) {
      Quat cur = GetBoneLocalRot(t);
      SetBoneLocalRot(t, NormQ(cur * dRot));
    }
  }
  return used;
}
