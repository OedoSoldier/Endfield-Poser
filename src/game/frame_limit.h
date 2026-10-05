#pragma once
#include "game/mmd_runtime_api.h"
#include <atomic>

// Requested from the panel; engine settings are read/written only by the game
// maintenance callback, including when the overlay or plugin is disabled.
namespace poser_frame_limit {
static std::atomic<int> requested{0};
static bool held=false;
static int savedTarget=-1,savedVsync=0,lastRequest=-1;
static double nextCheck=0;
static const char *status=u8"跟随游戏设置";

struct Property {
  using Get=int(__cdecl*)();
  using Set=void(__cdecl*)(int);
  void *get=nullptr,*set=nullptr;
  Get nativeGet=nullptr;
  Set nativeSet=nullptr;
  bool ready() const {return (get||nativeGet)&&(set||nativeSet);}
  bool read(int &value) const {
    if(RuntimeClosing())return false;
    if(get)return mmd_api::Value(get,nullptr,value);
    __try {if(!nativeGet)return false;value=nativeGet();return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
  bool write(int value) const {
    if(RuntimeClosing())return false;
    if(set){void *r=nullptr,*args[]={&value};return mmd_api::Call(set,nullptr,args,r);}
    __try {if(!nativeSet)return false;nativeSet(value);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
  }
};
static Property target,vsync;
inline void Resolve(Property &p,const char *type,const char *getter,const char *setter,
                    const char *nativeGetter,const char *nativeSetter) {
  if(p.ready())return;
  auto cls=mmd_api::Class("UnityEngine",type);
  p.get=mmd_api::Method(cls,getter,"System.Int32",{},true);
  p.set=mmd_api::Method(cls,setter,"System.Void",{"System.Int32"},true);
  // Stripped managed accessors may still expose Unity's registered internal
  // calls. Both are static scalar-int APIs; never use version-specific offsets.
  if(il2cpp_resolve_icall) {
    if(!p.get)p.nativeGet=reinterpret_cast<Property::Get>(il2cpp_resolve_icall(nativeGetter));
    if(!p.set)p.nativeSet=reinterpret_cast<Property::Set>(il2cpp_resolve_icall(nativeSetter));
  }
}
inline bool Ready() {
  Resolve(target,"Application","get_targetFrameRate","set_targetFrameRate",
          "UnityEngine.Application::get_targetFrameRate","UnityEngine.Application::set_targetFrameRate");
  Resolve(vsync,"QualitySettings","get_vSyncCount","set_vSyncCount",
          "UnityEngine.QualitySettings::get_vSyncCount","UnityEngine.QualitySettings::set_vSyncCount");
  return target.ready()&&vsync.ready();
}
inline void Request(int fps) {requested.store(fps==30||fps==60?fps:0);}
inline void Tick(double now,bool allowed) {
  if(RuntimeClosing())return;
  const int want=allowed?requested.load():0;
  if(want==lastRequest&&now<nextCheck)return;
  lastRequest=want;nextCheck=now+.5;
  if(!want&&!held){status=u8"跟随游戏设置";return;}
  if(!Ready()){status=u8"帧率接口暂不可用";return;}
  int actual=0,sync=0;
  if(!target.read(actual)||!vsync.read(sync)){status=u8"无法读取帧率设置，稍后重试";return;}
  if(!held) {
    savedTarget=actual;savedVsync=sync;held=true;
    Log("[FPS] acquired target=%d vsync=%d",savedTarget,savedVsync);
  }
  const int targetValue=want?want:savedTarget,syncValue=want?0:savedVsync;
  // Desktop Unity ignores targetFrameRate while vSyncCount != 0. Restore both
  // captured settings on Off/hot-disable/tool close, without touching timeScale.
  bool ok=true;
  if(actual!=targetValue)ok=target.write(targetValue)&&ok;
  if(sync!=syncValue)ok=vsync.write(syncValue)&&ok;
  ok=target.read(actual)&&ok;ok=vsync.read(sync)&&ok;
  if(!ok||actual!=targetValue||sync!=syncValue) {
    status=want?u8"正在应用限帧设置…":u8"正在恢复游戏帧率…";return;
  }
  if(!want){held=false;status=u8"跟随游戏设置";Log("[FPS] restored target=%d vsync=%d",actual,sync);}
  else status=want==30?u8"目标 30 FPS":u8"目标 60 FPS";
}
} // namespace poser_frame_limit
