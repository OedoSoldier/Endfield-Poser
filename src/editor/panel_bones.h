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
    SetBoneLocalRot(t, Quat::FromEulerDeg(e));
  }
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputFloat3(u8"##rotin", &e.x, "%.2f")) {
    SetBoneLocalRot(t, Quat::FromEulerDeg(e));
  }
  Vec3 p = GetBoneLocalPos(t);
  ImGui::TextDisabled(u8"\u4f4d\u7f6e (XYZ)");
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat3(u8"##pos", &p.x, -1.0f, 1.0f, "%.3f")) {
    SetBoneLocalPos(t, p);
  }
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputFloat3(u8"##posin", &p.x, "%.4f")) {
    SetBoneLocalPos(t, p);
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
  ImGui::TextDisabled(u8"\u5728 3D \u89c6\u56fe\u91cc\u70b9\u9009\u9aa8\u9abc\uff08\u9700\u52fe\u9009\u5168\u91cf/\u4ece\u9aa8\u94fe\uff09");
  DrawBoneParams();
  DrawIkPanel();
  ImGui::End();
}
