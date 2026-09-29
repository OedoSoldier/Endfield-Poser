#pragma once
#include "core/frame_driver.h"
#include "core/game_hooks.h"
#include "math/mmd_camera.h"
#include <string>
#include <memory>

// Camera objects are only read/written in CameraManager's verified game callback.
// Playback/editor threads publish a pose under g_poseMutex and never call Unity.
namespace mmd_camera {
struct Request {
  bool active = false;
  uint64_t session = 0;
  void *actor = nullptr;
  mmd::CameraPose pose;
  void *followActor = nullptr;
  uint64_t sequence = 0;
  double sourceFrame = 0;
};
static Request request;
static std::shared_ptr<const Request> published;
static uint64_t publishSequence=0;
static std::atomic<bool> desiredActive{false};
static std::atomic<uint64_t> desiredSession{0};
static void Publish(const Request &next) {
  request=next;request.sequence=++publishSequence;
  std::atomic_store(&published,std::make_shared<const Request>(request));
  desiredSession.store(next.session);desiredActive.store(next.active);
}
static void Stop() {desiredActive.store(false);request.active=false;}
static uint64_t nextSession = 0;
static std::atomic<uint64_t> applied{0},callbacks{0},lastSequence{0},repeatedFrames{0};
static std::atomic<double> lastCallback{-1e30},sourceFrame{0};
static std::atomic<bool> restorePending{false},driverPaused{false};
static std::atomic<const char*> status{u8"镜头未启用"};
static void *getMain = nullptr, *getFov = nullptr, *setFov = nullptr,
            *getOrtho = nullptr, *setOrtho = nullptr, *getSize = nullptr, *setSize = nullptr,
            *getPhysical = nullptr, *setPhysical = nullptr;
static bool ready = false;
static void *brainClass=nullptr,*getDriverEnabled=nullptr,*setDriverEnabled=nullptr;
struct LensVector {float x=0,y=0;};
static void *getFocal=nullptr,*setFocal=nullptr,*getSensor=nullptr,
            *getGate=nullptr,*setGate=nullptr,*getShift=nullptr,*setShift=nullptr;
// Photo mode stores focus in HGAdditionalCameraData, not UnityEngine.Camera.
// Resolve the inline value-type layout from metadata; never use game offsets.
static void *focusClass=nullptr;
static size_t focusOffset=0;
static std::atomic<bool> focusActive{false};
static std::atomic<float> focusDistance{0};
struct Lease {
  void *camera = nullptr, *transform = nullptr;
  uint32_t cameraRef = 0, transformRef = 0;
  uint64_t session = 0;
  Vec3 position;
  Quat rotation;
  float fov = 60, size = 5;
  bool ortho = false, physical = false;
  bool hasPose = false;
  mmd::CameraPose lastPose;
  void *driver=nullptr;
  uint32_t driverRef=0;
  bool driverEnabled=false;
  void *focusData=nullptr;
  uint32_t focusRef=0;
  float originalFocus=0;
  bool physicalLens=false;
  float focalLength=0;
  int gateFit=0;
  LensVector lensShift;
} static lease;
static bool Call(void *method,void *object,void **args=nullptr,void **result=nullptr) {
  if (!method || !il2cpp_runtime_invoke) return false;
  __try {
    void *error=nullptr, *box=il2cpp_runtime_invoke(method,object,args,&error);
    if (result) *result=box;
    return !error;
  } __except(1) { return false; }
}
template<class T> static bool Read(void *method,void *object,T &value) {
  void *box=nullptr;
  if (!Call(method,object,nullptr,&box) || !box) return false;
  __try { memcpy(&value,static_cast<char*>(box)+16,sizeof(T)); return true; }
  __except(1) { return false; }
}
template<class T> static bool Write(void *method,void *object,T value) {
  void *args[]={&value};return Call(method,object,args);
}
static void *CameraComponent(void *camera,void *klass) {
  if(!klass || !g_component_get_gameObject || !g_gameObject_GetComponent ||
     !il2cpp_class_get_type || !il2cpp_type_get_object || !il2cpp_object_get_class) return nullptr;
  void *go=nullptr,*component=nullptr;
  void *type=il2cpp_type_get_object(il2cpp_class_get_type(klass));void *args[]={type};
  if(!type || !Call(g_component_get_gameObject,camera,nullptr,&go) || !UnityObjAlive(go) ||
     !Call(g_gameObject_GetComponent,go,args,&component) || !UnityObjAlive(component) ||
     il2cpp_object_get_class(component)!=klass) return nullptr;
  return component;
}
static bool FocusValue(void *data,float &value,bool write=false) {
  if(!focusOffset || !UnityObjAlive(data) || (write && (!std::isfinite(value) || value<0))) return false;
  __try {
    auto p=static_cast<char*>(data)+focusOffset;
    if(write) memcpy(p,&value,sizeof(value));
    else memcpy(&value,p,sizeof(value));
    return std::isfinite(value) && value>=0;
  } __except(1) {return false;}
}
static void ReleaseRefs() {
  if (il2cpp_gchandle_free) {
    if (lease.cameraRef) il2cpp_gchandle_free(lease.cameraRef);
    if (lease.transformRef) il2cpp_gchandle_free(lease.transformRef);
    if (lease.driverRef) il2cpp_gchandle_free(lease.driverRef);
    if (lease.focusRef) il2cpp_gchandle_free(lease.focusRef);
  }
  lease={};
  restorePending=false;driverPaused=false;
  focusActive=false;focusDistance=0;
}
static bool Restore() {
  if (!lease.camera) return true;
  bool ok=true;
  if(UnityObjAlive(lease.focusData)) ok=FocusValue(lease.focusData,lease.originalFocus,true)&&ok;
  if (UnityObjAlive(lease.camera)) {
    // Physical mode may recalculate FOV from focal length. Restore it first.
    if (setPhysical) ok=Write(setPhysical,lease.camera,lease.physical)&&ok;
    ok=Write(setOrtho,lease.camera,lease.ortho)&&ok;
    ok=Write(setFov,lease.camera,lease.fov)&&ok;
    ok=Write(setSize,lease.camera,lease.size)&&ok;
    if(lease.physicalLens) {
      ok=Write(setGate,lease.camera,lease.gateFit)&&ok;
      ok=Write(setShift,lease.camera,lease.lensShift)&&ok;
      ok=Write(setFocal,lease.camera,lease.focalLength)&&ok;
    }
  }
  if (UnityObjAlive(lease.transform)) {
    ok=Write(g_transform_set_localPosition,lease.transform,lease.position)&&ok;
    ok=Write(g_transform_set_localRotation,lease.transform,lease.rotation)&&ok;
  }
  if(UnityObjAlive(lease.driver)) ok=Write(setDriverEnabled,lease.driver,lease.driverEnabled)&&ok;
  if (ok) ReleaseRefs();
  else status=u8"等待恢复原相机设置";
  return ok;
}
static bool Capture(void *camera,const Request &sample) {
  if (!UnityObjAlive(camera) || !il2cpp_gchandle_new || !il2cpp_gchandle_free) return false;
  void *transform=nullptr;
  if (!Call(g_component_get_transform,camera,nullptr,&transform) || !UnityObjAlive(transform)) return false;
  Lease saved;
  saved.camera=camera;saved.transform=transform;saved.session=sample.session;
  if (!Read(g_transform_get_localPosition,transform,saved.position) ||
      !Read(g_transform_get_localRotation,transform,saved.rotation) ||
      !Read(getFov,camera,saved.fov) || !Read(getOrtho,camera,saved.ortho) ||
      !Read(getSize,camera,saved.size) ||
      (getPhysical && !Read(getPhysical,camera,saved.physical))) return false;
  if (!std::isfinite(saved.fov) || !std::isfinite(saved.size)) return false;
  if(saved.physical && getFocal && setFocal && getSensor && getGate && setGate && getShift && setShift) {
    LensVector sensor;
    if(!Read(getFocal,camera,saved.focalLength) || !Read(getGate,camera,saved.gateFit) ||
       !Read(getShift,camera,saved.lensShift) || !Read(getSensor,camera,sensor) ||
       !std::isfinite(saved.focalLength) || saved.focalLength<=0 ||
       !std::isfinite(saved.lensShift.x) || !std::isfinite(saved.lensShift.y) ||
       mmd::CameraFocalLength(saved.fov,sensor.y)<=0) return false;
    saved.physicalLens=true;
  }
  saved.cameraRef=il2cpp_gchandle_new(camera,false);
  saved.transformRef=il2cpp_gchandle_new(transform,false);
  if(focusOffset) {
    void *data=CameraComponent(camera,focusClass);
    if(data && FocusValue(data,saved.originalFocus)) {
      saved.focusRef=il2cpp_gchandle_new(data,false);
      if(saved.focusRef) saved.focusData=data;
    }
  }
  // Discover once per lease. Use Behaviour's verified bool property and retain
  // its exact original value, including an already disabled camera driver.
  if(brainClass && getDriverEnabled && setDriverEnabled && g_component_get_gameObject &&
     g_gameObject_GetComponent && il2cpp_class_get_type && il2cpp_type_get_object) {
    void *go=nullptr,*driver=nullptr;
    void *type=il2cpp_type_get_object(il2cpp_class_get_type(brainClass));void *args[]={type};
    if(type && Call(g_component_get_gameObject,camera,nullptr,&go) && go &&
       Call(g_gameObject_GetComponent,go,args,&driver) && UnityObjAlive(driver) &&
       Read(getDriverEnabled,driver,saved.driverEnabled)) {
      saved.driver=driver;saved.driverRef=il2cpp_gchandle_new(driver,false);
      if(!saved.driverRef)saved.driver=nullptr;
    }
  }
  lease=saved;
  if (!saved.cameraRef || !saved.transformRef) {ReleaseRefs();return false;}
  restorePending=true;
  if(lease.driver && !Write(setDriverEnabled,lease.driver,false)) {desiredActive=false;Restore();return false;}
  driverPaused=lease.driver!=nullptr;
  Log("[MMD-CAMERA] acquired camera=%p session=%llu focus=%p original_focus=%.4f physical_lens=%d",camera,
      (unsigned long long)saved.session,saved.focusData,saved.originalFocus,saved.physicalLens);
  return true;
}
static bool Apply(void *camera,void *transform,const mmd::CameraPose &p) {
  bool ok=true;
  // HG's DOF shader selects physical vs manual focus using this camera flag.
  // Turning it off makes photo mode's saved manual ranges blur the subject.
  if (setPhysical) ok=Write(setPhysical,camera,lease.physicalLens)&&ok;
  ok=Write(setOrtho,camera,!p.perspective)&&ok;
  if(lease.physicalLens) {
    LensVector sensor;
    if(!Read(getSensor,camera,sensor)) return false;
    float focal=mmd::CameraFocalLength(p.fov,sensor.y);
    if(!std::isfinite(focal) || focal<=0) return false;
    // Vertical gate fit preserves the VMD vertical FOV at any window aspect.
    ok=Write(setGate,camera,1)&&ok;
    ok=Write(setShift,camera,LensVector{})&&ok;
    ok=Write(setFocal,camera,focal)&&ok;
  } else ok=Write(setFov,camera,p.fov)&&ok;
  ok=Write(setSize,camera,p.orthoSize)&&ok;
  ok=Write(g_transform_set_position,transform,p.position)&&ok;
  ok=Write(g_transform_set_rotation,transform,p.rotation)&&ok;
  if(UnityObjAlive(lease.focusData)) {
    float distance=mmd::CameraFocusDistance(p);
    bool focused=FocusValue(lease.focusData,distance,true);
    focusActive=focused;focusDistance=focused?distance:0;
    ok=focused&&ok;
  } else {focusActive=false;focusDistance=0;}
  return ok;
}
static void Pump(void *camera,const Request &sample) {
  ++callbacks;lastCallback=FrameNow();
  bool active=sample.active && !CharacterSwitchInProgress() &&
              sample.actor==g_charAnimator && UnityObjAlive(sample.actor) &&
              (!sample.followActor || UnityObjAlive(sample.followActor));
  if (!active) {
    if (Restore()) status=u8"镜头已停止，原相机已恢复";
    return;
  }
  const auto &p=sample.pose;
  auto finite=[](Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
  if (!finite(p.position) || !finite(p.target) || !std::isfinite(p.fov) || !std::isfinite(p.orthoSize) ||
      !std::isfinite(QuatLen(p.rotation)) || QuatLen(p.rotation)<.5f) {
    desiredActive=false;Restore();status=u8"镜头参数不是有限数值，请复位镜头调整";return;
  }
  if (lease.camera && (lease.camera!=camera || lease.session!=sample.session))
    if (!Restore()) return;
  if (!UnityObjAlive(camera)) {status=u8"等待游戏主相机";return;}
  if (!lease.camera && !Capture(camera,sample)) {status=u8"无法保存原相机状态，未接管";return;}
  if (!UnityObjAlive(lease.transform)) {Restore();status=u8"相机实例已失效";return;}
  if (!Apply(camera,lease.transform,p)) {desiredActive=false;Restore();status=u8"相机写入失败，已退出接管";return;}
  lease.lastPose=p;lease.hasPose=true;
  if(sample.sequence && sample.sequence==lastSequence.load())++repeatedFrames;
  lastSequence=sample.sequence;sourceFrame=sample.sourceFrame;
  ++applied;status=u8"镜头播放中（暂停时保持当前镜头）";
}
static void Pump(void *camera) {Pump(camera,request);}
// The native game uses instance void TailLateTick(float), including MethodInfo.
using TailFn=void(__fastcall *)(void*,float,void*);
static TailFn original=nullptr;
static void (*framePulse)() = nullptr;
static bool (*needsCamera)() = nullptr;
static void (*afterCamera)(void *) = nullptr;
static void __fastcall Tail(void *self,float dt,void *method) {
  original(self,dt,method);
  if (RuntimeClosing()) return;
  if (framePulse) framePulse();
  try {
    // Immutable body/camera sample: editor lock contention cannot delay camera
    // handoff, replacement or restoration. Leases belong only to this callback.
    auto snapshot=std::atomic_load(&published);
    Request sample=snapshot?*snapshot:Request{};
    sample.active=sample.active && desiredActive.load() && sample.session==desiredSession.load();
    void *camera=nullptr;
    Call(getMain,self,nullptr,&camera);
    Pump(camera,sample);
    // Observe the final game/MMD camera, after any playback offset is applied.
    std::unique_lock<std::recursive_mutex> lock(g_poseMutex,std::try_to_lock);
    if(lock.owns_lock() && afterCamera)afterCamera(camera);
  } catch (...) {desiredActive=false;status=u8"相机回调异常，等待恢复";}
}
static bool Signature(void *method,bool isStatic,int result,int argument=-1) {
  if (!method || !il2cpp_method_get_flags || !il2cpp_method_get_param_count ||
      !il2cpp_method_get_return_type || !il2cpp_type_get_type || !il2cpp_method_get_param) return false;
  uint32_t flags=0;
  if (bool(il2cpp_method_get_flags(method,&flags)&0x10)!=isStatic ||
      il2cpp_method_get_param_count(method)!=(argument<0?0u:1u) ||
      il2cpp_type_get_type(il2cpp_method_get_return_type(method))!=result) return false;
  return argument<0 || il2cpp_type_get_type(il2cpp_method_get_param(method,0))==argument;
}
static void *Typed(void *klass,const char *name,int result,int argument=-1) {
  if (!klass) return nullptr;
  void *iter=nullptr;
  while (void *m=il2cpp_class_get_methods(klass,&iter))
    if (!strcmp(il2cpp_method_get_name(m),name) && Signature(m,false,result,argument)) return m;
  return nullptr;
}
static void *InstanceField(void *klass,const char *name) {
  if(!klass || !il2cpp_class_get_fields || !il2cpp_field_get_name || !il2cpp_field_get_flags) return nullptr;
  void *iter=nullptr;
  while(void *field=il2cpp_class_get_fields(klass,&iter))
    if(!strcmp(il2cpp_field_get_name(field),name) && !(il2cpp_field_get_flags(field)&0x10)) return field;
  return nullptr;
}
static void InitializeFocus(void **assemblies,size_t count) {
  focusClass=nullptr;focusOffset=0;
  if(!il2cpp_field_get_type || !il2cpp_field_get_offset || !il2cpp_type_get_type ||
     !il2cpp_class_value_size || !il2cpp_class_from_type) return;
  auto dataClass=FindClass("HG.Rendering.Runtime","HGAdditionalCameraData",assemblies,count);
  auto physical=InstanceField(dataClass,"physicalParameters");
  if(!physical) return;
  auto type=il2cpp_field_get_type(physical);
  if(il2cpp_type_get_type(type)!=0x11 ||
     !MetadataClassIs(type,"HG.Rendering.Runtime","HGPhysicalCamera")) return;
  auto valueClass=il2cpp_class_from_type(type);
  auto distance=InstanceField(valueClass,"m_FocusDistance");
  if(!distance || il2cpp_type_get_type(il2cpp_field_get_type(distance))!=0xc) return;
  uint32_t alignment=0;
  auto size=il2cpp_class_value_size(valueClass,&alignment);
  auto outer=il2cpp_field_get_offset(physical),inner=il2cpp_field_get_offset(distance);
  // IL2CPP reports value-type field offsets with the boxed object header.
  if(size<4 || size>256 || outer<16 || outer>65536 || inner<16 || inner-16+4>size_t(size)) return;
  focusClass=dataClass;focusOffset=outer+inner-16;
}
static void Initialize() {
  if (ready) return;
  size_t count=0;auto assemblies=il2cpp_domain_get_assemblies(il2cpp_domain_get(),&count);
  auto manager=FindClass("Beyond.Gameplay.View","CameraManager",assemblies,count);
  auto camera=FindClass("UnityEngine","Camera",assemblies,count);
  InitializeFocus(assemblies,count);
  brainClass=FindClass("Cinemachine","CinemachineBrain",assemblies,count);
  auto behaviour=FindClass("UnityEngine","Behaviour",assemblies,count);
  getDriverEnabled=Typed(behaviour,"get_enabled",2);
  setDriverEnabled=Typed(behaviour,"set_enabled",1,2);
  auto tail=Typed(manager,"TailLateTick",1,0xc);
  getMain=Typed(manager,"get_mainCamera",0x12);
  if (getMain && !MetadataClassIs(il2cpp_method_get_return_type(getMain),"UnityEngine","Camera")) getMain=nullptr;
  getFov=Typed(camera,"get_fieldOfView",0xc);setFov=Typed(camera,"set_fieldOfView",1,0xc);
  getOrtho=Typed(camera,"get_orthographic",2);setOrtho=Typed(camera,"set_orthographic",1,2);
  getSize=Typed(camera,"get_orthographicSize",0xc);setSize=Typed(camera,"set_orthographicSize",1,0xc);
  getPhysical=Typed(camera,"get_usePhysicalProperties",2);setPhysical=Typed(camera,"set_usePhysicalProperties",1,2);
  if (!getPhysical || !setPhysical) getPhysical=setPhysical=nullptr;
  getFocal=Typed(camera,"get_focalLength",0xc);setFocal=Typed(camera,"set_focalLength",1,0xc);
  getSensor=Typed(camera,"get_sensorSize",0x11);
  getGate=Typed(camera,"get_gateFit",0x11);setGate=Typed(camera,"set_gateFit",1,0x11);
  getShift=Typed(camera,"get_lensShift",0x11);setShift=Typed(camera,"set_lensShift",1,0x11);
  auto valueIs=[](void *method,bool setter,const char *name){
    return method && MetadataClassIs(setter?il2cpp_method_get_param(method,0):il2cpp_method_get_return_type(method),"UnityEngine",name);
  };
  if(!valueIs(getSensor,false,"Vector2") || !valueIs(getShift,false,"Vector2") ||
     !valueIs(setShift,true,"Vector2") || !valueIs(getGate,false,"GateFitMode") || !valueIs(setGate,true,"GateFitMode"))
    getSensor=getShift=setShift=getGate=setGate=nullptr;
  bool api=getMain&&getFov&&setFov&&getOrtho&&setOrtho&&getSize&&setSize&&
      g_transform_set_position&&g_transform_set_rotation&&g_transform_get_localPosition&&
      g_transform_get_localRotation&&g_transform_set_localPosition&&g_transform_set_localRotation;
  ready=api && tail && Hook(tail,"CameraManager.TailLateTick",(void*)Tail,(void**)&original);
  status=ready?u8"镜头接口已就绪":u8"游戏相机接口不兼容，镜头接管不可用";
  Log("[MMD-CAMERA] ready=%d api=%d tail=%p main=%p physical=%d autofocus=%d",ready,api,tail,getMain,getPhysical!=nullptr,focusOffset!=0);
}
} // namespace mmd_camera
