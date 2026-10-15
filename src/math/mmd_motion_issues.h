#pragma once
#include "math/mmd_adaptation.h"

namespace mmd {
enum class MotionIssueKind { Binding, Grip, Spread, ArmIk, Gaze, Parent, Accessory, Unknown, Disconnected };
struct BoneMotionIssue {
  std::string track, reason, advice;
  MotionIssueKind kind = MotionIssueKind::Unknown;
  uint32_t firstFrame = 0;
  size_t keys = 0;
};
struct MotionIssues {
  std::vector<BoneMotionIssue> unresolved;
  std::vector<std::string> neutral, ignored, replaced;
  // Applied user configuration, rebuilt from the preset (not transient UI state).
  // Presence here records a manual mapping; unresolved can still flag an invalid output.
  std::map<std::string, std::string> manuallyMapped;
  std::map<std::string, std::vector<std::string>> automaticallyMapped, destinations;
};
// A pending editor choice. Saving must apply these choices before taking the
// serialized snapshot, including choices made on previously selected rows.
struct MotionIssueResolution {
  int action=0;
  int role=0;
  std::string destination;
  std::vector<std::string> previousDestinations;
  MotionControl control;
};
inline void ApplyMotionIssueResolution(RigAdaptation &next,const std::string &track,
                                       const MotionIssueResolution &choice) {
  if(choice.action<0||choice.action>6||track.empty())throw std::runtime_error(u8"无效的轨道处理方式");
  next.ignoredTracks.erase(track);
  if((choice.action<=1||choice.action==6)&&choice.destination.empty())throw std::runtime_error(u8"请为轨道选择目标骨骼："+track);
  if(choice.action==6) {
    if(choice.role<0||choice.role>=55)throw std::runtime_error(u8"无效的角色部位");
    next.roles[choice.role]=choice.destination;
    next.tracks[choice.destination]=track;
  } else if(choice.action==0) {
    // Rebinding an automatic match must stop its former output as well.
    for(const auto &bone:choice.previousDestinations)
      if(bone!=choice.destination)next.tracks[bone]="";
    for(auto &binding:next.tracks)
      if(binding.second==track&&binding.first!=choice.destination)binding.second.clear();
    next.tracks[choice.destination]=track;
  }
  else if(choice.action==1) {
    ExtraBone b;b.name=track;b.before=choice.destination;b.anchor=choice.destination;
    auto old=std::find_if(next.extra.begin(),next.extra.end(),[&](const ExtraBone &v){return v.name==track;});
    if(old==next.extra.end())next.extra.push_back(b);else *old=b;
  } else {
    auto c=choice.control;c.name=track;c.kind=MotionControlKind(choice.action-2);
    const auto mirrored=MirrorBoneName(track);
    if(c.kind!=MotionControlKind::Gaze&&mirrored!=track)
      for(auto &other:next.controls)if(other.name==mirrored){other.paired=false;c.paired=false;}
    auto old=std::find_if(next.controls.begin(),next.controls.end(),[&](const MotionControl &v){return v.name==track;});
    if(old==next.controls.end())next.controls.push_back(c);else *old=c;
  }
}
inline void RestoreAutomaticTrackMapping(RigAdaptation &next,const RigDefinition &rig,const MotionClip &clip,const std::string &track) {
  const auto automatic=ResolvedTrackBindings(rig,clip,{});
  const auto previous=ResolvedTrackBindings(rig,clip,next.tracks);
  for(auto it=next.roles.begin();it!=next.roles.end();) {
    const auto binding=previous.find(it->second);
    if((binding==previous.end()?it->second:binding->second)==track)it=next.roles.erase(it);
    else ++it;
  }
  next.ignoredTracks.erase(track);
  for(auto it=next.tracks.begin();it!=next.tracks.end();) {
    const auto found=automatic.find(it->first);
    const auto &defaultTrack=found==automatic.end()?it->first:found->second;
    if(it->second==track||(it->second.empty()&&defaultTrack==track))it=next.tracks.erase(it);
    else ++it;
  }
}
inline MotionIssueKind GuessMotionIssueKind(const std::string &name) {
  if(name.find(u8"親")!=name.npos||name.find(u8"多段")!=name.npos)return MotionIssueKind::Parent;
  if(name.find(u8"握")!=name.npos)return MotionIssueKind::Grip;
  if(name.find(u8"拡散")!=name.npos)return MotionIssueKind::Spread;
  if(name.find(u8"視線")!=name.npos)return MotionIssueKind::Gaze;
  if(name.find(u8"IK")!=name.npos&&(name.find(u8"腕")!=name.npos||name.find(u8"手")!=name.npos))return MotionIssueKind::ArmIk;
  if(name.find(u8"ダミー")!=name.npos||name.find(u8"手先")!=name.npos)return MotionIssueKind::Accessory;
  if(name.find(u8"親")!=name.npos||name.find(u8"センター")!=name.npos||name.find(u8"グルーブ")!=name.npos||name.find(u8"多段")!=name.npos)return MotionIssueKind::Parent;
  return MotionIssueKind::Unknown;
}
inline const char *MotionIssueAdvice(MotionIssueKind kind) {
  switch(kind) {
  case MotionIssueKind::Grip: return u8"可选择握拳控制模板；核对轴向、手指范围和强度。";
  case MotionIssueKind::Spread: return u8"可选择张指控制模板；不同追加插件的规则可能不同。";
  case MotionIssueKind::ArmIk: return u8"可选择手臂 IK 模板；确认目标父级和手腕朝向。多段 IK 先补父级。";
  case MotionIssueKind::Gaze: return u8"可选择视线目标模板。视线父级应通过多段父级接入，不能当作目标直接套用。";
  case MotionIssueKind::Parent: return u8"选择实际受控骨骼并插入父级；名称本身不能确定层级和旋转中心。";
  case MotionIssueKind::Accessory: return u8"可能控制道具或特殊手部结构。确认用途后绑定；不要直接叠加到手腕。";
  case MotionIssueKind::Binding: return u8"检查轨道绑定是否来自其他动作；移除旧绑定或选择当前动作轨道。";
  case MotionIssueKind::Disconnected: return u8"映射已记录，但目标骨未参与角色输出。请改绑到实际使用的源骨骼，或在高级设置中把该源骨分配到角色部位。";
  default: return u8"确认这条轨道控制的部位，手动绑定或追加控制结构；不需要时可明确忽略。";
  }
}
inline MotionIssues AnalyzeMotionIssues(const RigDefinition &rig,const MotionClip &clip,const RigAdaptation &adaptation) {
  MotionIssues result;
  RigEvaluator evaluator;
  auto roles=AdaptedRoles(adaptation);const auto defaults=DefaultSourceRoles(rig);std::vector<int> outputs;
  for(int i=0;i<55;++i)outputs.push_back(roles.count(i)?rig.find(roles.at(i)):defaults[i]);
  evaluator.bind(rig,clip,adaptation.tracks,outputs);
  const auto bindings=ResolvedTrackBindings(rig,clip,adaptation.tracks);
  std::set<std::string> ignored;
  for(const auto &name:adaptation.ignoredTracks)
    if(clip.bones.count(name)||clip.ik.count(name))ignored.insert(name);
  for(const auto &name:evaluator.unmapped) {
    const auto &keys=clip.bones.at(name);
    if(adaptation.ignoredTracks.count(name)||(adaptation.tracks.count(name)&&adaptation.tracks.at(name).empty())) {ignored.insert(name);continue;}
    const auto replacement=adaptation.tracks.find(name);
    if(replacement!=adaptation.tracks.end()&&replacement->second!=name&&clip.bones.count(replacement->second)) {
      result.replaced.push_back(name+u8" → 改用 "+replacement->second);continue;
    }
    auto active=std::find_if(keys.begin(),keys.end(),[](const BoneKey &k) {
      const Quat q=NormQ(k.rotation);
      return Len(k.position)>1e-5f || q.x*q.x+q.y*q.y+q.z*q.z>1e-10f;
    });
    const auto ik=clip.ik.find(name);
    if(active==keys.end()&&(ik==clip.ik.end()||ik->second.empty())) {result.neutral.push_back(name);continue;}
    BoneMotionIssue issue;issue.track=name;issue.keys=keys.size();issue.firstFrame=active==keys.end()?ik->second.front().frame:active->frame;
    issue.kind=GuessMotionIssueKind(name);
    issue.reason=rig.find(name)<0?u8"有实际动作，但当前源骨架没有对应控制骨":u8"骨骼存在，但未连接到角色输出";
    std::string targets;
    for(const auto &binding:adaptation.tracks)if(binding.second==name) {
      if(!targets.empty())targets+=u8"、";targets+=binding.first;
    }
    if(!targets.empty()) {
      issue.kind=MotionIssueKind::Disconnected;
      issue.reason=u8"已绑定到 "+targets+u8"，但目标骨未接到角色输出";
    }
    issue.advice=MotionIssueAdvice(issue.kind);result.unresolved.push_back(std::move(issue));
  }
  for(const auto &name:evaluator.unmappedIk) {
    if(std::any_of(result.unresolved.begin(),result.unresolved.end(),[&](const BoneMotionIssue &v){return v.track==name;}))continue;
    if(adaptation.tracks.count(name)&&adaptation.tracks.at(name).empty()){ignored.insert(name);continue;}
    const auto kind=GuessMotionIssueKind(name);
    const auto &keys=clip.ik.at(name);
    if(keys.empty())continue;
    result.unresolved.push_back({name,u8"动作包含 IK 开关，但当前没有对应的有效求解链",MotionIssueAdvice(kind),kind,keys.front().frame,keys.size()});
  }
  for(const auto &kv:adaptation.tracks)
    if(!kv.second.empty()&&!clip.bones.count(kv.second)&&!clip.ik.count(kv.second)&&!clip.bones.empty())
      result.unresolved.push_back({kv.second,u8"绑定的动作轨道不存在（目标："+kv.first+u8"）",MotionIssueAdvice(MotionIssueKind::Binding),MotionIssueKind::Binding});
  auto manualBone=[&](const std::string &bone) {
    const auto binding=bindings.find(bone);
    const auto &track=binding==bindings.end()?bone:binding->second;
    if(track.empty()||ignored.count(track)||adaptation.ignoredTracks.count(track)||
       (!clip.bones.count(track)&&!clip.ik.count(track)))return;
    auto &targets=result.manuallyMapped[track];
    if(!targets.empty())targets+=u8"、";
    targets+=bone;
  };
  std::set<std::string> manualBones;
  for(const auto &kv:adaptation.tracks)if(!kv.second.empty())manualBones.insert(kv.first);
  for(const auto &kv:adaptation.roles)if(!kv.second.empty())manualBones.insert(kv.second);
  for(const auto &bone:adaptation.extra)manualBones.insert(bone.name);
  for(const auto &control:adaptation.controls) {
    manualBones.insert(control.name);
    if(control.paired&&control.kind!=MotionControlKind::Gaze)manualBones.insert(MirrorBoneName(control.name));
  }
  for(const auto &bone:manualBones)manualBone(bone);
  result.destinations=evaluator.mappedTracks;
  for(const auto &binding:evaluator.mappedTracks)
    if(!result.manuallyMapped.count(binding.first))result.automaticallyMapped.emplace(binding);
  result.ignored.assign(ignored.begin(),ignored.end());
  return result;
}

// Connect an existing anatomical source to an output, rather than just recording
// another track-name alias. Known D/EX equivalents are explicit; accessory,
// viewpoint and intermediate skin-only bones must not become arbitrary joints.
inline int ExistingOutputRole(const std::string &bone) {
  const auto key=MotionBindingKey(bone);
  for(int role=0;role<55;++role)if(key==MotionBindingKey(RoleNames()[role]))return role;
  for(int side=0;side<2;++side) {
    const std::string prefix=side?u8"右":u8"左";
    const char *parts[]={u8"足D",u8"ひざD",u8"足首D",u8"足先EX"};
    const int roles[]={1+side,3+side,5+side,19+side};
    for(int i=0;i<4;++i)if(key==MotionBindingKey(prefix+parts[i]))return roles[i];
  }
  return -1;
}
inline MotionIssueResolution CurrentMotionIssueResolution(const BoneMotionIssue &item,const MotionIssues &issues,
    const RigDefinition &rig,const RigAdaptation &adaptation,bool unresolved) {
  MotionIssueResolution result;result.control.side=item.track.find(u8"右")!=std::string::npos?1:0;
  result.action=item.kind==MotionIssueKind::Grip?2:item.kind==MotionIssueKind::Spread?3:
    item.kind==MotionIssueKind::ArmIk?4:item.kind==MotionIssueKind::Gaze?5:item.kind==MotionIssueKind::Parent?1:0;
  auto targets=issues.destinations.find(item.track);
  if(targets!=issues.destinations.end())result.previousDestinations=targets->second;
  if(result.previousDestinations.size()==1)result.destination=result.previousDestinations.front();
  for(const auto &control:adaptation.controls)if(control.name==item.track) {
    result.action=2+int(control.kind);result.control=control;return result;
  }
  for(const auto &bone:adaptation.extra)if(bone.name==item.track&&!bone.before.empty()) {
    result.action=1;result.destination=bone.before;return result;
  }
  // Read the applied mapping instead of keeping a stale default in the UI.
  for(const auto &role:adaptation.roles)if(!role.second.empty()) {
    const auto b=adaptation.tracks.find(role.second);
    if((b==adaptation.tracks.end()?role.second:b->second)==item.track||
        std::find(result.previousDestinations.begin(),result.previousDestinations.end(),role.second)!=result.previousDestinations.end()) {
      result.action=6;result.destination=role.second;result.role=role.first;return result;
    }
  }
  for(const auto &binding:adaptation.tracks)if(binding.second==item.track) {
    result.action=0;result.destination=binding.first;return result;
  }
  if(unresolved&&rig.find(item.track)>=0&&ExistingOutputRole(item.track)>=0) {
    result.action=6;result.destination=item.track;result.role=ExistingOutputRole(item.track);
  }
  return result;
}
struct OutputConnectionResult {
  RigAdaptation adaptation;
  std::map<std::string,std::string> connected;
};
inline OutputConnectionResult ConnectExistingMotionOutputs(const RigDefinition &rig,const MotionClip &clip,
    const RigAdaptation &current,const std::vector<const MotionClip *> &otherClips={}) {
  OutputConnectionResult result;result.adaptation=current;
  const auto initial=AnalyzeMotionIssues(rig,clip,current);
  const auto bindings=ResolvedTrackBindings(rig,clip,current.tracks);
  for(const auto &issue:initial.unresolved) {
    if(current.ignoredTracks.count(issue.track))continue;
    std::vector<int> candidates;
    for(int i=0;i<int(rig.bones.size());++i) {
      const auto &name=rig.bones[i].name;auto bind=bindings.find(name);
      if((bind==bindings.end()?name:bind->second)==issue.track&&ExistingOutputRole(name)>=0)candidates.push_back(i);
    }
    // Ambiguous source definitions and explicit user assignments are untouched.
    if(candidates.size()!=1)continue;
    const auto &bone=rig.bones[candidates[0]].name;const int role=ExistingOutputRole(bone);
    if(result.adaptation.roles.count(role)||current.tracks.count(bone))continue;
    auto next=result.adaptation;next.roles[role]=bone;
    const auto before=AnalyzeMotionIssues(rig,clip,result.adaptation),after=AnalyzeMotionIssues(rig,clip,next);
    if(before.destinations.count(issue.track)||!after.destinations.count(issue.track))continue;
    auto loses=[](const MotionIssues &a,const MotionIssues &b) {
      // Neutral keys do not occupy a body output, but authored motion and IK do.
      for(const auto &v:a.destinations)if(!b.destinations.count(v.first)&&
          std::find(b.neutral.begin(),b.neutral.end(),v.first)==b.neutral.end())return true;
      return false;
    };
    bool conflict=loses(before,after);
    for(const auto *other:otherClips)if(other&&other!=&clip&&!conflict)
      conflict=loses(AnalyzeMotionIssues(rig,*other,result.adaptation),AnalyzeMotionIssues(rig,*other,next));
    if(conflict)continue;
    result.adaptation=std::move(next);result.connected[issue.track]=bone;
  }
  return result;
}
} // namespace mmd
