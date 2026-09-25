#pragma once
// Coordinate externally posed Transforms with the game's own BBC solver.
// No solver jobs, particle arrays or Animator RW buffers are modified here.
#include "core/frame_driver.h"

static std::atomic<bool> g_bbcRequested{false};
static bool g_bbcSyncEnabled=true, g_bbcHookReady=false;
static thread_local bool g_bbcInCallback=false;
static void *g_bbcCrossFrameField=nullptr,*g_bbcSetCrossFrame=nullptr;
static void *g_bbcAnimatorField=nullptr,*g_bbcComplete=nullptr;
static bool g_bbcSaved=false,g_bbcOriginalCrossFrame=false;
static bool g_bbcOriginalAnimator=false;
static bool g_bbcDirectTransforms=false;
static const char *g_bbcStatus=u8"等待 BBC 原生更新";
static uint64_t g_bbcFrames=0;
static void (*g_bbcPreparePose)(bool)=nullptr;
static void (*g_bbcObserve)()=nullptr;
static void (*g_bbcMaintenance)(bool)=nullptr;
static bool g_bbcMaintenancePending=false; // protected by g_poseMutex
using BbcUpdateFn=void(__fastcall *)(void *,void *);
static BbcUpdateFn g_bbcOriginalUpdate=nullptr;

static bool BbcMethodSignature(void *m,bool isStatic,int argumentKind=-1) {
  if(!m || !il2cpp_method_get_flags || !il2cpp_method_get_param_count ||
     !il2cpp_method_get_return_type || !il2cpp_type_get_type || !il2cpp_method_get_param) return false;
  if(bool(il2cpp_method_get_flags(m,nullptr)&0x10)!=isStatic ||
     il2cpp_method_get_param_count(m)!=(argumentKind<0?0u:1u)) return false;
  void *ret=il2cpp_method_get_return_type(m);
  if(!ret || il2cpp_type_get_type(ret)!=1) return false;
  void *arg=argumentKind<0?nullptr:il2cpp_method_get_param(m,0);
  return argumentKind<0 || (arg && il2cpp_type_get_type(arg)==argumentKind);
}
static void *BbcFindMethod(void *cls,const char *name,bool isStatic,int kind=-1) {
  if(!cls || !il2cpp_class_get_methods || !il2cpp_method_get_name) return nullptr;
  void *iter=nullptr,*m;
  while((m=il2cpp_class_get_methods(cls,&iter))) {
    const char *n=il2cpp_method_get_name(m);
    if(n && !strcmp(n,name) && BbcMethodSignature(m,isStatic,kind)) return m;
  }
  return nullptr;
}
static bool BbcReadFlag(void *field,bool &value) {
  if(!field || !il2cpp_field_static_get_value) return false;
  __try {
    unsigned char v=2;il2cpp_field_static_get_value(field,&v);
    if(v>1) return false;
    value=v!=0;return true;
  } __except(1) {return false;}
}
static bool BbcReadCrossFrame(bool &value) {return BbcReadFlag(g_bbcCrossFrameField,value);}
static bool BbcSetAnimator(bool value) {
  if(!g_bbcAnimatorField || !il2cpp_field_static_set_value)return false;
  __try {
    unsigned char v=value?1:0;
    il2cpp_field_static_set_value(g_bbcAnimatorField,&v);
    bool actual=false;return BbcReadFlag(g_bbcAnimatorField,actual) && actual==value;
  } __except(1) {return false;}
}
static bool BbcComplete(void *self) {
  if(!self || !g_bbcComplete || !il2cpp_runtime_invoke)return false;
  __try {
    void *exc=nullptr;il2cpp_runtime_invoke(g_bbcComplete,self,nullptr,&exc);return !exc;
  } __except(1) {return false;}
}
static bool BbcSetCrossFrame(bool value) {
  if(!g_bbcSetCrossFrame || !il2cpp_runtime_invoke) return false;
  __try {
    unsigned char v=value?1:0;void *args[]={&v},*exc=nullptr;
    // The native setter completes the previous master job before switching.
    il2cpp_runtime_invoke(g_bbcSetCrossFrame,nullptr,args,&exc);
    bool actual=false;
    return !exc && BbcReadCrossFrame(actual) && actual==value;
  } __except(1) {return false;}
}
static bool BbcSynchronizeMode(bool wanted) {
  bool current=false,animator=false;
  if(!BbcReadCrossFrame(current) || !BbcReadFlag(g_bbcAnimatorField,animator)) {
    g_bbcDirectTransforms=false;g_bbcStatus=u8"无法读取 BBC 完整调度状态";return false;
  }
  if(wanted) {
    if(!g_bbcSaved) {
      g_bbcOriginalCrossFrame=current;g_bbcOriginalAnimator=animator;g_bbcSaved=true;
      Log("[BBC] native Transform playback requested; original cross-frame=%d animator-buffer=%d",int(current),int(animator));
    }
    if(current && !BbcSetCrossFrame(false)) {
      g_bbcDirectTransforms=false;g_bbcStatus=u8"BBC 原生同步接口未生效";return false;
    }
    // UseAnimatorTransform also gates OnEarlyClothUpdate.RestoreTransform.
    // Leaving it enabled feeds last frame's simulated output back as the next
    // animation/rest pose, even though ClothUpdate now reads Transforms.
    if(animator && !BbcSetAnimator(false)) {
      g_bbcDirectTransforms=false;g_bbcStatus=u8"BBC 骨骼恢复模式切换失败";return false;
    }
    g_bbcDirectTransforms=true;
    g_bbcStatus=u8"BBC 读取当前骨骼，同帧模拟与写回";
    return true;
  }
  if(g_bbcSaved) {
    if(animator!=g_bbcOriginalAnimator && !BbcSetAnimator(g_bbcOriginalAnimator)) {
      g_bbcStatus=u8"等待恢复 BBC 动画缓冲模式";return false;
    }
    if(current!=g_bbcOriginalCrossFrame && !BbcSetCrossFrame(g_bbcOriginalCrossFrame)) {
      g_bbcStatus=u8"等待恢复 BBC 调度";return false;
    }
    Log("[BBC] native scheduling restored: cross-frame=%d animator-buffer=%d",int(g_bbcOriginalCrossFrame),int(g_bbcOriginalAnimator));
    g_bbcSaved=false;
  }
  g_bbcDirectTransforms=false;g_bbcStatus=u8"使用游戏原 BBC 调度";
  return true;
}
static void __fastcall BbcClothUpdate(void *self,void *method) {
  // Keep the original call exactly once on every path. Editor contention must
  // not block the game; callbacks run only inside the native simulation phase.
  if(g_bbcInCallback) {if(g_bbcOriginalUpdate)g_bbcOriginalUpdate(self,method);return;}
  std::unique_lock<std::recursive_mutex> lock(g_poseMutex,std::try_to_lock);
  if(!lock.owns_lock()) {if(g_bbcOriginalUpdate)g_bbcOriginalUpdate(self,method);return;}
  struct Scope {Scope(){g_bbcInCallback=true;}~Scope(){g_bbcInCallback=false;}} scope;
  bool wanted=g_bbcHookReady && g_bbcSyncEnabled && g_bbcRequested.load() && g_frameRunning.load();
  bool synced=false;
  try {
    // Fence even on the first frame and when releasing per-character weights.
    // Never change native lists/flags while the previous job is still running.
    bool work=wanted || g_bbcSaved || g_bbcMaintenancePending;
    bool safe=!work || BbcComplete(self);
    if(work && !safe)g_bbcStatus=u8"等待 BBC 原生任务完成，未修改物理状态";
    if(safe && wanted) synced=BbcSynchronizeMode(true);
    if(synced) {
      int frame=ReadUnityFrameCount();
      bool stepped=frame>=0 && RunFrameTick(true,frame,3);
      if(g_bbcPreparePose) g_bbcPreparePose(stepped);
      // Stop/switch may have been processed by the frame tick itself.
    }
    bool keep=synced && g_bbcRequested.load() && g_bbcSyncEnabled && g_frameRunning.load();
    if(safe && g_bbcMaintenance)g_bbcMaintenance(keep);
    if(safe && !keep && g_bbcSaved)BbcSynchronizeMode(false);
  } catch(...) {g_bbcStatus=u8"BBC 同步异常，保留原生模拟";}
  if(g_bbcOriginalUpdate) g_bbcOriginalUpdate(self,method);
  if(synced) {
    ++g_bbcFrames;
    try {if(g_bbcObserve)g_bbcObserve();} catch(...) {}
  }
}
static void InstallBbcFrameHook() {
  if(!il2cpp_class_get_fields || !il2cpp_field_get_name || !il2cpp_field_get_flags ||
     !il2cpp_field_get_type || !il2cpp_type_get_type || !il2cpp_field_static_get_value ||
     !il2cpp_field_static_set_value) return;
  size_t count=0;void **assemblies=il2cpp_domain_get_assemblies(il2cpp_domain_get(),&count);
  void *manager=FindClass("BeyondDynamicBone","MagicaManager",assemblies,count);
  void *cloth=FindClass("BeyondDynamicBone","ClothManager",assemblies,count);
  g_bbcSetCrossFrame=BbcFindMethod(manager,"SetUseCrossFrameJob",true,2);
  g_bbcComplete=BbcFindMethod(cloth,"CompleteMasterJob",false);
  void *update=BbcFindMethod(cloth,"ClothUpdate",false);
  void *it=nullptr,*field;
  while(manager && (field=il2cpp_class_get_fields(manager,&it))) {
    const char *name=il2cpp_field_get_name(field);
    void *type=il2cpp_field_get_type(field);
    int flags=il2cpp_field_get_flags(field);
    if(name && (flags&0x10) && !(flags&0x60) && type && il2cpp_type_get_type(type)==2) {
      if(!strcmp(name,"UseCrossFrameJob"))g_bbcCrossFrameField=field;
      if(!strcmp(name,"UseAnimatorTransform"))g_bbcAnimatorField=field;
    }
  }
  g_bbcHookReady=g_bbcSetCrossFrame && g_bbcCrossFrameField && g_bbcAnimatorField && g_bbcComplete && g_frameCountMethod &&
      Hook(update,"BBC.ClothManager.ClothUpdate",(void*)BbcClothUpdate,(void**)&g_bbcOriginalUpdate);
  if(!g_bbcHookReady)g_bbcStatus=u8"当前 BBC 接口不支持同帧接入，保持游戏原设置";
  Log("[BBC] native simulation bridge ready=%d",int(g_bbcHookReady));
}
