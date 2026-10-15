#pragma once
static bool ClothBoneTeamRegistered(void *process,int team);
static bool ClothBoneSharedMember(const ClothBoneRuntime &a,const ClothBoneRuntime &b){
  return a.profile&&b.profile&&a.local.requested&&b.local.requested&&a.local.recipe&&b.local.recipe&&
      a.local.recipe==a.profile->generatedLocal&&b.local.recipe==b.profile->generatedLocal&&
      a.local.recipe->SharedSkin()&&b.local.recipe->SharedSkin()&&a.owner==b.owner&&a.command==b.command&&
      !strcmp(a.profile->prefabSha,b.profile->prefabSha)&&
      !strcmp(a.local.recipe->sharedSkinGroup,b.local.recipe->sharedSkinGroup)&&
      !strcmp(a.local.recipe->sharedPublisher,b.local.recipe->sharedPublisher);
}
static int ClothBoneSharedPublisher(const ClothBoneRuntime &s){
  if(!s.local.requested||!s.local.recipe||!s.local.recipe->SharedSkin())return -1;int found=-1;
  for(int n=0;n<s_clothBoneCount;++n){const auto &p=s_clothBoneSlots[n];if(ClothBoneSharedMember(s,p)&&!p.local.recipe->sharedSkinSupplier&&
      !strcmp(p.profile->component,s.local.recipe->sharedPublisher)){if(found>=0)return -1;found=n;}}
  return found;
}
static bool ClothBoneSharedGroupValid(const ClothBoneRuntime &s,bool ready=false){
  const int publisher=ClothBoneSharedPublisher(s);if(publisher<0)return false;const auto &p=s_clothBoneSlots[publisher];int members=0,suppliers=0;
  for(int n=0;n<s_clothBoneCount;++n){const auto &part=s_clothBoneSlots[n];if(!ClothBoneSharedMember(s,part))continue;++members;
    if(part.local.recipe->sharedSkinSupplier){++suppliers;if(!part.local.recipe->SharedSupplier()||!(p.dependencies&(1u<<n)))return false;}
    else if(n!=publisher)return false;
    if(!part.pending||part.stopRequested||part.failed||part.tx.cancelled||part.local.cleanup)return false;
    if(ready&&n!=publisher&&(!part.lease||part.tx.phase!=eiem_cloth_rebuild::Phase::Active||!part.teamModeConfirmed||!part.local.created||!part.local.solverConfirmed))return false;
  }
  return members==3&&suppliers==2&&p.profile->dependencyCount==2;
}
static bool ClothBoneSharedReleaseReady(const ClothBoneRuntime &s){
  if(!s.local.recipe||!s.local.recipe->SharedSupplier())return true;
  for(int n=0;n<s_clothBoneCount;++n){const auto &p=s_clothBoneSlots[n];if(ClothBoneSharedMember(s,p)&&!p.local.recipe->sharedSkinSupplier)
    for(const auto &layer:p.local.layers)if(layer.published||layer.publishAttempted)return false;}
  return true;
}
static bool ClothBoneSharedTeamsRead(const ClothBoneRuntime &s){
  if(!ClothBoneSharedGroupValid(s,true))return false;
  for(int n=0;n<s_clothBoneCount;++n){const auto &p=s_clothBoneSlots[n];if(!ClothBoneSharedMember(s,p)||!p.local.recipe->sharedSkinSupplier)continue;
    auto bbc=ClothBindingTarget(p.bbc);void *process=nullptr,*data=nullptr;
    if(!bbc||!CollisionField(bbc,"process","BeyondDynamicBone.ClothProcess",process)||process!=CollisionGc(p.process[1])||
        !ClothInvoke(SurfaceMethod(il2cpp_object_get_class(bbc),"get_SerializeData","BeyondDynamicBone.ClothSerializeData"),bbc,nullptr,data)||
        data!=CollisionGc(p.candidateData)||!ClothBoneTeamRegistered(process,p.team[1]))return false;
  }return true;
}
static bool ClothBoneSharedTeamsValid(const ClothBoneRuntime &s){
  if(!ClothBindingReading())return ClothBoneSharedTeamsRead(s);
  return s_clothBindingReads.teams.Get({uintptr_t(&s)},[&]{return ClothBoneSharedTeamsRead(s);});
}
static ClothRef ClothBoneSharedBindingRead(const ClothBoneRuntime &s,int encoded){
  if(!ClothOnMainThread()||!s.local.recipe||!s.local.recipe->SharedSkin()||s.local.recipe->sharedSkinSupplier||!ClothBoneSharedGroupValid(s,true))return {};
  const int index=-encoded-ClothSharedSkinBase;if(index<0||index>=s.local.recipe->sharedBindingCount)return {};
  const auto &binding=s.local.recipe->sharedBindings[index];int found=-1;
  for(int n=0;n<s_clothBoneCount;++n)if(ClothBoneSharedMember(s,s_clothBoneSlots[n])&&!strcmp(s_clothBoneSlots[n].profile->component,binding.component)){if(found>=0)return {};found=n;}
  if(found<0)return {};const auto &p=s_clothBoneSlots[found];if(!p.local.recipe->SharedSupplier()||binding.bone<0||size_t(binding.bone)>=p.bones.size()||
      !ClothBoneCandidate(p).bones[binding.bone].attribute)return {};const auto &b=p.bones[binding.bone];auto t=ClothBindingTarget(b.bone);
  if(!t||!ClothOwns(s.owner)||!ClothBindingUnderOwner(t)||ClothBindingParent(t)!=ClothBindingTarget(b.parent))return {};return b.bone;
}
static ClothRef ClothBoneSharedBindingRef(const ClothBoneRuntime &s,int encoded){
  if(!ClothBindingReading())return ClothBoneSharedBindingRead(s,encoded);
  return s_clothBindingReads.bindings.Get({uintptr_t(&s),uintptr_t(encoded)},[&]{return ClothBoneSharedBindingRead(s,encoded);});
}
