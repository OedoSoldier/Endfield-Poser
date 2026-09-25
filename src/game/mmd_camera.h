#pragma once
#include "core/frame_driver.h"
#include "core/game_hooks.h"
#include "math/mmd_camera.h"
#include <string>

// Camera objects are only read/written in CameraManager's verified game callback.
// Playback/editor threads publish a pose under g_poseMutex and never call Unity.
namespace mmd_camera {
struct Request {
  bool active = false;
  uint64_t session = 0;
  void *actor = nullptr;
  mmd::CameraPose pose;
};
static Request request;
static std::atomic<bool> desiredActive{false};
static std::atomic<uint64_t> desiredSession{0};
static void Publish(const Request &next) {
  request=next;desiredSession.store(next.session);desiredActive.store(next.active);
}
static void Stop() {desiredActive.store(false);request.active=false;}
static uint64_t nextSession = 0, applied = 0, callbacks = 0;
static double lastCallback = -1e30;
static std::string status = u8"镜头未启用";
static void *getMain = nullptr, *getFov = nullptr, *setFov = nullptr,
            *getOrtho = nullptr, *setOrtho = nullptr, *getSize = nullptr, *setSize = nullptr,
            *getPhysical = nullptr, *setPhysical = nullptr;
static bool ready = false;
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
static void ReleaseRefs() {
  if (il2cpp_gchandle_free) {
    if (lease.cameraRef) il2cpp_gchandle_free(lease.cameraRef);
    if (lease.transformRef) il2cpp_gchandle_free(lease.transformRef);
  }
  lease={};
}
static bool Restore() {
  if (!lease.camera) return true;
  bool ok=true;
  if (UnityObjAlive(lease.camera)) {
    // Physical mode may recalculate FOV from focal length. Restore it first.
    if (setPhysical) ok=Write(setPhysical,lease.camera,lease.physical)&&ok;
    ok=Write(setOrtho,lease.camera,lease.ortho)&&ok;
    ok=Write(setFov,lease.camera,lease.fov)&&ok;
    ok=Write(setSize,lease.camera,lease.size)&&ok;
  }
  if (UnityObjAlive(lease.transform)) {
    ok=Write(g_transform_set_localPosition,lease.transform,lease.position)&&ok;
    ok=Write(g_transform_set_localRotation,lease.transform,lease.rotation)&&ok;
  }
  if (ok) ReleaseRefs();
  else status=u8"等待恢复原相机设置";
  return ok;
}
static bool Capture(void *camera) {
  if (!UnityObjAlive(camera) || !il2cpp_gchandle_new || !il2cpp_gchandle_free) return false;
  void *transform=nullptr;
  if (!Call(g_component_get_transform,camera,nullptr,&transform) || !UnityObjAlive(transform)) return false;
  Lease saved;
  saved.camera=camera;saved.transform=transform;saved.session=request.session;
  if (!Read(g_transform_get_localPosition,transform,saved.position) ||
      !Read(g_transform_get_localRotation,transform,saved.rotation) ||
      !Read(getFov,camera,saved.fov) || !Read(getOrtho,camera,saved.ortho) ||
      !Read(getSize,camera,saved.size) ||
      (getPhysical && !Read(getPhysical,camera,saved.physical))) return false;
  if (!std::isfinite(saved.fov) || !std::isfinite(saved.size)) return false;
  saved.cameraRef=il2cpp_gchandle_new(camera,false);
  saved.transformRef=il2cpp_gchandle_new(transform,false);
  lease=saved;
  if (!saved.cameraRef || !saved.transformRef) {ReleaseRefs();return false;}
  Log("[MMD-CAMERA] acquired camera=%p session=%llu",camera,(unsigned long long)saved.session);
  return true;
}
static bool Apply(void *camera,void *transform,const mmd::CameraPose &p) {
  bool ok=true;
  if (setPhysical) ok=Write(setPhysical,camera,false)&&ok;
  ok=Write(setOrtho,camera,!p.perspective)&&ok;
  ok=Write(setFov,camera,p.fov)&&ok;
  ok=Write(setSize,camera,p.orthoSize)&&ok;
  ok=Write(g_transform_set_position,transform,p.position)&&ok;
  ok=Write(g_transform_set_rotation,transform,p.rotation)&&ok;
  return ok;
}
static void Pump(void *camera) {
  ++callbacks;lastCallback=FrameNow();
  bool active=request.active && !CharacterSwitchInProgress() &&
              request.actor==g_charAnimator && UnityObjAlive(request.actor);
  if (!active) {
    if (Restore()) status=u8"镜头已停止，原相机已恢复";
    return;
  }
  const auto &p=request.pose;
  auto finite=[](Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
  if (!finite(p.position) || !std::isfinite(p.fov) || !std::isfinite(p.orthoSize) ||
      !std::isfinite(QuatLen(p.rotation)) || QuatLen(p.rotation)<.5f) {
    Stop();Restore();status=u8"镜头参数不是有限数值，请复位镜头调整";return;
  }
  if (lease.camera && (lease.camera!=camera || lease.session!=request.session))
    if (!Restore()) return;
  if (!UnityObjAlive(camera)) {status=u8"等待游戏主相机";return;}
  if (!lease.camera && !Capture(camera)) {status=u8"无法保存原相机状态，未接管";return;}
  if (!UnityObjAlive(lease.transform)) {Restore();status=u8"相机实例已失效";return;}
  if (!Apply(camera,lease.transform,p)) {Stop();Restore();status=u8"相机写入失败，已退出接管";return;}
  lease.lastPose=p;lease.hasPose=true;
  ++applied;status=u8"镜头播放中（暂停时保持当前镜头）";
}
// The native game uses instance void TailLateTick(float), including MethodInfo.
using TailFn=void(__fastcall *)(void*,float,void*);
static TailFn original=nullptr;
static void __fastcall Tail(void *self,float dt,void *method) {
  original(self,dt,method);
  std::unique_lock<std::recursive_mutex> lock(g_poseMutex,std::try_to_lock);
  if (!lock.owns_lock()) {
    // The editor may hold the pose lock while drawing. Reapply the last camera
    // frame instead of flashing the game's camera for one frame. Lease changes
    // occur only in this game callback; do not inspect mutable editor settings.
    if (desiredActive.load() && lease.hasPose && lease.session==desiredSession.load() &&
        !CharacterSwitchInProgress() && UnityObjAlive(lease.camera) && UnityObjAlive(lease.transform)) {
      void *camera=nullptr;
      if (Call(getMain,self,nullptr,&camera) && camera==lease.camera && desiredActive.load())
        Apply(camera,lease.transform,lease.lastPose);
    }
    return;
  }
  try {
    void *camera=nullptr;
    if (request.active) Call(getMain,self,nullptr,&camera);
    Pump(camera);
  } catch (...) {Stop();status=u8"相机回调异常，等待恢复";}
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
static void Initialize() {
  if (ready) return;
  size_t count=0;auto assemblies=il2cpp_domain_get_assemblies(il2cpp_domain_get(),&count);
  auto manager=FindClass("Beyond.Gameplay.View","CameraManager",assemblies,count);
  auto camera=FindClass("UnityEngine","Camera",assemblies,count);
  auto tail=Typed(manager,"TailLateTick",1,0xc);
  getMain=Typed(manager,"get_mainCamera",0x12);
  if (getMain && !MetadataClassIs(il2cpp_method_get_return_type(getMain),"UnityEngine","Camera")) getMain=nullptr;
  getFov=Typed(camera,"get_fieldOfView",0xc);setFov=Typed(camera,"set_fieldOfView",1,0xc);
  getOrtho=Typed(camera,"get_orthographic",2);setOrtho=Typed(camera,"set_orthographic",1,2);
  getSize=Typed(camera,"get_orthographicSize",0xc);setSize=Typed(camera,"set_orthographicSize",1,0xc);
  getPhysical=Typed(camera,"get_usePhysicalProperties",2);setPhysical=Typed(camera,"set_usePhysicalProperties",1,2);
  if (!getPhysical || !setPhysical) getPhysical=setPhysical=nullptr;
  bool api=getMain&&getFov&&setFov&&getOrtho&&setOrtho&&getSize&&setSize&&
      g_transform_set_position&&g_transform_set_rotation&&g_transform_get_localPosition&&
      g_transform_get_localRotation&&g_transform_set_localPosition&&g_transform_set_localRotation;
  ready=api && tail && Hook(tail,"CameraManager.TailLateTick",(void*)Tail,(void**)&original);
  status=ready?u8"镜头接口已就绪":u8"游戏相机接口不兼容，镜头接管不可用";
  Log("[MMD-CAMERA] ready=%d api=%d tail=%p main=%p physical=%d",ready,api,tail,getMain,getPhysical!=nullptr);
}
} // namespace mmd_camera
