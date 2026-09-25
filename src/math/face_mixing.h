#pragma once
#include "math/face_templates.h"
#include "nlohmann/json.hpp"

namespace face_mixing {
enum Region { Brows, Eyes, Mouth, Cheeks, RegionCount };
enum class Driver { Template, Eiem };
inline const char *Key(int r) {
  static const char *keys[]={"brows","eyes","mouth","cheeks"};return keys[r];
}
inline const char *Label(int r) {
  static const char *labels[]={u8"眉毛",u8"眼部",u8"嘴部",u8"脸颊"};return labels[r];
}
struct Settings {
  std::array<Driver,RegionCount> driver{{Driver::Eiem,Driver::Eiem,Driver::Eiem,Driver::Eiem}};
  std::array<float,RegionCount> gain{{1,1,1,1}};
  float strength=1;
  bool uses(Driver d) const {for(auto value:driver)if(value==d)return true;return false;}
  bool all(Driver d) const {for(auto value:driver)if(value!=d)return false;return true;}
  float amount(int region) const {
    return face_template::Clamp(strength,0,2)*(region>=0&&region<RegionCount?face_template::Clamp(gain[region],0,2):1.f);
  }
  bool uniform() const {
    for(int r=1;r<RegionCount;++r)
      if(driver[r]!=driver[0]||amount(r)!=amount(0))return false;
    return true;
  }
  bool selects(int region,Driver source) const {
    // Unknown native helper bones are retained only in the all-EIEM mode.
    return region>=0&&region<RegionCount?driver[region]==source:source==Driver::Eiem&&all(Driver::Eiem);
  }
};
inline int BoneRegion(const std::string &name) {
  auto n=face_template::Canonical(name);
  if(n.find("brow")==0)return Brows;
  if(n.find("eye")==0)return Eyes;
  if(n.find("lip")==0||n=="jawjoint"||n=="facemdjawdnjoint"||
      n.find("facemdtooth")==0||n=="line_toothjoint")return Mouth;
  if(n.find("facelfcheek")==0||n.find("facertcheek")==0)return Cheeks;
  return -1;
}
inline int TemplateRegion(int id) {
  if(id<0||id>=face_template::Count)return -1;
  if(id>=face_template::BrowUp&&id<=face_template::WorriedR)return Brows;
  if(id>=face_template::Blink&&id<=face_template::LowerLids)return Eyes;
  return id==face_template::CheekPuff?Cheeks:Mouth;
}
// A region selects a complete evaluated face, not isolated local bone deltas.
// Otherwise a game-driven jaw can drag template cheeks/lips away from their
// targets, or a zero-strength child can still inherit its parent's expression.
struct Transform {
  Vec3 position;
  Quat rotation;
};
using Pose=std::array<Transform,face_template::MaxBones>;
struct Hierarchy {
  int count=0;
  bool ready=false;
  Pose rest;
  std::array<int,face_template::MaxBones> parent{},region{},order{};
  std::array<Vec3,face_template::MaxBones> scale{};
  std::array<mmd::Matrix,face_template::MaxBones> external{};
  std::array<Quat,face_template::MaxBones> externalRotation{};
};
inline Hierarchy BindHierarchy(const std::vector<face_template::Bone> &nodes,
                              const Pose &rest,const std::vector<Vec3> &scales) {
  Hierarchy h;h.count=int(nodes.size());h.rest=rest;
  if(h.count<=0||h.count>face_template::MaxBones||scales.size()!=nodes.size())return h;
  std::array<int,face_template::MaxBones> state{};int next=0;
  for(int i=0;i<h.count;++i) {
    h.parent[i]=nodes[i].parent;h.region[i]=BoneRegion(nodes[i].name);
    h.scale[i]=scales[i];h.external[i]=nodes[i].parentNeutral;
    h.externalRotation[i]=mmd::Rotation(nodes[i].parentNeutral);
    mmd::Matrix inverse;
    if(!mmd::Inverse(nodes[i].parentNeutral,inverse)||
       !std::isfinite(Len(scales[i]))||std::fabs(scales[i].x)<1e-6f||
       std::fabs(scales[i].y)<1e-6f||std::fabs(scales[i].z)<1e-6f)return h;
  }
  std::function<bool(int)> visit=[&](int i) {
    if(state[i]==2)return true;
    if(state[i]==1)return false;
    state[i]=1;int p=h.parent[i];
    if(p < -1||p>=h.count||(p>=0&&!visit(p)))return false;
    // Unnamed helpers follow their owning branch, never disappear just
    // because another region switches driver.
    if(h.region[i]<0&&p>=0)h.region[i]=h.region[p];
    h.order[next++]=i;state[i]=2;return true;
  };
  for(int i=0;i<h.count;++i)if(!visit(i))return h;
  h.ready=true;return h;
}
inline Pose Globals(const Hierarchy &h,const Pose &local) {
  Pose result;
  std::array<mmd::Matrix,face_template::MaxBones> matrices;
  for(int n=0;n<h.count;++n) {
    int i=h.order[n],p=h.parent[i];
    const auto &parent=p>=0?matrices[p]:h.external[i];
    auto parentRotation=p>=0?result[p].rotation:h.externalRotation[i];
    matrices[i]=parent*mmd::TRS(local[i].position,local[i].rotation,h.scale[i]);
    result[i]={matrices[i].position(),NormQ(parentRotation*local[i].rotation)};
  }
  return result;
}
inline bool Compose(const Hierarchy &h,const std::array<Pose,RegionCount> &complete,
                    const Pose &fallback,Pose &output) {
  if(!h.ready)return false;
  Pose desired=Globals(h,fallback);
  for(int r=0;r<RegionCount;++r) {
    auto whole=Globals(h,complete[r]);
    for(int i=0;i<h.count;++i)if(h.region[i]==r)desired[i]=whole[i];
  }
  Pose result;
  std::array<mmd::Matrix,face_template::MaxBones> actual,inverses;
  std::array<Quat,face_template::MaxBones> rotations;
  for(int n=0;n<h.count;++n) {
    int i=h.order[n],p=h.parent[i];
    const auto &parent=p>=0?actual[p]:h.external[i];
    mmd::Matrix inverse;
    if(p>=0)inverse=inverses[p];
    else if(!mmd::Inverse(parent,inverse))return false;
    auto parentRotation=p>=0?rotations[p]:h.externalRotation[i];
    result[i].position=face_template::Vector(inverse,desired[i].position)+inverse.position();
    result[i].rotation=NormQ(Conj(parentRotation)*desired[i].rotation);
    actual[i]=parent*mmd::TRS(result[i].position,result[i].rotation,h.scale[i]);
    rotations[i]=NormQ(parentRotation*result[i].rotation);
    if(!mmd::Inverse(actual[i],inverses[i]))return false;
  }
  output=result;return true;
}
inline Settings Read(const nlohmann::json &j) {
  int version=j.value("version",0);
  if(version!=1&&version!=2)throw std::runtime_error("Unsupported face settings version");
  Settings s;s.strength=face_template::Clamp(j.value("strength",1.f),0,2);
  if(version==1) {
    // Legacy whole-face settings use the current all-game regional defaults.
    // Explicit per-region choices in version 2 remain authoritative below.
    return s;
  }
  if(!j.contains("regions")||!j["regions"].is_object())throw std::runtime_error("Missing facial region settings");
  for(int r=0;r<RegionCount;++r) {
    if(!j["regions"].contains(Key(r)))continue;
    const auto &v=j["regions"][Key(r)];auto source=v.value("source",std::string("game"));
    if(source!="template"&&source!="game"&&source!="eiem")throw std::runtime_error("Invalid facial region driver");
    s.driver[r]=source=="template"?Driver::Template:Driver::Eiem;
    s.gain[r]=face_template::Clamp(v.value("strength",1.f),0,2);
  }
  return s;
}
inline nlohmann::json Write(const Settings &s) {
  nlohmann::json j={{"version",2},{"strength",face_template::Clamp(s.strength,0,2)}};
  for(int r=0;r<RegionCount;++r)j["regions"][Key(r)]={
    {"source",s.driver[r]==Driver::Template?"template":"game"},
    {"strength",face_template::Clamp(s.gain[r],0,2)}};
  return j;
}
} // namespace face_mixing
