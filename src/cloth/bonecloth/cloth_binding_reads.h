#pragma once

// A binding audit is synchronous and read-only on the game thread. LODs and
// shared sleeves repeatedly ask for the same retained objects and ancestors.
// Deduplicate those reads ONLY within that audit, never across frames or a
// publication/restoration write. Fixed storage avoids per-frame allocations.
struct ClothBindingKey {
  uintptr_t a=0,b=0,c=0;
  bool operator==(const ClothBindingKey &r) const {return a==r.a&&b==r.b&&c==r.c;}
};
template<class T,size_t Capacity> struct ClothBindingTable {
  static_assert(Capacity&&(Capacity&(Capacity-1))==0,"power-of-two read table");
  struct Entry {ClothBindingKey key{};T value{};uint64_t epoch=0;};
  std::array<Entry,Capacity> entries{};
  uint64_t epoch=0,queries=0,misses=0;
  void Reset() {
    if(++epoch==0){for(auto &e:entries)e.epoch=0;++epoch;}
    queries=misses=0;
  }
  template<class Read> T Get(ClothBindingKey key,Read read) {
    ++queries;
    auto hash=(key.a*11400714819323198485ull)^(key.b*14029467366897019727ull)^key.c;
    hash^=hash>>33;hash*=0xff51afd7ed558ccdull;hash^=hash>>33;
    for(size_t n=0;n<(std::min)(Capacity,size_t(16));++n) {
      auto &e=entries[(size_t(hash)+n)&(Capacity-1)];
      if(e.epoch!=epoch){++misses;e.key=key;e.value=read();e.epoch=epoch;return e.value;}
      if(e.key==key)return e.value;
    }
    // Saturation must never turn an unchecked object into a valid binding.
    ++misses;return read();
  }
};
struct ClothBindingReadState {
  bool active=false;
  unsigned actor=0;
  eiem_cloth::Owner owner{};
  ClothBindingTable<void*,4096> targets;
  ClothBindingTable<void*,1024> parents;
  ClothBindingTable<bool,1024> ancestors;
  ClothBindingTable<ClothRef,512> bindings;
  ClothBindingTable<bool,64> teams;
  bool rootRead=false;void *root=nullptr;
};
static thread_local ClothBindingReadState s_clothBindingReads;
struct ClothBindingReadScope {
  bool entered=false;
  ClothBindingReadScope() {
    auto &s=s_clothBindingReads;
    if(s.active||!ClothOnMainThread())return;
    s.targets.Reset();s.parents.Reset();s.ancestors.Reset();s.bindings.Reset();s.teams.Reset();
    s.rootRead=false;s.root=nullptr;s.actor=s_clothActorIndex;s.owner=s_cloth.owner;
    s.active=entered=true;
  }
  ~ClothBindingReadScope(){
    if(!entered)return;
    auto &r=s_clothBindingReads;auto &s=ClothBoneState();
    if(r.actor==s_clothActorIndex&&r.owner==s_cloth.owner){
      ++s.bindingAudits;
      s.bindingQueries+=r.targets.queries+r.parents.queries+r.ancestors.queries+r.bindings.queries+r.teams.queries;
      s.bindingReads+=r.targets.misses+r.parents.misses+r.ancestors.misses+r.bindings.misses+r.teams.misses;
    }
    r.active=false;
  }
  ClothBindingReadScope(const ClothBindingReadScope&)=delete;
  ClothBindingReadScope &operator=(const ClothBindingReadScope&)=delete;
};
static bool ClothBindingReading() {
  const auto &s=s_clothBindingReads;
  return s.active&&s.actor==s_clothActorIndex&&s.owner==s_cloth.owner&&ClothOwns(s.owner);
}
static void *ClothBindingTarget(const ClothRef &r) {
  if(!ClothBindingReading())return ClothTarget(r);
  // Each LOD retains its own GC handle for many of the very same bones.
  // Resolve the handle every time, but audit that object's lifetime/instance
  // only once in this read scope. A replaced handle cannot hit the old object.
  auto object=r.handle?il2cpp_gchandle_get_target(r.handle):nullptr;
  if(!object)return nullptr;
  return s_clothBindingReads.targets.Get({uintptr_t(object),uintptr_t(r.id.instance)},[&]{return ClothTarget(r);});
}
static void *ClothBindingParent(void *t) {
  if(!ClothBindingReading())return CollisionParent(t);
  return s_clothBindingReads.parents.Get({uintptr_t(t)},[&]{return CollisionParent(t);});
}
static bool ClothBindingUnderOwner(void *t) {
  if(!ClothBindingReading())return ClothAnchorUnderOwner(t);
  auto &s=s_clothBindingReads;
  if(!s.rootRead) {
    s.rootRead=true;auto animator=ClothBindingTarget(s_cloth.animator);
    if(!animator||!ClothInvoke(s_clothUnity.getTransform,animator,nullptr,s.root))s.root=nullptr;
  }
  if(!t||!s.root||t==s.root)return false;
  return s.ancestors.Get({uintptr_t(t)},[&]{
    for(int depth=0;t&&depth<128;++depth){if(t==s.root)return true;t=ClothBindingParent(t);}
    return false;
  });
}
static bool ClothBindingSameBones(void *renderer,const std::vector<ClothRef> &expected) {
  // Read the actual palette each time: no old array or success result survives
  // the audit, and mesh/bone replacement is still detected on the next frame.
  void *array=nullptr;uintptr_t count=0;
  if(!renderer||!ClothInvoke(SurfaceMethod(il2cpp_object_get_class(renderer),"get_bones","UnityEngine.Transform[]"),renderer,nullptr,array)||!array)return false;
  const auto hold=il2cpp_gchandle_new(array,false);if(!hold)return false;
  bool ok=ClothArray(array,"UnityEngine.Transform[]",count)&&count==expected.size();
  for(size_t n=0;ok&&n<count;++n){auto actual=reinterpret_cast<void**>((char*)array+32)[n];ok=actual&&actual==ClothBindingTarget(expected[n]);}
  il2cpp_gchandle_free(hold);return ok;
}
