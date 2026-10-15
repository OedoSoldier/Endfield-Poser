#pragma once
#include "game/mmd_player.h"

struct MmdAdaptationDraft {
  struct Choice {mmd::MotionIssueResolution value;bool dirty=false;};
  mmd::RigAdaptation value;
  std::map<std::string,Choice> choices;
  int revision=-1,editing=-1;
  bool dirty=false;
};
static MmdAdaptationDraft &MmdEditAdaptation() {
  static MmdAdaptationDraft edit;
  if(edit.revision!=g_mmd.adaptationRevision) {
    edit.value=g_mmd.adaptation;edit.revision=g_mmd.adaptationRevision;
    edit.dirty=false;edit.editing=-1;edit.choices.clear();
  }
  return edit;
}
static bool MmdAdaptationDraftDirty() {
  const auto &edit=MmdEditAdaptation();
  return edit.dirty||std::any_of(edit.choices.begin(),edit.choices.end(),[](const auto &v){return v.second.dirty;});
}
static bool MmdCommitAdaptationDraft(const std::string &onlyTrack={}) {
  auto &edit=MmdEditAdaptation();auto next=edit.value;
  try {
    std::set<std::string> destinations;
    std::set<int> roles;
    for(const auto &v:edit.choices)if(v.second.dirty&&(onlyTrack.empty()||onlyTrack==v.first)) {
      if(v.second.value.action==6&&!roles.insert(v.second.value.role).second)
        throw std::runtime_error(u8"多个待处理轨道选择了同一个角色部位，请分别选择");
      if(v.second.value.action==0&&!destinations.insert(v.second.value.destination).second)
        throw std::runtime_error(u8"多个待处理轨道选择了同一个目标骨；请分别选择目标，或使用多段父级叠加动作");
      mmd::ApplyMotionIssueResolution(next,v.first,v.second.value);
    }
    if(!MmdApplyAdaptation(next,g_mmd.sourcePreset))return false;
    edit.value=g_mmd.adaptation;edit.revision=g_mmd.adaptationRevision;edit.dirty=false;
    for(auto &v:edit.choices)if(onlyTrack.empty()||onlyTrack==v.first)v.second.dirty=false;
    return true;
  } catch(const std::exception &e) {g_mmd.status=std::string(u8"未应用，保留当前适配：")+e.what();return false;}
}
