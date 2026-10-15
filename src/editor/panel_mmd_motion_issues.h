#pragma once
#include "game/mmd_squad.h"
#include "editor/panel_mmd_adaptation.h"

static bool s_motionIssuesShow=false;
static int s_motionIssuesTarget=0;
static std::string s_motionIssueTrack;
static const mmd::MotionIssues &MmdMotionIssuesFor(int target) {
  static const mmd::MotionIssues empty;
  if(target==0)return g_mmd.editedBody?empty:g_mmd.boneIssues;
  auto &slot=g_squad.slots[target-1];
  if(slot.issueRevision!=g_mmd.adaptationRevision) {
    slot.boneIssues=slot.edited?mmd::MotionIssues{}:mmd::AnalyzeMotionIssues(g_mmd.rig,slot.clip,g_mmd.adaptation);
    slot.issueRevision=g_mmd.adaptationRevision;
  }
  return slot.boneIssues;
}
static void DrawMmdMotionIssueBanner(int target) {
  const auto &issues=MmdMotionIssuesFor(target);
  if(!issues.unresolved.empty()) {
    ImGui::TextColored(ImVec4(1,.65f,.2f,1),u8"动作适配：%zu 项需要处理",issues.unresolved.size());
    ImGui::SameLine();
  }
  if(!issues.manuallyMapped.empty()) {
    ImGui::TextColored(ImVec4(.45f,.85f,.55f,1),u8"已手动映射 %zu 项",issues.manuallyMapped.size());
    ImGui::SameLine();
  }
  if(ImGui::SmallButton(issues.unresolved.empty()?u8"查看骨骼映射 / 扩展骨…":u8"查看并处理…")) {
    s_motionIssuesShow=true;s_motionIssuesTarget=target;s_motionIssueTrack.clear();
  }
}
static void DrawMmdMotionIssuesPanel() {
  if(!s_motionIssuesShow)return;
  poser_ui::NextPanel(u8"动作扩展骨与导入检查",{630,160},{620,630},{450,350},g_resetPanelLayoutFrames>0);
  if(!ImGui::Begin(u8"动作扩展骨与导入检查",&s_motionIssuesShow,g_pinPanels?ImGuiWindowFlags_NoMove:0)) {ImGui::End();return;}
  if(ImGui::Combo(u8"检查动作",&s_motionIssuesTarget,u8"单人动作\0小队第 1 位\0小队第 2 位\0小队第 3 位\0小队第 4 位\0"))s_motionIssueTrack.clear();
  const auto &file=s_motionIssuesTarget==0?g_mmd.file:g_squad.slots[s_motionIssuesTarget-1].file;
  if(file.empty())ImGui::TextDisabled(u8"此位置尚未导入动作");
  else ImGui::TextWrapped("%s",file.c_str());
  const bool busy=g_mmd.session.active||g_mmd.loading||s_mmdStartRequest.active||MmdSquadBusy()||g_blenderEditing||g_mmd.preview;
  ImGui::BeginDisabled(busy||file.empty());
  if(ImGui::Button(u8"一键接入已有骨骼")) {
    if(!MmdAdaptationDraftDirty()||MmdCommitAdaptationDraft()) {
      const auto &clip=s_motionIssuesTarget==0?g_mmd.clip:g_squad.slots[s_motionIssuesTarget-1].clip;
      std::vector<const mmd::MotionClip *> clips{&g_mmd.clip};
      for(const auto &slot:g_squad.slots)if(!slot.edited)clips.push_back(&slot.clip);
      const auto result=mmd::ConnectExistingMotionOutputs(g_mmd.rig,clip,g_mmd.adaptation,clips);
      if(result.connected.empty())g_mmd.status=u8"没有可直接接入的骨骼；已有手动设置、输出冲突或用途不明确的项目请逐项处理";
      else if(MmdApplyAdaptation(result.adaptation,g_mmd.sourcePreset))
        g_mmd.status=u8"已接入 "+std::to_string(result.connected.size())+u8" 项；可继续修改角色部位映射，并保存适配预设";
    }
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped(u8"将未接入的已有身体骨连接到角色部位（如脚尖 / 足先EX）。保留手动设置，不覆盖其他已加载动作的有效轨道。");
  const auto &issues=MmdMotionIssuesFor(s_motionIssuesTarget);
  ImGui::TextWrapped(u8"已有源骨骼按同名或明确的标准别名自动匹配，手动设置优先。点击轨道可改绑；无法确定用途的扩展骨仍需处理。适配设置由单人和小队共用，每个动作分别匹配。");
  ImGui::TextColored(issues.unresolved.empty()?ImVec4(.45f,.85f,.55f,1):ImVec4(1,.65f,.2f,1),
                    u8"需要处理 %zu 项 / 已自动适配 %zu 项 / 已手动映射 %zu 项",issues.unresolved.size(),issues.automaticallyMapped.size(),issues.manuallyMapped.size());
  ImGui::TextDisabled(u8"仅归零 %zu 项 / 已忽略 %zu 项",issues.neutral.size(),issues.ignored.size());
  static char filter[160]={};ImGui::InputText(u8"筛选轨道",filter,sizeof(filter));
  ImGui::BeginChild("##motionissues",poser_ui::Size(0,170),true);
  auto &edit=MmdEditAdaptation();
  for(const auto &item:issues.unresolved) {
    if(!mmd_ui::BoneMatches(item.track,filter))continue;
    ImGui::PushID(item.track.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.7f,.3f,1));
    const auto pending=edit.choices.find(item.track);
    const auto label=mmd_ui::BoneLabel(item.track)+(issues.manuallyMapped.count(item.track)?u8"（已手动映射，需检查）":"")+
                     (pending!=edit.choices.end()&&pending->second.dirty?u8"（待应用）":"");
    if(ImGui::Selectable(label.c_str(),s_motionIssueTrack==item.track))s_motionIssueTrack=item.track;
    ImGui::PopStyleColor();
    ImGui::TextWrapped("%s",item.reason.c_str());ImGui::PopID();
  }
  for(const auto &mapping:issues.manuallyMapped) {
    if(!mmd_ui::BoneMatches(mapping.first,filter))continue;
    if(std::any_of(issues.unresolved.begin(),issues.unresolved.end(),[&](const mmd::BoneMotionIssue &v){return v.track==mapping.first;}))continue;
    const auto label=mmd_ui::BoneLabel(mapping.first)+u8"（已手动映射）";
    if(ImGui::Selectable(label.c_str(),s_motionIssueTrack==mapping.first))s_motionIssueTrack=mapping.first;
    ImGui::TextWrapped(u8"目标源骨骼：%s",mmd_ui::BoneListLabel(mapping.second).c_str());
  }
  if(ImGui::TreeNode(u8"已自动适配（点击可手动改绑）")) {
    for(const auto &mapping:issues.automaticallyMapped) {
      if(!mmd_ui::BoneMatches(mapping.first,filter))continue;
      if(ImGui::Selectable(mmd_ui::BoneLabel(mapping.first).c_str(),s_motionIssueTrack==mapping.first))s_motionIssueTrack=mapping.first;
      for(const auto &bone:mapping.second)ImGui::TextDisabled(u8"→ %s",mmd_ui::BoneLabel(bone).c_str());
    }
    ImGui::TreePop();
  }
  if(issues.unresolved.empty())ImGui::TextWrapped(u8"未发现未处理的有效骨骼轨道；这不代表动作已完成画面验收。");
  ImGui::EndChild();
  auto selected=std::find_if(issues.unresolved.begin(),issues.unresolved.end(),[](const mmd::BoneMotionIssue &v){return v.track==s_motionIssueTrack;});
  if(selected!=issues.unresolved.end()||issues.destinations.count(s_motionIssueTrack)||issues.manuallyMapped.count(s_motionIssueTrack)) {
    const bool unresolved=selected!=issues.unresolved.end();
    const auto item=unresolved?*selected:mmd::BoneMotionIssue{s_motionIssueTrack,"",u8"当前轨道已生效，可改绑到其他已有源骨骼。"};
    ImGui::TextWrapped(u8"%s：%s",mmd_ui::BoneLabel(item.track).c_str(),item.advice.c_str());
    if(item.keys)ImGui::Text(u8"首个待处理关键帧：第 %u 帧 / %.2f 秒；关键帧 %zu",item.firstFrame,item.firstFrame/30.,item.keys);
    auto [where,created]=edit.choices.try_emplace(item.track);
    auto &choice=where->second;auto &resolution=choice.value;auto &c=resolution.control;
    if(!choice.dirty)
      resolution=mmd::CurrentMotionIssueResolution(item,issues,g_mmd.rig,edit.value,unresolved);
    auto &action=resolution.action;auto &destination=resolution.destination;
    const bool busy=g_mmd.session.active||g_mmd.loading||s_mmdStartRequest.active||MmdSquadBusy()||g_blenderEditing||g_mmd.preview;
    if(busy)ImGui::TextWrapped(u8"停止播放或断开 Blender 后可修改适配。");
    ImGui::BeginDisabled(busy);
    choice.dirty|=ImGui::Combo(u8"处理方式",&action,u8"绑定到已有源骨骼\0插入多段父级\0通用握拳控制\0通用张指控制\0手臂 IK 目标\0视线目标\0连接到角色部位\0");
    if(action<=1||action==6) {
      choice.dirty|=MmdBoneChoice(action==1?u8"插入到此骨骼之前":u8"目标源骨骼",destination,u8"请选择实际控制部位");
      if(action==6) {
        if(ImGui::BeginCombo(u8"角色部位",mmd_ui::RoleLabel(resolution.role).c_str())) {
          for(int role=0;role<55;++role)if(ImGui::Selectable(mmd_ui::RoleLabel(role).c_str(),resolution.role==role)) {
            resolution.role=role;choice.dirty=true;
          }
          ImGui::EndCombo();
        }
      }
      if(action==1)ImGui::TextWrapped(u8"父级初始位置与目标骨一致。不同旋转中心可在“高级 → 骨架适配”调整；已有同名骨需修正其连接关系。");
    } else {
      if(action!=5)choice.dirty|=ImGui::Combo(u8"控制哪只手",&c.side,u8"左手\0右手\0");
      if(action==2||action==3) {
        int finger=c.finger+1;
        if(ImGui::Combo(u8"手指范围",&finger,u8"全部手指\0拇指\0食指\0中指\0无名指\0小指\0")){c.finger=finger-1;choice.dirty=true;}
        choice.dirty|=ImGui::Combo(u8"动作输入轴",&c.inputAxis,"X\0Y\0Z\0");
        choice.dirty|=ImGui::SliderFloat(u8"控制比例（负值反向）",&c.gain,-3,3,"%.2f");
        ImGui::TextWrapped(u8"通用模板按手掌方向分配关节旋转，并非所有握拳插件的精确复刻。应用后逐帧核对轴向和比例。");
      }
      choice.dirty|=MmdBoneChoice(u8"控制骨父级",c.parent,u8"默认（手腕 / 总父级 / 头部）");
      if(action!=5)choice.dirty|=ImGui::Checkbox(u8"同时建立左右对应控制（无轨道的一侧不启用）",&c.paired);
      if(action==4)choice.dirty|=ImGui::Checkbox(u8"使用目标骨的手腕朝向",&c.wristRotation);
    }
    ImGui::BeginDisabled((action<=1||action==6)&&destination.empty());
    if(ImGui::Button(u8"应用此处理并重新检查")) {
      choice.dirty=true;MmdCommitAdaptationDraft(item.track);
    }
    ImGui::EndDisabled();ImGui::SameLine();
    if(ImGui::Button(u8"恢复自动轨道匹配")) {
      auto next=edit.value;
      const auto &clip=s_motionIssuesTarget==0?g_mmd.clip:g_squad.slots[s_motionIssuesTarget-1].clip;
      mmd::RestoreAutomaticTrackMapping(next,g_mmd.rig,clip,item.track);
      if(MmdApplyAdaptation(next,g_mmd.sourcePreset)) {
        choice.dirty=false;edit.value=g_mmd.adaptation;edit.revision=g_mmd.adaptationRevision;edit.dirty=false;
      }
    }
    if(ImGui::Button(u8"明确忽略此轨道")) {
      auto next=edit.value;
      if(item.kind==mmd::MotionIssueKind::Binding) {
        for(auto i=next.tracks.begin();i!=next.tracks.end();)if(i->second==item.track)i=next.tracks.erase(i);else ++i;
      } else next.ignoredTracks.insert(item.track);
      if(MmdApplyAdaptation(next,g_mmd.sourcePreset)) {
        choice.dirty=false;edit.value=g_mmd.adaptation;edit.revision=g_mmd.adaptationRevision;edit.dirty=false;
      }
    }
    ImGui::EndDisabled();
  }
  if(ImGui::TreeNode(u8"仅归零的轨道（通常无需处理）")) {
    for(const auto &name:issues.neutral)ImGui::TextDisabled("%s",mmd_ui::BoneLabel(name).c_str());ImGui::TreePop();
  }
  if(!issues.replaced.empty()&&ImGui::TreeNode(u8"被手动映射替换的原轨道")) {
    for(const auto &name:issues.replaced)ImGui::TextWrapped("%s",name.c_str());ImGui::TreePop();
  }
  if(ImGui::TreeNode(u8"已忽略轨道")) {
    std::string restore;
    ImGui::BeginDisabled(g_mmd.session.active||g_mmd.loading||s_mmdStartRequest.active||MmdSquadBusy()||g_blenderEditing||g_mmd.preview);
    for(const auto &name:issues.ignored) {ImGui::PushID(name.c_str());ImGui::TextUnformatted(mmd_ui::BoneLabel(name).c_str());ImGui::SameLine();if(ImGui::SmallButton(u8"恢复检查"))restore=name;ImGui::PopID();}
    if(!restore.empty()) {
      auto next=edit.value;next.ignoredTracks.erase(restore);auto binding=next.tracks.find(restore);
      if(binding!=next.tracks.end()&&binding->second.empty())next.tracks.erase(binding);
      if(MmdApplyAdaptation(next,g_mmd.sourcePreset)){edit.value=g_mmd.adaptation;edit.revision=g_mmd.adaptationRevision;edit.dirty=false;}
    }
    ImGui::EndDisabled();ImGui::TreePop();
  }
  if(MmdAdaptationDraftDirty())ImGui::TextColored(ImVec4(1,.7f,.3f,1),u8"有待应用的选择；保存时会一并应用并重新检查");
  ImGui::BeginDisabled(g_mmd.loading||(MmdAdaptationDraftDirty()&&busy));
  if(ImGui::Button(MmdAdaptationDraftDirty()?u8"应用并保存适配预设…":u8"保存适配预设…")) {
    if(!MmdAdaptationDraftDirty()||MmdCommitAdaptationDraft())MmdBeginLoad(4);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(g_mmd.session.active||g_mmd.loading||s_mmdStartRequest.active||MmdSquadBusy()||g_blenderEditing||g_mmd.preview);
  if(ImGui::Button(u8"载入适配预设…"))MmdBeginLoad(3);
  ImGui::EndDisabled();
  if(!g_mmd.adaptationFile.empty()) {
    ImGui::TextWrapped(u8"预设文件：%s",g_mmd.adaptationFile.c_str());
    if(g_mmd.adaptationSavedRevision!=g_mmd.adaptationRevision)ImGui::TextColored(ImVec4(1,.7f,.3f,1),u8"当前适配已修改，请重新保存");
  }
  if(!g_mmd.adaptationRestoreStatus.empty())ImGui::TextWrapped("%s",g_mmd.adaptationRestoreStatus.c_str());
  ImGui::TextWrapped(u8"保存会包含已选择但尚未应用的处理。重启后自动恢复最近一次成功保存 / 载入的预设。");
  ImGui::TextWrapped(u8"已启用控制的参数与删除：MMD 播放器 → 高级 → 骨架适配 → 动作扩展骨。");
  ImGui::TextWrapped("%s",g_mmd.status.c_str());
  ImGui::End();
}
