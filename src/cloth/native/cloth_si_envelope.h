#pragma once
// Narrow, actor-scoped reduction of the two outer upper-sleeve shells around
// the waist. Keep all inner body, lower-sleeve and leg contacts. Use the native
// enabled property at its job fence; no list edits or collider destruction.
struct ClothSiEnvelope {
  eiem_cloth::Owner owner{};
  struct Entry {ClothRef collider;bool original=true;};
  std::vector<Entry> entries;
  bool checked=false,releasing=false;
  unsigned command=0;
};
static ClothActorBank<ClothSiEnvelope> s_clothSiEnvelopes;
#define s_clothSiEnvelope (s_clothSiEnvelopes.Get())
static bool ClothSiEnvelopePending(){return !s_clothSiEnvelope.entries.empty();}
static bool ClothSiEnvelopeNeeded(){return ClothSiEnvelopePending()||
    (s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothEnhancementEnabled());}
static void ClothSiEnvelopeRelease(){s_clothSiEnvelope.releasing=true;}
static bool ClothSiOuterShape(const CollisionGeometry &g) {
  const bool left=!strcmp(g.bone,"L_sleeveD_02_jnt")&&!strcmp(g.parentName,"L_sleeveD_01_jnt");
  const bool right=!strcmp(g.bone,"R_sleeveD_02_jnt")&&!strcmp(g.parentName,"R_sleeveD_01_jnt");
  return (left||right)&&g.valid&&g.uniform&&g.flagsKnown&&g.active&&
      !strcmp(g.type,"BeyondBoneCapsuleCollider")&&!strcmp(g.direction,"X")&&
      !g.reverse&&!g.centered&&!g.separated&&
      fabsf(g.size.z-.415f)<1e-5f&&fabsf(g.size.x-(left?.116f:.087f))<1e-5f;
}
static bool ClothSiEnvelopeSet(void *collider,bool enabled) {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return false;
  bool actual=false;void *unused=nullptr,*args[]{&enabled};
  return collider&&ClothInvoke(s_clothUnity.setEnabled,collider,args,unused)&&
      ClothValue(s_clothUnity.getEnabled,collider,actual)&&actual==enabled&&
      ClothInvoke(ClothMethod(il2cpp_object_get_class(collider),"UpdateParameters","System.Void"),collider,nullptr,unused);
}
static bool ClothSiEnvelopeRestore(ClothSiEnvelope &state) {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return false;
  for(auto it=state.entries.begin();it!=state.entries.end();) {
    void *collider=nullptr;const auto life=ClothInspect(it->collider,collider);
    if(life==ClothLife::Unreadable)return false;
    // The retained old instance is the only permissible restoration target.
    if(life==ClothLife::Alive&&!ClothSiEnvelopeSet(collider,it->original))return false;
    ClothFree(it->collider);it=state.entries.erase(it);
  }
  return true;
}
static void ClothSiEnvelopeBoundary() {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1)return;
  auto &state=s_clothSiEnvelope;const auto command=s_clothBoneRequest.load();
  const bool enabled=s_cloth.active&&!s_cloth.releasing&&s_clothRequested&&ClothOwns(s_cloth.owner)&&ClothEnhancementEnabled();
  if(!enabled||!(state.owner==s_cloth.owner)||state.command!=command)state.releasing=true;
  if(state.releasing) {
    if(!ClothSiEnvelopeRestore(state))return;
    state={};state.owner=s_cloth.owner;state.command=command;
  }
  if(!enabled||state.checked||!ClothAttachmentStable())return;
  state.checked=true;
  if(ClothAutoModelPath()!="assets/beyond/dynamicassets/gameplay/actors/postmodels/characters/chr_0036_jsspsi_postmodel.prefab")return;
  std::vector<void*> selected;
  for(int n=0;n<s_cloth.count;++n) {
    void *data=nullptr,*list=nullptr;std::vector<void*> colliders;
    if(!ClothAttachmentList(ClothTarget(s_cloth.instances[n].ref),data,list,colliders))continue;
    for(auto c:colliders)if(std::find(selected.begin(),selected.end(),c)==selected.end()&&
        ClothAnchorUnderOwner(CollisionTransform(c))&&ClothSiOuterShape(CollisionReadGeometry(c)))selected.push_back(c);
  }
  if(selected.size()!=2)return;
  // Acquire both undo records before changing either original component.
  for(auto c:selected){bool original=false;const bool read=ClothValue(s_clothUnity.getEnabled,c,original);
    state.entries.push_back({read?ClothProtect(c):ClothRef{},original});
    if(!state.entries.back().collider.handle) {
      for(auto &e:state.entries)ClothFree(e.collider);state.entries.clear();return;
    }
  }
  for(auto &e:state.entries)if(e.original&&!ClothSiEnvelopeSet(ClothTarget(e.collider),false)){state.releasing=true;return;}
  Log("[CLOTH-SI-ENVELOPE] outerUpperSleeveShellsDisabled=2 innerBodyUnchanged=1 lowerSleeveUnchanged=1 actor=%u restoreOnStop=1 visualVerified=0",s_clothActorIndex);
}
