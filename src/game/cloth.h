#pragma once
// Native cloth adjustment, originally based on EIEM cloth.h (AGPL-3.0).
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "core/bbc_frame.h"
#include <algorithm>
#include <cstring>
#include <vector>

static float g_skirtHipRadiusDelta=.124f, g_skirtRadiusA=1.f, g_skirtLengthScale=1.f;
static float g_skirtHeightScale=.5f;
static bool g_skirtTaperOn=true, g_skirtCollisionEnabled=true, g_skirtDirty=false;
static bool g_skirtEdgeCollision=true;
static bool g_skirtGeometryOverride=false;
static const char *g_skirtStatus=u8"等待角色裙摆组件";
static int g_skirtRetryFrames=0, g_skirtMatched=0, g_skirtExamined=0, g_skirtUnsupported=0;
struct SkirtCloth {
  void *object=nullptr;uint32_t handle=0;int processOffset=-1;
  void *data=nullptr,*settings=nullptr,*notify=nullptr;
  uint32_t dataHandle=0,settingsHandle=0;
  int dataOffset=-1,settingsOffset=-1,modeOffset=-1;
  int originalMode=-1,currentMode=-1,pointMode=-1,edgeMode=-1;
  bool modeChanged=false,modePending=false;
  char name[128]={};
  const char *modeStatus=u8"等待布料碰撞设置";
  float simulateWeight=-1,animationPoseRatio=-1,blendWeight=-1;
  float runtimeWeight=-1,runtimeBlend=-1;
  int running=-1,skipWriting=-1,enabled=-1,culled=-1,effectiveMode=-1;
};
struct SkirtCapsule {
  void *object=nullptr,*setSize=nullptr,*update=nullptr;
  uint32_t handle=0;
  int sizeOffset=-1,separationOffset=-1;
  Vec3 originalSize;
  unsigned char originalSeparation=0;
  bool changed=false,seen=false;
  char attachment[128]={};
  Vec3 center;
  int axis=-1,reverse=-1,aligned=-1;
};
static std::vector<SkirtCloth> g_skirtCloths;
static std::vector<SkirtCapsule> g_skirtCapsules;
// Components owned by the playback collision lease are resized/restored there.
static std::vector<void*> g_skirtTemporaryCapsules;

static bool SkirtClass(void *cls,const char *name,const char *ns) {
  if(!cls || !il2cpp_class_get_name || !il2cpp_class_get_namespace) return false;
  const char *n=il2cpp_class_get_name(cls),*s=il2cpp_class_get_namespace(cls);
  return n && s && !strcmp(n,name) && !strcmp(s,ns);
}
static void *SkirtNamedField(void *cls,const char *name) {
  if(!cls || !il2cpp_class_get_fields || !il2cpp_field_get_name) return nullptr;
  void *iter=nullptr,*f;
  while((f=il2cpp_class_get_fields(cls,&iter))) {
    const char *n=il2cpp_field_get_name(f);
    if(n && !strcmp(n,name)) return f;
  }
  return nullptr;
}
// Resolve enum constants from runtime metadata, not assumed numeric values.
// value__ must be Int32; the nested declaring class is part of the contract.
static bool SkirtEnumField(void *cls,const char *fieldName,const char *owner,
                          const char *constant,int &offset,int &value) {
  if(!il2cpp_class_get_declaring_type || !il2cpp_field_static_get_value ||
     !il2cpp_class_value_size || !il2cpp_field_get_flags || !il2cpp_field_get_type ||
     !il2cpp_type_get_type || !il2cpp_class_from_type || !il2cpp_field_get_offset) return false;
  void *field=SkirtNamedField(cls,fieldName);
  if(!field || (il2cpp_field_get_flags(field)&0x10)) return false;
  void *type=il2cpp_field_get_type(field);
  if(!type || il2cpp_type_get_type(type)!=0x11) return false;
  void *en=il2cpp_class_from_type(type);uint32_t align=0;
  if(!en || !SkirtClass(il2cpp_class_get_declaring_type(en),owner,"BeyondDynamicBone") ||
     il2cpp_class_value_size(en,&align)!=4) return false;
  void *underlying=SkirtNamedField(en,"value__"),*member=SkirtNamedField(en,constant);
  if(!underlying || !member || (il2cpp_field_get_flags(underlying)&0x10) ||
     il2cpp_type_get_type(il2cpp_field_get_type(underlying))!=8 ||
     (il2cpp_field_get_flags(member)&0x50)!=0x50 ||
     il2cpp_class_from_type(il2cpp_field_get_type(member))!=en) return false;
  size_t off=il2cpp_field_get_offset(field);
  if(off<16 || off>4092) return false;
  offset=int(off);il2cpp_field_static_get_value(member,&value);return true;
}
static float SkirtOptionalFloat(void *object,const char *name) {
  void *field=SkirtNamedField(il2cpp_object_get_class(object),name);
  if(!field || (il2cpp_field_get_flags(field)&0x10) ||
     il2cpp_type_get_type(il2cpp_field_get_type(field))!=0xc) return -1;
  size_t off=il2cpp_field_get_offset(field);
  if(off<16 || off>4092) return -1;
  float v=*(float*)((char*)object+off);
  return std::isfinite(v)?v:-1;
}

static bool SkirtVectorType(void *type) {
  if(!type || !il2cpp_class_from_type || !il2cpp_class_value_size ||
     !il2cpp_class_get_name || !il2cpp_class_get_namespace) return false;
  void *klass=il2cpp_class_from_type(type);
  if(!klass) return false;
  const char *name=il2cpp_class_get_name(klass),*ns=il2cpp_class_get_namespace(klass);
  uint32_t alignment=0;
  return name && ns && !strcmp(name,"Vector3") && !strcmp(ns,"UnityEngine") &&
      il2cpp_class_value_size(klass,&alignment)==sizeof(Vec3);
}
static int SkirtField(void *klass,const char *name,bool vector=false,bool boolean=false) {
  if(!il2cpp_class_get_fields || !il2cpp_field_get_name || !il2cpp_field_get_offset ||
     !il2cpp_field_get_flags || !il2cpp_field_get_type || !il2cpp_type_get_type ||
     !il2cpp_class_get_parent) return -1;
  for(int depth=0;klass && depth<16;++depth,klass=il2cpp_class_get_parent(klass)) {
    void *iter=nullptr,*field;
    while((field=il2cpp_class_get_fields(klass,&iter))) {
      const char *n=il2cpp_field_get_name(field);
      if(!n || strcmp(n,name) || (il2cpp_field_get_flags(field)&0x10)) continue;
      void *type=il2cpp_field_get_type(field);
      int kind=type?il2cpp_type_get_type(type):-1;
      if(vector ? !SkirtVectorType(type) : boolean ? kind!=2 : (kind!=0x12 && kind!=0x15)) return -1;
      size_t off=il2cpp_field_get_offset(field);
      return off>=16 && off<4096?int(off):-1;
    }
  }
  return -1;
}
// Sphere.SetSize(float) and Collider.SetSize(Vector3) share name and arity.
// Validate the signature for each collider, including inherited methods.
static void *SkirtMethod(void *klass,const char *name,bool vector) {
  if(!il2cpp_class_get_methods || !il2cpp_method_get_name || !il2cpp_method_get_flags ||
     !il2cpp_method_get_param_count || !il2cpp_method_get_param ||
     !il2cpp_method_get_return_type || !il2cpp_type_get_type || !il2cpp_class_get_parent) return nullptr;
  for(int depth=0;klass && depth<16;++depth,klass=il2cpp_class_get_parent(klass)) {
    void *iter=nullptr,*method;
    while((method=il2cpp_class_get_methods(klass,&iter))) {
      const char *n=il2cpp_method_get_name(method);
      if(!n || strcmp(n,name) || (il2cpp_method_get_flags(method,nullptr)&0x10) ||
         il2cpp_method_get_param_count(method)!=(vector?1u:0u)) continue;
      void *ret=il2cpp_method_get_return_type(method);
      if(!ret || il2cpp_type_get_type(ret)!=1) continue;
      if(!vector || SkirtVectorType(il2cpp_method_get_param(method,0))) return method;
    }
  }
  return nullptr;
}
static void SkirtObjectName(void *object,char *out,int size) {
  out[0]=0;
  if(g_object_get_name) ReadStr(Invoke(g_object_get_name,object),out,size);
  for(char *p=out;*p;++p) if(*p>='A' && *p<='Z') *p+=32;
}
static bool SkirtLegCapsule(void *object) {
  void *klass=il2cpp_object_get_class(object);
  const char *name=klass?il2cpp_class_get_name(klass):nullptr;
  const char *ns=klass?il2cpp_class_get_namespace(klass):nullptr;
  if(!name || !ns || strcmp(name,"BeyondBoneCapsuleCollider") || strcmp(ns,"BeyondDynamicBone")) return false;
  void *transform=g_component_get_transform?Invoke(g_component_get_transform,object):nullptr;
  // A leg-attached capsule often has center.x == 0. Check bone ancestry instead
  // of interpreting its local center as a position in the character skeleton.
  for(int depth=0;transform && depth<32;++depth) {
    for(int i=0;i<s_humanBoneCount;++i) {
      auto &b=s_humanBones[i];
      if(b.transform!=transform) continue;
      if(b.humanBone==LeftUpperLeg || b.humanBone==RightUpperLeg ||
         b.humanBone==LeftLowerLeg || b.humanBone==RightLowerLeg) return true;
      if(b.humanBone==Hips || b.humanBone==Spine) return false;
    }
    char n[128];SkirtObjectName(transform,n,sizeof(n));
    if(strstr(n,"thigh") || strstr(n,"calf") || strstr(n,"upperleg") || strstr(n,"lowerleg")) return true;
    transform=g_transform_get_parent?Invoke(g_transform_get_parent,transform):nullptr;
  }
  return false;
}
static bool SkirtWrite(SkirtCapsule &c,const Vec3 &size,unsigned char separation) {
  if(!UnityObjAlive(c.object)) return false;
  __try {
    *(unsigned char*)((char*)c.object+c.separationOffset)=separation;
    void *exc=nullptr,*args[]={const_cast<Vec3*>(&size)};
    il2cpp_runtime_invoke(c.setSize,c.object,args,&exc);
    if(exc) return false;
    il2cpp_runtime_invoke(c.update,c.object,nullptr,&exc);
    return exc==nullptr;
  } __except(1) {return false;}
}
static bool SkirtNotifySurface(SkirtCloth &c) {
  if(!UnityObjAlive(c.object) || !c.notify || c.dataOffset<0 || c.settingsOffset<0) return false;
  // Never notify a newly installed settings object with an old object's state.
  if(*(void**)((char*)c.object+c.dataOffset)!=c.data ||
     *(void**)((char*)c.data+c.settingsOffset)!=c.settings) return false;
  void *exc=nullptr;il2cpp_runtime_invoke(c.notify,c.object,nullptr,&exc);
  return !exc;
}
static void SkirtRestoreSurface(SkirtCloth &c) {
  __try {
    if(c.modeChanged && c.settings && c.settingsHandle && c.modeOffset>=16) {
      *(int*)((char*)c.settings+c.modeOffset)=c.originalMode;
      SkirtNotifySurface(c);
    }
  } __except(1) {Log("[SKIRT] could not restore cloth collision mode");}
  if(c.dataHandle && il2cpp_gchandle_free) il2cpp_gchandle_free(c.dataHandle);
  if(c.settingsHandle && il2cpp_gchandle_free) il2cpp_gchandle_free(c.settingsHandle);
  c.data=c.settings=c.notify=nullptr;c.dataHandle=c.settingsHandle=0;
  c.modeChanged=false;c.originalMode=c.currentMode=-1;
}
static bool SkirtApplySurface(SkirtCloth &c) {
  // Only BoneCloth / MeshCloth support Edge. BoneSpring ignores it and must
  // retain its native Point mode. No changes to topology, animation or weights.
  c.modePending=false;
  void *cls=il2cpp_object_get_class(c.object);
  int dataOff=SkirtField(cls,"serializeData");
  void *data=dataOff>=0?*(void**)((char*)c.object+dataOff):nullptr;
  if(c.data && c.data!=data) SkirtRestoreSurface(c);
  if(!data) {c.modePending=dataOff>=0;c.modeStatus=u8"无可用布料设置";return false;}
  void *dc=il2cpp_object_get_class(data);
  if(!SkirtClass(dc,"ClothSerializeData","BeyondDynamicBone")) {c.modeStatus=u8"布料设置类型不支持";return false;}
  int typeOff=-1,bone=-1,mesh=-1;
  if(!SkirtEnumField(dc,"clothType","ClothProcess","BoneCloth",typeOff,bone) ||
     !SkirtEnumField(dc,"clothType","ClothProcess","MeshCloth",typeOff,mesh)) {
    c.modeStatus=u8"无法验证布料类型";return false;
  }
  int clothType=*(int*)((char*)data+typeOff);
  if(clothType!=bone && clothType!=mesh) {c.modeStatus=u8"弹簧型布料不支持边碰撞";return false;}
  int settingsOff=SkirtField(dc,"colliderCollisionConstraint");
  void *settings=settingsOff>=0?*(void**)((char*)data+settingsOff):nullptr;
  if(!settings) {c.modePending=settingsOff>=0;c.modeStatus=u8"等待碰撞设置就绪";return false;}
  if(c.settings && (c.data!=data || c.settings!=settings)) SkirtRestoreSurface(c);
  if(!c.settings) {
    void *sc=il2cpp_object_get_class(settings);
    int modeOff=-1,point=-1,edge=-1;
    if(!sc || !il2cpp_class_get_declaring_type ||
       !SkirtClass(il2cpp_class_get_declaring_type(sc),"ColliderCollisionConstraint","BeyondDynamicBone") ||
       !SkirtEnumField(sc,"mode","ColliderCollisionConstraint","Point",modeOff,point) ||
       !SkirtEnumField(sc,"mode","ColliderCollisionConstraint","Edge",modeOff,edge) || point==edge) {
      c.modeStatus=u8"碰撞模式接口不支持";return false;
    }
    void *notify=SkirtMethod(cls,"SetParameterChange",false);
    if(!notify || !il2cpp_gchandle_new || !il2cpp_gchandle_free) {c.modeStatus=u8"缺少布料参数更新接口";return false;}
    int original=*(int*)((char*)settings+modeOff);
    // Two cloth components may reference one serialized settings instance.
    for(auto &other:g_skirtCloths) if(other.settings==settings) {original=other.originalMode;break;}
    if(original!=point && original!=edge) {c.modeStatus=u8"原布料未启用碰撞，保持原设置";return false;}
    uint32_t dh=il2cpp_gchandle_new(data,false),sh=il2cpp_gchandle_new(settings,false);
    if(!dh || !sh) {if(dh)il2cpp_gchandle_free(dh);if(sh)il2cpp_gchandle_free(sh);c.modePending=true;return false;}
    c.data=data;c.settings=settings;c.notify=notify;c.dataHandle=dh;c.settingsHandle=sh;
    c.dataOffset=dataOff;c.settingsOffset=settingsOff;c.modeOffset=modeOff;
    c.originalMode=original;c.pointMode=point;c.edgeMode=edge;
    Log("[SKIRT] garment '%s' collision baseline=%d Point=%d Edge=%d",c.name,original,point,edge);
  }
  c.simulateWeight=SkirtOptionalFloat(data,"clothSimulateWeight");
  c.animationPoseRatio=SkirtOptionalFloat(data,"animationPoseRatio");
  c.blendWeight=SkirtOptionalFloat(data,"blendWeight");
  int desired=g_skirtEdgeCollision?c.edgeMode:c.originalMode;
  c.modeChanged=c.modeChanged || desired!=c.originalMode;
  *(int*)((char*)settings+c.modeOffset)=desired;
  if(!SkirtNotifySurface(c)) {c.modePending=true;c.currentMode=-1;c.modeStatus=u8"布料参数通知失败";return false;}
  c.modeChanged=desired!=c.originalMode;
  c.currentMode=desired;
  c.modeStatus=desired==c.edgeMode?u8"边碰撞已请求（由游戏更新）":u8"使用原节点碰撞";
  return true;
}
static void ReleaseSkirtReferences() {
  for(auto &c:g_skirtCapsules) if(c.handle && il2cpp_gchandle_free) il2cpp_gchandle_free(c.handle);
  for(auto &c:g_skirtCloths) {
    SkirtRestoreSurface(c);
    if(c.handle && il2cpp_gchandle_free) il2cpp_gchandle_free(c.handle);
  }
  g_skirtCapsules.clear();g_skirtCloths.clear();
}
static void RestoreSkirtColliders() {
  for(auto &c:g_skirtCapsules) if(c.changed) SkirtWrite(c,c.originalSize,c.originalSeparation);
  ReleaseSkirtReferences();g_skirtDirty=false;g_skirtMatched=0;
  g_skirtStatus=u8"已恢复原裙摆碰撞";
}
static void ResetSkirtState() {
  RestoreSkirtColliders();g_skirtRetryFrames=g_skirtExamined=g_skirtUnsupported=0;
  g_skirtStatus=u8"等待角色裙摆组件";
}
static void CollectSkirtOnTransform(void *t,int depth,int &visited) {
  if(!t || depth>32 || ++visited>4096 || g_skirtCloths.size()>=64) return;
  __try {
    void *go=g_component_get_gameObject?Invoke(g_component_get_gameObject,t):nullptr;
    if(go && g_gameObject_GetComponents && g_componentClass) {
      void *type=il2cpp_class_get_type(g_componentClass);
      void *typeObj=type?il2cpp_type_get_object(type):nullptr;
      void *args[]={typeObj};
      void *arr=typeObj?Invoke(g_gameObject_GetComponents,go,args):nullptr;
      size_t count=arr?*(size_t*)((char*)arr+IL2CPP_ARRAY_LEN):0;
      if(count<=256) for(size_t i=0;i<count;++i) {
        void *object=((void**)((char*)arr+IL2CPP_ARRAY_DATA))[i];
        if(!UnityObjAlive(object)) continue;
        void *cls=il2cpp_object_get_class(object);
        const char *cn=cls?il2cpp_class_get_name(cls):nullptr;
        if(!cn || strcmp(cn,"BeyondBoneCloth")) continue;
        char name[128];SkirtObjectName(go,name,sizeof(name));
        if(!strstr(name,"skirt") && !strstr(name,"dress") && !strstr(name,"coat")) continue;
        bool found=false;for(auto &c:g_skirtCloths) if(c.object==object) found=true;
        if(found) continue;
        int off=SkirtField(cls,"process");
        if(off<0 || !il2cpp_gchandle_new || !il2cpp_gchandle_free) continue;
        uint32_t handle=il2cpp_gchandle_new(object,false);
        if(handle) {
          SkirtCloth cloth;cloth.object=object;cloth.handle=handle;cloth.processOffset=off;
          strncpy_s(cloth.name,name,_TRUNCATE);g_skirtCloths.push_back(cloth);
          Log("[SKIRT] garment '%s' retained",name);
        }
      }
    }
    void *box=g_transform_get_childCount?Invoke(g_transform_get_childCount,t):nullptr;
    int count=box?*(int*)((char*)box+16):0;
    if(count<0 || count>1024 || !g_transform_GetChild) return;
    for(int i=0;i<count;++i) {void *args[]={&i};CollectSkirtOnTransform(Invoke(g_transform_GetChild,t,args),depth+1,visited);}
  } __except(1) {}
}
static void CollectSkirtCloth() {int visited=0;CollectSkirtOnTransform(GetCharRootTransform(),0,visited);}
static bool SkirtCapture(void *object,SkirtCapsule &c) {
  if(!il2cpp_gchandle_new || !il2cpp_gchandle_free) return false;
  void *cls=il2cpp_object_get_class(object);
  c.object=object;c.setSize=SkirtMethod(cls,"SetSize",true);c.update=SkirtMethod(cls,"UpdateParameters",false);
  c.sizeOffset=SkirtField(cls,"size",true);c.separationOffset=SkirtField(cls,"radiusSeparation",false,true);
  if(!c.setSize || !c.update || c.sizeOffset<0 || c.separationOffset<0) return false;
  c.originalSize=*(Vec3*)((char*)object+c.sizeOffset);
  c.originalSeparation=*(unsigned char*)((char*)object+c.separationOffset);
  void *t=g_component_get_transform?Invoke(g_component_get_transform,object):nullptr;
  if(t) SkirtObjectName(t,c.attachment,sizeof(c.attachment));
  int center=SkirtField(cls,"center",true);
  if(center>=0) c.center=*(Vec3*)((char*)object+center);
  int off=-1,x=-1;
  if(SkirtEnumField(cls,"direction","BeyondBoneCapsuleCollider","X",off,x)) c.axis=*(int*)((char*)object+off);
  off=SkirtField(cls,"reverseDirection",false,true);if(off>=0)c.reverse=*(unsigned char*)((char*)object+off);
  off=SkirtField(cls,"alignedOnCenter",false,true);if(off>=0)c.aligned=*(unsigned char*)((char*)object+off);
  auto v=c.originalSize;
  if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
     v.x<=0 || v.y<0 || v.z<=0 || v.x>10 || v.y>10 || v.z>10 || c.originalSeparation>1) return false;
  c.handle=il2cpp_gchandle_new(object,false);return c.handle!=0;
}
static bool ApplySkirtColliderScale() {
  bool pending=false;
  g_skirtExamined=g_skirtUnsupported=g_skirtMatched=0;
  for(auto &c:g_skirtCapsules) c.seen=false;
  __try {
    for(auto &cloth:g_skirtCloths) {
      if(!UnityObjAlive(cloth.object)) continue;
      if(!SkirtApplySurface(cloth)) {
        if(g_skirtEdgeCollision) ++g_skirtUnsupported;
        pending=pending || cloth.modePending;
      }
      void *process=*(void**)((char*)cloth.object+cloth.processOffset);
      if(!process) {pending=true;continue;}
      int off=SkirtField(il2cpp_object_get_class(process),"colliderList");
      if(off<0) {++g_skirtUnsupported;continue;}
      void *list=*(void**)((char*)process+off);
      if(!list) {pending=true;continue;}
      void *items=*(void**)((char*)list+0x10);int count=*(int*)((char*)list+0x18);
      if(!items || count<0 || count>128 || size_t(count)>*(size_t*)((char*)items+0x18)) {++g_skirtUnsupported;continue;}
      for(int i=0;i<count;++i) {
        void *object=((void**)((char*)items+0x20))[i];
        if(!UnityObjAlive(object)) continue;
        ++g_skirtExamined;
        if(std::find(g_skirtTemporaryCapsules.begin(),g_skirtTemporaryCapsules.end(),object)!=g_skirtTemporaryCapsules.end())continue;
        if(!SkirtLegCapsule(object)) continue;
        auto it=std::find_if(g_skirtCapsules.begin(),g_skirtCapsules.end(),[&](const SkirtCapsule &c){return c.object==object;});
        if(it==g_skirtCapsules.end()) {
          if(g_skirtCapsules.size()>=128) {++g_skirtUnsupported;continue;}
          SkirtCapsule c;
          if(!SkirtCapture(object,c)) {++g_skirtUnsupported;continue;}
          g_skirtCapsules.push_back(c);it=g_skirtCapsules.end()-1;
          Log("[SKIRT] verified leg capsule %p baseline=(%.3f %.3f %.3f) separation=%d",object,c.originalSize.x,c.originalSize.y,c.originalSize.z,c.originalSeparation);
          Log("[SKIRT] capsule attachment='%s' center=(%.3f %.3f %.3f) axis=%d reverse=%d aligned=%d",c.attachment,c.center.x,c.center.y,c.center.z,c.axis,c.reverse,c.aligned);
        }
        it->seen=true;
      }
    }
    // Keep the original baseline through delayed cloth rebuilds. Shared
    // colliders are cached once, even when several skirt panels use them.
    if(!pending) for(auto it=g_skirtCapsules.begin();it!=g_skirtCapsules.end();) {
      if(it->seen) {++it;continue;}
      if(it->changed) SkirtWrite(*it,it->originalSize,it->originalSeparation);
      il2cpp_gchandle_free(it->handle);it=g_skirtCapsules.erase(it);
    }
    for(auto &c:g_skirtCapsules) if(c.seen) {
      if(!g_skirtGeometryOverride) {
        if(c.changed && !SkirtWrite(c,c.originalSize,c.originalSeparation)) {pending=true;continue;}
        c.changed=false;continue;
      }
      Vec3 size=c.originalSize;
      // Preserve native radius Y. Zero expansion must not shrink it to X.
      float end=c.originalSeparation?size.y:size.x;
      size.x*=g_skirtRadiusA;
      size.y=g_skirtTaperOn?(std::min)(end*g_skirtRadiusA+g_skirtHipRadiusDelta*g_skirtHeightScale,(std::max)(end,c.originalSize.x)*3.f):size.x;
      size.z*=g_skirtLengthScale;c.changed=true;
      if(SkirtWrite(c,size,g_skirtTaperOn?1:0)) ++g_skirtMatched;else pending=true;
    }
  } __except(1) {pending=true;}
  g_skirtDirty=pending;
  g_skirtStatus=!g_skirtGeometryOverride && !g_skirtCapsules.empty()?u8"使用原 BBC 胶囊尺寸":g_skirtMatched?u8"已调整腿部胶囊；仍由游戏布料物理解算":
      pending?u8"等待布料组件就绪":u8"未匹配可调整的腿部胶囊，未应用增强";
  return !pending;
}
static void SkirtMarkDirty() {g_skirtDirty=true;g_skirtRetryFrames=60;}
static void *SkirtQuery(void *cls,const char *name,int kind) {
  if(!cls || !il2cpp_class_get_methods || !il2cpp_method_get_name ||
     !il2cpp_method_get_flags || !il2cpp_method_get_param_count ||
     !il2cpp_method_get_return_type || !il2cpp_type_get_type) return nullptr;
  void *it=nullptr,*m;
  while((m=il2cpp_class_get_methods(cls,&it))) {
    const char *n=il2cpp_method_get_name(m);
    void *ret=il2cpp_method_get_return_type(m);
    if(n && !strcmp(n,name) && !(il2cpp_method_get_flags(m,nullptr)&0x10) &&
       !il2cpp_method_get_param_count(m) && ret && il2cpp_type_get_type(ret)==kind) return m;
  }
  return nullptr;
}
static int SkirtQueryBool(void *object,const char *name) {
  void *m=SkirtQuery(il2cpp_object_get_class(object),name,2);
  if(!m)return -1;
  void *exc=nullptr,*box=il2cpp_runtime_invoke(m,object,nullptr,&exc);
  if(exc || !box)return -1;
  unsigned char v=*(unsigned char*)((char*)box+16);return v<=1?v:-1;
}
static int SkirtEffectiveMode(void *process) {
  void *getter=SkirtQuery(il2cpp_object_get_class(process),"get_parameters",0x11);
  if(!getter)return -1;
  void *type=il2cpp_class_from_type(il2cpp_method_get_return_type(getter));
  if(!SkirtClass(type,"ClothParameters","BeyondDynamicBone"))return -1;
  void *field=SkirtNamedField(type,"colliderCollisionConstraint");
  if(!field || (il2cpp_field_get_flags(field)&0x10) ||
     il2cpp_type_get_type(il2cpp_field_get_type(field))!=0x11)return -1;
  void *constraint=il2cpp_class_from_type(il2cpp_field_get_type(field));
  int modeOffset=-1,edge=-1;
  if(!SkirtEnumField(constraint,"mode","ColliderCollisionConstraint","Edge",modeOffset,edge))return -1;
  uint32_t align=0;int size=il2cpp_class_value_size(type,&align),partSize=il2cpp_class_value_size(constraint,&align);
  size_t off=il2cpp_field_get_offset(field);
  // Value-type field offsets include the boxed object's 16-byte header.
  if(size<=0 || partSize<=0 || off<16 || off+partSize>size_t(size)+16 || modeOffset+4>partSize+16)return -1;
  void *exc=nullptr,*box=il2cpp_runtime_invoke(getter,process,nullptr,&exc);
  if(exc || !box)return -1;
  return *(int*)((char*)box+off+modeOffset-16);
}
static void SkirtObserveSimulation() {
  static ULONGLONG next=0;
  if(GetTickCount64()<next)return;
  next=GetTickCount64()+500;
  __try {
    for(auto &c:g_skirtCloths) {
      if(!UnityObjAlive(c.object))continue;
      bool on=false;c.enabled=ReadBehaviourEnabled(c.object,on)?int(on):-1;
      c.runtimeWeight=SkirtOptionalFloat(c.object,"clothSimulateWeightProperty");
      c.runtimeBlend=SkirtOptionalFloat(c.object,"blendWeightProperty");
      void *process=c.processOffset>=16?*(void**)((char*)c.object+c.processOffset):nullptr;
      if(!process)continue;
      c.running=SkirtQueryBool(process,"IsRunning");
      c.skipWriting=SkirtQueryBool(process,"IsSkipWriting");
      c.culled=SkirtQueryBool(process,"IsLodCulled");
      int mode=SkirtEffectiveMode(process);
      if(mode!=c.effectiveMode)Log("[BBC] garment '%s' solver mode=%d running=%d skip=%d enabled=%d weight=%.3f blend=%.3f",c.name,mode,c.running,c.skipWriting,c.enabled,c.runtimeWeight,c.runtimeBlend);
      c.effectiveMode=mode;
    }
  } __except(1) {Log("[BBC] simulation status unavailable");}
}
static void SkirtBegin() {
  void *hips=GetHumanoidBone(Hips),*head=GetHumanoidBone(Head);
  if(hips && head) {float height=Len(GetBoneWorldPos(head)-GetBoneWorldPos(hips));if(height>.1f && height<3) g_skirtHeightScale=height;}
  CollectSkirtCloth();SkirtMarkDirty();
}
static void SkirtTick() {
  // Native parameter lists are consumed by BBC's game-thread update. During
  // playback, do not mutate them concurrently from the fallback/UI thread.
  if(g_bbcHookReady && g_bbcSyncEnabled && g_bbcRequested.load() && !g_bbcInCallback) return;
  if(!g_skirtDirty) return;
  if(!g_skirtCollisionEnabled) {RestoreSkirtColliders();g_skirtStatus=u8"裙摆增强已关闭";return;}
  if(g_skirtRetryFrames--<=0) {g_skirtDirty=false;g_skirtStatus=u8"部分裙摆组件不可用，未完成调整";return;}
  if(g_skirtCloths.empty()) {
    if(g_skirtRetryFrames%20==0) CollectSkirtCloth();
    if(g_skirtCloths.empty()) return;
  }
  ApplySkirtColliderScale();
}
