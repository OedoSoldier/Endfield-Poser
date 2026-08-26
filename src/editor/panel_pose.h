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

  // ---- IK 编辑（Task 3.2）：链选择 + 开关；IK 模式下 gizmo 拖目标 ----
  ImGui::Checkbox(u8"\u542f\u7528 IK", &g_ikActive);
  ImGui::SameLine();
  ImGui::Checkbox(u8"\u539f\u751f BipedIK", &g_ikNative);
  const char *chainNames[kIkChainCount] = {kIkChains[0].name, kIkChains[1].name,
                                           kIkChains[2].name, kIkChains[3].name};
  int chainIdx = (int)g_ikChain;
  ImGui::SameLine();
  if (ImGui::Combo(u8"IK \u94fe", &chainIdx, chainNames, kIkChainCount))
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
  bool changed = false;
  if (ImGui::SliderFloat(u8"X##rx", &euler.x, -180.0f, 180.0f, "%.1f"))
    changed = true;
  if (ImGui::SliderFloat(u8"Y##ry", &euler.y, -180.0f, 180.0f, "%.1f"))
    changed = true;
  if (ImGui::SliderFloat(u8"Z##rz", &euler.z, -180.0f, 180.0f, "%.1f"))
    changed = true;
  if (changed) {
    if (bh.locked) {
      // 锁定骨禁止修改
    } else {
      SetBoneLocalRot(bh.transform, Quat::FromEulerDeg(euler));
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u4f4d\u7f6e");
  Vec3 pos = GetBoneLocalPos(bh.transform);
  if (ImGui::SliderFloat(u8"X##px", &pos.x, -1.0f, 1.0f, "%.3f"))
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
  if (ImGui::SliderFloat(u8"Y##py", &pos.y, -1.0f, 1.0f, "%.3f"))
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
  if (ImGui::SliderFloat(u8"Z##pz", &pos.z, -1.0f, 1.0f, "%.3f"))
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
  if (!InPoseMode())
    return; // 镜头模式下隐藏骨骼手柄，避免误改
  if (g_ikActive) {
    DrawIkTargetGizmo(); // IK 模式：手柄拖 IK 目标，骨骼由求解器跟随
    return;
  }
  if (!g_gizmoEnabled || g_selectedBone < 0 ||
      g_selectedBone >= s_humanBoneCount)
    return;
  BoneHandle &bh = s_humanBones[g_selectedBone];
  if (bh.locked)
    return; // 锁定骨不响应手柄
  DrawBoneGizmo(bh.transform);
}
