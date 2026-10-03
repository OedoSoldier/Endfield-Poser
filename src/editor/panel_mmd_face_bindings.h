#pragma once

static bool MmdBindingUsable(int index,bool native) {
  if(native)return SMCNativeChannelReady(index);
  return g_mmd.characterFace&&g_mmd.characterFace==s_characterProfile&&s_characterBinding.ready&&
    s_characterBindingGeneration==s_faceGeneration&&index>=0&&index<int(s_characterBinding.usable.size())&&s_characterBinding.usable[index];
}
static std::string MmdBindingLabel(const std::string &name,int index,bool native,
                                 const std::vector<mmd_face_controls::Native> &catalog) {
  int panel=4;
  if(native) {
    for(const auto &c:catalog)if(c.channel==index){panel=c.panel;break;}
  } else if(g_mmd.characterFace&&index>=0&&index<int(g_mmd.characterFace->morphs.size()))
    panel=mmd_face_controls::Panel(*g_mmd.characterFace,g_mmd.characterFace->morphs[index]);
  return mmd_face_controls::Label(name,panel,(std::max)(0,index));
}
static void MmdBindingOriginalName(const std::string &name) {
  if(ImGui::IsItemHovered()){ImGui::BeginTooltip();ImGui::Text(u8"原始名称：%s",name.c_str());ImGui::EndTooltip();}
}
static void DrawMmdBindingPreview(const char *label,const mmd_face_bindings::Targets &targets,bool native) {
  auto &m=g_mmd;
  bool any=false;for(const auto &t:targets)any|=MmdBindingUsable(t.index,native)&&t.gain>0;
  bool ready=any&&g_frozen&&SMCSectionReady()&&s_faceBonesCaptured&&s_driveBaseReady&&!s_captureNeutral&&
    !MmdSquadBusy()&&!m.preview&&!m.loading&&(!m.session.active||m.timeline.state!=mmd::PlayState::Playing);
  ImGui::BeginDisabled(!ready);
  ImGui::SmallButton(label);
  if(ImGui::IsItemActive()&&ready) {
    SMCMotionFrame frame;frame.active=true;frame.animator=SMCAnimator();frame.generation=s_faceGeneration;
    frame.profile=s_characterProfile;frame.settings.fallback=false;
    frame.settings.driver.fill(native?face_mixing::Driver::Game:face_mixing::Driver::Character);
    mmd_face_bindings::Mapping mapping;(native?mapping.native:mapping.character)=targets;
    mmd_face_bindings::Apply(mapping,1.f,frame,[](int i){return MmdBindingUsable(i,false);});
    SMCShowBindingPreview(frame);
  }
  ImGui::EndDisabled();
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip(
    ready?u8"按住查看，松开恢复。以 100%% 查看当前映射，不受上方部位开关或整体强度影响。":
          u8"冻结角色或暂停单人播放，等待表情就绪后预览；当前角色不支持的表情无法预览。");
}
static void DrawMmdFaceBindings() {
  if(!ImGui::CollapsingHeader(u8"手动绑定（高级）"))return;
  auto &m=g_mmd;
  static int editSource=0;static char filter[128]{};
  ImGui::Combo(u8"编辑映射表",&editSource,u8"角色专属映射\0固定表情映射\0");
  const bool native=editSource==1;
  const auto catalog=SMCManualCatalog();
  ImGui::TextWrapped(u8"每条动作表情可组合多个目标，并分别调整强度。修改自动保存；实际使用方式由上方各部位设置决定。");
  ImGui::TextWrapped(u8"冻结角色或暂停单人播放后，可按住按钮预览标签或整组组合，松开恢复。原始名称放在悬停提示中。");
  if(!g_frozen&&!MmdOwnsPose()) {
    ImGui::BeginDisabled(!CharAnimatorAlive());
    if(ImGui::SmallButton(u8"冻结角色以预览"))FreezeCharacter();
    ImGui::EndDisabled();
  }
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##binding-filter",u8"搜索动作表情（中文或原名）",filter,sizeof(filter));
  if(m.morphMap.empty()){ImGui::TextDisabled(u8"先打开含有表情的 VMD 文件");return;}
  bool locked=m.loading||m.preview||MmdSquadBusy()||(m.session.active&&m.timeline.state==mmd::PlayState::Playing);
  if(locked)ImGui::TextDisabled(u8"暂停单人播放或停止多人播放后可修改绑定");
  ImGui::BeginDisabled(locked||(!native&&!m.characterFace));
  bool changed=false;int ordinal=0;
  for(auto &kv:m.morphMap) {
    int index=m.characterFace?character_face::FindMorph(*m.characterFace,kv.first):-1;
    auto label=MmdBindingLabel(kv.first,index,false,catalog);
    if(index<0) {
      index=mmd_face_bindings::NativeIndex(kv.first,catalog);
      label=index>=0?MmdBindingLabel(kv.first,index,true,catalog):mmd_face_controls::Label(kv.first,4,ordinal);
    }
    ++ordinal;
    if(filter[0]&&!strstr(label.c_str(),filter)&&!strstr(kv.first.c_str(),filter))continue;
    auto &targets=native?kv.second.native:kv.second.character;
    ImGui::PushID(kv.first.c_str());
    bool open=ImGui::TreeNodeEx("##track",ImGuiTreeNodeFlags_None,"%s",label.c_str());
    MmdBindingOriginalName(kv.first);
    ImGui::SameLine();DrawMmdBindingPreview(u8"按住预览组合",targets,native);
    if(open) {
      if(targets.empty())ImGui::TextDisabled(u8"未绑定，点击添加表情");
      int remove=-1;
      for(int row=0;row<int(targets.size());++row) {
        auto &t=targets[row];ImGui::PushID(row);
        auto targetLabel=t.name.empty()?std::string(u8"选择表情"):MmdBindingLabel(t.name,t.index,native,catalog);
        if(!t.name.empty()&&t.index<0)targetLabel=u8"缺失表情";
        ImGui::SetNextItemWidth((std::max)(poser_ui::Scale(130),ImGui::GetContentRegionAvail().x-poser_ui::Scale(155)));
        if(ImGui::BeginCombo("##target",targetLabel.c_str())) {
          const int count=native?int(catalog.size()):m.characterFace?int(m.characterFace->morphs.size()):0;
          for(int i=0;i<count;++i) {
            const int id=native?catalog[i].channel:i;
            if(!native&&!m.characterFace->morphs[i].supported)continue;
            const auto &name=native?catalog[i].name:m.characterFace->morphs[i].name;
            auto option=MmdBindingLabel(name,id,native,catalog);
            ImGui::PushID(i);
            if(ImGui::Selectable(option.c_str(),t.index==id,0,ImVec2(poser_ui::Scale(160),0))) {
              t.name=name;t.index=id;changed=true;
            }
            MmdBindingOriginalName(name);
            ImGui::SameLine();DrawMmdBindingPreview(u8"预览",{{name,id,1}},native);
            ImGui::PopID();
          }
          ImGui::EndCombo();
        }
        MmdBindingOriginalName(t.name);
        ImGui::SameLine();DrawMmdBindingPreview(u8"按住预览",{t},native);
        ImGui::SameLine();if(ImGui::SmallButton(u8"移除"))remove=row;
        float gain=t.gain*100;
        ImGui::SetNextItemWidth((std::max)(poser_ui::Scale(90),ImGui::GetContentRegionAvail().x-poser_ui::Scale(45)));
        if(ImGui::SliderFloat(u8"强度",&gain,0,200,"%.0f%%",ImGuiSliderFlags_AlwaysClamp))t.gain=gain*.01f;
        changed|=ImGui::IsItemDeactivatedAfterEdit();
        if(!native&&m.characterFace&&t.index>=0&&t.index<int(m.characterFace->morphs.size())&&m.characterFace->morphs[t.index].residual>.1f)
          ImGui::TextDisabled(u8"此表情部分形状为近似");
        ImGui::PopID();
      }
      if(remove>=0){targets.erase(targets.begin()+remove);changed=true;}
      ImGui::BeginDisabled(targets.size()>=mmd_face_bindings::MaxTargets);
      if(ImGui::SmallButton(u8"添加表情")){targets.push_back({});changed=true;}
      ImGui::EndDisabled();
      ImGui::SameLine();if(ImGui::SmallButton(u8"清空绑定")){targets.clear();changed=true;}
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if(changed){MmdSaveMappings(native);MmdReport();}
  ImGui::EndDisabled();
}
