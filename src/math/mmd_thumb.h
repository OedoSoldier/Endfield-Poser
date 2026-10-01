#pragma once
#include "math/mmd_retarget.h"

namespace mmd {
struct ThumbHandReference {
  Vec3 root, directions[3]; // In the character PMX's normalized palm frame.
  float lengths[3];
  Vec3 palmForward, palmAcross; // Rest palm axes in PMX/VMD model coordinates.
};
struct ThumbReference {
  const char *key, *label, *sourceHash;
  ThumbHandReference hands[2];
};
struct ThumbCalibration {
  const ThumbReference *reference=nullptr;
  int joints=0;
  bool sides[2]{};
};
} // namespace mmd

#include "../../resources/character-thumbs/builtin.h"

namespace mmd {
inline const ThumbReference *FindThumbReference(const std::string &key) {
  for(const auto &reference:CharacterThumbReferences)
    if(key==reference.key)return &reference;
  return nullptr;
}
inline bool ThumbPalm(const RetargetProfile &p,int side,Quat &frame,float &length) {
  const int hand=17+side,index=27+side*15;
  for(int role:{hand,index,index+3,index+9})
    if(p.roles[role]<0||!p.bones[p.roles[role]].calibrated)return false;
  auto at=[&](int role){return p.bones[p.roles[role]].restPos;};
  Vec3 forward=at(index+3)-at(hand),across=at(index)-at(index+9);
  length=Len(forward);forward=Norm(forward);
  across=Norm(across-forward*Dot(across,forward));
  if(!std::isfinite(length)||length<1e-5f||Len(across)<.9f)return false;
  frame=Basis(forward,across,Cross(forward,across));return true;
}
// Operates on a playback copy. Never save this over the original Avatar/body
// calibration, or the correction would be applied again on subsequent starts.
inline ThumbCalibration CalibrateThumbs(RetargetProfile &profile,const ThumbReference *reference) {
  ThumbCalibration result;result.reference=reference;
  profile.thumbSourcePalmValid.fill(false);
  if(!reference||!profile.valid())return result;
  for(int side=0;side<2;++side) {
    auto candidate=profile;Quat palm;float palmLength=0;
    if(!ThumbPalm(candidate,side,palm,palmLength))continue;
    const int hand=candidate.roles[17+side],first=24+side*15;
    const auto &ref=reference->hands[side];
    bool valid=true;int joints[3]{};
    for(int j=0;j<3;++j) {
      joints[j]=candidate.roles[first+j];
      const int i=joints[j],parent=j?joints[j-1]:hand;
      if(i<0||!candidate.bones[i].calibrated||candidate.bones[i].parent!=parent){valid=false;break;}
      for(int k=i;k>=0;k=candidate.bones[k].parent) {
        const auto scale=candidate.bones[k].localScale;
        if(scale.x<=1e-5f||std::fabs(scale.x-scale.y)>scale.x*1e-4f||
           std::fabs(scale.x-scale.z)>scale.x*1e-4f)valid=false;
      }
      const bool unavailableTip=j==2&&ref.lengths[j]==0&&Len(ref.directions[j])==0;
      if(!unavailableTip&&(!std::isfinite(Len(ref.directions[j]))||std::fabs(Len(ref.directions[j])-1)>.001f))valid=false;
    }
    if(!valid)continue;
    const auto root=Conj(palm)*(candidate.bones[joints[0]].restPos-candidate.bones[hand].restPos)*(1.f/palmLength);
    if(!std::isfinite(Len(ref.root))||Len(root-ref.root)>.4f)continue; // Different/custom hand geometry: leave it alone.
    for(int j=0;j<2;++j) {
      const float length=Len(candidate.bones[joints[j+1]].restPos-candidate.bones[joints[j]].restPos)/palmLength;
      if(!std::isfinite(ref.lengths[j])||ref.lengths[j]<1e-5f||length/ref.lengths[j]<.5f||length/ref.lengths[j]>2.f)valid=false;
    }
    if(!valid)continue;
    // The last joint requires a real calibrated tip marker. When absent, its
    // native local rotation follows the corrected middle joint; no tip guess.
    int tip=-1,children=0;
    for(size_t i=0;i<candidate.bones.size();++i)if(candidate.bones[i].parent==joints[2]) {
      ++children;if(candidate.bones[i].calibrated)tip=int(i);
    }
    if(children!=1||ref.lengths[2]==0)tip=-1;
    int corrected=0;
    for(int j=0;j<3;++j) {
      const int i=joints[j],child=j<2?joints[j+1]:tip;
      if(child<0)continue;
      Vec3 current=candidate.bones[child].restPos-candidate.bones[i].restPos;
      const Vec3 desired=palm*ref.directions[j];
      if(Len(current)<1e-6f||Dot(Norm(current),desired)<.15f){valid=false;break;}
      auto &bone=candidate.bones[i];
      const Quat world=NormQ(Quat::FromTo(current,desired)*bone.restRot);
      bone.localRot=NormQ(Conj(candidate.bones[bone.parent].restRot)*world);
      candidate.globals();++corrected;
    }
    if(valid) {
      Quat sourcePalm;
      if(PalmBasis({},ref.palmForward,ref.palmAcross,{},sourcePalm)) {
        candidate.thumbSourcePalm[side]=sourcePalm;
        candidate.thumbSourcePalmValid[side]=true;
      }
      profile=std::move(candidate);result.joints+=corrected;result.sides[side]=true;
    }
  }
  return result;
}
} // namespace mmd
