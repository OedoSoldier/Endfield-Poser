#pragma once
#include "math/face_mixing.h"
#include <cstring>

// Constrain the final eye aperture, after character/native mixing and strength.
// Unit authored poses define the envelope; no global eye-weight normalization.
namespace face_eyelid {
struct Pair {int upper=-1,lower=-1,side=0;Vec3 axis;float open=0,closure=0;};
struct Limits {
  std::vector<Pair> pairs;
  std::array<int,face_geometry::MaxBones> side=[] {std::array<int,face_geometry::MaxBones> a;a.fill(-1);return a;}();
  face_mixing::Pose neutral;
};
inline int Side(const std::string &name) {
  auto n=face_geometry::Canonical(name);
  static constexpr const char *prefixes[]={"eyelf","browlinelf","eyert","eyerf","browlinert","browlinerf"};
  for(int i=0;i<6;++i) {
    size_t length=std::strlen(prefixes[i]);
    if(n.compare(0,length,prefixes[i])==0&&n.size()>length&&
       n[length]>='0'&&n[length]<='9')return i<2?0:1;
  }
  return -1;
}
inline Limits Bind(const face_mixing::Hierarchy &h,const std::vector<face_geometry::Bone> &nodes) {
  Limits l;if(!h.ready||int(nodes.size())!=h.count)return l;
  l.neutral=face_mixing::Globals(h,h.rest);
  std::map<std::string,int> names;
  for(int i=0;i<h.count;++i){names[face_geometry::Canonical(nodes[i].name)]=i;l.side[i]=Side(nodes[i].name);}
  for(int n=0;n<h.count;++n) {
    int i=h.order[n],p=h.parent[i];
    if(l.side[i]<0&&p>=0&&face_mixing::BoneRegion(nodes[i].name)<0)l.side[i]=l.side[p];
  }
  for(int s=0;s<2;++s)for(int u=2;u<=4;++u) {
    auto prefix=s==0?"eyelf":"eyert";
    auto upper=names.find(std::string(prefix)+"0"+std::to_string(u)+"joint");
    auto lower=names.find(std::string(prefix)+"0"+std::to_string(10-u)+"joint");
    if(upper==names.end()||lower==names.end())continue;
    Vec3 d=l.neutral[upper->second].position-l.neutral[lower->second].position;
    float gap=Len(d);if(!std::isfinite(gap)||gap<1e-6f)continue;
    l.pairs.push_back({upper->second,lower->second,s,d*(1.f/gap),gap,0});
  }
  return l;
}
inline void Include(Limits &l,const face_mixing::Hierarchy &h,const face_mixing::Pose &local) {
  auto world=face_mixing::Globals(h,local);
  for(auto &p:l.pairs) {
    float closure=p.open-Dot(world[p.upper].position-world[p.lower].position,p.axis);
    if(std::isfinite(closure))p.closure=(std::max)(p.closure,closure);
  }
}
inline Limits Merge(Limits a,const Limits &b) {
  for(auto &p:a.pairs)for(const auto &q:b.pairs)if(p.upper==q.upper&&p.lower==q.lower)
    p.closure=(std::max)(p.closure,q.closure);
  return a;
}
inline bool Apply(const Limits &l,const face_mixing::Hierarchy &h,face_mixing::Pose &local) {
  if(!h.ready||l.pairs.empty())return false;
  auto world=face_mixing::Globals(h,local);float gain[2]={1,1};
  for(const auto &p:l.pairs) {
    // Missing closure data must not turn every eye expression off.
    if(p.closure<p.open*.05f)continue;
    float close=p.open-Dot(world[p.upper].position-world[p.lower].position,p.axis);
    if(std::isfinite(close)&&close>p.closure+p.open*1e-5f)
      gain[p.side]=(std::min)(gain[p.side],p.closure/close);
  }
  if(gain[0]==1&&gain[1]==1)return false;
  for(int i=0;i<h.count;++i)if(l.side[i]>=0&&gain[l.side[i]]<1) {
    float t=gain[l.side[i]];
    world[i].position=l.neutral[i].position+(world[i].position-l.neutral[i].position)*t;
    world[i].rotation=Quat::Slerp(l.neutral[i].rotation,world[i].rotation,t);
  }
  auto result=local;
  std::array<mmd::Matrix,face_geometry::MaxBones> matrices;
  std::array<Quat,face_geometry::MaxBones> rotations;
  for(int n=0;n<h.count;++n) {
    int i=h.order[n],p=h.parent[i];
    auto matrix=p>=0?matrices[p]:h.external[i];auto rotation=p>=0?rotations[p]:h.externalRotation[i];
    if(l.side[i]>=0&&gain[l.side[i]]<1) {
      mmd::Matrix inverse;if(!mmd::Inverse(matrix,inverse))return false;
      result[i].position=face_geometry::Vector(inverse,world[i].position)+inverse.position();
      result[i].rotation=NormQ(Conj(rotation)*world[i].rotation);
    }
    matrices[i]=matrix*mmd::TRS(result[i].position,result[i].rotation,h.scale[i]);
    rotations[i]=NormQ(rotation*result[i].rotation);
  }
  local=result;return true;
}
}
