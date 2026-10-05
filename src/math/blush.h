#pragma once
#include "math/character_face.h"

namespace blush {
// Reserved material channel; never sent to SMC's bone morph array.
constexpr int Channel=36;
struct Profile {bool enabled=true;int style=0;float gain=1,limit=1;};
inline bool Alias(const std::string &name) {
  auto n=character_face::MorphSpelling(name);
  for(auto c:{u8"照れ",u8"照れ2",u8"赤面",u8"頬染め",u8"頬染",u8"脸红",u8"臉紅",u8"腮红","blush","Blush"})if(n==c)return true;
  return false;
}
inline bool Valid(const Profile &p) {
  return (p.style==0||p.style==2)&&std::isfinite(p.gain)&&p.gain>=0&&p.gain<=2&&
    std::isfinite(p.limit)&&p.limit>=0&&p.limit<=1;
}
inline float Weight(float value,float master,const Profile &p) {
  if(!p.enabled||!Valid(p)||!std::isfinite(value)||!std::isfinite(master))return 0;
  return (std::min)(p.limit,(std::max)(0.f,value)*(std::max)(0.f,master)*p.gain);
}
inline Profile Read(const nlohmann::json &j) {
  Profile p;p.enabled=j.value("enabled",true);p.style=j.value("style",0);
  // Legacy atlas indices 1/3 are non-blush expressions. Keep either supported
  // blush style and migrate old non-blush choices to the default.
  if(p.style==1||p.style==3)p.style=0;
  p.gain=j.value("gain",1.f);p.limit=j.value("limit",1.f);
  if(!Valid(p))throw std::runtime_error("Invalid blush profile");return p;
}
inline nlohmann::json Write(const Profile &p) {return {{"enabled",p.enabled},{"style",p.style},{"gain",p.gain},{"limit",p.limit}};}
} // namespace blush
