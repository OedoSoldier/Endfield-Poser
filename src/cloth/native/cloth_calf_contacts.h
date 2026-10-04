#pragma once
// Native garments without a rebuilt candidate use the same bounded capsule
// preparation, with independent list ownership and retirement at the job fence.
struct ClothCalfContact {ClothAttachmentContact lease;ClothCalfShapes shapes;};
struct ClothCalfContacts {
  eiem_cloth::Owner owner{};std::vector<ClothCalfContact> leases;
  unsigned command=0;int scanned=0;double nextScan=0;bool releasing=false;
};
static ClothActorBank<ClothCalfContacts> s_clothCalfActors;
#define s_clothCalves (s_clothCalfActors.Get())
static bool ClothCalfPending(){return !s_clothCalves.leases.empty();}
static bool ClothCalfNeeded(){return ClothCalfPending()||(s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothEnhancementSetting().load());}
static void ClothCalfContactsRelease(){s_clothCalves.releasing=true;for(auto &s:s_clothCalves.leases)s.lease.retiring=true;}
static bool ClothCalfRetiring(){for(auto &s:s_clothCalves.leases)if(s.lease.retiring)return true;return false;}
static void ClothCalfDiscover(int index) {
  auto &i=s_cloth.instances[index];const auto part=cloth_turn::Classify(i.name);
  if(part!=cloth_turn::Part::Cloth&&!cloth_turn::AttachmentPart(part))return;
  auto bbc=ClothTarget(i.ref);void *data=nullptr,*list=nullptr,*process=nullptr,*constraint=nullptr;
  std::vector<void*> cols;
  if(!bbc||ClothAttachmentLeased(bbc)||!ClothAttachmentList(bbc,data,list,cols)||cols.size()>48)return;
  char mode[48]{};if(!CollisionList(data,constraint,list)||!CollisionEnum(constraint,"mode",CollisionModeType,mode,sizeof(mode))||
      (strcmp(mode,"Point")&&strcmp(mode,"Edge")))return;
  // Exact list identity matters: do not alter shared or externally owned lists.
  for(int n=0;n<s_cloth.count;++n)if(n!=index){auto other=ClothTarget(s_cloth.instances[n].ref);void *d=nullptr,*l=nullptr;std::vector<void*> c;
    if(other&&ClothAttachmentList(other,d,l,c)&&l==list)return;}
  int team=0;if(!ClothField(bbc,"process","BeyondDynamicBone.ClothProcess",process)||!process||
      !ClothValue(SurfaceMethod(il2cpp_object_get_class(process),"get_TeamId","System.Int32"),process,team)||team<=0||!ClothBoneTeamRegistered(process,team))return;
  auto &state=s_clothCalves;state.leases.emplace_back();auto &entry=state.leases.back();auto &l=entry.lease;
  l.receiver=ClothProtect(bbc);l.process=il2cpp_gchandle_new(process,false);l.data=il2cpp_gchandle_new(data,false);l.list=il2cpp_gchandle_new(list,false);
  l.team=team;l.started=ClothTurnNow();bool ok=l.receiver.handle&&l.process&&l.data&&l.list;
  if(ok)ok=ClothCalfPrepare(entry.shapes,cols,i.name);
  if(entry.shapes.empty()){ClothAttachmentFree(l);state.leases.pop_back();return;}
  if(ok)for(auto &s:entry.shapes){auto c=ClothTarget(s.collider);l.shapes.emplace_back();auto &own=l.shapes.back();own.collider=ClothProtect(c);own.added=true;
    void *unused=nullptr,*args[]{c};bool listed=false;
    if(!own.collider.handle||!ClothInvoke(ClothMethod(il2cpp_object_get_class(list),"Add","System.Void","BeyondDynamicBone.ColliderComponent"),list,args,unused)||
        !CollisionContains(list,c,"BeyondDynamicBone.ColliderComponent",listed)||!listed){ok=false;break;}}
  if(ok&&!l.shapes.empty())ok=l.notified=ClothAttachmentNotify(l);
  l.retiring=!ok||entry.shapes.empty();
}
static void ClothCalfBoundary() {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return;
  auto &state=s_clothCalves;const auto command=s_clothBoneRequest.load();
  const bool enabled=s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothOwns(s_cloth.owner)&&ClothEnhancementSetting().load();
  if(!enabled||!(state.owner==s_cloth.owner)||state.command!=command||!ClothAttachmentStable())ClothCalfContactsRelease();
  for(auto it=state.leases.begin();it!=state.leases.end();){auto &entry=*it;auto &l=entry.lease;
    if(!l.retiring&&(!ClothAttachmentIdentity(l)||ClothAttachmentLeased(ClothTarget(l.receiver))))l.retiring=true;
    if(!l.retiring&&(!l.confirmed||++l.frames%30==0)) {
      const bool registered=ClothCalfRegistration(entry.shapes,CollisionGc(l.process),l.team,true);
      if(!registered&&(l.confirmed||ClothTurnNow()-l.started>2))l.retiring=true;
      if(registered&&!l.confirmed){l.confirmed=true;Log("[CLOTH-CALF] stage=registered receiver=%d colliders=%zu team=%d",l.receiver.id.instance,entry.shapes.size(),l.team);}
    }
    if(l.retiring){const int team=l.team;
      if(ClothAttachmentRetire(l)&&ClothCalfRelease(entry.shapes,team)){it=state.leases.erase(it);continue;}}
    ++it;
  }
  if(state.releasing){if(!state.leases.empty())return;state={};state.owner=s_cloth.owner;state.command=command;state.nextScan=ClothTurnNow()+.02*s_clothActorIndex;}
  if(!enabled||!ClothAttachmentStable()||state.scanned>=s_cloth.count||ClothTurnNow()<state.nextScan)return;
  state.nextScan=ClothTurnNow()+.10;ClothCalfDiscover(state.scanned++);
}
