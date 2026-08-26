#pragma once

// Task 3.1：FK 姿态编辑面板。
// 结构：按身体部位分组的骨骼树 → 选中骨后可用 3D gizmo 拖拽（gizmo.h）或
// 三轴 Euler 滑条微调；锁定开关让该骨在姿态操作中不被改写。
// 依赖 skeleton.h 的 s_humanBones[] 与冻结态（freeze.h）。

#include "imgui.h"
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "math/quat_math.h"
#include "editor/gizmo.h"

// 当前选中骨在 s_humanBones 中的下标（-1 = 无）
static int g_selectedBone = -1;

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

// ---- 3D 手柄叠加层（在主窗口之外调用，覆盖整个视口）----
static void DrawPoseGizmoOverlay() {
  if (!g_gizmoEnabled || g_selectedBone < 0 ||
      g_selectedBone >= s_humanBoneCount)
    return;
  BoneHandle &bh = s_humanBones[g_selectedBone];
  if (bh.locked)
    return; // 锁定骨不响应手柄
  DrawBoneGizmo(bh.transform);
}
