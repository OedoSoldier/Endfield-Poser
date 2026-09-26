#pragma once
#include "core/game_hooks.h"
#include "core/frame_driver.h"
#include "math/eye_gaze.h"

// All access is under g_poseMutex, in the existing SMC/camera game callbacks.
// UI only changes settings. No camera objects or cross-actor commands are kept.
namespace poser_gaze {
static eye_gaze::Settings settings;
struct Binding {
  void *owner=nullptr,*head=nullptr,*eyes[2]={},*parents[2]={};
  uint64_t generation=0;
  eye_gaze::Basis basis;
  Quat eyeInHead[2];
  bool opticalReady=false;
};
static Binding binding;
struct Lease {bool active=false;uint32_t owner=0,eyes[2]={};Quat original[2];};
static Lease lease;
static Vec3 cameraPosition;
static double cameraTime=-1e30;
static const char *status=u8"跟随游戏或 MMD 眼神";
static bool Read(void *method,void *object,void *value,size_t size) {
  if(!method||!UnityObjAlive(object)||RuntimeClosing())return false;
  __try {
    void *error=nullptr,*box=il2cpp_runtime_invoke(method,object,nullptr,&error);
    if(error||!box)return false;memcpy(value,(char*)box+16,size);return true;
  } __except(1){return false;}
}
static bool Write(void *object,Quat q) {
  if(!g_transform_set_localRotation||!UnityObjAlive(object)||RuntimeClosing())return false;
  __try {void *error=nullptr,*args[]={&q};il2cpp_runtime_invoke(g_transform_set_localRotation,object,args,&error);return !error;}
  __except(1){return false;}
}
static bool Owned() {
  if(!lease.active||RuntimeClosing()||!il2cpp_gchandle_get_target)return false;
  __try {return il2cpp_gchandle_get_target(lease.owner)==binding.owner&&UnityObjAlive(binding.owner);}
  __except(1){return false;}
}
static void Release(bool restore=true,const Quat *fallback=nullptr) {
  if(restore&&Owned())for(int i=0;i<2;++i)
    if(il2cpp_gchandle_get_target(lease.eyes[i])==binding.eyes[i])Write(binding.eyes[i],fallback?fallback[i]:lease.original[i]);
  if(!RuntimeClosing()&&il2cpp_gchandle_free) {
    for(auto ref:lease.eyes)if(ref)il2cpp_gchandle_free(ref);
    if(lease.owner)il2cpp_gchandle_free(lease.owner);
  }
  lease={};
}
static void Reset(bool restore=true) {
  Release(restore);binding={};settings={};cameraTime=-1e30;status=u8"跟随游戏或 MMD 眼神";
}
static void SetCamera(void *camera) {
  cameraTime=-1e30;
  if(settings.mode!=eye_gaze::Mode::Camera||!UnityObjAlive(camera)||!g_component_get_transform)return;
  __try {
    void *error=nullptr,*t=il2cpp_runtime_invoke(g_component_get_transform,camera,nullptr,&error);
    if(!error&&Read(g_transform_get_position,t,&cameraPosition,sizeof(cameraPosition))&&eye_gaze::Finite(cameraPosition))cameraTime=FrameNow();
  } __except(1){}
}
static bool Capture() {
  if(!il2cpp_gchandle_new||!il2cpp_gchandle_free||!il2cpp_gchandle_get_target)return false;
  for(int i=0;i<2;++i)if(!Read(g_transform_get_localRotation,binding.eyes[i],&lease.original[i],sizeof(Quat)))return false;
  lease.owner=il2cpp_gchandle_new(binding.owner,false);
  for(int i=0;i<2;++i)lease.eyes[i]=il2cpp_gchandle_new(binding.eyes[i],false);
  lease.active=lease.owner&&lease.eyes[0]&&lease.eyes[1];
  if(!lease.active)Release(false);
  return lease.active;
}
static void Update(bool allowed,uint64_t generation,const Quat *fallback=nullptr) {
  if(RuntimeClosing()){Release(false);return;}
  if(CharacterSwitchInProgress())return;
  if(binding.owner!=g_charAnimator||binding.generation!=generation) {Reset();return;}
  if(settings.mode==eye_gaze::Mode::Follow||!allowed) {
    Release(true,fallback);status=allowed?u8"跟随游戏或 MMD 眼神":u8"冻结角色或播放动作后生效";return;
  }
  if(!binding.basis.ready||!UnityObjAlive(binding.owner)||!UnityObjAlive(binding.head)) {
    Release();status=u8"当前角色的眼睛控制尚未就绪";return;
  }
  for(int i=0;i<2;++i)if(!UnityObjAlive(binding.eyes[i])||!UnityObjAlive(binding.parents[i])) {Reset(false);return;}
  if(settings.mode==eye_gaze::Mode::Camera&&!binding.opticalReady) {
    Release(true,fallback);status=u8"缺少虹膜参考点，请使用手动方向";return;
  }
  if(settings.mode==eye_gaze::Mode::Camera&&FrameNow()-cameraTime>.5) {
    Release(true,fallback);status=u8"等待当前镜头，暂时跟随原眼神";return;
  }
  Quat head,parents[2];Vec3 points[2];
  if(!Read(g_transform_get_rotation,binding.head,&head,sizeof(head)))return;
  for(int i=0;i<2;++i)if(!Read(g_transform_get_rotation,binding.parents[i],&parents[i],sizeof(Quat))||
      !Read(g_transform_get_position,binding.eyes[i],&points[i],sizeof(Vec3)))return;
  if(!std::isfinite(QuatLen(head))||QuatLen(head)<.5f)return;
  for(int i=0;i<2;++i)if(!std::isfinite(QuatLen(parents[i]))||QuatLen(parents[i])<.5f||!eye_gaze::Finite(points[i]))return;
  if(lease.active&&!Owned()){Reset(false);return;}
  if(!lease.active&&!Capture()){status=u8"无法保存眼睛原始方向，未接管";return;}
  auto inverseHead=Conj(NormQ(head));auto center=(points[0]+points[1])*.5f;
  auto direction=inverseHead*(cameraPosition-center);
  Vec3 offsets[2]={inverseHead*(points[0]-center),inverseHead*(points[1]-center)};
  Quat eyeDelta[2];eye_gaze::AimEyes(binding.basis,settings,direction,offsets,eyeDelta);
  for(int i=0;i<2;++i) {
    auto q=NormQ(Conj(NormQ(parents[i]))*NormQ(head)*eyeDelta[i]*binding.eyeInHead[i]);
    if(!Write(binding.eyes[i],q)){Release(true,fallback);status=u8"眼睛写入失败，已恢复";return;}
  }
  status=settings.mode==eye_gaze::Mode::Manual?u8"手动眼睛方向（覆盖 MMD 眼神）":u8"自动看向当前镜头（覆盖 MMD 眼神）";
}
} // namespace poser_gaze
