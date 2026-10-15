#pragma once
#include "math/mmd_rig.h"

namespace mmd_ui {
// Presentation only: never use translated text as a rig/track key.
inline std::string RoleChinese(int role) {
  static const char *body[]={u8"骨盆",u8"左大腿",u8"右大腿",u8"左膝",u8"右膝",
    u8"左脚踝",u8"右脚踝",u8"腰背",u8"胸部",u8"颈部",u8"头部",u8"左肩",u8"右肩",
    u8"左上臂",u8"右上臂",u8"左肘",u8"右肘",u8"左手腕",u8"右手腕",u8"左脚尖",u8"右脚尖",
    u8"左眼",u8"右眼",u8"下颌"};
  if(role<0||role>=55)return u8"未选择部位";
  if(role<24)return body[role];
  if(role==54)return u8"上胸部";
  const char *finger[]={u8"拇指",u8"食指",u8"中指",u8"无名指",u8"小指"};
  int n=(role-24)%15;
  return std::string(role<39?u8"左":u8"右")+finger[n/3]+std::to_string(n%3+(n/3?1:0));
}
inline std::string RoleLabel(int role) {
  return role<0||role>=55?RoleChinese(role):RoleChinese(role)+u8"（"+mmd::RoleNames()[role]+u8"）";
}
inline std::string BoneLabel(const std::string &name) {
  static const auto labels=[] {
    std::map<std::string,std::string> out;
    for(int r=0;r<55;++r)for(const auto &key:mmd::SourceRoleKeys(r))out[key]=RoleChinese(r);
    for(const auto &p:std::initializer_list<std::pair<const char *,const char *>>{
        {u8"全ての親",u8"总父级"},{u8"センター",u8"中心"},{u8"グルーブ",u8"上下位移"},
        {u8"腰",u8"腰部控制"},{u8"上半身1",u8"上半身辅助1"},{u8"操作中心",u8"操作中心"},{u8"両目",u8"双眼"}})
      out[mmd::MotionBoneKey(p.first)]=p.second;
    for(const auto *side:{u8"左",u8"右"}) {
      for(const auto &p:std::initializer_list<std::pair<const char *,const char *>>{
          {u8"足D",u8"大腿D"},{u8"ひざD",u8"膝D"},{u8"足首D",u8"脚踝D"},{u8"足先EX",u8"脚尖EX"},
          {u8"足IK",u8"腿部IK"},{u8"つま先IK",u8"脚尖IK"},{u8"足IK親",u8"腿部IK父级"},
          {u8"肩P",u8"肩部父级"},{u8"肩C",u8"肩部取消"},{u8"腰キャンセル",u8"腰部取消"},
          {u8"腕捩",u8"上臂扭转"},{u8"手捩",u8"前臂扭转"},{u8"腕IK",u8"手臂IK"},
          {u8"握り",u8"握拳"},{u8"拡散",u8"张指"},{u8"ダミー",u8"手部辅助"}})
        out[mmd::MotionBoneKey(std::string(side)+p.first)]=std::string(side)+p.second;
      for(int i=1;i<=3;++i)for(const auto *part:{u8"腕捩",u8"手捩"})
        out[mmd::MotionBoneKey(std::string(side)+part+std::to_string(i))]=std::string(side)+
          (std::string(part)==u8"腕捩"?u8"上臂扭转":u8"前臂扭转")+std::to_string(i);
    }
    return out;
  }();
  auto i=labels.find(mmd::MotionBoneKey(name));
  // Unknown extension names remain exact; do not guess their anatomical role.
  return i==labels.end()?name:i->second+u8"（"+name+u8"）";
}
inline bool BoneMatches(const std::string &name,const char *filter) {
  return !filter[0]||name.find(filter)!=name.npos||BoneLabel(name).find(filter)!=std::string::npos;
}
inline std::string BoneListLabel(const std::string &names) {
  std::string out;size_t start=0;
  for(;;) {
    const auto end=names.find(u8"、",start);
    if(!out.empty())out+=u8"、";
    out+=BoneLabel(names.substr(start,end==names.npos?names.npos:end-start));
    if(end==names.npos)return out;
    start=end+std::strlen(u8"、");
  }
}
}
