#pragma once

// 骨骼参数面板：选中骨（3D 里点选）的旋转/位置参数 + 复位 + 撤销/重做入口。
// 注：曾经做过的"骨骼层级树"实测不理想，发布前已移除（代码在 git 历史里）。

#include "imgui.h"

#include "editor/rig_gizmo.h"
#include "editor/selection.h"
#include "editor/undo.h"
#include "editor/ik_control.h"
#include "game/accessory.h"
#include "game/skeleton.h"
#include "math/quat_math.h"

#include <cstring>
#include <vector>

static bool g_showBoneParams = true;

// 单轴 ± 步进按钮（人物位置、骨骼参数都用它）
static bool AxisStepper(const char *id, float *v, float step) {
  bool changed = false;
  ImGui::PushID(id);
  if (ImGui::SmallButton("-")) {
    *v -= step;
    changed = true;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+")) {
    *v += step;
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

// ---- 姿态级操作（全部重置 / 清空 / 回 A-pose），都走写骨接口 → 自动进撤销栈 ----
static void PoseOpResetToFreeze() {
  UndoStageLabel(u8"全部重置");
  ApplyPoseSnapshot();        // humanoid → 冻结帧快照（跳过锁定骨）
  ApplyAccessoryFrozenPose(); // 从骨 → 冻结瞬间
}

static void PoseOpClearRotations() {
  UndoStageLabel(u8"清空姿态");
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (s_humanBones[i].locked)
      continue;
    SetBoneLocalRot(s_humanBones[i].transform, Quat{0, 0, 0, 1});
  }
  ClearAccessoryPose();
}

static void PoseOpResetToAPose() {
  UndoStageLabel(u8"回到 A-pose");
  if (s_restCaptured) {
    for (int i = 0; i < s_humanBoneCount; i++) {
      if (s_humanBones[i].locked)
        continue;
      SetBoneLocalPos(s_humanBones[i].transform, s_restPos[i]);
      SetBoneLocalRot(s_humanBones[i].transform, s_restRot[i]);
    }
  }
  ApplyAccessoryFrozenPose();
}
// 复位：humanoid 骨回到 A-pose 基线（s_rest*），从骨回到冻结瞬间姿态
static void ResetBoneRot(void *t) {
  int hi = FindTransformIndex(t);
  if (hi >= 0 && s_restCaptured) {
    SetBoneLocalRot(t, s_restRot[hi]);
    return;
  }
  Quat q;
  if (GetAccessoryFrozenPose(t, nullptr, &q)) {
    SetBoneLocalRot(t, q);
    return;
  }
  SetBoneLocalRot(t, Quat{0, 0, 0, 1});
}

static void ResetBonePos(void *t) {
  int hi = FindTransformIndex(t);
  if (hi >= 0 && s_restCaptured) {
    SetBoneLocalPos(t, s_restPos[hi]);
    return;
  }
  Vec3 p;
  if (GetAccessoryFrozenPose(t, &p, nullptr))
    SetBoneLocalPos(t, p);
}

// 选中骨的参数：滑条可拖也可填（每个轴都能 Ctrl+点击数值直接输入），
// 另有输入框用于精确键入；写回走 SetBoneLocalRot/Pos，从骨自动同步冻结快照。
static void DrawBoneParams() {
  void *t = g_selectedTransform;
  if (!t) {
    ImGui::TextDisabled(u8"\u672a\u9009\u4e2d\u9aa8\u9abc");
    return;
  }
  ImGui::Text("%s", g_selectedName[0] ? g_selectedName : "(unnamed)");
  Vec3 e = GetBoneLocalRot(t).ToEulerDeg();
  ImGui::TextDisabled(u8"\u65cb\u8f6c (XYZ \u03a6)");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat3(u8"##rot", &e.x, -180.0f, 180.0f, "%.1f")) {
    UndoStageLabel(u8"旋转滑条");
    SetBoneLocalRot(t, Quat::FromEulerDeg(e));
  }
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputFloat3(u8"##rotin", &e.x, "%.2f")) {
    UndoStageLabel(u8"旋转输入");
    SetBoneLocalRot(t, Quat::FromEulerDeg(e));
  }
  static float s_rotStep = 1.0f;
  ImGui::SetNextItemWidth(70);
  ImGui::InputFloat(u8"步长##rotstep", &s_rotStep, 0.5f, 5.0f, "%.1f");
  ImGui::SameLine();
  ImGui::TextDisabled("X");
  ImGui::SameLine();
  {
    Vec3 ee = e;
    if (AxisStepper("rx", &ee.x, s_rotStep)) {
      UndoStageLabel(u8"旋转步进");
      SetBoneLocalRot(t, Quat::FromEulerDeg(ee));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Y");
    ImGui::SameLine();
    if (AxisStepper("ry", &ee.y, s_rotStep)) {
      UndoStageLabel(u8"旋转步进");
      SetBoneLocalRot(t, Quat::FromEulerDeg(ee));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Z");
    ImGui::SameLine();
    if (AxisStepper("rz", &ee.z, s_rotStep)) {
      UndoStageLabel(u8"旋转步进");
      SetBoneLocalRot(t, Quat::FromEulerDeg(ee));
    }
  }
  Vec3 p = GetBoneLocalPos(t);
  ImGui::TextDisabled(u8"\u4f4d\u7f6e (XYZ)");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat3(u8"##pos", &p.x, -1.0f, 1.0f, "%.3f")) {
    UndoStageLabel(u8"位置滑条");
    SetBoneLocalPos(t, p);
  }
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputFloat3(u8"##posin", &p.x, "%.4f")) {
    UndoStageLabel(u8"位置输入");
    SetBoneLocalPos(t, p);
  }
  static float s_posStep = 0.01f;
  ImGui::SetNextItemWidth(70);
  ImGui::InputFloat(u8"步长##posstep", &s_posStep, 0.005f, 0.05f, "%.3f");
  ImGui::SameLine();
  ImGui::TextDisabled("X");
  ImGui::SameLine();
  {
    Vec3 pp = p;
    if (AxisStepper("px", &pp.x, s_posStep)) {
      UndoStageLabel(u8"位置步进");
      SetBoneLocalPos(t, pp);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Y");
    ImGui::SameLine();
    if (AxisStepper("py", &pp.y, s_posStep)) {
      UndoStageLabel(u8"位置步进");
      SetBoneLocalPos(t, pp);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Z");
    ImGui::SameLine();
    if (AxisStepper("pz", &pp.z, s_posStep)) {
      UndoStageLabel(u8"位置步进");
      SetBoneLocalPos(t, pp);
    }
  }
  if (ImGui::Button(u8"\u590d\u4f4d\u65cb\u8f6c"))
    ResetBoneRot(t);
  ImGui::SameLine();
  if (ImGui::Button(u8"\u590d\u4f4d\u4f4d\u7f6e"))
    ResetBonePos(t);
  ImGui::SameLine();
  if (ImGui::Button(u8"\u5168\u90e8\u590d\u4f4d")) {
    ResetBoneRot(t);
    ResetBonePos(t);
  }
  int hi = FindTransformIndex(t);
  if (hi >= 0) {
    ImGui::SameLine();
    bool lk = s_humanBones[hi].locked;
    if (ImGui::Checkbox(u8"\u9501\u5b9a", &lk))
      s_humanBones[hi].locked = lk;
  } else {
    ImGui::SameLine();
    ImGui::TextDisabled(u8"(\u4ece\u9aa8)");
  }
}

static void DrawBoneTreePanel() {
  if (!g_showBoneParams)
    return;
  // 默认放主面板右侧：主面板高度随状态变化（冻结后会多出 root 滑条），放左下会重叠
  ImGui::SetNextWindowPos(ImVec2(340, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 280), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"\u9aa8\u9abc\u53c2\u6570", &g_showBoneParams,
                    ImGuiWindowFlags_NoCollapse |
                        (g_pinPanels ? ImGuiWindowFlags_NoMove : 0))) {
    ImGui::End();
    return;
  }
  if (ImGui::Button(u8"\u64a4\u9500 (Ctrl+Z)"))
    UndoPerform();
  ImGui::SameLine();
  if (ImGui::Button(u8"\u91cd\u505a (Ctrl+Y)"))
    RedoPerform();
  ImGui::SameLine();
  ImGui::TextDisabled("%d / %d", UndoDepth(), RedoDepth());
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u64cd\u4f5c\u5e8f\u5217\uff08\u6700\u65b0\u5728\u4e0a\uff09");
  ImGui::BeginChild("##undohist", ImVec2(0, 70), true);
  if (g_undoStack.empty()) {
    ImGui::TextDisabled(u8"\uff08\u7a7a\uff09");
  } else {
    int n = (int)g_undoStack.size();
    for (int i = n - 1; i >= 0; i--)
      ImGui::Text("%d. %s", n - i, UndoLabelAt(i));
  }
  ImGui::EndChild();
  if (!g_redoStack.empty()) {
    ImGui::TextDisabled(u8"\u53ef\u91cd\u505a\uff1a%d \u6b65", RedoDepth());
  }
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u5728 3D \u89c6\u56fe\u91cc\u70b9\u9009\u9aa8\u9abc\uff08\u9700\u52fe\u9009\u5168\u91cf/\u4ece\u9aa8\u94fe\uff09");
  DrawBoneParams();
  DrawIkPanel();
  ImGui::End();
}
