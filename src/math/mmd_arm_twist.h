#pragma once
#include "math/mmd_rig.h"
#include "math/mmd_motion_calibration.h"

namespace mmd {
// Endfield's two serial twist helpers share one MMD twist: half locally at
// each link, hence half/full cumulatively. This natural-bind convention also
// appears in Sasye/EIEM's DirectVmd phase-6 twist mapping (AGPL-3.0).
// Never distribute the elbow's bend or the wrist's own FK rotation as twist.
struct ArmTwistBinding {
  int sourceStart=-1, sourceEndParent=-1, start=-1, end=-1, endRole=-1;
  std::array<int,2> helpers{-1,-1};
  Vec3 sourceAxis;
  std::array<Vec3,2> axes;
};
inline std::string ArmBoneKey(const std::string &name) {
  std::string key;
  for(unsigned char c:name) {
    if(c=='_'||c==' '||c=='-')continue;
    key+=char(c>='A'&&c<='Z'?c+('a'-'A'):c);
  }
  return key;
}
inline bool ArmTwistAngle(Quat q,Vec3 axis,float &angle) {
  const float length=QuatLen(q),axisLength=Len(axis);
  if(!std::isfinite(length)||!std::isfinite(axisLength)||length<1e-6f||axisLength<1e-6f)return false;
  q=NormQ(q);axis=Norm(axis);
  float projection=Dot(Vec3{q.x,q.y,q.z},axis);
  if(q.w<0 || (q.w==0&&projection<0)) {q.w=-q.w;projection=-projection;}
  if(q.w*q.w+projection*projection<1e-10f)return false;
  angle=2.f*std::atan2(projection,q.w);
  return std::isfinite(angle);
}
template<class Profile>
std::vector<ArmTwistBinding> BindArmTwists(const RigDefinition &source,
    const std::array<int,55> &roles,const Profile &target) {
  std::vector<ArmTwistBinding> out;
  auto below=[&](int child,int ancestor) {
    for(int n=0;child>=0&&n<128;++n,child=target.bones[child].parent)if(child==ancestor)return true;
    return false;
  };
  for(int side=0;side<2;++side)for(int wrist=0;wrist<2;++wrist) {
    const int startRole=13+side+wrist*2,endRole=startRole+2;
    const int ss=roles[startRole],se=roles[endRole];
    ArmTwistBinding b;b.sourceStart=ss;b.start=target.roles[startRole];
    b.end=target.roles[endRole];b.endRole=endRole;
    if(ss<0||se<0||b.start<0||b.end<0||!below(b.end,b.start))continue;
    if(!target.bones[b.start].calibrated||!target.bones[b.end].calibrated)continue;
    b.sourceAxis=Norm(source.bones[se].rest-source.bones[ss].rest);
    if(Len(b.sourceAxis)<.9f)continue;
    // Only axial intermediates on this anatomical segment are eligible.
    // Explicit role/track remaps are already resolved by RigEvaluator.
    const auto twistName=Name(std::string(side?u8"右":u8"左")+(wrist?u8"手捩":u8"腕捩"));
    int at=source.bones[se].parent,links=0;
    b.sourceEndParent=at;
    bool axial=true;
    while(at>=0&&at!=ss&&links<16) {
      const auto &node=source.bones[at];
      bool body=false;for(int r:roles)body|=r==at;
      const bool fixed=node.fixed&&Len(node.axis)>1e-6f&&std::fabs(Dot(Norm(node.axis),b.sourceAxis))>.995f;
      if(body||(!fixed&&node.name!=twistName)){axial=false;break;}
      at=node.parent;++links;
    }
    if(!axial||at!=ss||links<1)continue;
    const std::string stem=ArmBoneKey(target.bones[b.start].name);
    const std::string suffix=std::string(side?"r":"l")+(wrist?"forearm":"upperarm");
    if(stem.size()<=suffix.size()||stem.compare(stem.size()-suffix.size(),suffix.size(),suffix))continue;
    const auto prefix=stem.substr(0,stem.size()-suffix.size());
    const auto helper=prefix+(side?"r":"l")+(wrist?"foretwist":"uparmtwist");
    int parent=b.start;bool valid=true;
    for(int link=0;link<2;++link) {
      int found=-1;
      for(int i=0;i<int(target.bones.size());++i) {
        const auto &candidate=target.bones[i];
        if(candidate.parent!=parent||ArmBoneKey(candidate.name)!=helper+(link?"1":""))continue;
        if(found>=0){valid=false;break;}found=i;
      }
      if(!valid||found<0||target.bones[found].role>=0||!target.bones[found].calibrated){valid=false;break;}
      b.helpers[link]=found;parent=found;
    }
    if(!valid)continue;
    // Helpers must be side branches. Writing a helper above the hand or a
    // finger would apply twist twice and change the actual gesture.
    for(int index:target.roles)if(index>=0&&below(index,b.helpers[0]))valid=false;
    const auto axis=target.bones[b.end].restPos-target.bones[b.start].restPos;
    const float length=Len(axis);
    if(!valid||length<1e-5f)continue;
    float previous=-1;
    for(int link=0;link<2;++link) {
      const auto &bone=target.bones[b.helpers[link]];
      const auto delta=bone.restPos-target.bones[b.start].restPos;
      const float t=Dot(delta,axis)/(length*length);
      if(t<-.02f||t>1.02f||t<previous||Len(delta-axis*t)>length*.03f){valid=false;break;}
      previous=t;
      b.axes[link]=Norm(Conj(target.bones[bone.parent].restRot)*axis);
    }
    if(valid)out.push_back(b);
  }
  return out;
}
template<class Profile,class Pose>
bool ApplyArmTwists(const std::vector<ArmTwistBinding> &bindings,
    const RigPose &source,const Profile &target,const MotionAmplitude &amplitude,Pose &output,
    const MotionCalibration &calibration = {}) {
  bool changed=false;
  for(const auto &b:bindings) {
    if(!output.write[b.start]||!output.write[b.end])continue;
    const auto delta=NormQ(Conj(source.rotations[b.sourceStart])*source.rotations[b.sourceEndParent]);
    float angle=0;
    if(!ArmTwistAngle(delta,b.sourceAxis,angle))continue;
    angle*=amplitude.factor(b.endRole)*calibration.factor(b.endRole);
    for(int link=0;link<2;++link) {
      const int i=b.helpers[link];
      output.localRot[i]=NormQ(Quat::AxisAngle(b.axes[link],angle*.5f)*target.bones[i].localRot);
      output.write[i]=true;
    }
    changed=true;
  }
  return changed;
}
} // namespace mmd
