#pragma once
#include "game/mmd_runtime_api.h"
#include <memory>
#include <unordered_set>

// Render-only model visibility. Never disable a GameObject, Animator or BBC.
// No mesh/material access; hidden actors retain the ordinary playback updates.
namespace mmd_visibility {
struct Api {
  void *query=nullptr,*rendererType=nullptr,*instance=nullptr;
  void *get=nullptr,*set=nullptr,*childOf=nullptr;
} static api;
inline bool Ready() {
  if(api.query&&api.rendererType&&api.instance&&api.get&&api.set&&api.childOf)return true;
  if(!il2cpp_class_get_type||!il2cpp_type_get_object||!g_gameObjectClass)return false;
  auto renderer=mmd_api::Class("UnityEngine","Renderer"),object=mmd_api::Class("UnityEngine","Object");
  auto transform=mmd_api::Class("UnityEngine","Transform");
  if(!renderer||!object||!transform)return false;
  Api next;
  next.query=mmd_api::Method(g_gameObjectClass,"GetComponentsInChildren","UnityEngine.Component[]",{"System.Type","System.Boolean"});
  auto type=il2cpp_class_get_type(renderer);next.rendererType=type?il2cpp_type_get_object(type):nullptr;
  next.instance=mmd_api::Method(object,"GetInstanceID","System.Int32");
  next.get=mmd_api::Method(renderer,"get_forceRenderingOff","System.Boolean");
  next.set=mmd_api::Method(renderer,"set_forceRenderingOff","System.Void",{"System.Boolean"});
  next.childOf=mmd_api::Method(transform,"IsChildOf","System.Boolean",{"UnityEngine.Transform"});
  if(!next.query||!next.rendererType||!next.instance||!next.get||!next.set||!next.childOf)return false;
  api=next;return true;
}
inline void Free(uint32_t handle) noexcept {
  __try {if(handle&&!RuntimeClosing()&&il2cpp_gchandle_free)il2cpp_gchandle_free(handle);}
  __except(EXCEPTION_EXECUTE_HANDLER){}
}
enum class Life { Dead, Alive, Unavailable };
struct Ref {
  uint32_t handle=0;int instance=0;
  Ref()=default;
  Ref(const Ref&)=delete;Ref &operator=(const Ref&)=delete;
  Ref(Ref &&r) noexcept:handle(r.handle),instance(r.instance){r.handle=0;}
  Ref &operator=(Ref &&r) noexcept {if(this!=&r){Free(handle);handle=r.handle;instance=r.instance;r.handle=0;}return *this;}
  ~Ref(){Free(handle);}
  bool capture(void *object) {
    if(!UnityObjAlive(object)||!il2cpp_gchandle_new||!il2cpp_gchandle_get_target||!il2cpp_gchandle_free||
        !mmd_api::Value(api.instance,object,instance))return false;
    handle=il2cpp_gchandle_new(object,false);return handle!=0;
  }
  Life inspect(void *&object) const {
    object=nullptr;
    __try {
      if(!handle)return Life::Dead;
      if(!il2cpp_gchandle_get_target)return Life::Unavailable;
      object=il2cpp_gchandle_get_target(handle);if(!object)return Life::Dead;
      uintptr_t native=0;int id=0;
      if(!mmd_api::Copy((char*)object+16,&native,sizeof(native)))return Life::Unavailable;
      if(!native)return Life::Dead;
      if(!mmd_api::Value(api.instance,object,id))return Life::Unavailable;
      return id==instance?Life::Alive:Life::Dead;
    } __except(EXCEPTION_EXECUTE_HANDLER){return Life::Unavailable;}
  }
  void *target() const {
    void *object=nullptr;return inspect(object)==Life::Alive?object:nullptr;
  }
};
struct Renderer {Ref object;bool original=false;};
struct Lease {Ref owner,root;std::vector<Renderer> renderers;double nextScan=0;};
struct State {std::shared_ptr<Lease> lease;bool failed=false;double retryAt=0;};
inline int Scope(void *renderer,void *root) {
  void *transform=nullptr,*result=nullptr,*args[]{root};bool child=false;
  if(!root||!mmd_api::Call(g_component_get_transform,renderer,nullptr,transform)||!UnityObjAlive(transform))return -1;
  if(transform==root)return 1;
  if(!mmd_api::Call(api.childOf,transform,args,result)||!result||!mmd_api::Copy((char*)result+16,&child,sizeof(child)))return -1;
  return child?1:0;
}
inline bool Write(void *renderer,bool value) {
  bool current=false;
  if(!mmd_api::Value(api.get,renderer,current))return false;
  if(current==value)return true;
  void *result=nullptr,*args[]{&value};
  return mmd_api::Call(api.set,renderer,args,result)&&mmd_api::Value(api.get,renderer,current)&&current==value;
}
inline bool Restore(State &s) {
  if(!s.lease)return true;
  void *root=nullptr;
  auto life=RuntimeClosing()?Life::Dead:s.lease->root.inspect(root);
  if(life==Life::Unavailable)return false;
  if(life==Life::Dead)root=nullptr;
  if(root)for(auto it=s.lease->renderers.begin();it!=s.lease->renderers.end();) {
    void *renderer=nullptr;life=it->object.inspect(renderer);
    int scope=life==Life::Alive?Scope(renderer,root):0;
    if(life==Life::Unavailable||scope<0||(scope>0&&!Write(renderer,it->original))){++it;continue;}
    it=s.lease->renderers.erase(it);
  }
  if(root&&!s.lease->renderers.empty())return false;
  s.lease.reset();return true;
}
inline bool Hide(Lease &lease,void *root) {
  for(auto &r:lease.renderers) {
    void *renderer=nullptr;auto life=r.object.inspect(renderer);
    if(life==Life::Dead)continue;
    if(life==Life::Unavailable)return false;
    bool current=false;
    if(!mmd_api::Value(api.get,renderer,current))return false;
    if(!current){int scope=Scope(renderer,root);if(scope<0||(scope>0&&!Write(renderer,true)))return false;}
  }
  return true;
}
inline bool Tick(State &s,void *owner,void *root,bool visible,double now) {
  if(visible){s.retryAt=0;return Restore(s);}
  if(RuntimeClosing())return false;
  if(now<s.retryAt)return false;
  auto fail=[&](){Restore(s);s.retryAt=now+1;return false;};
  if(!Ready()||!UnityObjAlive(owner)||!UnityObjAlive(root))return fail();
  if(!s.lease) {
    auto lease=std::make_shared<Lease>();
    if(!lease->owner.capture(owner)||!lease->root.capture(root))return fail();
    s.lease=std::move(lease);
  }
  auto &lease=*s.lease;
  if(lease.owner.target()!=owner||lease.root.target()!=root)return fail();
  if(now<lease.nextScan)return Hide(lease,root)?true:fail();
  lease.nextScan=now+.2;
  // Include inactive LODs and particle renderers so switching LODs or enabling
  // an existing effect cannot reveal an otherwise hidden character.
  void *go=nullptr,*array=nullptr;bool inactive=true;void *args[]{api.rendererType,&inactive};size_t count=0;
  if(!mmd_api::Call(g_component_get_gameObject,root,nullptr,go)||!UnityObjAlive(go)||
      !mmd_api::Call(api.query,go,args,array)||!array||
      !mmd_api::Copy((char*)array+IL2CPP_ARRAY_LEN,&count,sizeof(count))||count==0||count>1024)return fail();
  for(auto it=lease.renderers.begin();it!=lease.renderers.end();) {
    void *renderer=nullptr;auto life=it->object.inspect(renderer);
    int scope=life==Life::Alive?Scope(renderer,root):0;
    if(life==Life::Unavailable||scope<0)return fail();
    if(life==Life::Dead||!scope)it=lease.renderers.erase(it);else ++it;
  }
  std::unordered_set<void*> captured;
  for(const auto &r:lease.renderers)captured.insert(r.object.target());
  for(size_t n=0;n<count;++n) {
    void *renderer=nullptr;
    if(!mmd_api::Copy((char*)array+IL2CPP_ARRAY_DATA+n*sizeof(void*),&renderer,sizeof(renderer)))return fail();
    if(!UnityObjAlive(renderer)||captured.count(renderer))continue;
    if(Scope(renderer,root)!=1)return fail();
    Renderer entry;
    if(!mmd_api::Value(api.get,renderer,entry.original)||!entry.object.capture(renderer))return fail();
    lease.renderers.push_back(std::move(entry));captured.insert(renderer);
  }
  if(lease.renderers.empty())return fail();
  return Hide(lease,root)?true:fail();
}
} // namespace mmd_visibility
