#pragma once
// Native garments without a rebuilt candidate use the same bounded capsule
// preparation, with independent list ownership and retirement at the job fence.
struct ClothSleeveContactMode {
  uint32_t constraint=0;
  int original=0,edge=0;
  bool changed=false;
};
struct ClothCalfContact {ClothAttachmentContact lease;ClothCalfShapes shapes;ClothSleeveContactMode sleeve;};
struct ClothCalfContacts {
  eiem_cloth::Owner owner{};std::vector<ClothCalfContact> leases;
  unsigned command=0;int scanned=0;double nextScan=0;bool releasing=false;
};
static ClothActorBank<ClothCalfContacts> s_clothCalfActors;
#define s_clothCalves (s_clothCalfActors.Get())
static bool ClothCalfPending(){return !s_clothCalves.leases.empty();}
static bool ClothCalfNeeded(){return ClothCalfPending()||(s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothEnhancementEnabled());}
static void ClothCalfContactsRelease(){s_clothCalves.releasing=true;for(auto &s:s_clothCalves.leases)s.lease.retiring=true;}
static bool ClothCalfRetiring(){for(auto &s:s_clothCalves.leases)if(s.lease.retiring)return true;return false;}
static bool ClothSiModelPath(std::string path) {
  std::replace(path.begin(),path.end(),'\\','/');
  return eiem_cloth_asset::Lower(path)=="assets/beyond/dynamicassets/gameplay/actors/postmodels/characters/chr_0036_jsspsi_postmodel.prefab";
}
// Seven original points leave long intervals between contacts. Native Edge
// tests the links too, without replacing the solver, reference pose or skin.
// Save only the exact original constraint; never restore into a new Process.
static bool ClothSleeveContactModePrepare(ClothCalfContact &entry) {
  auto &s=entry.sleeve;auto &l=entry.lease;void *constraint=nullptr,*list=nullptr,*field=nullptr;
  int point=0,current=0;
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||
      !ClothAttachmentIdentity(l)||!CollisionList(CollisionGc(l.data),constraint,list)||
      !CollisionModeMetadata(il2cpp_object_get_class(constraint),field,point,s.edge)||
      !ClothField(constraint,"mode",CollisionModeType,current)||(current!=point&&current!=s.edge))return false;
  s.constraint=il2cpp_gchandle_new(constraint,false);if(!s.constraint)return false;
  s.original=current;if(current==s.edge)return true;
  s.changed=true; // Include a partial managed write in the undo record.
  return SurfaceScalar(constraint,"mode",CollisionModeType,s.edge)&&
      ClothField(constraint,"mode",CollisionModeType,current)&&current==s.edge;
}
static bool ClothSleeveContactModeIdentity(const ClothCalfContact &entry) {
  const auto &s=entry.sleeve;void *constraint=nullptr,*list=nullptr;int current=0;
  return !s.constraint||(ClothAttachmentIdentity(entry.lease)&&
      CollisionList(CollisionGc(entry.lease.data),constraint,list)&&constraint==CollisionGc(s.constraint)&&
      ClothField(constraint,"mode",CollisionModeType,current)&&current==s.edge);
}
static bool ClothSleeveContactModeConfirmed(const ClothCalfContact &entry) {
  int actual=0;
  return !entry.sleeve.constraint||(ClothSleeveContactModeIdentity(entry)&&
      CollisionProcessMode(CollisionGc(entry.lease.process),actual)&&actual==entry.sleeve.edge);
}
static bool ClothSleeveContactModeRestore(ClothCalfContact &entry) {
  auto &s=entry.sleeve;if(!s.constraint)return true;
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return false;
  void *constraint=CollisionGc(s.constraint);int current=0;
  if(!constraint||!ClothField(constraint,"mode",CollisionModeType,current))return false;
  // A later external edit owns its value. The retained old settings can still
  // be restored after replacement, but the replacement is never notified.
  if(s.changed&&current==s.edge) {
    if(!SurfaceScalar(constraint,"mode",CollisionModeType,s.original)||
        !ClothField(constraint,"mode",CollisionModeType,current)||current!=s.original)return false;
    entry.lease.removed=false; // Retirement notifies the original live receiver.
  }
  il2cpp_gchandle_free(s.constraint);s={};return true;
}
// Si keeps its authored seven-bone sleeve solver and skin. Add only private
// copies of the native body capsules; neither skirt shells nor sleeve shells
// are donors. This does not rebuild the sleeve Process or change its dynamics.
static bool ClothSiNativeSleeveContacts(ClothCalfShapes &shapes,std::vector<void*> &colliders,const char *&reason) {
  reason="model-path";
  if(!ClothSiModelPath(ClothAutoModelPath()))return false;
  reason="body-reference";auto animator=ClothTarget(s_cloth.animator);if(!animator)return false;
  constexpr int roles[]{1,2,3,6,9,13,14,17,18};
  std::array<void*,std::size(roles)> references{};ClothAutoCaptureHolds holds;
  auto cls=SurfaceClass("BeyondDynamicBone","BeyondBoneCapsuleCollider");
  reason="capsule-type";auto type=cls?il2cpp_type_get_object(il2cpp_class_get_type(cls)):nullptr;if(!holds.Add(type))return false;
  // Some authored body capsules are not in any original BBC collider list.
  // Find them among direct children of the verified Humanoid body bones.
  for(size_t k=0;k<references.size();++k){auto bone=CollisionBody(animator,roles[k]);int count=0;
    reason=CollisionBodyNames[roles[k]];
    if(!holds.Add(bone)||!ClothValue(s_clothUnity.childCount,bone,count)||count<0||count>128)return false;
    for(int n=0;n<count;++n){void *child=nullptr,*go=nullptr,*array=nullptr,*args[]{&n};char name[128]{};
      if(!ClothInvoke(s_clothUnity.child,bone,args,child)||!holds.Add(child))return false;
      CollisionName(child,name,sizeof(name));if(strncmp(name,"Magica Capsule Collider (",strlen("Magica Capsule Collider (")))continue;
      void *typed[]{type};size_t length=0;
      if(!ClothInvoke(s_clothUnity.getGO,child,nullptr,go)||!holds.Add(go)||
          !ClothInvoke(s_clothUnity.components,go,typed,array)||!holds.Add(array)||
          !ClothArray(array,"UnityEngine.Component[]",length)||length!=1)return false;
      auto c=reinterpret_cast<void**>((char*)array+32)[0];if(!holds.Add(c))return false;
      const auto g=CollisionReadGeometry(c);
      if(strcmp(g.type,"BeyondBoneCapsuleCollider")||!g.valid||!g.active||!g.enabled||!g.uniform||!g.flagsKnown||
          CollisionTransform(c)!=child||CollisionParent(child)!=bone||references[k])return false;
      references[k]=c;
    }
    if(!references[k])return false;
  }
  // Check the complete body envelope before creating any copies.
  for(auto c:references)if(!c)return false;
  reason="capsule-copy";for(auto c:references){
    if(std::find(colliders.begin(),colliders.end(),c)!=colliders.end())continue;
    if(!ClothCalfCreate(shapes,nullptr,nullptr,c))return false;
    colliders.push_back(ClothTarget(shapes.back().collider));
  }
  reason="none";return true;
}
static void ClothCalfDiscover(int index) {
  auto &i=s_cloth.instances[index];const auto part=cloth_turn::Classify(i.name);
  const bool nativeSleeve=eiem_cloth_asset::JsspsiSleeveName(i.name);
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
  const char *reason="calf-prepare";
  if(ok)ok=ClothCalfPrepare(entry.shapes,cols,i.name);
  if(ok&&nativeSleeve)ok=ClothSiNativeSleeveContacts(entry.shapes,cols,reason);
  if(entry.shapes.empty()){ClothAttachmentFree(l);state.leases.pop_back();return;}
  if(ok){reason="collider-list";for(auto &s:entry.shapes){auto c=ClothTarget(s.collider);l.shapes.emplace_back();auto &own=l.shapes.back();own.collider=ClothProtect(c);own.added=true;
    void *unused=nullptr,*args[]{c};bool listed=false;
    if(!own.collider.handle||!ClothInvoke(ClothMethod(il2cpp_object_get_class(list),"Add","System.Void","BeyondDynamicBone.ColliderComponent"),list,args,unused)||
        !CollisionContains(list,c,"BeyondDynamicBone.ColliderComponent",listed)||!listed){ok=false;break;}}}
  if(ok&&nativeSleeve){reason="edge-mode";ok=ClothSleeveContactModePrepare(entry);}
  if(ok&&!l.shapes.empty()){reason="parameter-notify";ok=l.notified=ClothAttachmentNotify(l);}
  l.retiring=!ok||entry.shapes.empty();
  if(nativeSleeve)Log("[CLOTH-SI-NATIVE-SLEEVE] component=%s bodyAndCalfCopies=%zu notified=%d reason=%s originalProcess=1 originalSkin=1 originalDynamics=1 contact=edges skirtContacts=0 registration=pending",i.name,entry.shapes.size(),int(ok),ok?"none":reason);
}
static void ClothCalfBoundary() {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return;
  auto &state=s_clothCalves;const auto command=s_clothBoneRequest.load();
  const bool enabled=s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothOwns(s_cloth.owner)&&ClothEnhancementEnabled();
  if(!enabled||!(state.owner==s_cloth.owner)||state.command!=command||!ClothAttachmentStable())ClothCalfContactsRelease();
  for(auto it=state.leases.begin();it!=state.leases.end();){auto &entry=*it;auto &l=entry.lease;
    if(!l.retiring&&(!ClothAttachmentIdentity(l)||!ClothSleeveContactModeIdentity(entry)||ClothAttachmentLeased(ClothTarget(l.receiver))))l.retiring=true;
    if(!l.retiring&&(!l.confirmed||++l.frames%30==0)) {
      const bool registered=ClothCalfRegistration(entry.shapes,CollisionGc(l.process),l.team,true)&&ClothSleeveContactModeConfirmed(entry);
      if(!registered&&(l.confirmed||ClothTurnNow()-l.started>2))l.retiring=true;
      if(registered&&!l.confirmed){l.confirmed=true;Log("[CLOTH-CALF] stage=registered receiver=%d colliders=%zu team=%d nativeSleeveEdgeConfirmed=%d",l.receiver.id.instance,entry.shapes.size(),l.team,int(entry.sleeve.constraint!=0));}
    }
    if(l.retiring){const int team=l.team;
      if(ClothSleeveContactModeRestore(entry)&&ClothAttachmentRetire(l)&&ClothCalfRelease(entry.shapes,team)){it=state.leases.erase(it);continue;}}
    ++it;
  }
  if(state.releasing){if(!state.leases.empty())return;state={};state.owner=s_cloth.owner;state.command=command;state.nextScan=ClothTurnNow()+.02*s_clothActorIndex;}
  if(!enabled||!ClothAttachmentStable()||state.scanned>=s_cloth.count||ClothTurnNow()<state.nextScan)return;
  state.nextScan=ClothTurnNow()+.10;ClothCalfDiscover(state.scanned++);
}
