#pragma once

// Task 3.1：FK 姿态编辑面板。
// 结构：按身体部位分组的骨骼树 → 选中骨后可用 3D gizmo 拖拽（gizmo.h）或
// 三轴 Euler 滑条微调；锁定开关让该骨在姿态操作中不被改写。
// 依赖 skeleton.h 的 s_humanBones[] 与冻结态（freeze.h）。

#include "imgui.h"
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "game/ik_driver.h"
#include "math/quat_math.h"
#include "editor/gizmo.h"
#include "editor/panel_mode.h"

// 当前选中骨在 s_humanBones 中的下标（-1 = 无）
static int g_selectedBone = -1;

// 按骨名选中骨骼（控制文件/外部测试用）
static bool SelectBoneByName(const char *name) {
  if (!name || !*name)
    return false;
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (strcmp(s_humanBones[i].name, name) == 0) {
      g_selectedBone = i;
      return true;
    }
  }
  for (int i = 0; i < s_humanBoneCount; i++) {
    const char *hn = HumanBoneName(s_humanBones[i].humanBone);
    if (hn && strcmp(hn, name) == 0) {
      g_selectedBone = i;
      return true;
    }
  }
  return false;
}

// 复制/粘贴选中骨缓冲区（Task 3.3）
static PoseBone g_copyBuffer;
static bool g_hasCopy = false;

// ---- 骨骼分组（树形展示用）----
struct BoneGroupDef {
  const char *name;
  const HumanBodyBones *bones;
  int count;
};

static const HumanBodyBones kBonesTorso[] = {Hips, Spine, Chest, UpperChest,
                                             Neck};
static const HumanBodyBones kBonesHead[] = {Head, Jaw, LeftEye, RightEye};
static const HumanBodyBones kBonesArmL[] = {LeftShoulder, LeftUpperArm,
                                            LeftLowerArm, LeftHand};
static const HumanBodyBones kBonesArmR[] = {RightShoulder, RightUpperArm,
                                            RightLowerArm, RightHand};
static const HumanBodyBones kBonesLegL[] = {LeftUpperLeg, LeftLowerLeg,
                                            LeftFoot, LeftToes};
static const HumanBodyBones kBonesLegR[] = {RightUpperLeg, RightLowerLeg,
                                            RightFoot, RightToes};
static const HumanBodyBones kBonesFingerL[] = {
    LeftThumbProximal, LeftThumbIntermediate, LeftThumbDistal,
    LeftIndexProximal, LeftIndexIntermediate, LeftIndexDistal,
    LeftMiddleProximal, LeftMiddleIntermediate, LeftMiddleDistal,
    LeftRingProximal, LeftRingIntermediate, LeftRingDistal,
    LeftLittleProximal, LeftLittleIntermediate, LeftLittleDistal};
static const HumanBodyBones kBonesFingerR[] = {
    RightThumbProximal, RightThumbIntermediate, RightThumbDistal,
    RightIndexProximal, RightIndexIntermediate, RightIndexDistal,
    RightMiddleProximal, RightMiddleIntermediate, RightMiddleDistal,
    RightRingProximal, RightRingIntermediate, RightRingDistal,
    RightLittleProximal, RightLittleIntermediate, RightLittleDistal};

static const BoneGroupDef kBoneGroups[] = {
    {u8"\u8eaf\u5e72", kBonesTorso, 5},      // 躯干
    {u8"\u5934\u90e8", kBonesHead, 4},       // 头部
    {u8"\u5de6\u81c2", kBonesArmL, 4},       // 左臂
    {u8"\u53f3\u81c2", kBonesArmR, 4},       // 右臂
    {u8"\u5de6\u817f", kBonesLegL, 4},       // 左腿
    {u8"\u53f3\u817f", kBonesLegR, 4},       // 右腿
    {u8"\u5de6\u624b\u624b\u6307", kBonesFingerL, 15}, // 左手手指
    {u8"\u53f3\u624b\u624b\u6307", kBonesFingerR, 15}, // 右手手指
};
static const int kBoneGroupCount = (int)(sizeof(kBoneGroups) / sizeof(kBoneGroups[0]));

static const char *BoneGroupName(HumanBodyBones b) {
  for (int g = 0; g < kBoneGroupCount; g++)
    for (int i = 0; i < kBoneGroups[g].count; i++)
      if (kBoneGroups[g].bones[i] == b)
        return kBoneGroups[g].name;
  return u8"\u5176\u4ed6";
}

// ---- 骨骼树 + 选中骨控制 ----
static void DrawSkeletonSchematic(); // 面板骨骼小人（定义在文件后部）

static void DrawPosePanel() {
  if (s_humanBoneCount <= 0) {
    ImGui::TextDisabled(u8"\u672a\u6355\u83b7\u5230\u89d2\u8272\u9aa8\u9abc");
    return;
  }

  // 工具行：gizmo 开关 / 操作类型 / 复位
  ImGui::Checkbox(u8"\u63d0\u793a\u624b\u67c4", &g_gizmoEnabled);
  ImGui::SameLine();
  ImGui::RadioButton(u8"\u65cb\u8f6c", (int *)&g_gizmoOp, ImGuizmo::ROTATE);
  ImGui::SameLine();
  ImGui::RadioButton(u8"\u79fb\u52a8", (int *)&g_gizmoOp, ImGuizmo::TRANSLATE);
  ImGui::SameLine();
  ImGui::RadioButton(u8"\u7f29\u653e", (int *)&g_gizmoOp, ImGuizmo::SCALE);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u91cd\u7f6e")) {
    ApplyPoseSnapshot();
    if (g_selectedBone >= 0) {
      // 刷新选中骨 Euler 显示
    }
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("T-Pose"))
    ApplyTPose();
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u955c\u50cf L\u2192R"))
    MirrorPose(true);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u955c\u50cf R\u2192L"))
    MirrorPose(false);

  // 复制/粘贴选中骨
  ImGui::Spacing();
  if (g_selectedBone >= 0) {
    ImGui::PushID("copy1");
    if (ImGui::SmallButton(u8"\u590d\u5236\u9009\u4e2d\u9aa8")) {
      BoneHandle &sb = s_humanBones[g_selectedBone];
      g_copyBuffer.name = sb.name;
      g_copyBuffer.pos = GetBoneLocalPos(sb.transform);
      g_copyBuffer.rot = GetBoneLocalRot(sb.transform);
      g_hasCopy = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"\u7c98\u8d34") && g_hasCopy && !s_humanBones[g_selectedBone].locked) {
      BoneHandle &db = s_humanBones[g_selectedBone];
      SetBoneLocalPos(db.transform, g_copyBuffer.pos);
      SetBoneLocalRot(db.transform, g_copyBuffer.rot);
    }
    ImGui::PopID();
  }

  ImGui::Separator();

  // 面板骨骼小人：点关节选骨，拖关节摆姿势（正视图，绕世界 Z 旋转）
  DrawSkeletonSchematic();
  ImGui::Separator();

  // ---- IK 编辑（Task 3.2）：链选择 + 开关；IK 模式下 gizmo 拖目标 ----
  ImGui::TextDisabled(u8"IK \u94fe\uff08\u53ef\u591a\u9009\uff0c\u5f00\u542f\u7684\u94fe\u7531\u6c42\u89e3\u5668\u9a71\u52a8\uff0c\u5176\u4f59 FK\uff09");
  for (int ci = 0; ci < kIkChainCount; ci++) {
    bool on = g_ikActiveChain[ci];
    if (ImGui::Checkbox(kIkChains[ci].name, &on))
      SetChainIkActive((IkChainId)ci, on);
    if (ci < kIkChainCount - 1)
      ImGui::SameLine();
  }
  ImGui::Checkbox(u8"\u539f\u751f BipedIK", &g_ikNative);
  ImGui::SameLine();
  const char *chainNames[kIkChainCount] = {kIkChains[0].name, kIkChains[1].name,
                                           kIkChains[2].name, kIkChains[3].name};
  int chainIdx = (int)g_ikChain;
  if (ImGui::Combo(u8"\u76ee\u6807\u94fe", &chainIdx, chainNames, kIkChainCount))
    IkSetChain((IkChainId)chainIdx);
  if (g_ikActive) {
    Vec3 t = IkTargetPos();
    ImGui::TextDisabled(u8"\u76ee\u6807 (%.2f, %.2f, %.2f)", t.x, t.y, t.z);
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"\u8fd8\u539f\u76ee\u6807"))
      IkSetChain(g_ikChain); // 目标回到当前末端位置
  }

  ImGui::Separator();

  // 左侧：分组骨骼树
  float treeW = ImGui::GetContentRegionAvail().x * 0.5f;
  ImGui::BeginChild("##bonetree", ImVec2(treeW, 0), true);
  ImGui::TextDisabled(u8"\u9aa8\u9abc\u6811");
  ImGui::Separator();
  for (int g = 0; g < kBoneGroupCount; g++) {
    const BoneGroupDef &grp = kBoneGroups[g];
    if (ImGui::CollapsingHeader(grp.name)) {
      ImGui::Indent();
      for (int i = 0; i < grp.count; i++) {
        int idx = FindHumanBoneIndex(grp.bones[i]);
        if (idx < 0)
          continue;
        BoneHandle &bh = s_humanBones[idx];
        ImGui::PushID(idx);
        bool selected = (g_selectedBone == idx);
        if (ImGui::Selectable(bh.name, selected)) {
          g_selectedBone = (selected ? -1 : idx); // 再次点击取消选择
        }
        if (bh.locked) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), u8"\u9501");
        }
        ImGui::PopID();
      }
      ImGui::Unindent();
    }
  }
  ImGui::EndChild();

  // 右侧：选中骨控制
  ImGui::SameLine();
  ImGui::BeginChild("##boneedit", ImVec2(0, 0), true);
  if (g_selectedBone < 0 || g_selectedBone >= s_humanBoneCount) {
    ImGui::TextDisabled(u8"\u8bf7\u5728\u5de6\u4fa7\u9009\u62e9\u4e00\u6839\u9aa8\u9abc");
    ImGui::EndChild();
    return;
  }

  BoneHandle &bh = s_humanBones[g_selectedBone];
  ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", bh.name);
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", BoneGroupName(bh.humanBone));

  ImGui::Spacing();
  bool locked = bh.locked;
  if (ImGui::Checkbox(u8"\u9501\u5b9a\u8be5\u9aa8", &locked)) {
    bh.locked = locked;
    if (locked) {
      // 锁定瞬间固化当前位姿，供恢复时钉住
      bh.localPos = GetBoneLocalPos(bh.transform);
      bh.localRot = GetBoneLocalRot(bh.transform);
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u53c9\u8f74\u65cb\u8f6c (\u00b0)");

  // 实时回读当前 localRotation 的 Euler 角，保证滑条跟随 gizmo 拖拽
  Quat cur = GetBoneLocalRot(bh.transform);
  Vec3 euler = cur.ToEulerDeg();
  // 增量式局部轴旋转：拖 X 只绕骨局部 X 轴转，避免 Euler 分解非唯一导致的"拖一个其他跟着跳"
  const float kD2R = 3.14159265f / 180.0f;
  static Quat s_dragBase = {0, 0, 0, 1};
  static float s_dragEuler[3] = {0, 0, 0};
  float *eptr[3] = {&euler.x, &euler.y, &euler.z};
  const char *ids[3] = {u8"X##rx", u8"Y##ry", u8"Z##rz"};
  Vec3 axs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int i = 0; i < 3; i++) {
    if (ImGui::SliderFloat(ids[i], eptr[i], -180.0f, 180.0f, "%.1f")) {
      if (ImGui::IsItemActivated()) {
        s_dragBase = cur;
        s_dragEuler[0] = euler.x;
        s_dragEuler[1] = euler.y;
        s_dragEuler[2] = euler.z;
      }
      if (ImGui::IsItemActive() && !bh.locked) {
        float d = (*eptr[i] - s_dragEuler[i]) * kD2R;
        SetBoneLocalRot(bh.transform, NormQ(Quat::AxisAngle(axs[i], d) * s_dragBase));
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u4f4d\u7f6e");
  Vec3 pos = GetBoneLocalPos(bh.transform);
  if (ImGui::SliderFloat(u8"X##px", &pos.x, -3.0f, 3.0f, "%.3f"))
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
  if (ImGui::SliderFloat(u8"Y##py", &pos.y, -3.0f, 3.0f, "%.3f"))
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
  if (ImGui::SliderFloat(u8"Z##pz", &pos.z, -3.0f, 3.0f, "%.3f"))
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);

  ImGui::EndChild();
}

// ---- IK 目标 3D 手柄（IK 模式下替代骨手柄；拖拽改写 g_ikTarget）----
static bool DrawIkTargetGizmo() {
  float view[16], proj[16];
  if (!GetCameraViewProj(view, proj))
    return false;
  Vec3 t = IkTargetPos();
  LogGizmoDiagnostics(view, proj, t, "ik_target");
  float obj[16], delta[16];
  Mat4Compose(t, Quat{0, 0, 0, 1}, obj);
  Mat4Identity(delta);

  ImGuiIO &io = ImGui::GetIO();
  ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
  ImGuizmo::SetGizmoSizeClipSpace(g_gizmoSize);
  bool used = ImGuizmo::Manipulate(view, proj, ImGuizmo::TRANSLATE,
                                   ImGuizmo::WORLD, obj, delta);
  if (used) {
    Vec3 dPos;
    Quat dRot;
    Mat4Decompose(delta, dPos, dRot);
    IkSetTarget(t + dPos);
  }
  return used;
}

// ---- 3D 手柄叠加层（在主窗口之外调用，覆盖整个视口）----
static void DrawPoseGizmoOverlay() {
  // 诊断：跳转原因变化时记录一次
  static int s_lastSkip = -2;
  int skip = -1;
  if (!g_gizmoEnabled) skip = 2; // 默认关手柄：滑条直调
  else if (!InPoseMode()) skip = 0;
  else if (g_ikActive) skip = 1;
  else if (g_selectedBone < 0 || g_selectedBone >= s_humanBoneCount) skip = 3;
  else if (s_humanBones[g_selectedBone].locked) skip = 4;
  if (skip != s_lastSkip) {
    s_lastSkip = skip;
    static const char *kWhy[] = {
        "not pose mode", "ik active -> ik target gizmo", "gizmo disabled",
        "no bone selected", "bone locked"};
    Log("[GIZMO] %s", skip >= 0 ? kWhy[skip] : "drawing");
  }
  if (skip == 0)
    return; // 镜头模式下隐藏骨骼手柄，避免误改
  if (skip == 1) {
    DrawIkTargetGizmo(); // IK 模式：手柄拖 IK 目标，骨骼由求解器跟随
    return;
  }
  if (skip == 2 || skip == 3 || skip == 4)
    return;
  DrawBoneGizmo(s_humanBones[g_selectedBone].transform);
}

// 在面板内找某个 transform 对应的骨骼下标（画父-子连线用）
static int FindTransformIndex(void *t) {
  if (!t)
    return -1;
  for (int i = 0; i < s_humanBoneCount; i++)
    if (s_humanBones[i].transform == t)
      return i;
  return -1;
}

// ---- 面板骨骼小人（2D 正视图）----
// 点关节 = 选中骨骼；拖关节 = FK 旋转其父骨，让该骨对准鼠标（绕世界 Z 轴，面板平面内）。
// 完全绕开游戏相机矩阵/手柄投影问题。
static void DrawSkeletonSchematic() {
  if (s_humanBoneCount <= 0) {
    ImGui::TextDisabled(u8"\u672a\u6355\u83b7\u89d2\u8272\u9aa8\u9abc");
    return;
  }
  const float kH = 240.0f;
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float w = avail.x < 60.0f ? 60.0f : avail.x;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + kH),
                    IM_COL32(25, 28, 38, 220));

  // 收集骨骼世界坐标 + 包围盒
  Vec3 wpos[64];
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < s_humanBoneCount && i < 64; i++) {
    Vec3 p = GetBoneWorldPos(s_humanBones[i].transform);
    wpos[i] = p;
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  float spanX = (maxX - minX) > 1e-4f ? (maxX - minX) : 1.0f;
  float spanY = (maxY - minY) > 1e-4f ? (maxY - minY) : 1.0f;
  float sc = (w - 24.0f) / spanX;
  float sy = (kH - 24.0f) / spanY;
  if (sy < sc) sc = sy;
  if (sc <= 0.0f) sc = 1.0f;
  float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;
  auto To2D = [&](Vec3 p) -> ImVec2 {
    float px = origin.x + w * 0.5f + (p.x - midX) * sc;
    float py = origin.y + kH * 0.5f - (p.y - midY) * sc; // 屏幕 Y 向下，翻转为世界 Y 向上
    return ImVec2(px, py);
  };

  // 父-子连线
  for (int i = 0; i < s_humanBoneCount && i < 64; i++) {
    if (!s_humanBones[i].transform)
      continue;
    void *parent =
        g_transform_get_parent
            ? Invoke(g_transform_get_parent, s_humanBones[i].transform)
            : nullptr;
    int pi = FindTransformIndex(parent);
    if (pi >= 0) {
      ImVec2 a = To2D(wpos[pi]), b = To2D(wpos[i]);
      dl->AddLine(a, b, IM_COL32(190, 195, 210, 255), 1.6f);
    }
  }

  // 拖拽旋转（FK）：拖第 i 关节点 → 旋转其父骨，让 骨i 对准鼠标
  static int s_dragBone = -1;
  ImVec2 mouse = ImGui::GetIO().MousePos;
  bool mouseDown = ImGui::IsMouseDown(0);
  if (mouseDown && s_dragBone >= 0 && s_dragBone < s_humanBoneCount) {
    void *t = s_humanBones[s_dragBone].transform;
    void *parent =
        g_transform_get_parent ? Invoke(g_transform_get_parent, t) : nullptr;
    if (parent && !s_humanBones[s_dragBone].locked) {
      Vec3 pj = GetBoneWorldPos(t);
      Vec3 pp = GetBoneWorldPos(parent);
      // 鼠标 → 世界 XY（面板平面 = 世界 XY，视图轴 +Z）
      float wx = midX + (mouse.x - (origin.x + w * 0.5f)) / sc;
      float wy = midY - (mouse.y - (origin.y + kH * 0.5f)) / sc;
      float curA = atan2f(pj.y - pp.y, pj.x - pp.x);
      float tgtA = atan2f(wy - pp.y, wx - pp.x);
      float da = tgtA - curA;
      if (fabsf(da) > 1e-4f)
        ApplyWorldRotDelta(parent, Quat::AxisAngle(Vec3{0, 0, 1}, da));
    }
  }

  // 关节点 + 点击选中
  for (int i = 0; i < s_humanBoneCount && i < 64; i++) {
    ImVec2 c = To2D(wpos[i]);
    bool sel = (i == g_selectedBone);
    ImU32 col = sel ? IM_COL32(255, 200, 60, 255)
                    : (s_humanBones[i].locked ? IM_COL32(255, 90, 90, 255)
                                              : IM_COL32(120, 200, 255, 255));
    dl->AddCircleFilled(c, sel ? 5.0f : 3.5f, col);
    float d = fabsf(mouse.x - c.x) + fabsf(mouse.y - c.y);
    if (ImGui::IsMouseClicked(0) && d < 10.0f) {
      g_selectedBone = i;
      s_dragBone = i;
    }
    if (ImGui::IsMouseReleased(0) && s_dragBone == i)
      s_dragBone = -1;
  }
  if (!mouseDown)
    s_dragBone = -1;

  ImGui::Dummy(ImVec2(w, kH));
}
