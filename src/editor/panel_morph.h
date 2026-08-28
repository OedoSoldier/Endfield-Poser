#pragma once

// Task 4.1：面部 BlendShape 面板。
// 按网格（CollapsingHeader）分组展示全部形态键滑条（0-100），拖动实时写入；
// 顶部提供搜索过滤与"恢复原始"。

#include "imgui.h"
#include "game/morph.h"
#include "game/smc_morph.h"

#include <cstring>

static char g_morphFilter[64] = "";

// SMC（游戏原生表情，参照 EIEM smc_face.h）区块：口型 + 表情滑条 0-1
static void DrawSMCSection() {
  if (!SMCSectionReady()) {
    if (s_smcClass)
      ImGui::TextDisabled(u8"SMC \u521d\u59cb\u5316\u4e2d\uff08\u8bf7\u5148\u51bb\u7ed3\u89d2\u8272\uff09");
    else
      ImGui::TextDisabled(u8"\u6e38\u620f\u539f\u751f SMC \u672a\u627e\u5230\uff0c\u53ea\u7528 BlendShape");
    return;
  }

  bool open = ImGui::CollapsingHeader(
      u8"\u6e38\u620f\u539f\u751f\u8868\u60c5 (SMC)",
      ImGuiTreeNodeFlags_DefaultOpen);
  if (!open)
    return;

  bool driving = SMCFaceDriving();
  if (ImGui::Checkbox(u8"\u542f\u7528 SMC \u9a71\u52a8", &driving))
    SMCFaceSetDriving(driving);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u5168\u90e8\u5f52\u96f6")) {
    SMCRestoreWeights();
    SMCFaceSetDriving(true);
  }
  ImGui::Separator();

  ImGui::BeginChild("##smclist", ImVec2(0, 0), false);
  int count = SMCSliderCount();
  for (int i = 0; i < count; i++) {
    const char *label = SMCSliderLabel(i);
    float v = SMCSliderValue(i);
    ImGui::PushID(i);
    if (ImGui::SliderFloat(label, &v, 0.0f, 1.0f, "%.2f"))
      SMCSliderSet(i, v);
    ImGui::PopID();
  }
  ImGui::EndChild();
}

static void DrawMorphPanel() {
  DrawSMCSection();
  if (s_blendShapes.empty()) {
    return;
  }

  ImGui::Separator();
  ImGui::TextDisabled(u8"BlendShape\uff08\u672c\u6e38\u620f\u53ef\u80fd\u4e3a\u7a7a\uff09");
  ImGui::TextDisabled(u8"\u5171 %zu \u4e2a\u5f62\u6001\u952e", s_blendShapes.size());
  ImGui::InputText(u8"\u641c\u7d22##morph", g_morphFilter, sizeof(g_morphFilter));
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u6062\u590d\u539f\u59cb")) {
    RestoreBlendShapes();
    g_morphFilter[0] = 0;
  }
  ImGui::Separator();

  ImGui::BeginChild("##morphlist");
  const char *filter = g_morphFilter;
  const char *curMesh = nullptr;
  bool meshOpen = false;
  for (BlendShapeSlot &s : s_blendShapes) {
    if (curMesh == nullptr || strcmp(curMesh, s.meshName) != 0) {
      if (meshOpen)
        ImGui::Unindent();
      curMesh = s.meshName;
      meshOpen = ImGui::CollapsingHeader(s.meshName);
      if (meshOpen)
        ImGui::Indent();
    }
    if (!meshOpen)
      continue;
    if (filter[0] && !strstr(s.name, filter))
      continue;
    int idx = (int)(&s - &s_blendShapes[0]);
    ImGui::PushID(idx);
    float v = s.value;
    if (ImGui::SliderFloat(s.name, &v, 0.0f, 100.0f, "%.0f"))
      SetBlendShapeWeight(s, v);
    ImGui::PopID();
  }
  if (meshOpen)
    ImGui::Unindent();
  ImGui::EndChild();
}
