#pragma once
// Playback owns only the selected actor's BBC weights. The native solver still
// owns particles, constraints, culling and the final accessory transforms.
#include "game/cloth.h"
#include "game/accessory.h"
#include "game/bbc_garment.h"

static bool g_bbcFullSimulation=true;
struct BbcScalar {
  void *object=nullptr;
  int offset=-1;
  float original=0;
  bool changed=false;
};
struct BbcPlaybackCloth {
  void *object=nullptr,*data=nullptr,*process=nullptr,*notify=nullptr;
  uint32_t handle=0,dataHandle=0,processHandle=0;
  int dataOffset=-1,processOffset=-1;
  bool active=false,compatible=false,notifyPending=false;
  char name[128]={};
  BbcScalar scalars[6]; // public property, animation cache, serialized setting
  int running=-1,skip=-1,culled=-1,enabled=-1,mode=-1;
  float weight=-1,blend=-1,poseRatio=-1;
  bool observed=false;
  BbcGarmentLease garment;
};
static std::vector<BbcPlaybackCloth> g_bbcPlaybackCloths;
static bool BbcClothCurrent(const BbcPlaybackCloth &c);

static int BbcFloatField(void *cls,const char *name) {
  if(!il2cpp_field_get_flags || !il2cpp_field_get_type || !il2cpp_type_get_type ||
     !il2cpp_field_get_offset)return -1;
  void *f=SkirtNamedField(cls,name);
  if(!f || (il2cpp_field_get_flags(f)&0x70))return -1;
  void *t=il2cpp_field_get_type(f);
  if(!t || il2cpp_type_get_type(t)!=0xc)return -1;
  size_t off=il2cpp_field_get_offset(f);
  return off>=16 && off<=4092?int(off):-1;
}
static bool BbcScalarCapture(BbcScalar &s,void *object,const char *name) {
  s.object=object;s.offset=BbcFloatField(il2cpp_object_get_class(object),name);
  if(s.offset<16)return false;
  s.original=*(float*)((char*)object+s.offset);
  if(!std::isfinite(s.original) || s.original<0 || s.original>1)return false;
  // Shared serialized data or immediate stop/restart must retain its baseline.
  for(const auto &c:g_bbcPlaybackCloths)for(const auto &other:c.scalars)
    if((object!=c.object || BbcClothCurrent(c)) &&
       other.object==s.object && other.offset==s.offset) {s.original=other.original;return true;}
  return true;
}
static bool BbcClothCurrent(const BbcPlaybackCloth &c) {
  return UnityObjAlive(c.object) && c.dataOffset>=16 && c.processOffset>=16 &&
      *(void**)((char*)c.object+c.dataOffset)==c.data &&
      c.process && *(void**)((char*)c.object+c.processOffset)==c.process;
}
static bool BbcNotifyCloth(BbcPlaybackCloth &c) {
  if(!c.notify || !BbcClothCurrent(c))return false;
  void *exc=nullptr;il2cpp_runtime_invoke(c.notify,c.object,nullptr,&exc);return !exc;
}
static bool BbcWriteWeights(BbcPlaybackCloth &c,bool apply) {
  __try {
    bool current=BbcClothCurrent(c),dirty=false;
    if(apply && (!current || !c.compatible))return false;
    bool enabled=false;
    // Never activate hidden costumes/LODs or resurrect a disabled component.
    if(apply && (!ReadBehaviourEnabled(c.object,enabled) || !enabled))return false;
    for(auto &s:c.scalars) {
      if(s.offset<16 || !s.object || (!apply && !s.changed))continue;
      // Managed data is retained, but a replaced/destroyed component is not a
      // safe place to restore public fields belonging to a new cloth process.
      if(s.object==c.object && !current) {s.changed=false;continue;}
      float &v=*(float*)((char*)s.object+s.offset);
      float desired=apply?1.f:s.original;
      if(v!=desired) {v=desired;dirty=true;}
      s.changed=apply && desired!=s.original;
    }
    c.notifyPending=c.notifyPending || dirty;
    if(c.notifyPending && current) {
      if(!BbcNotifyCloth(c))return false;
      c.notifyPending=false;
    }
    return true;
  } __except(1) {return false;}
}
static void BbcPlaybackEnd() {
  for(auto &c:g_bbcPlaybackCloths)c.active=false;
  g_bbcMaintenancePending=!g_bbcPlaybackCloths.empty();
}
static void BbcPlaybackBegin() {
  BbcPlaybackEnd();
  for(const auto &bone:s_accessoryBones)for(void *object:bone.physicsComps) {
    if(!UnityObjAlive(object))continue;
    void *cls=il2cpp_object_get_class(object);
    if(!SkirtClass(cls,"BeyondBoneCloth","BeyondDynamicBone"))continue;
    auto found=std::find_if(g_bbcPlaybackCloths.begin(),g_bbcPlaybackCloths.end(),
        [&](const BbcPlaybackCloth &c){return c.object==object && BbcClothCurrent(c);});
    if(found!=g_bbcPlaybackCloths.end()) {found->active=true;continue;}
    if(g_bbcPlaybackCloths.size()>=256 || !il2cpp_gchandle_new || !il2cpp_gchandle_free)continue;
    BbcPlaybackCloth c;c.object=object;c.active=true;
    c.dataOffset=SkirtField(cls,"serializeData");c.processOffset=SkirtField(cls,"process");
    if(c.dataOffset<16 || c.processOffset<16)continue;
    c.data=*(void**)((char*)object+c.dataOffset);
    c.process=*(void**)((char*)object+c.processOffset);
    if(!c.data || !c.process || !SkirtClass(il2cpp_object_get_class(c.data),"ClothSerializeData","BeyondDynamicBone"))continue;
    c.handle=il2cpp_gchandle_new(object,false);c.dataHandle=il2cpp_gchandle_new(c.data,false);
    c.processHandle=il2cpp_gchandle_new(c.process,false);
    if(!c.handle || !c.dataHandle || !c.processHandle) {
      if(c.handle)il2cpp_gchandle_free(c.handle);
      if(c.dataHandle)il2cpp_gchandle_free(c.dataHandle);
      if(c.processHandle)il2cpp_gchandle_free(c.processHandle);
      continue;
    }
    strncpy_s(c.name,bone.name,_TRUNCATE);
    c.notify=BbcFindMethod(cls,"SetParameterChange",false);
    c.compatible=c.notify!=nullptr;
    const char *fields[]={"clothSimulateWeightProperty","_clothSimulateWeightProperty","clothSimulateWeight",
                          "blendWeightProperty","_blendWeightProperty","blendWeight"};
    for(int i=0;i<6;++i)
      c.compatible=BbcScalarCapture(c.scalars[i],i%3==2?c.data:c.object,fields[i]) && c.compatible;
    BbcGarmentBind(c.garment,c.object,c.data,c.process,c.name);
    g_bbcPlaybackCloths.push_back(c);
    Log("[BBC] playback cloth '%s': weight=%.3f blend=%.3f weight-control=%d",
        c.name,c.scalars[0].original,c.scalars[3].original,int(c.compatible));
  }
  g_bbcMaintenancePending=!g_bbcPlaybackCloths.empty();
}
static void BbcPlaybackMaintain(bool applying) {
  // Called only after CompleteMasterJob, on the BBC update thread. End/Begin
  // merely queue ownership changes; UI/HTTP threads never edit native queues.
  for(auto &c:g_bbcPlaybackCloths)
    if(!applying || !c.active || !g_bbcFullSimulation)BbcWriteWeights(c,false);
  for(auto &c:g_bbcPlaybackCloths)
    BbcGarmentMaintain(c.garment,applying && c.active && BbcClothCurrent(c));
  for(auto it=g_bbcPlaybackCloths.begin();it!=g_bbcPlaybackCloths.end();) {
    if(it->active) {++it;continue;}
    // A failed notification is retried while the original live object exists.
    if(!BbcWriteWeights(*it,false) && BbcClothCurrent(*it)) {++it;continue;}
    if(BbcGarmentPending(it->garment)) {++it;continue;}
    BbcGarmentRelease(it->garment);
    if(it->handle)il2cpp_gchandle_free(it->handle);
    if(it->dataHandle)il2cpp_gchandle_free(it->dataHandle);
    if(it->processHandle)il2cpp_gchandle_free(it->processHandle);
    it=g_bbcPlaybackCloths.erase(it);
  }
  for(auto &c:g_bbcPlaybackCloths)
    if(applying && c.active && g_bbcFullSimulation)BbcWriteWeights(c,true);
  g_bbcMaintenancePending=false;
  for(const auto &c:g_bbcPlaybackCloths) {
    if(!c.active || c.notifyPending)g_bbcMaintenancePending=true;
    if(BbcGarmentPending(c.garment))g_bbcMaintenancePending=true;
    for(const auto &s:c.scalars)if(s.changed)g_bbcMaintenancePending=true;
  }
}
static void BbcPlaybackObserve() {
  SkirtObserveSimulation();
  static ULONGLONG next=0;
  if(GetTickCount64()<next)return;
  next=GetTickCount64()+500;
  for(auto &c:g_bbcPlaybackCloths) {
    if(!c.active || !BbcClothCurrent(c))continue;
    void *p=*(void**)((char*)c.object+c.processOffset);
    bool on=false;int enabled=ReadBehaviourEnabled(c.object,on)?int(on):-1;
    int running=SkirtQueryBool(p,"IsRunning"),skip=SkirtQueryBool(p,"IsSkipWriting"),culled=SkirtQueryBool(p,"IsLodCulled");
    float weight=SkirtOptionalFloat(c.object,"clothSimulateWeightProperty"),blend=SkirtOptionalFloat(c.object,"blendWeightProperty");
    c.poseRatio=SkirtOptionalFloat(c.object,"animationPoseRatioProperty");
    c.mode=SkirtEffectiveMode(p);
    if(!c.observed || c.enabled!=enabled || c.running!=running || c.skip!=skip || c.culled!=culled ||
       std::abs(c.weight-weight)>.01f || std::abs(c.blend-blend)>.01f)
      Log("[BBC] cloth '%s': running=%d skip=%d enabled=%d culled=%d weight=%.3f blend=%.3f pose=%.3f mode=%d",
          c.name,running,skip,enabled,culled,weight,blend,c.poseRatio,c.mode);
    c.observed=true;c.enabled=enabled;c.running=running;c.skip=skip;c.culled=culled;c.weight=weight;c.blend=blend;
  }
}
