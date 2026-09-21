#pragma once

// 骨骼层级面板（Blender 风格）：树 + 搜索 + 与 3D 选中联动 + 旋转/位置参数。
// 数据源是 game/skeleton.h 的 s_allBones（DFS 顺序 + parentIdx），过滤规则复用
// rig_gizmo.h 的 RigShowBone：humanoid 恒定显示，从骨链根看开关，全量模式全给。

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

static bool g_showBoneTree = true;
static char g_boneTreeSearch[64] = "";

static bool BoneNameMatch(const char *name, const char *q) {
  if (!q || !q[0])
    return true;
  if (!name)
    return false;
  size_t qn = strlen(q);
  for (const char *p = name; *p; p++) {
    size_t i = 0;
    while (i < qn && p[i]) {
      char a = p[i], b = q[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
      if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
      if (a != b)
        break;
      i++;
    }
    if (i == qn)
      return true;
  }
  return false;
}

// humanoid 骨显示成 "LeftUpperArm (Bip001_L_UpperArm)" 这类更好认的标签
static void BoneTreeLabel(int idx, char *out, size_t sz) {
  const AllBone &b = s_allBones[idx];
  int hi = FindTransformIndex(b.transform);
  if (hi >= 0) {
    const char *hn = HumanBoneName(s_humanBones[hi].humanBone);
    if (hn && hn[0])
      snprintf(out, sz, "%s (%s)", hn, b.name);
    else
      snprintf(out, sz, "%s", b.name);
  } else {
    snprintf(out, sz, "%s", b.name);
  }
}

static void DrawBoneTreeRow(int idx, bool useAll) {
  const AllBone &b = s_allBones[idx];
  char label[192];
  BoneTreeLabel(idx, label, sizeof(label));
  int hi = FindTransformIndex(b.transform);
  bool locked = hi >= 0 && s_humanBones[hi].locked;
  bool selected = (b.transform == g_selectedTransform);
  if (locked)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
  if (ImGui::Selectable(label, selected))
    SelectTransform(b.transform, b.name);
  if (locked)
    ImGui::PopStyleColor();
}

static void DrawBoneTreeNode(int idx, const std::vector<std::vector<int>> &children,
                             bool useAll, int depth) {
  const AllBone &b = s_allBones[idx];
  if (!RigShowBone(b.transform, b.name, useAll))
    return;
  std::vector<int> vis;
  for (int c : children[idx]) {
    const AllBone &cb = s_allBones[c];
    if (RigShowBone(cb.transform, cb.name, useAll))
      vis.push_back(c);
  }
  char label[192];
  BoneTreeLabel(idx, label, sizeof(label));
  int hi = FindTransformIndex(b.transform);
  bool locked = hi >= 0 && s_humanBones[hi].locked;
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_SpanAvailWidth;
  if (b.transform == g_selectedTransform)
    flags |= ImGuiTreeNodeFlags_Selected;
  if (vis.empty())
    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
  if (depth < 1)
    flags |= ImGuiTreeNodeFlags_DefaultOpen;
  if (locked)
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
  bool open = ImGui::TreeNodeEx((void *)b.transform, flags, "%s", label);
  if (locked)
    ImGui::PopStyleColor();
  // 点标签选中（点小三角只展开/收起，不改变选中）
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    SelectTransform(b.transform, b.name);
  if (open && !vis.empty()) {
    for (int c : vis)
      DrawBoneTreeNode(c, children, useAll, depth + 1);
    ImGui::TreePop();
  }
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
  if (!g_showBoneTree)
    return;
  ImGui::SetNextWindowPos(ImVec2(10, 205), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(320, 470), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"\u9aa8\u9abc\u5c42\u7ea7", &g_showBoneTree,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##bsearch",
                           u8"\u641c\u7d22\u9aa8\u9abc\uff08\u540d\u5b57\u5305\u542b\u5373\u53ef\uff09",
                           g_boneTreeSearch, sizeof(g_boneTreeSearch));
  if (ImGui::Button(u8"\u64a4\u9500 (Ctrl+Z)"))
    UndoPerform();
  ImGui::SameLine();
  if (ImGui::Button(u8"\u91cd\u505a (Ctrl+Y)"))
    RedoPerform();
  ImGui::SameLine();
  ImGui::TextDisabled("%d / %d", UndoDepth(), RedoDepth());
  ImGui::Separator();
  bool useAll = g_fullBones && !s_allBones.empty();
  ImGui::BeginChild("##tree", ImVec2(0, 300), true);
  if (s_allBones.empty()) {
    ImGui::TextDisabled(u8"\u5c1a\u672a\u91c7\u96c6\u5230\u9aa8\u9abc");
  } else if (g_boneTreeSearch[0]) {
    // 搜索态：平铺列出命中的骨，不再画层级（更快也更好找）
    for (size_t i = 0; i < s_allBones.size(); i++) {
      const AllBone &b = s_allBones[i];
      if (!RigShowBone(b.transform, b.name, useAll))
        continue;
      if (!BoneNameMatch(b.name, g_boneTreeSearch))
        continue;
      DrawBoneTreeRow((int)i, useAll);
    }
  } else {
    std::vector<std::vector<int>> children(s_allBones.size());
    std::vector<int> roots;
    for (size_t i = 0; i < s_allBones.size(); i++) {
      int p = s_allBones[i].parentIdx;
      if (p >= 0 && p < (int)s_allBones.size())
        children[p].push_back((int)i);
      else
        roots.push_back((int)i);
    }
    for (int r : roots)
      DrawBoneTreeNode(r, children, useAll, 0);
  }
  ImGui::EndChild();
  ImGui::Separator();
  DrawBoneParams();
  DrawIkPanel();
  ImGui::End();
}
