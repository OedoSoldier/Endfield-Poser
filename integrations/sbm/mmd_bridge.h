#pragma once
#include <windows.h>
#include <array>
#include <unordered_map>
#if __has_include("sbm_mmd_api.h")
#include "sbm_mmd_api.h"
#else
#include "integrations/sbm_mmd_api.h"
#endif
// Included by SBM's config_loader.h after config_types.h. The immutable copied
// profile table never exposes ConfigSnapshot pointers to another DLL.
namespace sbm_mmd_bridge {
static SRWLOCK profilesLock=SRWLOCK_INIT,writerLock=SRWLOCK_INIT;
static std::unordered_map<std::string,sbm_mmd::Profile> profiles;
struct Lease {void *actor=nullptr;uint64_t token=0;};
static std::array<Lease,4> leases;
static uint64_t generation=0;
inline void Publish(const ConfigSnapshot &cfg) {
  std::unordered_map<std::string,sbm_mmd::Profile> next;
  for(const auto &entry:cfg.characters) {
    const auto &p=entry.second;sbm_mmd::Profile q;
    q.enabled=cfg.pluginEnabled&&p.enabled&&p.motionMode!=MotionMode::Off;
    q.allowFallback=p.bones.allowFallbackCandidates;q.explicitAxis=p.axisExplicit;
    q.axis=uint32_t(p.axis.axis);q.sign=p.axisExplicit?p.axis.sign:1.f;
    q.scale=p.amplitudeScale;q.frequency=2.5f;
    if(p.bones.rightName.size()>=sizeof(q.right)||p.bones.leftName.size()>=sizeof(q.left))continue;
    strcpy_s(q.right,p.bones.rightName.c_str());strcpy_s(q.left,p.bones.leftName.c_str());
    next.emplace(entry.first,q);
  }
  AcquireSRWLockExclusive(&profilesLock);profiles.swap(next);ReleaseSRWLockExclusive(&profilesLock);
}
inline int __cdecl Profile(const char *model,sbm_mmd::Profile *out) {
  if(!model||!out||out->size!=sizeof(*out))return -1;
  if(!TryAcquireSRWLockShared(&profilesLock))return -1;
  auto it=profiles.find(model);int result=it!=profiles.end();
  if(result)*out=it->second;
  ReleaseSRWLockShared(&profilesLock);return result;
}
inline int __cdecl Acquire(void *actor,uint64_t token) {
  if(!actor||!token||!TryAcquireSRWLockExclusive(&writerLock))return 0;
  int result=0;
  for(auto &l:leases)if(l.actor==actor) {result=l.token==token;ReleaseSRWLockExclusive(&writerLock);return result;}
  for(auto &l:leases)if(!l.actor) {l={actor,token};++generation;result=1;break;}
  ReleaseSRWLockExclusive(&writerLock);return result;
}
inline void __cdecl Release(void *actor,uint64_t token) {
  AcquireSRWLockExclusive(&writerLock);
  for(auto &l:leases)if(l.actor==actor&&l.token==token) {l={};++generation;break;}
  ReleaseSRWLockExclusive(&writerLock);
}
// Writer exclusion includes target construction AND replay, so a callback
// already in progress finishes before Poser obtains ownership. No Unity calls
// or file operations in exported API functions. No lease timeout that could
// silently re-enable native writes while the player is paused.
struct NativeWriter {
  bool locked=TryAcquireSRWLockExclusive(&writerLock)!=FALSE;
  ~NativeWriter(){if(locked)ReleaseSRWLockExclusive(&writerLock);}
  bool owned(void *actor)const {if(!actor)return false;for(const auto &l:leases)if(l.actor==actor)return true;return false;}
};
}
extern "C" __declspec(dllexport) int __cdecl SBM_MmdGetApi(uint32_t version,uint32_t size,sbm_mmd::Api *out) {
  if(!out||version!=sbm_mmd::Version||size!=sizeof(*out))return 0;
  *out={};out->profile=sbm_mmd_bridge::Profile;out->acquire=sbm_mmd_bridge::Acquire;out->release=sbm_mmd_bridge::Release;return 1;
}
