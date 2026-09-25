#pragma once
#include "core/game_hooks.h"
#include "math/mmd_contact.h"

// Only called from the existing, verified game SMC callback, under g_poseMutex.
// UI and independent playback ticks consume copies; they never query Physics.
struct MmdGroundProbe {
  void *raycast=nullptr, *hitClass=nullptr, *getCollider=nullptr;
  int stride=0, pointOffset=-1, normalOffset=-1;
  uint32_t bufferHandle=0;
  bool attempted=false, ready=false;
  const char *status=u8"等待游戏物理采样";
  void release() {
    if(bufferHandle && il2cpp_gchandle_free) il2cpp_gchandle_free(bufferHandle);
    bufferHandle=0;
  }
  bool type(void *t,const char *space,const char *name) {
    void *c=t?il2cpp_class_from_type(t):nullptr;
    if(!c) return false;
    const char *n=il2cpp_class_get_name(c), *ns=il2cpp_class_get_namespace(c);
    return n && ns && !strcmp(n,name) && !strcmp(ns,space);
  }
  int field(void *c,const char *name) {
    void *iter=nullptr,*f=nullptr;
    while((f=il2cpp_class_get_fields(c,&iter))) {
      const char *n=il2cpp_field_get_name(f);
      if(n && !strcmp(n,name) && type(il2cpp_field_get_type(f),"UnityEngine","Vector3")) {
        // IL2CPP reports boxed field offsets; the value array has no headers.
        int offset=int(il2cpp_field_get_offset(f))-int(2*sizeof(void*));
        return offset>=0 && offset+int(sizeof(Vec3))<=stride?offset:-1;
      }
    }
    return -1;
  }
  bool resolve() {
    if(attempted) return ready;
    attempted=true;
    status=u8"地面接口不兼容：可切换固定平面";
    if(!il2cpp_method_get_flags || !il2cpp_class_value_size || !il2cpp_array_new ||
       !il2cpp_gchandle_new || !il2cpp_gchandle_get_target || !il2cpp_gchandle_free ||
       !il2cpp_class_from_type || !il2cpp_field_get_type || !g_component_get_transform ||
       !g_transform_get_parent) return false;
    size_t count=0;
    void **assemblies=il2cpp_domain_get_assemblies(il2cpp_domain_get(),&count);
    void *physics=FindClass("UnityEngine","Physics",assemblies,count);
    hitClass=FindClass("UnityEngine","RaycastHit",assemblies,count);
    raycast=FindMethod(physics,"RaycastNonAlloc",6);
    getCollider=FindMethod(hitClass,"get_collider",0);
    if(!raycast || !getCollider) {
      Log("[MMD] ground query unavailable: raycast=%p hitClass=%p collider=%p",raycast,hitClass,getCollider);
      return false;
    }
    for(int i=-1;i<6;++i) {
      void *t=i<0?il2cpp_method_get_return_type(raycast):il2cpp_method_get_param(raycast,i);
      void *c=t?il2cpp_class_from_type(t):nullptr;
      const char *ns=c?il2cpp_class_get_namespace(c):nullptr,*name=c?il2cpp_class_get_name(c):nullptr;
      Log("[MMD] ground query slot=%d %s.%s",i,ns?ns:"?",name?name:"?");
    }
    uint32_t flags=0, alignment=0;
    if(!(il2cpp_method_get_flags(raycast,&flags)&0x10) ||
       (il2cpp_method_get_flags(getCollider,&flags)&0x10) ||
       !type(il2cpp_method_get_return_type(raycast),"System","Int32") ||
       !type(il2cpp_method_get_return_type(getCollider),"UnityEngine","Collider")) return false;
    const char *spaces[]={"UnityEngine","UnityEngine","UnityEngine","System","System","UnityEngine"};
    const char *names[]={"Vector3","Vector3","RaycastHit[]","Single","Int32","QueryTriggerInteraction"};
    for(int i=0;i<6;++i)
      if(!type(il2cpp_method_get_param(raycast,i),spaces[i],names[i])) return false;
    if(il2cpp_class_value_size(il2cpp_class_from_type(il2cpp_method_get_param(raycast,0)),&alignment)!=sizeof(Vec3) ||
       il2cpp_class_value_size(il2cpp_class_from_type(il2cpp_method_get_param(raycast,5)),&alignment)!=sizeof(int)) return false;
    if(il2cpp_type_get_type(il2cpp_class_get_type(hitClass))!=0x11) return false;
    stride=il2cpp_class_value_size(hitClass,&alignment);
    if(stride<24 || stride>128) return false;
    pointOffset=field(hitClass,"m_Point"); normalOffset=field(hitClass,"m_Normal");
    if(pointOffset<0 || normalOffset<0) return false;
    ready=true;
    Log("[MMD] ground query metadata verified: hit=%d point=%d normal=%d",stride,pointOffset,normalOffset);
    return true;
  }
  bool belongsToActor(void *collider,void *root) {
    void *t=Invoke(g_component_get_transform,collider);
    for(int i=0;t && i<96;++i) {
      if(t==root) return true;
      t=Invoke(g_transform_get_parent,t);
    }
    return t!=nullptr; // reject malformed / excessively deep hierarchies
  }
  bool sample(Vec3 foot,void *actorRoot,mmd::GroundPlane &out) {
    out={};
    if(!mmd::ContactFinite(foot) || !UnityObjAlive(actorRoot) || !resolve()) return false;
    if(!bufferHandle) {
      void *array=il2cpp_array_new(hitClass,64);
      if(!array || !(bufferHandle=il2cpp_gchandle_new(array,true))) return false;
    }
    void *array=il2cpp_gchandle_get_target(bufferHandle);
    if(!array) return false;
    Vec3 origin=foot+Vec3{0,.65f,0},direction{0,-1,0};
    float distance=2.f;
    int mask=-5,trigger=1; // Unity DefaultRaycastLayers, QueryTriggerInteraction.Ignore
    void *args[]={&origin,&direction,array,&distance,&mask,&trigger},*exc=nullptr;
    void *boxed=il2cpp_runtime_invoke(raycast,nullptr,args,&exc);
    if(exc || !boxed) {status=u8"地面查询失败，当前帧不修正";return false;}
    int hits=*reinterpret_cast<int*>(static_cast<char*>(boxed)+2*sizeof(void*));
    if(hits<0 || hits>=64) {status=u8"地面命中过多，当前帧不修正";return false;}
    float best=1e9f;
    for(int i=0;i<hits;++i) {
      char *hit=static_cast<char*>(array)+IL2CPP_ARRAY_DATA+i*stride;
      Vec3 p,n; memcpy(&p,hit+pointOffset,sizeof(p)); memcpy(&n,hit+normalOffset,sizeof(n));
      if(!mmd::ContactFinite(p) || !mmd::ContactFinite(n) || Norm(n).y<.65f) continue;
      exc=nullptr;
      void *collider=il2cpp_runtime_invoke(getCollider,hit,nullptr,&exc);
      if(exc || !UnityObjAlive(collider) || belongsToActor(collider,actorRoot)) continue;
      float d=origin.y-p.y;
      if(d<0 || d>distance || d>=best) continue;
      best=d;out={p,Norm(n),true};
    }
    status=out.valid?u8"已探测游戏地面":u8"未找到可用地面：保留原动作";
    return out.valid;
  }
};
