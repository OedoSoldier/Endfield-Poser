#pragma once

// Task 4.1：面部 BlendShape 面板。
// 按网格（CollapsingHeader）分组展示全部形态键滑条（0-100），拖动实时写入；
// 顶部提供搜索过滤与"恢复原始"。

#include "imgui.h"
#include "game/morph.h"
#include "game/freeze.h"
#include "game/smc_morph.h"

#include <cstring>

static char g_morphFilter[64] = "";
static char g_mmdFaceFilter[128] = "";

static void DrawMmdFaceSection() {
  SMCManualPrepare();
  auto &face=s_manualFace;
  bool playing=MmdOwnsPose();
  bool ready=SMCSectionReady()&&s_faceBonesCaptured&&s_driveBaseReady&&!s_captureNeutral;
  ImGui::BeginChild("##mmd-face-list",ImVec2(0,0),false);
  ImGui::TextWrapped(face.profile?u8"当前角色：%s":u8"当前角色暂无专属表情，使用固定映射",face.profile?face.profile->label.c_str():"");
  if(playing)ImGui::TextWrapped(u8"播放器正在控制表情，停止后可手动调节。");
  else if(!g_frozen) {
    ImGui::TextWrapped(u8"先冻结角色，再调节表情；无需载入动作。");
    ImGui::BeginDisabled(!CharAnimatorAlive());
    if(ImGui::Button(u8"冻结并编辑"))FreezeCharacter();
    ImGui::EndDisabled();
  } else if(!ready)ImGui::TextWrapped(u8"正在准备角色表情，请稍候。");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##mmd-face-search",u8"搜索表情",g_mmdFaceFilter,sizeof(g_mmdFaceFilter));
  ImGui::BeginDisabled(playing||!g_frozen||!ready);
  ImGui::Checkbox(u8"缺失时使用固定映射",&face.fallback);
  ImGui::SetNextItemWidth((std::max)(80.f,ImGui::GetContentRegionAvail().x-94.f));
  float strength=face.strength*100;
  if(ImGui::SliderFloat(u8"整体强度",&strength,0,200,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) {face.strength=strength*.01f;face.applied=true;}
  if(ImGui::Button(u8"全部归零"))face.clear();
  ImGui::SameLine();ImGui::TextDisabled(u8"可叠加多个表情");
  ImGui::EndDisabled();
  const char *groups[]={"",u8"眉毛",u8"眼睛",u8"嘴部",u8"其他"};
  for(int group=1;group<=4;++group) {
    int visible=0;
    for(int i=0;i<int(face.controls.size());++i) {
      const auto &c=face.controls[i];auto label=mmd_face_controls::Label(c.name,c.panel,i);
      if(c.panel==group&&(!g_mmdFaceFilter[0]||strstr(label.c_str(),g_mmdFaceFilter)||strstr(c.name.c_str(),g_mmdFaceFilter)))++visible;
    }
    if(!visible)continue;
    ImGui::PushID(group);
    if(ImGui::CollapsingHeader(groups[group],ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::BeginDisabled(playing||!g_frozen||!ready);
      if(ImGui::SmallButton(u8"本组归零"))face.clear(group);
      ImGui::EndDisabled();
      for(int i=0;i<int(face.controls.size());++i) {
        const auto &c=face.controls[i];auto label=mmd_face_controls::Label(c.name,c.panel,i);
        if(c.panel!=group||(g_mmdFaceFilter[0]&&!strstr(label.c_str(),g_mmdFaceFilter)&&!strstr(c.name.c_str(),g_mmdFaceFilter)))continue;
        int source=SMCManualSource(c);
        ImGui::PushID(i);
        ImGui::TextUnformatted(label.c_str());
        if(ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();ImGui::Text(u8"原始名称：%s",c.name.c_str());
          ImGui::TextUnformatted(source==1?u8"角色专属映射":source==2?u8"固定映射":u8"当前没有可用映射");
          if(c.morph>=0&&face.profile&&!face.profile->morphs[c.morph].reason.empty())
            ImGui::TextWrapped("%s",face.profile->morphs[c.morph].reason.c_str());
          ImGui::EndTooltip();
        }
        if(!source){ImGui::SameLine();ImGui::TextDisabled(u8"（不可用）");}
        ImGui::BeginDisabled(playing||!g_frozen||!ready||!source);
        float value=face.weights[i];
        ImGui::SetNextItemWidth((std::max)(60.f,ImGui::GetContentRegionAvail().x-48.f));
        if(ImGui::SliderFloat("##weight",&value,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp))face.set(i,value);
        ImGui::SameLine();if(ImGui::SmallButton(u8"归零"))face.set(i,0);
        ImGui::EndDisabled();ImGui::PopID();
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

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
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u8bfb\u5165\u5f53\u524d\u8868\u60c5")) {
    // 把角色脸上正在演的表情读成滑条初值（原来是"全 0 起步"）
    SMCReadCurrentToSliders();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(u8"\u628a\u6e38\u620f\u5f53\u524d\u7684 morph \u6743\u91cd"
                      u8"\u8bfb\u8fdb\u6ed1\u6761\uff0c\u518d\u63a5\u7740\u8c03");
  const char *readStatus = SMCReadStatusText();
  if (readStatus && readStatus[0])
    ImGui::TextDisabled("%s", readStatus);
  ImGui::Separator();

  ImGui::BeginChild("##smclist", ImVec2(0, 0), false);
  int count = SMCSliderCount();
  auto catalog=SMCManualCatalog();
  for (int i = 0; i < count; i++) {
    auto label=mmd_face_controls::Label(catalog[i].name,catalog[i].panel,i);
    float v = SMCSliderValue(i);
    ImGui::PushID(i);
    if (ImGui::SliderFloat(label.c_str(), &v, 0.0f, 1.0f, "%.2f"))
      SMCSliderSet(i, v);
    if(ImGui::IsItemHovered())ImGui::SetTooltip(u8"原始通道：%s",SMCSliderLabel(i));
    ImGui::PopID();
  }
  ImGui::EndChild();
}

static void DrawGameMorphPanel() {
  DrawSMCSection();
  if (s_blendShapes.empty()) {
    ImGui::TextDisabled(
        u8"\u672c\u4f5c\u9762\u90e8\u7531 SMC \u9aa8\u9abc\u9a71\u52a8\uff0c\u7f51\u683c\u4e0a"
        u8"\u6ca1\u6709 BlendShape \u76ee\u6807\uff08\u8be5\u533a\u4e3a\u7a7a\u662f\u6b63\u5e38\u7684\uff09");
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

static void DrawGazeSection() {
  if(!ImGui::CollapsingHeader(u8"眼睛朝向",ImGuiTreeNodeFlags_DefaultOpen))return;
  bool ready=poser_gaze::binding.basis.ready&&poser_gaze::binding.owner==g_charAnimator&&
      poser_gaze::binding.generation==s_faceGeneration;
  ImGui::BeginDisabled(!ready);
  int mode=int(poser_gaze::settings.mode);
  ImGui::SetNextItemWidth(-1);
  if(ImGui::Combo("##gaze-mode",&mode,u8"跟随游戏 / 动作\0手动方向\0自动看向镜头\0"))
    poser_gaze::settings.mode=eye_gaze::Mode(mode);
  if(mode!=0) {
    ImGui::SetNextItemWidth((std::max)(70.f,ImGui::GetContentRegionAvail().x-52.f));
    ImGui::SliderFloat(u8"左右",&poser_gaze::settings.yaw,-30,30,"%.1f°",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::IsItemHovered())ImGui::SetTooltip(u8"正值向角色右侧，负值向角色左侧");
    ImGui::SetNextItemWidth((std::max)(70.f,ImGui::GetContentRegionAvail().x-52.f));
    ImGui::SliderFloat(u8"上下",&poser_gaze::settings.pitch,-20,20,"%.1f°",ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::IsItemHovered())ImGui::SetTooltip(u8"正值向上，负值向下");
    if(ImGui::SmallButton(u8"方向归零"))poser_gaze::settings.yaw=poser_gaze::settings.pitch=0;
    if(mode==2)ImGui::TextWrapped(u8"滑条微调看向镜头的方向；镜头在身后时回到正前方。");
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped("%s",ready?poser_gaze::status:u8"冻结角色，等待眼睛控制就绪");
  if(ready&&!g_frozen) {
    ImGui::BeginDisabled(!CharAnimatorAlive());
    if(ImGui::SmallButton(u8"冻结并控制眼睛"))FreezeCharacter();
    ImGui::EndDisabled();
  }
  ImGui::Separator();
}
static void DrawMorphPanel() {
  DrawGazeSection();
  int mode=s_mmdFaceMode?1:0;
  ImGui::SetNextItemWidth(-1);
  if(ImGui::Combo("##face-mode",&mode,u8"游戏模式\0MMD 模式\0"))SMCManualMode(mode==1);
  if(s_mmdFaceMode)DrawMmdFaceSection();
  else {
    ImGui::BeginDisabled(MmdOwnsPose());
    DrawGameMorphPanel();
    ImGui::EndDisabled();
  }
}
