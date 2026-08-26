#pragma once

// Task 4.1：面部 BlendShape 面板。
// 按网格（CollapsingHeader）分组展示全部形态键滑条（0-100），拖动实时写入；
// 顶部提供搜索过滤与"恢复原始"。

#include "imgui.h"
#include "game/morph.h"

#include <cstring>

static char g_morphFilter[64] = "";

static void DrawMorphPanel() {
  if (s_blendShapes.empty()) {
    ImGui::TextDisabled(u8"\u672a\u53d1\u73b0 BlendShape\uff08\u8bf7\u5148\u51bb\u7ed3\u89d2\u8272\uff09");
    return;
  }

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
