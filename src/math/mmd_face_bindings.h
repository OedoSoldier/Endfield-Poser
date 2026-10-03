#pragma once
#include "math/mmd_face_controls.h"

namespace mmd_face_bindings {
constexpr size_t MaxTargets=32;
struct Target {std::string name;int index=-1;float gain=1;};
using Targets=std::vector<Target>;
struct Mapping {Targets character,native;};

inline int NativeIndex(const std::string &name,const std::vector<mmd_face_controls::Native> &catalog) {
  auto spelling=character_face::MorphSpelling(name);
  for(const auto &c:catalog)if(character_face::MorphSpelling(c.name)==spelling)return c.channel;
  return -1;
}
inline std::string NativeName(int index,const std::vector<mmd_face_controls::Native> &catalog) {
  for(const auto &c:catalog)if(c.channel==index)return c.name;
  return {};
}
inline Targets Read(const nlohmann::json &value,bool native,const character_face::Profile *profile,
                    const std::vector<mmd_face_controls::Native> &catalog) {
  auto rows=value.contains("targets")?value.at("targets"):nlohmann::json::array({value});
  if(!rows.is_array()||rows.size()>MaxTargets)throw std::runtime_error("Invalid expression combination");
  Targets result;
  for(const auto &row:rows) {
    if(!row.is_object())throw std::runtime_error("Invalid expression target");
    Target t;t.gain=row.value("gain",1.f);
    if(!std::isfinite(t.gain))throw std::runtime_error("Invalid expression strength");
    t.gain=face_geometry::Clamp(t.gain,0,2);
    if(native) {
      t.name=row.value("name",std::string{});
      if(t.name.empty())t.name=NativeName(row.value("slider",-1),catalog);
      t.index=NativeIndex(t.name,catalog);
    } else {
      t.name=row.value("morph",std::string{});
      if(profile&&!t.name.empty())t.index=character_face::FindMorph(*profile,t.name);
    }
    if(!t.name.empty())result.push_back(std::move(t));
  }
  return result;
}
inline nlohmann::json Write(const Targets &targets,bool native) {
  auto rows=nlohmann::json::array();
  for(const auto &t:targets) {
    if(rows.size()>=MaxTargets)break;
    if(t.name.empty())continue;
    nlohmann::json row={{"gain",face_geometry::Clamp(t.gain,0,2)}};
    if(native){row["name"]=t.name;row["slider"]=t.index;}
    else row["morph"]=t.name;
    rows.push_back(std::move(row));
  }
  return {{"targets",std::move(rows)}};
}
inline Mapping Resolve(const std::string &track,const character_face::Profile *profile,
                       const std::vector<mmd_face_controls::Native> &catalog,
                       const nlohmann::json &characters,const nlohmann::json &fixed) {
  Mapping map;
  if(profile) {
    int index=character_face::FindMorph(*profile,track);
    if(index>=0)map.character.push_back({profile->morphs[index].name,index,1});
    try {
      if(characters.contains(profile->key)&&characters.at(profile->key).contains(track))
        map.character=Read(characters.at(profile->key).at(track),false,profile,catalog);
    }catch(const std::exception &){} // A malformed entry cannot damage other tracks.
  }
  int index=NativeIndex(track,catalog);
  if(index>=0)map.native.push_back({NativeName(index,catalog),index,1});
  try {if(fixed.contains(track))map.native=Read(fixed.at(track),true,profile,catalog);}
  catch(const std::exception &){}
  return map;
}
template<class Frame,class Usable>
inline void Apply(const Mapping &map,float sample,Frame &frame,Usable usable) {
  if(!std::isfinite(sample))return;
  sample=(std::max)(0.f,sample);
  bool calibrated=false;
  // Preserve the existing max blend between VMD tracks. Targets within one
  // combination add together, including duplicate channels, then clamp once.
  std::array<std::pair<int,float>,MaxTargets> combined{};size_t count=0;
  for(const auto &t:map.character)if(t.index>=0&&t.index<int(frame.expressions.size())&&usable(t.index)) {
    calibrated=true;size_t i=0;while(i<count&&combined[i].first!=t.index)++i;
    if(i==count) {if(count==MaxTargets)continue;combined[count++]={t.index,0.f};}
    combined[i].second+=sample*face_geometry::Clamp(t.gain,0,2);
  }
  for(size_t i=0;i<count;++i) {const auto &v=combined[i];frame.expressions[v.first]=(std::max)(frame.expressions[v.first],face_geometry::Clamp(v.second,0,1));}
  for(const auto &t:map.native)if(t.index>=0&&t.index<int(std::size(frame.weights))) {
    float v=sample*face_geometry::Clamp(t.gain,0,2);
    frame.weights[t.index]=face_geometry::Clamp(frame.weights[t.index]+v,0,1);
    if(!calibrated)frame.fallbackWeights[t.index]=face_geometry::Clamp(frame.fallbackWeights[t.index]+v,0,1);
  }
}
} // namespace mmd_face_bindings
