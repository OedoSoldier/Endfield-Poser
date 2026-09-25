#pragma once
// Temporary native BBC colliders. Only mutate these leases after BBC's job
// fence; leave the game's colliders and all particle buffers owned by BBC.
#include "game/cloth.h"

static bool g_bbcLegCoverage=true;
static float g_bbcLegPadding=.015f;

static void *BbcRefMethod(void *cls,const char *name,bool isStatic,
                          const char *param,const char *ns,int result) {
  for(int depth=0;cls && depth<16;++depth,cls=il2cpp_class_get_parent(cls)) {
    void *it=nullptr,*m;
    while((m=il2cpp_class_get_methods(cls,&it))) {
      const char *n=il2cpp_method_get_name(m);
      if(!n || strcmp(n,name) || !!(il2cpp_method_get_flags(m,nullptr)&0x10)!=isStatic ||
          il2cpp_method_get_param_count(m)!=1)continue;
      void *p=il2cpp_method_get_param(m,0),*r=il2cpp_method_get_return_type(m);
      if(p && r && il2cpp_type_get_type(p)==0x12 &&
          SkirtClass(il2cpp_class_from_type(p),param,ns) && il2cpp_type_get_type(r)==result)return m;
    }
  }
  return nullptr;
}
struct BbcColliderList {
  void *object=nullptr,*contains=nullptr,*add=nullptr,*remove=nullptr;
  uint32_t handle=0;
};
static bool BbcListCapture(BbcColliderList &list,void *object) {
  if(!object)return false;
  void *cls=il2cpp_object_get_class(object);
  // Only the actual List<ColliderComponent>, never a similarly named API.
  if(!SkirtClass(cls,"List`1","System.Collections.Generic"))return false;
  list.contains=BbcRefMethod(cls,"Contains",false,"ColliderComponent","BeyondDynamicBone",2);
  list.add=BbcRefMethod(cls,"Add",false,"ColliderComponent","BeyondDynamicBone",1);
  list.remove=BbcRefMethod(cls,"Remove",false,"ColliderComponent","BeyondDynamicBone",2);
  if(!list.contains || !list.add || !list.remove)return false;
  list.object=object;list.handle=il2cpp_gchandle_new(object,false);return list.handle!=0;
}
static int BbcListContains(const BbcColliderList &list,void *value) {
  if(!list.object || !list.contains)return -1;
  void *exc=nullptr,*args[]={value};
  void *box=il2cpp_runtime_invoke(list.contains,list.object,args,&exc);
  if(exc || !box)return -1;
  int v=*(unsigned char*)((char*)box+16);return v<=1?v:-1;
}
static bool BbcListSet(const BbcColliderList &list,void *value,bool present) {
  int found=BbcListContains(list,value);
  if(found<0)return false;
  if(found==int(present))return true;
  void *exc=nullptr,*args[]={value};
  il2cpp_runtime_invoke(present?list.add:list.remove,list.object,args,&exc);
  return !exc && BbcListContains(list,value)==int(present);
}
// Read-only enumeration, using the managed getters (no List layout writes).
static bool BbcListItems(void *list,std::vector<void*> &out) {
  if(!list)return false;
  void *cls=il2cpp_object_get_class(list);
  if(!SkirtClass(cls,"List`1","System.Collections.Generic"))return false;
  void *count=SkirtQuery(cls,"get_Count",8),*item=nullptr,*it=nullptr,*m;
  while((m=il2cpp_class_get_methods(cls,&it))) {
    if(strcmp(il2cpp_method_get_name(m),"get_Item") || (il2cpp_method_get_flags(m,nullptr)&0x10) ||
       il2cpp_method_get_param_count(m)!=1)continue;
    void *p=il2cpp_method_get_param(m,0),*r=il2cpp_method_get_return_type(m);
    if(p && r && il2cpp_type_get_type(p)==8 && il2cpp_type_get_type(r)==0x12 &&
        SkirtClass(il2cpp_class_from_type(r),"ColliderComponent","BeyondDynamicBone"))item=m;
  }
  if(!count || !item)return false;
  void *exc=nullptr,*box=il2cpp_runtime_invoke(count,list,nullptr,&exc);
  if(exc || !box)return false;
  int n=*(int*)((char*)box+16);if(n<0 || n>128)return false;
  for(int i=0;i<n;++i) {
    void *args[]={&i};void *o=il2cpp_runtime_invoke(item,list,args,&exc);
    if(exc)return false;
    if(o)out.push_back(o);
  }
  return true;
}
struct BbcLegShape {Vec3 center,size;int axis=0;};
static bool BbcLegGeometry(Vec3 endpoint,float radius,float padding,BbcLegShape &shape) {
  float length=Len(endpoint);
  if(!std::isfinite(length) || length<.05f || length>2 || !std::isfinite(radius) ||
      radius<.005f || radius>.3f || !std::isfinite(padding) || padding<0 || padding>.06f)return false;
  float v[]={endpoint.x,endpoint.y,endpoint.z};int axis=0;
  for(int i=1;i<3;++i)if(std::abs(v[i])>std::abs(v[axis]))axis=i;
  // Capsules align with a Transform axis. Reject rigs needing an additional
  // rotated Transform, rather than rotating a character's real bone.
  if(std::abs(v[axis])<length*.999f)return false;
  float r=radius+padding;
  shape.center=endpoint*.5f;shape.axis=axis;
  // Include both joint centres even when BBC counts rounded caps in length.
  shape.size={r,r,length+2*r};return true;
}
struct BbcExtraCapsule {
  void *bone=nullptr,*object=nullptr,*cls=nullptr,*setSize=nullptr,*update=nullptr;
  uint32_t boneHandle=0,handle=0;
  Vec3 endpoint;
  float radius=0,padding=-1;
  bool listed=false,destroyPending=false,unavailable=false;
  int effective=-1;
};
struct BbcGarmentLease {
  void *cloth=nullptr,*data=nullptr,*process=nullptr,*settings=nullptr,*notify=nullptr;
  int dataOffset=-1,processOffset=-1,settingsOffset=-1,listOffset=-1;
  uint32_t settingsHandle=0;
  BbcColliderList list;
  BbcExtraCapsule legs[4]; // L thigh/calf, R thigh/calf; original bones retained
  bool garment=false,ready=false,notifyPending=false,failed=false,diagnosed=false;
  int maxDistance=-1,backstop=-1,connection=-1;
  int lastSubmitted=-1,lastEffective=-1;
  ULONGLONG nextObservation=0;
  const char *status=u8"等待原生裙摆碰撞数据";
};
static bool BbcGarmentCurrent(const BbcGarmentLease &e) {
  return UnityObjAlive(e.cloth) && e.dataOffset>=16 && e.processOffset>=16 &&
    *(void**)((char*)e.cloth+e.dataOffset)==e.data &&
    *(void**)((char*)e.cloth+e.processOffset)==e.process && e.settingsOffset>=16 &&
    *(void**)((char*)e.data+e.settingsOffset)==e.settings && e.listOffset>=16 &&
    *(void**)((char*)e.settings+e.listOffset)==e.list.object;
}
static void BbcGarmentBind(BbcGarmentLease &e,void *cloth,void *data,void *process,const char *name) {
  char lower[128];strncpy_s(lower,name,_TRUNCATE);
  for(char *p=lower;*p;++p)if(*p>='A' && *p<='Z')*p+=32;
  if(!strstr(lower,"skirt") && !strstr(lower,"dress") && !strstr(lower,"coat"))return;
  e.garment=true;e.cloth=cloth;e.data=data;e.process=process;
  int start[]={LeftUpperLeg,LeftLowerLeg,RightUpperLeg,RightLowerLeg};
  int end[]={LeftLowerLeg,LeftFoot,RightLowerLeg,RightFoot};
  for(int i=0;i<4;++i) {
    void *a=nullptr,*b=nullptr;
    for(int j=0;j<s_humanBoneCount;++j) {
      if(s_humanBones[j].humanBone==start[i])a=s_humanBones[j].transform;
      if(s_humanBones[j].humanBone==end[i])b=s_humanBones[j].transform;
    }
    if(!UnityObjAlive(a) || !UnityObjAlive(b) || !g_transform_get_parent || Invoke(g_transform_get_parent,b)!=a)continue;
    auto &leg=e.legs[i];leg.bone=a;leg.endpoint=GetBoneLocalPos(b);
    leg.boneHandle=il2cpp_gchandle_new(a,false);
    if(!leg.boneHandle)leg.bone=nullptr;
  }
}
static void BbcGarmentDiagnose(BbcGarmentLease &e) {
  if(e.diagnosed)return;
  e.diagnosed=true;
  void *dc=il2cpp_object_get_class(e.data);
  int off=SkirtField(dc,"motionConstraint");
  void *motion=off>=16?*(void**)((char*)e.data+off):nullptr;
  if(motion && SkirtClass(il2cpp_class_get_declaring_type(il2cpp_object_get_class(motion)),"MotionConstraint","BeyondDynamicBone")) {
    int a=SkirtField(il2cpp_object_get_class(motion),"useMaxDistance",false,true);
    int b=SkirtField(il2cpp_object_get_class(motion),"useBackstop",false,true);
    if(a>=16)e.maxDistance=*(unsigned char*)((char*)motion+a);
    if(b>=16)e.backstop=*(unsigned char*)((char*)motion+b);
  }
  int mode=-1,line=-1;
  if(SkirtEnumField(dc,"connectionMode","RenderSetupData","Line",mode,line))e.connection=*(int*)((char*)e.data+mode);
  // Export the game's actual curves/topology once, not every frame. Kept in
  // the user's local diagnostic log, never read/imported as configuration.
  void *exporter=SkirtQuery(dc,"ExportJson",0xe);
  if(exporter) {
    void *exc=nullptr,*s=il2cpp_runtime_invoke(exporter,e.data,nullptr,&exc);
    if(s && !exc) {char json[32768];ReadStr(s,json,sizeof(json));Log("[BBC] garment native settings: %s",json);}
  }
}
static bool BbcGarmentPrepare(BbcGarmentLease &e) {
  if(e.ready)return BbcGarmentCurrent(e);
  void *cc=il2cpp_object_get_class(e.cloth),*dc=il2cpp_object_get_class(e.data);
  int off=-1,bone=-1,mesh=-1;
  if(!SkirtEnumField(dc,"clothType","ClothProcess","BoneCloth",off,bone) ||
     !SkirtEnumField(dc,"clothType","ClothProcess","MeshCloth",off,mesh))return false;
  int kind=*(int*)((char*)e.data+off);
  if(kind!=bone && kind!=mesh)return false;
  e.dataOffset=SkirtField(cc,"serializeData");e.processOffset=SkirtField(cc,"process");
  e.settingsOffset=SkirtField(dc,"colliderCollisionConstraint");
  if(e.settingsOffset<16)return false;
  e.settings=*(void**)((char*)e.data+e.settingsOffset);
  if(!e.settings)return false;
  void *sc=il2cpp_object_get_class(e.settings);int point=-1,edge=-1;
  if(!SkirtEnumField(sc,"mode","ColliderCollisionConstraint","Point",off,point) ||
     !SkirtEnumField(sc,"mode","ColliderCollisionConstraint","Edge",off,edge))return false;
  kind=*(int*)((char*)e.settings+off);if(kind!=point && kind!=edge)return false;
  e.listOffset=SkirtField(sc,"colliderList");
  e.notify=SkirtMethod(cc,"SetParameterChange",false);
  if(e.listOffset<16 || !e.notify)return false;
  void *list=*(void**)((char*)e.settings+e.listOffset);
  std::vector<void*> items;if(!BbcListItems(list,items))return false;
  // Use this garment's own registered thigh radii as the size reference.
  // No global name lookup or foreign character collider is borrowed.
  for(void *o:items) {
    if(!UnityObjAlive(o))continue;
    if(std::find(g_skirtTemporaryCapsules.begin(),g_skirtTemporaryCapsules.end(),o)!=g_skirtTemporaryCapsules.end())continue;
    void *cls=il2cpp_object_get_class(o);
    if(!SkirtClass(cls,"BeyondBoneCapsuleCollider","BeyondDynamicBone"))continue;
    void *t=Invoke(g_component_get_transform,o);int size=SkirtField(cls,"size",true);
    if(size<16)continue;
    Vec3 v=*(Vec3*)((char*)o+size);
    if(!std::isfinite(v.x) || v.x<.005f || v.x>.3f)continue;
    for(int side=0;side<2;++side)if(t==e.legs[side*2].bone) {
      for(int j=0;j<2;++j) {auto &leg=e.legs[side*2+j];leg.cls=cls;leg.radius=v.x;}
    }
  }
  if(!BbcListCapture(e.list,list))return false;
  e.settingsHandle=il2cpp_gchandle_new(e.settings,false);
  if(!e.settingsHandle) {il2cpp_gchandle_free(e.list.handle);e.list={};return false;}
  e.ready=true;return BbcGarmentCurrent(e);
}
static void *BbcDestroyMethod() {
  return BbcRefMethod(g_componentClass,"Destroy",true,"Object","UnityEngine",1);
}
static bool BbcDestroyExtra(BbcExtraCapsule &leg) {
  if(leg.object && UnityObjAlive(leg.object)) {
    void *m=BbcDestroyMethod();if(!m)return false;
    void *exc=nullptr,*args[]={leg.object};il2cpp_runtime_invoke(m,nullptr,args,&exc);
    if(exc)return false;
  }
  if(leg.handle)il2cpp_gchandle_free(leg.handle);
  g_skirtTemporaryCapsules.erase(std::remove(g_skirtTemporaryCapsules.begin(),g_skirtTemporaryCapsules.end(),leg.object),g_skirtTemporaryCapsules.end());
  leg.object=nullptr;leg.handle=0;leg.padding=-1;leg.destroyPending=false;leg.effective=-1;return true;
}
static bool BbcConfigureExtra(BbcExtraCapsule &leg,float padding) {
  if(leg.object && leg.padding==padding && UnityObjAlive(leg.object) && UnityObjAlive(leg.bone))return true;
  BbcLegShape shape;if(!leg.cls || !leg.boneHandle || !UnityObjAlive(leg.bone) ||
      !BbcLegGeometry(leg.endpoint,leg.radius,padding,shape))return false;
  int axisOff=-1,axis=-1,center=SkirtField(leg.cls,"center",true);
  int aligned=SkirtField(leg.cls,"alignedOnCenter",false,true),sep=SkirtField(leg.cls,"radiusSeparation",false,true);
  int reverse=SkirtField(leg.cls,"reverseDirection",false,true);
  const char *axes[]={"X","Y","Z"};
  if(center<16 || aligned<16 || sep<16 || reverse<16 ||
      !SkirtEnumField(leg.cls,"direction","BeyondBoneCapsuleCollider",axes[shape.axis],axisOff,axis))return false;
  leg.setSize=SkirtMethod(leg.cls,"SetSize",true);leg.update=SkirtMethod(leg.cls,"UpdateParameters",false);
  if(!leg.setSize || !leg.update || !BbcDestroyMethod())return false;
  if(!leg.object) {
    void *add=BbcRefMethod(g_gameObjectClass,"AddComponent",false,"Type","System",0x12);
    if(!add || !SkirtClass(il2cpp_class_from_type(il2cpp_method_get_return_type(add)),"Component","UnityEngine"))return false;
    void *go=Invoke(g_component_get_gameObject,leg.bone);
    if(!UnityObjAlive(go))return false;
    void *type=il2cpp_type_get_object(il2cpp_class_get_type(leg.cls)),*exc=nullptr,*args[]={type};
    if(!type || !g_gameObject_GetComponents)return false;
    void *existing=il2cpp_runtime_invoke(g_gameObject_GetComponents,go,args,&exc);
    if(exc || !existing)return false;
    size_t count=*(size_t*)((char*)existing+IL2CPP_ARRAY_LEN);
    if(count>128)return false;
    std::vector<void*> prior;
    for(size_t i=0;i<count;++i)prior.push_back(((void**)((char*)existing+IL2CPP_ARRAY_DATA))[i]);
    leg.object=il2cpp_runtime_invoke(add,go,args,&exc);
    if(exc || !UnityObjAlive(leg.object)) {leg.object=nullptr;return false;}
    if(std::find(prior.begin(),prior.end(),leg.object)!=prior.end()) {leg.object=nullptr;return false;}
    leg.handle=il2cpp_gchandle_new(leg.object,false);
    if(!leg.handle) {BbcDestroyExtra(leg);return false;}
    if(il2cpp_object_get_class(leg.object)!=leg.cls) {BbcDestroyExtra(leg);return false;}
    g_skirtTemporaryCapsules.push_back(leg.object);
    Log("[BBC] native leg capsule created: bone=%p component=%p length=%.3f radius=%.3f padding=%.3f",leg.bone,leg.object,Len(leg.endpoint),leg.radius,padding);
  }
  if(!UnityObjAlive(leg.object))return false;
  if(leg.padding==padding)return true;
  *(Vec3*)((char*)leg.object+center)=shape.center;
  *(int*)((char*)leg.object+axisOff)=axis;
  *(unsigned char*)((char*)leg.object+aligned)=1;
  *(unsigned char*)((char*)leg.object+sep)=0;
  *(unsigned char*)((char*)leg.object+reverse)=0;
  void *exc=nullptr,*args[]={&shape.size};il2cpp_runtime_invoke(leg.setSize,leg.object,args,&exc);
  if(exc)return false;
  il2cpp_runtime_invoke(leg.update,leg.object,nullptr,&exc);
  if(exc)return false;
  leg.padding=padding;return true;
}
static bool BbcGarmentMaintain(BbcGarmentLease &e,bool applying) {
  if(!e.garment)return true;
  if(applying && !e.diagnosed)BbcGarmentDiagnose(e);
  if(applying && !e.ready && !e.failed) {
    if(!BbcGarmentPrepare(e)) {e.failed=true;e.status=u8"原生裙摆/腿部接口不兼容，未添加碰撞体";}
  }
  bool current=e.ready && BbcGarmentCurrent(e),ok=true;
  bool enabled=false;
  applying=applying && !e.failed && g_bbcLegCoverage && g_skirtCollisionEnabled && current &&
      ReadBehaviourEnabled(e.cloth,enabled) && enabled;
  int configured=0;bool needObserve=GetTickCount64()>=e.nextObservation;
  for(auto &leg:e.legs) {
    if(applying && leg.bone && leg.cls && !leg.unavailable) {
      if(!BbcConfigureExtra(leg,g_bbcLegPadding)) {leg.unavailable=true;ok=false;continue;}
      if(!leg.listed) {
        // An exceptional Add may still insert; mark ownership before the
        // call so a failed transaction is removed during stop/rollback.
        leg.listed=true;e.notifyPending=true;
        if(!BbcListSet(e.list,leg.object,true)) {e.failed=true;e.status=u8"原生列表登记失败，正在回退";ok=false;continue;}
      }
      leg.destroyPending=false;++configured;
    } else if(leg.object) {
      if(leg.listed) {
        if(!BbcListSet(e.list,leg.object,false)) {ok=false;continue;}
        leg.listed=false;e.notifyPending=true;
      }
      leg.destroyPending=true;
      needObserve=true;
    }
  }
  if(e.notifyPending) {
    if(current) {
      void *exc=nullptr;il2cpp_runtime_invoke(e.notify,e.cloth,nullptr,&exc);
      if(exc)ok=false;else e.notifyPending=false;
    } else e.notifyPending=false;
  }
  // SetParameterChange queues removal. Wait until the native process has
  // actually dropped the component before destroying it (also on pause/off).
  std::vector<void*> runtime;bool read=false;
  if(needObserve && e.process) {
    int off=SkirtField(il2cpp_object_get_class(e.process),"colliderList");
    if(off>=16)read=BbcListItems(*(void**)((char*)e.process+off),runtime);
  }
  if(needObserve)e.nextObservation=GetTickCount64()+500;
  for(auto &leg:e.legs)if(leg.object) {
    if(needObserve)leg.effective=read?int(std::find(runtime.begin(),runtime.end(),leg.object)!=runtime.end()):-1;
    if(leg.destroyPending) {
      if(current && (!read || leg.effective!=0 || e.notifyPending)) {ok=false;continue;}
      if(!BbcDestroyExtra(leg))ok=false;
    }
  }
  int submitted=0,effective=0;
  for(const auto &leg:e.legs) {submitted+=leg.listed;effective+=leg.effective==1;}
  if(submitted!=e.lastSubmitted || effective!=e.lastEffective) {
    Log("[BBC] garment %p native leg coverage: submitted=%d registered=%d max-distance=%d backstop=%d connection=%d",
        e.cloth,submitted,effective,e.maxDistance,e.backstop,e.connection);
    e.lastSubmitted=submitted;e.lastEffective=effective;
  }
  if(applying)e.status=configured==4?u8"腿部原生碰撞体已提交；下方显示实际登记数":
      configured?u8"部分腿部碰撞已补齐，其余骨架不兼容":u8"未找到可用腿部碰撞参考";
  else if(!e.failed)e.status=ok?u8"已恢复原生碰撞体列表":u8"等待原生求解器释放临时碰撞体";
  return ok;
}
static bool BbcGarmentPending(const BbcGarmentLease &e) {
  if(e.notifyPending)return true;
  for(const auto &leg:e.legs)if(leg.object)return true;
  return false;
}
static void BbcGarmentRelease(BbcGarmentLease &e) {
  for(auto &leg:e.legs)if(leg.boneHandle)il2cpp_gchandle_free(leg.boneHandle);
  if(e.settingsHandle)il2cpp_gchandle_free(e.settingsHandle);
  if(e.list.handle)il2cpp_gchandle_free(e.list.handle);
  e={};
}
