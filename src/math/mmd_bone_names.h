#pragma once
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>

namespace mmd {
// Normalize common ASCII/full-width variants without changing Japanese names.
inline std::string Name(std::string s) {
  const char *from[] = {u8"０", u8"１", u8"２", u8"３", u8"４", u8"５",
                        u8"６", u8"７", u8"８", u8"９", u8"Ｉ", u8"Ｋ"};
  const char *to[] = {"0", "1", "2", "3", "4", "5",
                      "6", "7", "8", "9", "I", "K"};
  for (size_t i = 0; i < 12; i++) {
    size_t p = 0;
    while ((p = s.find(from[i], p)) != s.npos) {
      s.replace(p, std::strlen(from[i]), to[i]);
      p++;
    }
  }
  return s;
}
// Same numeric roles as Unity HumanBodyBones; independent of game headers.
inline const std::array<const char *, 55> &RoleNames() {
  static const std::array<const char *, 55> n = {
      {u8"下半身",   u8"左足",    u8"右足",    u8"左ひざ",  u8"右ひざ",
       u8"左足首",   u8"右足首",  u8"上半身",  u8"上半身2", u8"首",
       u8"頭",       u8"左肩",    u8"右肩",    u8"左腕",    u8"右腕",
       u8"左ひじ",   u8"右ひじ",  u8"左手首",  u8"右手首",  u8"左つま先",
       u8"右つま先", u8"左目",    u8"右目",    u8"あご",    u8"左親指0",
       u8"左親指1",  u8"左親指2", u8"左人指1", u8"左人指2", u8"左人指3",
       u8"左中指1",  u8"左中指2", u8"左中指3", u8"左薬指1", u8"左薬指2",
       u8"左薬指3",  u8"左小指1", u8"左小指2", u8"左小指3", u8"右親指0",
       u8"右親指1",  u8"右親指2", u8"右人指1", u8"右人指2", u8"右人指3",
       u8"右中指1",  u8"右中指2", u8"右中指3", u8"右薬指1", u8"右薬指2",
       u8"右薬指3",  u8"右小指1", u8"右小指2", u8"右小指3", u8"上半身3"}};
  return n;
}
// Only explicit naming equivalents are automatic. A name containing "hand"
// or "parent" does not establish a control's purpose or its rotation centre.
inline std::string MotionBoneKey(std::string name) {
  name=Name(std::move(name));std::string key;
  for(unsigned char c:name) {
    if(c==' '||c=='_'||c=='-'||c=='.')continue;
    key+=char(c>='A'&&c<='Z'?c+('a'-'A'):c);
  }
  for(const auto &pair:{std::pair<const char *,const char *>{u8"肘",u8"ひじ"},{u8"膝",u8"ひざ"}}) {
    size_t at=0;while((at=key.find(pair.first,at))!=key.npos){key.replace(at,std::strlen(pair.first),pair.second);at+=std::strlen(pair.second);}
  }
  return key;
}
inline std::string NativeRoleBone(int role) {
  static const char *body[]={"Pelvis","L_Thigh","R_Thigh","L_Calf","R_Calf","L_Foot","R_Foot",
    "Spine","Spine1","Neck","Head","L_Clavicle","R_Clavicle","L_UpperArm","R_UpperArm",
    "L_Forearm","R_Forearm","L_Hand","R_Hand","L_Toe0","R_Toe0","L_Eye","R_Eye","Jaw"};
  if(role<24)return std::string("Bip001_")+body[role];
  if(role==54)return "Bip001_Spine2";
  int joint=(role-24)%15,finger=joint/3,segment=joint%3;
  return std::string(role<39?"Bip001_L_Finger":"Bip001_R_Finger")+std::to_string(finger)+(segment?std::to_string(segment):"");
}
inline std::array<std::string,3> SourceRoleKeys(int role) {
  static const char *body[]={"Hips","LeftUpperLeg","RightUpperLeg","LeftLowerLeg","RightLowerLeg","LeftFoot","RightFoot",
    "Spine","Chest","Neck","Head","LeftShoulder","RightShoulder","LeftUpperArm","RightUpperArm",
    "LeftLowerArm","RightLowerArm","LeftHand","RightHand","LeftToes","RightToes","LeftEye","RightEye","Jaw"};
  std::string human;
  if(role<24)human=body[role];
  else if(role==54)human="UpperChest";
  else {
    const char *fingers[]={"Thumb","Index","Middle","Ring","Little"};
    const char *segments[]={"Proximal","Intermediate","Distal"};int k=(role-24)%15;
    human=std::string(role<39?"Left":"Right")+fingers[k/3]+segments[k%3];
  }
  return {MotionBoneKey(RoleNames()[role]),MotionBoneKey(NativeRoleBone(role)),MotionBoneKey(human)};
}
inline std::string MotionBindingKey(const std::string &name) {
  const auto key=MotionBoneKey(name);
  static const auto aliases=[] {
    std::map<std::string,std::string> result;
    for(int role=0;role<55;++role)for(const auto &alias:SourceRoleKeys(role))result[alias]=MotionBoneKey(RoleNames()[role]);
    return result;
  }();
  auto found=aliases.find(key);return found==aliases.end()?key:found->second;
}
} // namespace mmd
