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
