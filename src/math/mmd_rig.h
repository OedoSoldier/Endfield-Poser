#pragma once
#include "mmd_mirror.h"
#include "math/ik_two_bone.h"
#include "math/mmd_motion.h"
#include <numeric>
#include <set>

namespace mmd {
enum class BuiltinRigPreset { StandardMmd, ExtractedTPose };
struct IkLink {
  int bone = -1;
  bool limited = false;
  Vec3 minimum, maximum;
};
// Explicit motion controls, independent of the destination model's skinning.
struct RotationDriver {
  int source = -1, inputAxis = 0;
  Vec3 axis;
  float weight = 1;
};
struct AimControl {
  int source = -1, bone = -1;
};
struct RigBone {
  std::string name;
  Vec3 rest;
  int parent = -1, layer = 0;
  int grant = -1;
  float grantWeight = 0;
  bool grantRotation = false, grantPosition = false, grantLocal = false;
  bool fixed = false, localAxes = false;
  Vec3 axis, axisX, axisZ;
  int effector = -1, iterations = 0;
  float angleLimit = 0;
  std::vector<IkLink> links;
  std::vector<RotationDriver> drivers;
  bool controlIk = false, copyIkRotation = false, motionControl = false;
};
struct RigDefinition {
  std::string name;
  bool builtin = false;
  BuiltinRigPreset builtinPreset = BuiltinRigPreset::StandardMmd;
  std::vector<RigBone> bones;
  std::vector<int> order;
  std::map<std::string, int> names;
  std::map<std::string, uint8_t> morphTypes;
  std::vector<std::string> warnings;
  std::vector<AimControl> aims;
  std::set<std::string> ignoredTracks;
  int find(const std::string &name) const {
    auto i = names.find(Name(name));
    return i == names.end() ? -1 : i->second;
  }
  void finish() {
    names.clear();
    order.clear();
    std::vector<int> mark(bones.size());
    std::vector<unsigned> depths(bones.size());
    for (size_t i = 0; i < bones.size(); i++) {
      bones[i].name = Name(bones[i].name);
      names.emplace(bones[i].name, int(i));
    }
    std::function<void(int, unsigned)> visit = [&](int i, unsigned depth) {
      if (i < 0)
        return;
      if (size_t(i) >= bones.size())
        throw std::runtime_error("PMX bone index out of range");
      if (mark[i] == 1)
        throw std::runtime_error("Cyclic PMX skeleton or append dependency");
      if (mark[i] == 2)
        return;
      if (depth > 256)
        throw std::runtime_error("PMX dependency chain exceeds 256 bones");
      mark[i] = 1;
      visit(bones[i].parent, depth + 1);
      visit(bones[i].grant, depth + 1);
      for (const auto &d : bones[i].drivers) {
        if (d.source < 0 || d.source >= int(bones.size())) throw std::runtime_error("Invalid motion control driver");
        visit(d.source, depth + 1);
      }
      depths[i] =
          1 + (std::max)(bones[i].parent >= 0 ? depths[bones[i].parent] : 0,
                         bones[i].grant >= 0 ? depths[bones[i].grant] : 0);
      for (const auto &d : bones[i].drivers)
        depths[i] = (std::max)(depths[i], depths[d.source] + 1);
      if (depths[i] > 256)
        throw std::runtime_error("PMX dependency chain exceeds 256 bones");
      mark[i] = 2;
      order.push_back(i);
    };
    for (size_t i = 0; i < bones.size(); i++) {
      visit(int(i), 0);
      auto &b = bones[i];
      if (b.effector >= int(bones.size()))
        throw std::runtime_error("Invalid IK effector");
      if (b.grant < -1 || b.effector < -1)
        throw std::runtime_error("Invalid PMX constraint index");
      for (auto &l : b.links)
        if (l.bone < 0 || l.bone >= int(bones.size()))
          throw std::runtime_error("Invalid IK link");
      for (const auto &d : b.drivers)
        if (d.source < 0 || d.source >= int(bones.size()) || d.inputAxis < 0 || d.inputAxis > 2)
          throw std::runtime_error("Invalid motion control driver");
    }
    for (const auto &a : aims) {
      if (a.source < 0 || a.bone < 0 || a.source >= int(bones.size()) || a.bone >= int(bones.size()))
        throw std::runtime_error("Invalid motion aim control");
      for (int i=a.source;i>=0;i=bones[i].parent)
        if(i==a.bone)throw std::runtime_error("Gaze target cannot depend on the eye it controls");
    }
    for (size_t c=0;c<bones.size();++c) if(bones[c].controlIk)
      for (int i=int(c);i>=0;i=bones[i].parent)
        for(const auto &link:bones[c].links)
          if(i==link.bone)throw std::runtime_error("Arm IK target cannot depend on its solved chain");
  }
};
inline std::string PmxText(Reader &r, unsigned encoding,
                           const Decoder &decode) {
  auto n = r.count(1, 1048576);
  return decode(r.raw(n), encoding ? 65001 : 1200);
}
inline int PmxIndex(Reader &r, int n) {
  if (n == 1)
    return r.read<int8_t>();
  if (n == 2)
    return r.read<int16_t>();
  if (n == 4)
    return r.read<int32_t>();
  throw std::runtime_error("Invalid PMX index width");
}
inline RigDefinition ReadPmx(const std::vector<uint8_t> &data,
                             const Decoder &decode) {
  Reader r(data);
  RigDefinition rig;
  if (r.raw(4) != "PMX ")
    throw std::runtime_error("Not a PMX file");
  float ver = r.number();
  if (std::fabs(ver - 2.f) > 0.001f && std::fabs(ver - 2.1f) > 0.001f)
    throw std::runtime_error("PMX version must be 2.0 or 2.1");
  int header = r.read<uint8_t>();
  if (header < 8)
    throw std::runtime_error("Invalid PMX header");
  auto h = r.raw(header);
  unsigned enc = uint8_t(h[0]), uv = uint8_t(h[1]);
  if (enc > 1 || uv > 4)
    throw std::runtime_error("Invalid PMX encoding/UV count");
  int vi = h[2], ti = h[3], mi = h[4], bi = h[5];
  for (int i = 2; i < 8; i++)
    if (h[i] != 1 && h[i] != 2 && h[i] != 4)
      throw std::runtime_error("Invalid PMX index width");
  rig.name = PmxText(r, enc, decode);
  for (int i = 0; i < 3; i++)
    PmxText(r, enc, decode);
  auto n = r.count(33);
  for (uint32_t i = 0; i < n; i++) {
    r.skip(32 + 16 * uv);
    int skin = r.read<uint8_t>();
    switch (skin) {
    case 0:
      r.skip(bi);
      break;
    case 1:
      r.skip(2 * bi + 4);
      break;
    case 2:
    case 4:
      r.skip(4 * bi + 16);
      break;
    case 3:
      r.skip(2 * bi + 4 + 36);
      break;
    default:
      throw std::runtime_error("Unknown PMX skinning type");
    }
    r.skip(4);
  }
  n = r.count(vi, 20000000);
  r.skip(size_t(n) * vi);
  n = r.count(4, 100000);
  for (uint32_t i = 0; i < n; i++)
    PmxText(r, enc, decode);
  n = r.count(8, 100000);
  for (uint32_t i = 0; i < n; i++) {
    PmxText(r, enc, decode);
    PmxText(r, enc, decode);
    r.skip(65);
    r.skip(ti * 2 + 1);
    auto shared = r.read<uint8_t>();
    if (shared > 1)
      throw std::runtime_error("Invalid PMX toon flag");
    r.skip(shared ? 1 : ti);
    PmxText(r, enc, decode);
    r.skip(4);
  }
  n = r.count(8, 8192);
  rig.bones.reserve(n);
  for (uint32_t i = 0; i < n; i++) {
    RigBone b;
    b.name = PmxText(r, enc, decode);
    PmxText(r, enc, decode);
    b.rest = r.vec();
    b.parent = PmxIndex(r, bi);
    b.layer = r.read<int32_t>();
    auto flags = r.read<uint16_t>();
    if (b.parent < -1)
      throw std::runtime_error("Invalid PMX parent");
    if (flags & 1)
      PmxIndex(r, bi);
    else
      r.skip(12);
    if (flags & 0x300) {
      b.grant = PmxIndex(r, bi);
      b.grantWeight = r.number();
      b.grantRotation = (flags & 0x100) != 0;
      b.grantPosition = (flags & 0x200) != 0;
      b.grantLocal = (flags & 0x80) != 0;
    }
    if (flags & 0x400) {
      b.fixed = true;
      b.axis = r.vec();
    }
    if (flags & 0x800) {
      b.localAxes = true;
      b.axisX = r.vec();
      b.axisZ = r.vec();
    }
    if (flags & 0x2000) {
      r.skip(4);
      rig.warnings.push_back(b.name + ": external parent ignored");
    }
    if (flags & 0x1000)
      rig.warnings.push_back(b.name +
                             ": after-physics bone evaluated without physics");
    if (flags & 0x20) {
      b.effector = PmxIndex(r, bi);
      int loops = r.read<int32_t>();
      if (loops < 0 || loops > 100000)
        throw std::runtime_error("Invalid PMX IK iteration count");
      b.iterations = (std::min)(loops, 64);
      if (loops > 64)
        rig.warnings.push_back(b.name + ": IK iterations limited to 64");
      b.angleLimit = Clamp(std::fabs(r.number()), 0, 3.14159265f);
      auto k = r.count(bi + 1, 256);
      for (uint32_t j = 0; j < k; j++) {
        IkLink l;
        l.bone = PmxIndex(r, bi);
        l.limited = r.read<uint8_t>() != 0;
        if (l.limited) {
          l.minimum = r.vec();
          l.maximum = r.vec();
          if (l.minimum.x > l.maximum.x || l.minimum.y > l.maximum.y ||
              l.minimum.z > l.maximum.z)
            throw std::runtime_error("Invalid PMX IK limits");
        }
        b.links.push_back(l);
      }
    }
    rig.bones.push_back(std::move(b));
  }
  // Read morph metadata for the report, without importing mesh/material data.
  if (r.remaining()) {
    n = r.count(14, 100000);
    for (uint32_t i = 0; i < n; ++i) {
      auto name = Name(PmxText(r, enc, decode));
      PmxText(r, enc, decode);
      r.read<uint8_t>();
      uint8_t type = r.read<uint8_t>();
      size_t stride = 0;
      if (type == 0 || type == 9)
        stride = mi + 4;
      else if (type == 1)
        stride = vi + 12;
      else if (type == 2)
        stride = bi + 28;
      else if (type >= 3 && type <= 7)
        stride = vi + 16;
      else if (type == 8)
        stride = mi + 113;
      else if (type == 10)
        stride = uint8_t(h[7]) + 25;
      else
        throw std::runtime_error("Unknown PMX morph type");
      auto entries = r.count(stride, 20000000);
      r.skip(size_t(entries) * stride);
      rig.morphTypes[name] = type;
    }
  }
  rig.finish();
  return rig;
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
inline int FindSourceRole(const RigDefinition &rig,int role) {
  int exact=rig.find(RoleNames()[role]);if(exact>=0)return exact;
  const auto keys=SourceRoleKeys(role);int found=-1;
  for(int i=0;i<int(rig.bones.size());++i) {
    auto candidate=MotionBoneKey(rig.bones[i].name);
    if(std::find(keys.begin(),keys.end(),candidate)==keys.end())continue;
    if(found>=0)return -1; // An ambiguous role remains a manual choice.
    found=i;
  }
  return found;
}
inline std::array<int,55> DefaultSourceRoles(const RigDefinition &rig) {
  std::array<int,55> roles;for(int r=0;r<55;++r)roles[r]=FindSourceRole(rig,r);
  if(roles[0]<0)roles[0]=rig.find(u8"センター");
  if(roles[8]<0)roles[8]=roles[7];if(roles[54]<0)roles[54]=roles[8];
  for(int side=0;side<2;++side)if(roles[24+side*15]<0) {
    const auto prefix=std::string(side?u8"右":u8"左");
    roles[24+side*15]=rig.find(prefix+u8"親指1");roles[25+side*15]=rig.find(prefix+u8"親指2");roles[26+side*15]=-1;
  }
  return roles;
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
inline std::map<std::string,std::string> ResolvedTrackBindings(const RigDefinition &rig,const MotionClip &clip,
                                                           const std::map<std::string,std::string> &manual) {
  auto bindings=manual;
  std::map<std::string,std::vector<std::string>> bones,tracks;
  std::set<std::string> reserved;
  for(const auto &b:rig.bones) {
    bones[MotionBindingKey(b.name)].push_back(b.name);
    auto m=manual.find(b.name);
    if(m!=manual.end())reserved.insert(m->second);
    else if(clip.bones.count(b.name)||clip.ik.count(b.name))reserved.insert(b.name);
  }
  std::set<std::string> names;for(const auto &v:clip.bones)names.insert(v.first);for(const auto &v:clip.ik)names.insert(v.first);
  for(const auto &name:names)if(!rig.ignoredTracks.count(name))tracks[MotionBindingKey(name)].push_back(name);
  for(const auto &v:bones)if(v.second.size()==1) {
    const auto &bone=v.second[0];auto found=tracks.find(v.first);
    if(manual.count(bone)||clip.bones.count(bone)||clip.ik.count(bone)||found==tracks.end()||found->second.size()!=1)continue;
    const auto &track=found->second[0];if(!reserved.count(track)) {bindings[bone]=track;reserved.insert(track);}
  }
  return bindings;
}
// The source rest pose is selected manually; a VMD contains no rest skeleton.
inline RigDefinition StandardRig(BuiltinRigPreset preset = BuiltinRigPreset::StandardMmd) {
  RigDefinition r;
  r.name = "Standard MMD";
  r.builtin = true;
  r.builtinPreset = preset;
  auto add = [&](std::string name, std::string parent, Vec3 p) {
    RigBone b;
    b.name = Name(name);
    b.parent = parent.empty() ? -1 : r.find(parent);
    b.rest = p;
    int i = int(r.bones.size());
    r.names[b.name] = i;
    r.bones.push_back(b);
    return i;
  };
  add(u8"全ての親", "", {0, 0, 0});
  add(u8"センター", u8"全ての親", {0, 10, 0});
  add(u8"グルーブ", u8"センター", {0, 10, 0});
  add(u8"腰", u8"グルーブ", {0, 10, 0});
  add(u8"下半身", u8"腰", {0, 10, 0});
  add(u8"上半身", u8"腰", {0, 11.3f, 0});
  add(u8"上半身2", u8"上半身", {0, 13, 0});
  add(u8"上半身3", u8"上半身2", {0, 14, 0});
  add(u8"首", u8"上半身3", {0, 15.6f, 0});
  add(u8"頭", u8"首", {0, 16.6f, 0});
  add(u8"両目", u8"頭", {0, 17.1f, -0.5f});
  for (int side = 0; side < 2; side++) {
    std::string s = side ? u8"右" : u8"左";
    float x = side ? -1.f : 1.f;
    add(s + u8"目", u8"両目", {x * .35f, 17.1f, -.6f});
    add(s + u8"肩P", u8"上半身3", {x * .7f, 14.9f, 0});
    add(s + u8"肩", s + u8"肩P", {x * .9f, 14.9f, 0});
    add(s + u8"肩C", s + u8"肩", {x * 1.7f, 14.8f, 0});
    auto &cancel = r.bones.back();
    cancel.grant = r.find(s + u8"肩P");
    cancel.grantWeight = -1;
    cancel.grantRotation = true;
    cancel.grantLocal = true;
    add(s + u8"腕", s + u8"肩C", {x * 1.7f, 14.8f, 0});
    add(s + u8"腕捩", s + u8"腕", {x * 2.5f, 14, 0});
    add(s + u8"ひじ", s + u8"腕捩", {x * 3.7f, 12.8f, 0});
    add(s + u8"手捩", s + u8"ひじ", {x * 4.6f, 11.9f, 0});
    add(s + u8"手首", s + u8"手捩", {x * 5.6f, 10.9f, 0});
    const char *fingers[] = {u8"親指", u8"人指", u8"中指", u8"薬指", u8"小指"};
    for (int f = 0; f < 5; f++) {
      std::string parent = s + u8"手首";
      for (int j = 0; j < 3; j++) {
        std::string name = s + fingers[f] + std::to_string(f == 0 ? j : j + 1);
        add(name, parent,
            {x * (5.8f + j * .3f), 10.7f - j * .3f, (f - 2) * .17f});
        parent = name;
      }
    }
    add(s + u8"足", u8"下半身", {x * .85f, 9.5f, 0});
    add(s + u8"ひざ", s + u8"足", {x * .85f, 5, -.15f});
    add(s + u8"足首", s + u8"ひざ", {x * .85f, 1, 0});
    add(s + u8"つま先", s + u8"足首", {x * .85f, .25f, -1.2f});
    add(s + u8"足IK親", u8"全ての親", {x * .85f, 1, 0});
    int foot = add(s + u8"足IK", s + u8"足IK親", {x * .85f, 1, 0});
    r.bones[foot].effector = r.find(s + u8"足首");
    r.bones[foot].iterations = 64;
    r.bones[foot].angleLimit = .6f;
    r.bones[foot].links = {
        {r.find(s + u8"ひざ"), true, {-3.13f, 0, 0}, {-.001f, 0, 0}},
        {r.find(s + u8"足"), false, {}, {}}};
    int toe = add(s + u8"つま先IK", s + u8"足IK", {x * .85f, .25f, -1.2f});
    r.bones[toe].effector = r.find(s + u8"つま先");
    r.bones[toe].iterations = 8;
    r.bones[toe].angleLimit = .5f;
    r.bones[toe].links = {{r.find(s + u8"足首"), false, {}, {}}};
  }
  if (preset == BuiltinRigPreset::ExtractedTPose) {
    r.name = "Extracted T-pose";
    r.bones[r.find(u8"センター")].rest = {};
    r.bones[r.find(u8"グルーブ")].rest = r.bones[r.find(u8"下半身")].rest;
    const char *fingers[] = {u8"親指", u8"人指", u8"中指", u8"薬指", u8"小指"};
    for (auto s : {std::string(u8"左"), std::string(u8"右")}) {
      float armHeight = r.bones[r.find(s + u8"腕")].rest.y;
      for (auto part : {u8"腕捩", u8"ひじ", u8"手捩", u8"手首"})
        r.bones[r.find(s + part)].rest.y = armHeight;
      for (int f = 0; f < 5; ++f)
        for (int j = 0; j < 3; ++j) {
          int index = r.find(s + fingers[f] + std::to_string(f == 0 ? j : j + 1));
          r.bones[index].rest.y = armHeight;
        }
      r.bones[r.find(s + u8"ひざ")].rest.z = 0;
    }
  }
  // A thumb is an oblique, out-of-plane chain, not another parallel finger.
  // Approximate an open hand in palm-length units: forward, index-finger side,
  // and palm normal. Construct AFTER the A/T preset so changing arm rest pose
  // cannot flatten the thumb's opposition. Imported PMX geometry is untouched.
  const Vec3 thumb[] = {{.14f,.20f,-.16f}, {.47f,.40f,-.30f}, {.80f,.61f,-.44f}};
  for (int side=0;side<2;++side) {
    const std::string s=side?u8"右":u8"左";
    const Vec3 wrist=r.bones[r.find(s+u8"手首")].rest;
    const Vec3 palm=r.bones[r.find(s+u8"中指1")].rest-wrist;
    const float length=Len(palm);
    const Vec3 forward=Norm(palm);
    Vec3 radial=r.bones[r.find(s+u8"人指1")].rest-r.bones[r.find(s+u8"小指1")].rest;
    radial=Norm(radial-forward*Dot(radial,forward));
    const Vec3 normal=Cross(forward,radial)*(side?-1.f:1.f);
    for (int joint=0;joint<3;++joint) {
      const auto p=thumb[joint];
      r.bones[r.find(s+u8"親指"+std::to_string(joint))].rest=
          wrist+(forward*p.x+radial*p.y+normal*p.z)*length;
    }
  }
  r.finish();
  return r;
}
inline Quat AxisOnly(Quat q, Vec3 axis) {
  axis = Norm(axis);
  float t = q.x * axis.x + q.y * axis.y + q.z * axis.z;
  return NormQ({axis.x * t, axis.y * t, axis.z * t, q.w});
}
inline Vec3 EulerXYZ(Quat q) {
  return {
      std::atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)),
      std::asin(Clamp(2 * (q.w * q.y - q.z * q.x), -1, 1)),
      std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))};
}
inline Quat FromXYZ(Vec3 v) {
  return NormQ(Quat::AxisAngle({0, 0, 1}, v.z) *
               Quat::AxisAngle({0, 1, 0}, v.y) *
               Quat::AxisAngle({1, 0, 0}, v.x));
}
struct RigPose {
  std::vector<Vec3> positions;
  std::vector<Quat> rotations, localRot;
};
class RigEvaluator {
  const RigDefinition *rig_ = nullptr;
  const MotionClip *clip_ = nullptr;
  std::vector<const std::vector<BoneKey> *> tracks_;
  std::vector<std::string> trackNames_, mirrorNames_;
  std::vector<const std::vector<BoneKey> *> mirrorTracks_;
  bool mirrored_=false;
  std::vector<LocalPose> anim_;
  std::vector<Quat> ik_;
  std::vector<Quat> appendRot_;
  std::vector<Vec3> appendPos_;
  std::vector<int> controllers_;
  std::vector<bool> needed_;
  void world() {
    for (int i : rig_->order) {
      const auto &b = rig_->bones[i];
      Quat q = anim_[i].rotation;
      Vec3 p = anim_[i].position;
      for (const auto &d : b.drivers) {
        Quat control = NormQ(anim_[d.source].rotation);
        if (control.w < 0) control = {-control.x,-control.y,-control.z,-control.w};
        const float component = d.inputAxis == 0 ? control.x : d.inputAxis == 1 ? control.y : control.z;
        const float angle = 2 * std::atan2(component, control.w);
        q = NormQ(q * Quat::AxisAngle(d.axis, Clamp(angle * d.weight, -2.6f, 2.6f)));
      }
      appendRot_[i] = {};
      appendPos_[i] = {};
      if (b.grant >= 0) {
        const auto &source = rig_->bones[b.grant];
        Quat g = (b.grantLocal || source.grant < 0) ? anim_[b.grant].rotation
                                                    : appendRot_[b.grant];
        g = NormQ(ik_[b.grant] * g);
        if (b.grantRotation) {
          float w = b.grantWeight;
          if (w < 0)
            g = Conj(g);
          appendRot_[i] = Quat::Slerp({}, g, std::fabs(w));
          q = NormQ(q * appendRot_[i]);
        }
        if (b.grantPosition) {
          appendPos_[i] =
              ((b.grantLocal || source.grant < 0) ? anim_[b.grant].position
                                                  : appendPos_[b.grant]) *
              b.grantWeight;
          p = p + appendPos_[i];
        }
      }
      q = NormQ(ik_[i] * q);
      if (b.fixed)
        q = AxisOnly(q, b.axis);
      pose.localRot[i] = q;
      if (b.parent >= 0) {
        pose.positions[i] = pose.positions[b.parent] +
                            pose.rotations[b.parent] *
                                (b.rest - rig_->bones[b.parent].rest + p);
        pose.rotations[i] = NormQ(pose.rotations[b.parent] * q);
      } else {
        pose.positions[i] = b.rest + p;
        pose.rotations[i] = q;
      }
    }
  }
  void setWorld(int i, Quat q) {
    int p = rig_->bones[i].parent;
    Quat parent = p >= 0 ? pose.rotations[p] : Quat{};
    Quat local = NormQ(Conj(parent) * q);
    Quat base = NormQ(Conj(ik_[i]) * pose.localRot[i]);
    ik_[i] = NormQ(local * Conj(base));
    world();
  }

public:
  RigPose pose;
  std::vector<std::string> unmapped;
  std::vector<std::string> unmappedIk;
  std::map<std::string,std::vector<std::string>> mappedTracks;
  void bind(const RigDefinition &rig, const MotionClip &clip,
            const std::map<std::string, std::string> &bindings = {},
            const std::vector<int> &outputs = {}) {
    rig_ = &rig;
    clip_ = &clip;
    tracks_.assign(rig.bones.size(), nullptr);
    trackNames_.resize(rig.bones.size());
    anim_.resize(rig.bones.size());
    ik_.resize(rig.bones.size());
    appendRot_.resize(rig.bones.size());
    appendPos_.resize(rig.bones.size());
    pose.positions.resize(rig.bones.size());
    pose.rotations.resize(rig.bones.size());
    pose.localRot.resize(rig.bones.size());
    needed_.assign(rig.bones.size(), false);
    unmapped.clear();
    mappedTracks.clear();
    const auto resolved=ResolvedTrackBindings(rig,clip,bindings);
    for (size_t i = 0; i < rig.bones.size(); ++i) {
      auto binding = resolved.find(rig.bones[i].name);
      trackNames_[i] = binding == resolved.end() ? rig.bones[i].name : binding->second;
      auto track = clip.bones.find(trackNames_[i]);
      if (!trackNames_[i].empty() && !rig.ignoredTracks.count(trackNames_[i]) && track != clip.bones.end()) tracks_[i] = &track->second;
    }
    mirrorNames_.resize(rig.bones.size());mirrorTracks_.assign(rig.bones.size(),nullptr);mirrored_=false;
    for(size_t i=0;i<rig.bones.size();++i) {
      auto other=rig.find(MirrorBoneName(rig.bones[i].name));
      if(outputs.size()==55)for(int role=0;role<55;++role)if(outputs[role]==int(i)&&outputs[MirrorRole(role)]>=0){other=outputs[MirrorRole(role)];break;}
      mirrorNames_[i]=other>=0?trackNames_[other]:MirrorBoneName(trackNames_[i]);
      auto track=clip.bones.find(mirrorNames_[i]);
      if(!mirrorNames_[i].empty()&&!rig.ignoredTracks.count(mirrorNames_[i])&&track!=clip.bones.end())mirrorTracks_[i]=&track->second;
    }
    std::set<int> scalarInputs;
    std::function<void(int)> need = [&](int i) {
      if (i < 0 || needed_[i])
        return;
      needed_[i] = true;
      need(rig.bones[i].parent);
      need(rig.bones[i].grant);
      // Scalar controls consume their own local key, not their parent's pose.
      for (const auto &d : rig.bones[i].drivers) scalarInputs.insert(d.source);
    };
    if (outputs.empty()) {
      for (int i : DefaultSourceRoles(rig)) need(i);
    } else {
      for (int i : outputs) need(i);
    }
    controllers_.clear();
    // Controllers can depend on other IK/aim chains. Discover to a fixed point
    // instead of relying on file order (a controller may precede its consumer).
    bool added = true;
    while (added) { added = false;
    for (const auto &a : rig.aims) if (needed_[a.bone] && !needed_[a.source]) {need(a.source);added=true;}
    for (size_t i = 0; i < rig.bones.size(); ++i) {
      const auto &b = rig.bones[i];
      bool relevant = false;
      for (const auto &l : b.links)
        relevant = relevant || needed_[l.bone];
      if (b.effector >= 0 && relevant && std::find(controllers_.begin(),controllers_.end(),int(i))==controllers_.end()) {
        controllers_.push_back(int(i));
        need(int(i));
        added = true;
      }
    }
    }
    std::stable_sort(
        controllers_.begin(), controllers_.end(),
        [&](int a, int b) { return rig.bones[a].layer < rig.bones[b].layer; });
    std::set<std::string> used;
    for (size_t i = 0; i < rig.bones.size(); ++i)
      if ((needed_[i] || scalarInputs.count(int(i))) && tracks_[i]) {
        used.insert(trackNames_[i]);mappedTracks[trackNames_[i]].push_back(rig.bones[i].name);
      }
    for (const auto &kv : clip.bones)
      if (!used.count(kv.first)) unmapped.push_back(kv.first);
    std::set<std::string> usedIk;
    auto useIk=[&](int c) {
      const auto &name=trackNames_[c];usedIk.insert(name);
      if(clip.ik.count(name)&&!rig.ignoredTracks.count(name)) {
        auto &destinations=mappedTracks[name];const auto &bone=rig.bones[c].name;
        if(std::find(destinations.begin(),destinations.end(),bone)==destinations.end())destinations.push_back(bone);
      }
    };
    for(int c:controllers_)useIk(c);
    for(const auto &a:rig.aims)if(needed_[a.bone])useIk(a.source);
    unmappedIk.clear();
    for(const auto &kv:clip.ik)if(!usedIk.count(kv.first)&&!rig.ignoredTracks.count(kv.first))unmappedIk.push_back(kv.first);
  }
  bool hasTrack(int bone,bool mirror=false) const { return bone >= 0 && size_t(bone) < tracks_.size() && (mirror?mirrorTracks_:tracks_)[bone]; }
  bool controlTracked(int bone) const {
    for (int i = bone; i >= 0; i = rig_->bones[i].parent) {
      if (i != bone && !rig_->bones[i].motionControl) break;
      if (hasTrack(i, mirrored_)) return true;
      const auto &name=(mirrored_?mirrorNames_:trackNames_)[i];
      if(!rig_->ignoredTracks.count(name)&&clip_->ik.count(name))return true;
    }
    return false;
  }
  bool ikEnabled(int controller, double frame, IkMode mode) const {
    if (std::find(controllers_.begin(), controllers_.end(), controller) == controllers_.end())
      return false;
    // Follow mode preserves FK-only clips. Force-on uses even stationary IK
    // targets from the selected rig; force-off bypasses every IK chain.
    return SampleIk(*clip_, (mirrored_?mirrorNames_:trackNames_)[controller], frame, mode) &&
           (!rig_->builtin || (rig_->bones[controller].controlIk ? controlTracked(controller) : hasTrack(controller,mirrored_)) || mode == IkMode::ForceOn);
  }
  void sample(double frame, IkMode mode = IkMode::FollowMotion,bool mirror=false) {
    mirrored_=mirror;const auto &tracks=mirror?mirrorTracks_:tracks_;
    for (size_t i = 0; i < anim_.size(); i++) {
      anim_[i] = tracks[i] ? SampleBone(*tracks[i], frame) : LocalPose{};
      if(mirror){anim_[i].position=MirrorX(anim_[i].position);anim_[i].rotation=MirrorX(anim_[i].rotation);}
      ik_[i] = {};
    }
    world();
    // PMX deformation order is relevant when IK chains share links.
    for (int c : controllers_) {
      const auto &controller = rig_->bones[c];
      if (!ikEnabled(c, frame, mode))
        continue;
      const Vec3 target = pose.positions[c];
      if (controller.controlIk && controller.links.size()==2) {
        const int a=controller.links[1].bone,b=controller.links[0].bone,e=controller.effector;
        const Vec3 pa=pose.positions[a],pb=pose.positions[b],pc=pose.positions[e];
        const Vec3 axis=Norm(pc-pa);
        Vec3 bend=(pb-pa)-axis*Dot(pb-pa,axis);
        const int parent=rig_->bones[a].parent;
        const Quat pr=parent>=0?pose.rotations[parent]:Quat{};
        // Preserve the animated elbow plane; use a body-relative backward pole
        // for a straight arm. Analytic IK converges even at full extension.
        if(Len(bend)<1e-4f)bend=pr*Vec3{0,0,1};
        Vec3 sa=pa,sb=pb,sc=pc;
        SolveTwoBone(sa,sb,sc,target,pa+Norm(bend)*10,true);
        setWorld(a,NormQ(Quat::FromTo(pb-pa,sb-sa)*pose.rotations[a]));
        setWorld(b,NormQ(Quat::FromTo(pose.positions[e]-pose.positions[b],sc-sb)*pose.rotations[b]));
        if(controller.copyIkRotation)setWorld(e,pose.rotations[c]);
        continue;
      }
      if (rig_->builtin &&
          (controller.name == u8"左足IK" || controller.name == u8"右足IK")) {
        int a = controller.links[1].bone, b = controller.links[0].bone,
            e = controller.effector;
        Vec3 pa = pose.positions[a], pb = pose.positions[b],
             pc = pose.positions[e];
        int parent = rig_->bones[a].parent;
        Vec3 pole = pa + (parent >= 0 ? pose.rotations[parent] : Quat{}) *
                             Vec3{0, 0, -10};
        Vec3 sa = pa, sb = pb, sc = pc;
        SolveTwoBone(sa, sb, sc, target, pole, true);
        setWorld(a, NormQ(Quat::FromTo(pb - pa, sb - sa) * pose.rotations[a]));
        setWorld(b, NormQ(Quat::FromTo(pose.positions[e] - pose.positions[b],
                                       sc - sb) *
                          pose.rotations[b]));
        continue;
      }
      for (int iter = 0; iter < controller.iterations; iter++) {
        if (Len(pose.positions[controller.effector] - target) < 1e-4f)
          break;
        for (const auto &link : controller.links) {
          int i = link.bone;
          Vec3 a = pose.positions[controller.effector] - pose.positions[i],
               b = target - pose.positions[i];
          if (Len(a) < 1e-6f || Len(b) < 1e-6f)
            continue;
          Quat d = Quat::FromTo(a, b);
          float angle = Quat::Angle({}, d);
          if (angle > controller.angleLimit && angle > 1e-6f)
            d = Quat::Slerp({}, d, controller.angleLimit / angle);
          int parent = rig_->bones[i].parent;
          Quat pr = parent >= 0 ? pose.rotations[parent] : Quat{};
          Quat local = NormQ(Conj(pr) * d * pose.rotations[i]);
          if (link.limited) {
            Vec3 e = EulerXYZ(local);
            e = {Clamp(e.x, link.minimum.x, link.maximum.x),
                 Clamp(e.y, link.minimum.y, link.maximum.y),
                 Clamp(e.z, link.minimum.z, link.maximum.z)};
            local = FromXYZ(e);
          }
          Quat base = NormQ(Conj(ik_[i]) * pose.localRot[i]);
          ik_[i] = NormQ(local * Conj(base));
          world();
        }
      }
      if (controller.copyIkRotation)
        setWorld(controller.effector, pose.rotations[c]);
    }
    for (const auto &a : rig_->aims) {
      if (!needed_[a.bone] || !controlTracked(a.source) ||
          !SampleIk(*clip_,(mirrored_?mirrorNames_:trackNames_)[a.source],frame,mode)) continue;
      const int parent = rig_->bones[a.bone].parent;
      const Quat pr = parent >= 0 ? pose.rotations[parent] : Quat{};
      const Vec3 direction = Conj(pr) * (pose.positions[a.source] - pose.positions[a.bone]);
      const Vec3 rest = rig_->bones[a.source].rest - rig_->bones[a.bone].rest;
      if (Len(direction)<1e-5f || Len(rest)<1e-5f) continue;
      Vec3 e = EulerXYZ(Quat::FromTo(rest,direction));
      // A gaze target must never roll the eye or flip it backward.
      e.x=Clamp(e.x,-.45f,.45f);e.y=Clamp(e.y,-.6f,.6f);e.z=0;
      setWorld(a.bone,NormQ(pr*FromXYZ(e)));
    }
  }
};
} // namespace mmd
