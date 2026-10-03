#pragma once
#include "math/secondary_body_motion.h"
#include "integrations/secondary_bone_profiles.h"
#include "integrations/sbm_mmd_api.h"
#include <memory>
#include <string>
#include <vector>
#include <cstring>

namespace poser_secondary {
static bool enabled=false;
static secondary_body::Settings settings;
static float strength=1;
static uint64_t nextToken=0;
inline std::string ModelKey(std::string name) {
  for(char &c:name)if(c>='A'&&c<='Z')c+=char('a'-'A');
  auto end=name.find("_postmodel");if(end!=name.npos)name.resize(end);
  end=name.find("(clone)");if(end!=name.npos)name.resize(end);
  end=name.find('#');if(end!=name.npos)name.resize(end);
  return name; // SBM uses the complete chr_XXXX_name, unlike facial templates.
}
// Included after the MMD safe Transform helpers. Stored pointers are backed by
// strong handles owned by Driver as well as the MMD session; no mesh access.
inline bool Read(void *method,void *object,void *out,size_t size) {
  __try {
    if(!method||!UnityObjAlive(object))return false;
    void *value=Invoke(method,object);if(!value)return false;
    memcpy(out,static_cast<char *>(value)+16,size);return true;
  } __except(1) {return false;}
}
inline bool Rotation(void *object,Quat q) {
  __try {
    if(!g_transform_set_localRotation||!UnityObjAlive(object))return false;
    void *args[]={&q};Invoke(g_transform_set_localRotation,object,args);return true;
  } __except(1) {return false;}
}
inline bool Position(void *object,Vec3 p) {
  __try {
    if(!g_transform_set_localPosition||!UnityObjAlive(object))return false;
    void *args[]={&p};Invoke(g_transform_set_localPosition,object,args);return true;
  } __except(1) {return false;}
}
struct Driver {
  HMODULE module=nullptr;sbm_mmd::Api api;
  void *actor=nullptr;uint64_t token=0;bool owned=false;
  void *bones[2]{},*parents[2]{};std::vector<uint32_t> handles;
  float peakAngle=0,peakOffset=0;unsigned writes=0;
  Quat original[2],basis[2],target[2];Vec3 originalPos[2],anchorOffset[2],targetPos[2];
  secondary_body::Spring spring[2];
  float positionScale[2]{1,1};bool sampled=false;
  ~Driver() {release();if(module)FreeLibrary(module);if(!RuntimeClosing()&&il2cpp_gchandle_free)for(auto h:handles)il2cpp_gchandle_free(h);}
  void release() {if(owned){api.release(actor,token);owned=false;}}
  bool retain(void *p) {
    if(!p||!il2cpp_gchandle_new||!il2cpp_gchandle_free)return false;
    auto h=il2cpp_gchandle_new(p,false);if(!h)return false;handles.push_back(h);return true;
  }
  void restore(bool alive) {
    if(alive)for(int i=0;i<2;++i){Rotation(bones[i],original[i]);Position(bones[i],originalPos[i]);}
    release();
  }
  bool write() {
    if(!sampled||!UnityObjAlive(actor))return false;
    for(int i=0;i<2;++i)if(!UnityObjAlive(bones[i])||!UnityObjAlive(parents[i]))return false;
    for(int i=0;i<2;++i)if(!Rotation(bones[i],target[i])||!Position(bones[i],targetPos[i]))return false;
    ++writes;return true;
  }
};
struct State {
  std::shared_ptr<Driver> driver;
  double retryAt=0;
  std::string status=u8"等待 MMD 动作播放";
};
inline void Stop(State &s,bool alive,bool restore=true) {
  if(s.driver) {if(restore)s.driver->restore(alive);else s.driver->release();s.driver.reset();}
  s.retryAt=0;s.status=u8"已恢复原有第二骨骼姿态";
}
// Original SBM cannot be safely suppressed. A cooperative DLL is optional;
// its lease prevents two writers, but its gait/enable settings do not control us.
inline bool ExternalLease(Driver &d,std::string &status) {
  if(!GetModuleHandleExW(0,L"sbm.dll",&d.module))return true;
  auto get=reinterpret_cast<sbm_mmd::GetApi>(GetProcAddress(d.module,"SBM_MmdGetApi"));
  if(!get||!get(sbm_mmd::Version,sizeof(d.api),&d.api)||d.api.version!=sbm_mmd::Version||
     d.api.size!=sizeof(d.api)||!d.api.acquire||!d.api.release) {
    status=u8"检测到独立 SBM，请退出游戏后停用 sbm.dll，避免重复控制";return false;
  }
  if(!d.api.acquire(d.actor,d.token)){status=u8"等待 SBM 交还骨骼";return false;}
  d.owned=true;return true;
}
template<class Bones,class Saved>
bool Prepare(State &s,void *actor,const std::string &model,const Bones &bones,const Saved &saved,double now) {
  if(s.driver)return true;
  if(now<s.retryAt)return false;
  s.retryAt=now+1;
  if(!UnityObjAlive(actor)){s.status=u8"等待角色就绪";return false;}
  auto d=std::make_shared<Driver>();d->actor=actor;d->token=++nextToken;
  auto find=[&](const char *name) {for(int i=0;i<int(bones.size());++i)if(!strcmp(bones[i].name,name))return i;return -1;};
  int indices[2]={-1,-1};
  for(const auto &p:profiles)if(model==p.model){indices[0]=find(p.right);indices[1]=find(p.left);break;}
  if(indices[0]<0||indices[1]<0)for(const auto &p:fallbackPairs) {
    int r=find(p[0]),l=find(p[1]);if(r>=0&&l>=0){indices[0]=r;indices[1]=l;break;}
  }
  if(indices[0]<0||indices[1]<0){s.status=u8"当前模型没有支持的第二骨骼，保持原样";return false;}
  Vec3 points[2],parentPos[2];Quat parentRot[2];
  for(int n=0;n<2;++n) {
    const int idx=indices[n],parent=bones[idx].parentIdx;
    if(parent<0||parent>=int(bones.size())){s.status=u8"第二骨骼层级不完整";return false;}
    d->bones[n]=bones[idx].transform;d->parents[n]=bones[parent].transform;
    bool captured=false;
    for(const auto &b:saved)if(b.transform==d->bones[n]){d->original[n]=b.rot;d->originalPos[n]=b.pos;captured=true;break;}
    if(!captured||!mmd_secondary::Finite(d->original[n])||!mmd_secondary::Finite(d->originalPos[n])||
       !Read(g_transform_get_rotation,d->parents[n],&parentRot[n],sizeof(Quat))||
       !Read(g_transform_get_position,d->parents[n],&parentPos[n],sizeof(Vec3))||
       !Read(g_transform_get_position,d->bones[n],&points[n],sizeof(Vec3))||
       !mmd_secondary::Finite(parentRot[n])||!mmd_secondary::Finite(points[n])||!mmd_secondary::Finite(parentPos[n])) {
      s.status=u8"无法读取第二骨骼基准";return false;
    }
    d->anchorOffset[n]=Conj(NormQ(parentRot[n]))*(points[n]-parentPos[n]);
    float worldLength=Len(d->anchorOffset[n]),localLength=Len(d->originalPos[n]);
    if(worldLength>1e-5f&&localLength>1e-5f)d->positionScale[n]=std::clamp(localLength/worldLength,.01f,100.f);
  }
  Vec3 across=points[0]-points[1],up{0,1,0},center=(points[0]+points[1])*.5f;
  // The torso direction is anatomical, not the arbitrary local bone X/Y/Z.
  // Prefer neck/head when starting in a leaning native pose.
  for(const char *name:{"neck_01_jnt","neck_jnt","head_jnt","Head"}) {
    int idx=find(name);Vec3 p;
    if(idx>=0&&Read(g_transform_get_position,bones[idx].transform,&p,sizeof(p))&&
       mmd_secondary::Finite(p)&&Len(p-center)>.04f){up=Norm(p-center);break;}
  }
  Vec3 right=Norm(across-up*Dot(across,up));
  if(Len(right)<.5f||Len(across)<.01f||Len(across)>1.f){s.status=u8"第二骨骼方向退化，保持原样";return false;}
  Vec3 forward=Norm(Cross(right,up));up=Norm(Cross(forward,right));
  Quat q=Quat::FromTo({1,0,0},right);q=NormQ(Quat::FromTo(q*Vec3{0,1,0},up)*q);
  for(int n=0;n<2;++n) {
    d->basis[n]=NormQ(Conj(NormQ(parentRot[n]))*q);
    d->target[n]=d->original[n];d->targetPos[n]=d->originalPos[n];
    if(!d->retain(d->bones[n])||!d->retain(d->parents[n])){s.status=u8"无法保留第二骨骼句柄";return false;}
  }
  if(d->bones[0]==d->bones[1]||!d->retain(actor)||!ExternalLease(*d,s.status))return false;
  s.driver=std::move(d);s.status=u8"内置三维运动驱动已就绪";
  Log("[BODY-PHYSICS] acquired actor=%p model=%s right=%s left=%s externalLease=%d",actor,model.c_str(),bones[indices[0]].name,bones[indices[1]].name,s.driver->owned);
  return true;
}
template<class Bones,class Saved>
void Tick(State &s,void *actor,const std::string &model,const Bones &bones,const Saved &saved,
          double now,double cursor,uint64_t epoch,bool playing,bool bodyMotion) {
  if(!enabled||!bodyMotion){if(s.driver)Stop(s,UnityObjAlive(actor));s.status=u8"第二骨骼物理增强已关闭";return;}
  if(s.driver&&s.driver->actor!=actor)Stop(s,UnityObjAlive(s.driver->actor));
  if(!Prepare(s,actor,model,bones,saved,now))return;
  auto &d=*s.driver;
  if(!UnityObjAlive(actor)){Stop(s,false);return;}
  secondary_body::Sample samples[2];
  for(int n=0;n<2;++n) {
    Vec3 p;Quat q;
    if(!Read(g_transform_get_position,d.parents[n],&p,sizeof(p))||
       !Read(g_transform_get_rotation,d.parents[n],&q,sizeof(q))||!mmd_secondary::Finite(q)||!mmd_secondary::Finite(p)) {
      Stop(s,false);s.status=u8"骨骼已失效，停止增强";return;
    }
    samples[n]={p+NormQ(q)*d.anchorOffset[n],NormQ(q*d.basis[n])};
  }
  auto cfg=settings;cfg.strength=strength;
  for(int n=0;n<2;++n) {
    auto r=d.spring[n].step(samples[n],now,cursor,epoch,playing,cfg);
    if(!(strength>0))r={};
    d.peakAngle=(std::max)(d.peakAngle,Len(r.angle));d.peakOffset=(std::max)(d.peakOffset,Len(r.offset));
    d.target[n]=NormQ(d.basis[n]*secondary_body::RotationVector(r.angle)*Conj(d.basis[n])*d.original[n]);
    d.targetPos[n]=d.originalPos[n]+d.basis[n]*(r.offset*d.positionScale[n]);
  }
  d.sampled=true;
  if(!d.write()){Stop(s,false);s.status=u8"写入失败，停止增强";return;}
  s.status=playing?u8"内置增强：跟随上下、左右、前后运动":u8"已暂停并保持摆动姿态";
}
inline void Replay(State &s) {
  if(!enabled){if(s.driver)Stop(s,UnityObjAlive(s.driver->actor));return;}
  if(s.driver&&!s.driver->write())Stop(s,false);
}
}
