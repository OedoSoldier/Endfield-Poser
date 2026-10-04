#pragma once
#include "math/cloth_attachment.h"

static bool s_clothAttachmentContacts=true;
// Private collider copies isolate native ownership accounting. No original
// geometry, skin, solver arrays, or existing cloth-cloth pairs are modified.
struct ClothAttachmentContact {
  ClothRef receiver{},donor{},animator{};
  uint32_t process=0,data=0,list=0,donorProcess=0;
  struct Shape {ClothRef source{},go{},collider{},sourceTransform{},targetTransform{};bool added=false,destroyed=false;};
  std::vector<Shape> shapes;
  int team=0;bool retiring=false,notified=false,confirmed=false,removed=false;
  double started=0;unsigned frames=0;
};
struct ClothAttachmentContacts {
  eiem_cloth::Owner owner{};
  std::vector<ClothAttachmentContact> leases;
  int scanned=0;double nextScan=0;bool releasing=false;
  unsigned pairs=0,shapes=0,skipped=0;
};
static ClothActorBank<ClothAttachmentContacts> s_clothAttachmentActors;
#define s_clothAttachments (s_clothAttachmentActors.Get())
static bool ClothAttachmentPending(){return !s_clothAttachments.leases.empty();}
static bool ClothAttachmentNeeded(){return ClothAttachmentPending()||
    (s_clothAttachmentContacts&&s_cloth.active&&!s_cloth.releasing&&s_clothRequested);}
static void ClothAttachmentRelease(){s_clothAttachments.releasing=true;for(auto &l:s_clothAttachments.leases)l.retiring=true;}
static bool ClothAttachmentUnder(void *child,void *root) {
  if(!child||!root)return false;void *box=nullptr,*args[]{root};
  return child==root||(ClothInvoke(ClothMethod(g_transformClass,"IsChildOf","System.Boolean","UnityEngine.Transform"),child,args,box)&&box&&ClothUnboxBool(box));
}
static bool ClothAttachmentRoots(void *data,std::vector<void*> &roots) {
  void *list=nullptr;if(!data||!ClothField(data,"rootBones","System.Collections.Generic.List<UnityEngine.Transform>",list))return false;
  const int count=CollisionCount(list);if(count<1||count>32)return false;
  for(int n=0;n<count;++n){auto t=CollisionItem(list,n,"UnityEngine.Transform");if(!t||!ClothAnchorUnderOwner(t))return false;roots.push_back(t);}return true;
}
static bool ClothAttachmentIn(void *t,const std::vector<void*> &roots) {
  for(auto root:roots)if(ClothAttachmentUnder(t,root))return true;return false;
}
static bool ClothAttachmentList(void *bbc,void *&data,void *&list,std::vector<void*> &colliders) {
  void *constraint=nullptr;
  if(!bbc||!ClothInvoke(SurfaceMethod(il2cpp_object_get_class(bbc),"get_SerializeData","BeyondDynamicBone.ClothSerializeData"),bbc,nullptr,data)||
      !CollisionList(data,constraint,list))return false;
  const int count=CollisionCount(list);if(count<0||count>64)return false;
  for(int n=0;n<count;++n){auto c=CollisionItem(list,n,"BeyondDynamicBone.ColliderComponent");if(!c)return false;colliders.push_back(c);}return true;
}
static bool ClothAttachmentIdentity(const ClothAttachmentContact &l) {
  auto bbc=ClothTarget(l.receiver);void *p=nullptr,*data=nullptr,*list=nullptr,*constraint=nullptr;
  return bbc&&ClothField(bbc,"process","BeyondDynamicBone.ClothProcess",p)&&p==CollisionGc(l.process)&&
      ClothInvoke(SurfaceMethod(il2cpp_object_get_class(bbc),"get_SerializeData","BeyondDynamicBone.ClothSerializeData"),bbc,nullptr,data)&&
      data==CollisionGc(l.data)&&CollisionList(data,constraint,list)&&list==CollisionGc(l.list);
}
static bool ClothAttachmentNotify(const ClothAttachmentContact &l) {
  auto bbc=ClothTarget(l.receiver);void *unused=nullptr;
  return bbc&&ClothInvoke(SurfaceMethod(il2cpp_object_get_class(bbc),"SetParameterChange","System.Void"),bbc,nullptr,unused);
}
static void ClothAttachmentFree(ClothAttachmentContact &l) {
  for(auto &s:l.shapes){ClothFree(s.source);ClothFree(s.go);ClothFree(s.collider);ClothFree(s.sourceTransform);ClothFree(s.targetTransform);}
  ClothFree(l.receiver);ClothFree(l.donor);ClothFree(l.animator);
  for(auto h:{l.process,l.data,l.list,l.donorProcess})if(h)il2cpp_gchandle_free(h);l={};
}
static bool ClothAttachmentSync(const ClothAttachmentContact::Shape &s) {
  // Strong managed handles plus native liveness prevent stale Transform access.
  // Avoid reflection, hierarchy walks and instance-ID calls in this hot path.
  auto t=CollisionGc(s.sourceTransform.handle),target=CollisionGc(s.targetTransform.handle);void *unused=nullptr;
  Vector3 p{},current{};Quaternion q{},rotation{};
  if(!t||!target||!UnityObjAlive(t)||!UnityObjAlive(target)||!ClothValue(g_transform_get_position,t,p)||
      !ClothValue(g_transform_get_rotation,t,q)||!ClothValue(g_transform_get_position,target,current)||
      !ClothValue(g_transform_get_rotation,target,rotation))return false;
  // Private objects are parented to the actor, never inserted into a simulated
  // chain. Copy completed donor transforms before the native collider pass.
  if(ClothSameLocal(current,rotation,p,q))return true;
  void *pos[]{&p},*rot[]{&q};
  return ClothInvoke(g_transform_set_position,target,pos,unused)&&ClothInvoke(g_transform_set_rotation,target,rot,unused);
}
static bool ClothAttachmentCopy(ClothAttachmentContact &l,void *source) {
  const auto geometry=CollisionReadGeometry(source);
  if(!geometry.valid||!geometry.enabled||!geometry.active||!geometry.uniform||
      (strcmp(geometry.type,"BeyondBoneSphereCollider")&&strcmp(geometry.type,"BeyondBoneCapsuleCollider")))return false;
  auto cls=il2cpp_object_get_class(source),parent=CollisionTransform(ClothTarget(l.animator));
  auto ctor=ClothMethod(g_gameObjectClass,".ctor","System.Void","System.String");
  auto add=ClothMethod(g_gameObjectClass,"AddComponent","UnityEngine.Component","System.Type");
  auto update=ClothMethod(cls,"UpdateParameters","System.Void");
  if(!parent||!ctor||!add||!update)return false;
  void *go=il2cpp_object_new(g_gameObjectClass),*label=il2cpp_string_new("Poser_AttachmentContact"),*unused=nullptr,*args[]{label};
  if(!go||!label)return false;const uint32_t hold=il2cpp_gchandle_new(go,false),text=il2cpp_gchandle_new(label,false);
  if(!hold||!text){if(hold)il2cpp_gchandle_free(hold);if(text)il2cpp_gchandle_free(text);return false;}
  const bool made=ClothInvoke(ctor,go,args,unused);l.shapes.emplace_back();auto &s=l.shapes.back();
  s.go=ClothProtect(go);s.source=ClothProtect(source);il2cpp_gchandle_free(hold);il2cpp_gchandle_free(text);
  Vector3 parentScale{};if(!CollisionScale(parent,parentScale)||!eiem_collision::UniformPositive(CollisionV(parentScale)))return false;
  const float scale=geometry.scale.x/parentScale.x;
  if(!made||!s.go.handle||!s.source.handle||!SurfaceTRS(CollisionTransform(go),parent,Vector3{},Quaternion{0,0,0,1},Vector3{scale,scale,scale}))return false;
  s.sourceTransform=ClothProtect(CollisionTransform(source));s.targetTransform=ClothProtect(CollisionTransform(go));
  if(!s.sourceTransform.handle||!s.targetTransform.handle)return false;
  void *type=il2cpp_type_get_object(il2cpp_class_get_type(cls)),*component=nullptr,*addArgs[]{type};
  if(!type||!ClothInvoke(add,go,addArgs,component)||!component)return false;s.collider=ClothProtect(component);
  if(!s.collider.handle||!SurfaceScalar(component,"center","UnityEngine.Vector3",geometry.center)||
      !SurfaceScalar(component,"size","UnityEngine.Vector3",geometry.size))return false;
  if(!strcmp(geometry.type,"BeyondBoneCapsuleCollider")) {
    int direction=0;if(!ClothField(source,"direction","BeyondDynamicBone.BeyondBoneCapsuleCollider.Direction",direction)||
        !SurfaceScalar(component,"direction","BeyondDynamicBone.BeyondBoneCapsuleCollider.Direction",direction)||
        !SurfaceScalar(component,"reverseDirection","System.Boolean",geometry.reverse)||
        !SurfaceScalar(component,"radiusSeparation","System.Boolean",geometry.separated)||
        !SurfaceScalar(component,"alignedOnCenter","System.Boolean",geometry.centered))return false;
  }
  if(!ClothAttachmentSync(s)||!ClothInvoke(update,component,nullptr,unused))return false;
  auto check=CollisionReadGeometry(component);
  if(!check.valid||!SurfaceVisibleSame(check.size,geometry.size)||!SurfaceVisibleSame(check.center,geometry.center))return false;
  auto list=CollisionGc(l.list);bool has=false;void *addCollider[]{component};
  if(!list||!CollisionContains(list,component,"BeyondDynamicBone.ColliderComponent",has)||has)return false;
  s.added=true; // Record ownership before calling managed code (including partial failure).
  return ClothInvoke(ClothMethod(il2cpp_object_get_class(list),"Add","System.Void","BeyondDynamicBone.ColliderComponent"),list,addCollider,unused)&&
      CollisionContains(list,component,"BeyondDynamicBone.ColliderComponent",has)&&has;
}
static bool ClothAttachmentUnregistered(const ClothAttachmentContact &l,void *c) {
  bool member=false,listed=true;int count=-1;
  auto process=CollisionGc(l.process);
  if(!CollisionTeams(c,l.team,member,count))return false;
  if(!member)return count==0;
  if(!process||!CollisionProcessContains(process,c,listed)||listed)return false;
  // Native UpdateColliders removes the solver slot but does not call the
  // component's Exit(team). Complete that bookkeeping only after readback of
  // the retained Process proves removal. Never touch a reused Team or a shared
  // collider, and never free a shape while native jobs can still reference it.
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||count!=1||l.team<=0)return false;
  void *manager=nullptr,*registered=nullptr,*unused=nullptr;int team=l.team;void *args[]{&team};
  if(!ClothContactManager("get_Team","BeyondDynamicBone.TeamManager",manager)||
      !ClothInvoke(ClothMethod(il2cpp_object_get_class(manager),"GetClothProcess","BeyondDynamicBone.ClothProcess","System.Int32"),manager,args,registered)||
      (registered&&registered!=process)||
      !ClothInvoke(ClothMethod(il2cpp_object_get_class(c),"Exit","System.Void","System.Int32"),c,args,unused)||
      !CollisionTeams(c,team,member,count)||member||count!=0)return false;
  Log("[CLOTH-CONTACT-RELEASE] receiver=%d team=%d nativeSlotAbsent=1 componentExitConfirmed=1",l.receiver.id.instance,team);
  return true;
}
static bool ClothAttachmentRetire(ClothAttachmentContact &l) {
  void *list=CollisionGc(l.list),*unused=nullptr;
  for(auto &s:l.shapes)if(s.added) {
    auto c=CollisionGc(s.collider.handle);bool has=false;
    if(c&&(!list||!CollisionContains(list,c,"BeyondDynamicBone.ColliderComponent",has)))return false;
    if(c&&has){void *args[]{c};if(!ClothInvoke(ClothMethod(il2cpp_object_get_class(list),"Remove","System.Boolean","BeyondDynamicBone.ColliderComponent"),list,args,unused)||
        !CollisionContains(list,c,"BeyondDynamicBone.ColliderComponent",has)||has)return false;}
    s.added=false;
  }
  if(!l.removed) {
    // The retained old list may survive a character/Process replacement. Never
    // assign it into a new actor; notify only its original live component.
    if(ClothAttachmentIdentity(l)&&!ClothAttachmentNotify(l))return false;
    l.removed=true;
  }
  for(auto &s:l.shapes) {
    void *c=nullptr;auto life=ClothInspect(s.collider,c);
    if(life==ClothLife::Unreadable)return false;
    if(life==ClothLife::Alive&&!ClothAttachmentUnregistered(l,c))return false;
    if(s.go.handle&&!s.destroyed){auto go=ClothTarget(s.go);void *args[]{go};
      if(go&&!ClothInvoke(ClothMethod(SurfaceClass("UnityEngine","Object"),"Destroy","System.Void","UnityEngine.Object",true),nullptr,args,unused))return false;
      s.destroyed=true;}
  }
  ClothAttachmentFree(l);return true;
}
static bool ClothAttachmentLeased(void *bbc) {
  for(const auto &s:s_clothBoneSlots)if(s.lease) {
    if(ClothTarget(s.bbc)==bbc)return true;
    // These producers have an existing, audited layer transaction. Its exact
    // collider list belongs to that transaction and must not be extended here.
    for(const auto &p:s.nativeProducers)if(ClothTarget(p.bbc)==bbc)return true;
  }
  return false;
}
static bool ClothAttachmentStable() {
  for(const auto &s:s_clothBoneSlots)if(s.pending&&(s.stopRequested||s.tx.cancelled||
      (s.tx.phase!=eiem_cloth_rebuild::Phase::Active&&s.tx.phase!=eiem_cloth_rebuild::Phase::Complete)))return false;
  return s_cloth.discovery.complete;
}
static bool ClothAttachmentSurface(void *data) {
  void *settings=nullptr;int sync=0,self=0,none=0,full=0;
  return !ClothField(data,"selfCollisionConstraint","BeyondDynamicBone.SelfCollisionConstraint.SerializeData",settings)||!settings||
      !ClothBoneContactMode(settings,"syncMode",sync,none,full)||sync!=none||
      !ClothBoneContactMode(settings,"selfMode",self,none,full)||self!=none;
}
static void ClothAttachmentDiscover(int index) {
  auto &state=s_clothAttachments;auto &instance=s_cloth.instances[index];
  if(!cloth_turn::AttachmentPart(cloth_turn::Classify(instance.name)))return;
  auto receiver=ClothTarget(instance.ref);void *data=nullptr,*list=nullptr;std::vector<void*> original,roots;
  if(!receiver||ClothAttachmentLeased(receiver)||!ClothAttachmentList(receiver,data,list,original)||
      original.size()>48||!ClothAttachmentRoots(data,roots)||ClothAttachmentSurface(data)){++state.skipped;return;}
  void *donor=nullptr;std::vector<void*> donorRoots,pool;
  // Evidence is an authored collider on the inner cloth, or an outer root
  // parented to that cloth. Ambiguous pairs and reciprocal contact are skipped.
  for(int n=0;n<s_cloth.count;++n) {
    const auto &other=s_cloth.instances[n];if(cloth_turn::Classify(other.name)!=cloth_turn::Part::Cloth)continue;
    auto bbc=ClothTarget(other.ref);void *otherData=nullptr,*otherList=nullptr;std::vector<void*> cols,bases;
    if(!bbc||bbc==receiver||!ClothAttachmentList(bbc,otherData,otherList,cols)||!ClothAttachmentRoots(otherData,bases))continue;
    bool related=false,reverse=false;
    for(auto c:original)related|=ClothAttachmentIn(CollisionTransform(c),bases);
    for(auto root:roots)related|=ClothAttachmentIn(CollisionParent(root),bases);
    for(auto c:cols)reverse|=ClothAttachmentIn(CollisionTransform(c),roots);
    if(!related||reverse)continue;
    if(donor){++state.skipped;return;}donor=bbc;donorRoots=std::move(bases);
  }
  if(!donor){++state.skipped;return;}
  for(int n=0;n<s_cloth.count;++n) {
    auto part=cloth_turn::Classify(s_cloth.instances[n].name);
    if(part!=cloth_turn::Part::Cloth&&!cloth_turn::AttachmentPart(part))continue;
    auto bbc=ClothTarget(s_cloth.instances[n].ref);void *d=nullptr,*l=nullptr;std::vector<void*> cols;
    if(!bbc||!ClothAttachmentList(bbc,d,l,cols))continue;
    if(bbc!=receiver&&l==list){++state.skipped;return;} // Shared lists cannot be extended independently.
    for(auto c:cols)if(std::find(original.begin(),original.end(),c)==original.end()&&std::find(pool.begin(),pool.end(),c)==pool.end()&&
        ClothAttachmentIn(CollisionTransform(c),donorRoots)&&!ClothAttachmentIn(CollisionTransform(c),roots)) {
      const auto g=CollisionReadGeometry(c);if(!g.valid||!g.active||!g.enabled)continue;
      bool nearby=false,inside=false;
      for(int p=0;p<instance.poseProbeCount;++p){Vector3 point{};auto t=ClothTarget(instance.poseProbes[p].ref);
        if(t&&ClothValue(g_transform_get_position,t,point)){double dist=eiem_collision::SegmentDistance(CollisionV(point),g.world.a,g.world.b)-(std::max)(g.world.ra,g.world.rb);
          inside|=dist<-.002;nearby|=dist>=.002&&dist<.20;}}
      if(nearby&&!inside)pool.push_back(c);
    }
  }
  if(!cloth_turn::LayerPair(true,true,true,false,false,unsigned(pool.size()))||pool.size()>4){++state.skipped;return;}
  state.leases.emplace_back();auto &lease=state.leases.back();
  lease.receiver=ClothProtect(receiver);lease.donor=ClothProtect(donor);lease.animator=ClothProtect(ClothTarget(s_cloth.animator));
  void *process=nullptr;
  bool ok=lease.receiver.handle&&lease.donor.handle&&lease.animator.handle&&
      ClothField(receiver,"process","BeyondDynamicBone.ClothProcess",process)&&process&&
      ClothValue(SurfaceMethod(il2cpp_object_get_class(process),"get_TeamId","System.Int32"),process,lease.team)&&lease.team>0;
  void *donorProcess=nullptr;
  if(ok)ok=ClothField(donor,"process","BeyondDynamicBone.ClothProcess",donorProcess)&&donorProcess;
  if(ok){lease.process=il2cpp_gchandle_new(process,false);lease.data=il2cpp_gchandle_new(data,false);lease.list=il2cpp_gchandle_new(list,false);
    lease.donorProcess=il2cpp_gchandle_new(donorProcess,false);
    ok=lease.process&&lease.data&&lease.list&&lease.donorProcess&&ClothBoneTeamRegistered(process,lease.team);}
  lease.started=ClothTurnNow();
  if(ok)for(auto c:pool)if(!ClothAttachmentCopy(lease,c)){ok=false;break;}
  if(ok)ok=lease.notified=ClothAttachmentNotify(lease);
  lease.retiring=!ok;
  Log("[CLOTH-ATTACHMENT-CONTACT] component=%s privateColliders=%zu notified=%d relation=authored-or-hierarchy originalGeometryWrites=0 registration=pending visualVerified=0",
      instance.name,lease.shapes.size(),int(ok));
}
static void ClothAttachmentBoundary() {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return;
  auto &state=s_clothAttachments;
  const bool enabled=s_clothAttachmentContacts&&s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothOwns(s_cloth.owner);
  if(!enabled||!(state.owner==s_cloth.owner)||!ClothAttachmentStable())ClothAttachmentRelease();
  state.pairs=state.shapes=0;
  for(auto it=state.leases.begin();it!=state.leases.end();) {
    auto &l=*it;
    auto donor=ClothTarget(l.donor);void *donorProcess=nullptr;
    if(!l.retiring&&(!ClothAttachmentIdentity(l)||!donor||!ClothField(donor,"process","BeyondDynamicBone.ClothProcess",donorProcess)||
        donorProcess!=CollisionGc(l.donorProcess)||ClothAttachmentLeased(ClothTarget(l.receiver))))l.retiring=true;
    if(!l.retiring) {
      bool ok=true,registered=true;const bool audit=!l.confirmed||++l.frames%30==0;
      for(auto &s:l.shapes) {ok&=ClothAttachmentSync(s);
        if(audit){bool member=false,listed=false;int count=-1;auto c=ClothTarget(s.collider);
          registered&=c&&CollisionTeams(c,l.team,member,count)&&member&&count==1&&CollisionProcessContains(CollisionGc(l.process),c,listed)&&listed;}}
      if(!ok||(!registered&&(l.confirmed||ClothTurnNow()-l.started>2)))l.retiring=true;
      if(registered&&!l.confirmed){l.confirmed=true;Log("[CLOTH-ATTACHMENT-CONTACT] stage=registered receiver=%d colliders=%zu team=%d",l.receiver.id.instance,l.shapes.size(),l.team);}
      if(l.confirmed&&!l.retiring){++state.pairs;state.shapes+=unsigned(l.shapes.size());}
    }
    if(l.retiring&&ClothAttachmentRetire(l)){it=state.leases.erase(it);continue;}++it;
  }
  if(state.releasing) {
    if(!state.leases.empty())return;
    state={};if(!enabled)return;state.owner=s_cloth.owner;state.nextScan=ClothTurnNow()+.02*s_clothActorIndex;
  }
  if(!enabled||!ClothAttachmentStable()||state.leases.size()>=4||state.scanned>=s_cloth.count||ClothTurnNow()<state.nextScan)return;
  state.nextScan=ClothTurnNow()+.10;ClothAttachmentDiscover(state.scanned++);
}
