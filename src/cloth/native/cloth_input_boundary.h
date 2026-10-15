#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace poser_cloth_boundary {
struct CodeRange { size_t offset, bytes; uint64_t hash; };
// Audited against the 1.6.3 installed PE and its chained unwind ranges.
// Required creation/retirement contracts are certified separately from optional
// injected solver/Burst jobs. Runtime signatures, layouts and code still gate use.
inline constexpr bool RequiredChain163Certified = true;
inline constexpr CodeRange Update163[]{ {0,4485,0x53fe3b53da6b6c60ULL} };
inline constexpr CodeRange Read163[]{
  {0,610,0x15d819eb22942d9eULL},{0x2011186,52,0xdaff3cddf045a23cULL} };
inline constexpr CodeRange Write163[]{
  {0,514,0xafca75842a03973aULL},{0x2010e12,52,0xa8df833056ef6873ULL} };
inline constexpr CodeRange Valid163[]{
  {0,693,0x96fa8fb34546dcd0ULL},{0x2011366,223,0x6c2b591d3bb81262ULL} };
inline constexpr CodeRange TeamUpdate163[]{ {0,12444,0x32bd2eca3bb98ea1ULL} };
inline constexpr CodeRange GetTeam163[]{ {0,277,0x016c2ece065e5b2eULL} };
inline constexpr CodeRange GetBone163[]{
  {0,287,0x419c6e8b0ebf87f8ULL},{0x1e39c4a,7,0x324c4a511fec1755ULL} };
inline constexpr size_t EarlyTeam163=0xf3, Bone163=0x121,
    CompletedTeam163=0x574, BeforeTeam163=0x1e7;

inline bool DirectCallAt(const unsigned char *code,size_t bytes,size_t offset,const void *target) {
  if(!code||!target||offset>bytes||bytes-offset<5||code[offset]!=0xe8)return false;
  int32_t relative=0;std::memcpy(&relative,code+offset+1,4);
  return uintptr_t(code+offset+5)+intptr_t(relative)==uintptr_t(target);
}
inline unsigned DirectCallCount(const unsigned char *code,size_t bytes,const void *target) {
  unsigned count=0;
  if(code&&target)for(size_t offset=0;offset+5<=bytes;++offset)
    if(DirectCallAt(code,bytes,offset,target))++count;
  return count;
}
struct HookFunctions {
  int (*create)(void*,void*,void**);
  int (*queueEnable)(void*);
  int (*apply)();
  int (*disable)(void*);
  int (*remove)(void*);
};
inline bool InstallHooks(void *const *targets,void *const *detours,void ***originals,size_t count,
                         const HookFunctions &api,unsigned &rollbackFailed) {
  rollbackFailed=0;size_t created=0;
  for(;created<count;++created)if(api.create(targets[created],detours[created],originals[created])!=0)break;
  bool ok=created==count;
  if(ok)for(size_t n=0;n<count;++n)if(api.queueEnable(targets[n])!=0){ok=false;break;}
  if(ok)ok=api.apply()==0;
  if(ok)return true;
  for(size_t n=0;n<created;++n) {
    api.disable(targets[n]);
    if(api.remove(targets[n])==0)*originals[n]=nullptr;
    else ++rollbackFailed; // Keep the trampoline valid if a detour remains active.
  }
  return false;
}
struct Identity { void *cloth=nullptr,*team=nullptr,*bone=nullptr; };
struct Context {
  Identity observed{};
  bool entered=false,observing=false,consumed=false;
  void Begin(void *cloth) { *this={};observed.cloth=cloth;entered=cloth!=nullptr; }
  void End() { *this={}; }
  void RecordTeam(void *team) { if(entered&&!observing&&!consumed)observed.team=team; }
  void RecordBone(void *bone) { if(entered&&!observing&&!consumed)observed.bone=bone; }
  bool IdentityMatches(Identity current,void *team) const {
    return entered&&!observing&&!consumed&&observed.cloth&&observed.team&&observed.bone&&
        team==observed.team&&current.cloth==observed.cloth&&
        current.team==observed.team&&current.bone==observed.bone;
  }
  bool BeginObservation(Identity current,void *returnedTeam,bool main,unsigned depth,
                        bool exactCaller,int animator,int cross,bool fingerprint) {
    if(!main||depth!=1||!exactCaller||animator!=0||cross!=1||!fingerprint||
        !IdentityMatches(current,returnedTeam))return false;
    observing=consumed=true;return true;
  }
  void EndObservation() { observing=false; }
};
}
