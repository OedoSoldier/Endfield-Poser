#pragma once
#include "math/mmd_retarget.h"

namespace mmd {
struct ContactOptions {
  bool enabled = false, scene = true, slope = true;
  float strength = 1, maxLift = .25f, sole = .025f, groundOffset = 0;
};
struct GroundPlane {
  Vec3 point, normal{0, 1, 0};
  bool valid = false;
};
struct ContactResult {
  std::array<bool, 2> active{false, false};
  int limited = 0;
  float penetration = 0;
};
inline bool ContactFinite(Vec3 v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 TransformPoint(const Matrix &m, Vec3 v) {
  return {m.m[0]*v.x+m.m[4]*v.y+m.m[8]*v.z+m.m[12],
          m.m[1]*v.x+m.m[5]*v.y+m.m[9]*v.z+m.m[13],
          m.m[2]*v.x+m.m[6]*v.y+m.m[10]*v.z+m.m[14]};
}
inline void ContactWorld(const RetargetProfile &p, SampledPose &s) {
  for (size_t i=0;i<p.bones.size();++i) {
    const auto &b=p.bones[i];
    s.worldRot[i]=b.parent<0?s.localRot[i]:NormQ(s.worldRot[b.parent]*s.localRot[i]);
    s.worldMatrix[i]=(b.parent<0?Matrix{}:s.worldMatrix[b.parent])*TRS(b.localPos,s.localRot[i],b.localScale);
    s.worldPos[i]=s.worldMatrix[i].position();
  }
}
inline void ContactSetWorld(const RetargetProfile &p, SampledPose &s, int i, Quat q) {
  int parent=p.bones[i].parent;
  s.localRot[i]=NormQ((parent<0?Quat{}:Conj(s.worldRot[parent]))*q);
  s.write[i]=true;
  ContactWorld(p,s);
}
// A conservative heel/toe support estimate from the calibrated rig. The
// user's sole allowance accounts for mesh/shoe thickness not in the skeleton.
inline std::array<Vec3,2> ContactSole(const RetargetProfile &p, int side,
                                       const Matrix &anchor) {
  int foot=p.roles[5+side], toe=p.roles[19+side];
  Vec3 a=TransformPoint(anchor,p.bones[foot].restPos);
  Vec3 t=toe<0?a:TransformPoint(anchor,p.bones[toe].restPos);
  float y=(std::min)(a.y,t.y);
  Matrix footWorld=anchor*p.bones[foot].restMatrix, inv;
  if (!Inverse(footWorld,inv)) return {};
  return {TransformPoint(inv,{a.x,y,a.z}),TransformPoint(inv,{t.x,y,t.z})};
}
// Runs after motion amplitude. No history: pause, seeking and loops produce
// the same result; a fresh sample is always the input, never a corrected pose.
inline ContactResult ApplyGroundContact(const RetargetProfile &p, SampledPose &s,
    const Matrix &anchor, const std::array<GroundPlane,2> &planes,
    const ContactOptions &o, const std::array<bool,2> &allowed={true,true}) {
  ContactResult result;
  if (!o.enabled || !std::isfinite(o.strength) || o.strength<=0 ||
      !std::isfinite(o.maxLift) || o.maxLift<=0 || !std::isfinite(o.sole) ||
      !std::isfinite(o.groundOffset) || s.localRot.size()!=p.bones.size()) return result;
  Matrix frame=anchor*TRS(s.rootOffset,Quat{}), inv;
  if (!Inverse(frame,inv)) return result;
  Quat rootRotation=Rotation(anchor);
  for(int side=0;side<2;++side) {
    int a=p.roles[1+side], b=p.roles[3+side], c=p.roles[5+side];
    const auto &g=planes[side];
    if (!allowed[side] || a<0 || b<0 || c<0 || !s.write[a] || !s.write[b] ||
        !s.write[c] || !g.valid || !ContactFinite(g.point) || !ContactFinite(g.normal)) continue;
    Vec3 n=Norm(g.normal);
    if (n.y<.65f) continue; // Reject walls/steep surfaces, never tilt the rig sideways.
    auto supports=ContactSole(p,side,anchor);
    auto clearance=[&]() {
      Matrix fw=frame*s.worldMatrix[c];
      float d=1e6f;
      for (Vec3 v:supports)
        d=(std::min)(d,Dot(TransformPoint(fw,v)-g.point,n)-Clamp(o.sole,0,.2f)-o.groundOffset*n.y);
      return d;
    };
    float before=clearance();
    if (!std::isfinite(before) || before>.035f) continue; // Leave lifted feet and jumps alone.
    float strength=Clamp(o.strength,0,1);
    Quat originalFoot=s.worldRot[c];
    if(o.slope) {
      float weight=Clamp((.035f-before)/.035f,0,1)*strength;
      Quat tilt=Quat::Slerp(Quat{},Quat::FromTo({0,1,0},n),weight);
      // Add terrain tilt to the authored foot pose, preserving tiptoe/heel gestures.
      ContactSetWorld(p,s,c,NormQ(Conj(rootRotation)*tilt*rootRotation*originalFoot));
    }
    float depth=(std::max)(0.f,-clearance()/n.y);
    float lift=(std::min)(depth,Clamp(o.maxLift,0,.5f))*strength;
    if(depth>o.maxLift+.001f) ++result.limited;
    if(lift>1e-6f) {
      Vec3 pa=s.worldPos[a],pb=s.worldPos[b],pc=s.worldPos[c];
      Vec3 goal=TransformPoint(inv,TransformPoint(frame,pc)+Vec3{0,lift,0});
      Vec3 pole=pb;
      if(Len(Cross(pc-pa,pb-pa))<1e-5f) {
        Vec3 bend=p.bones[b].restPos-(p.bones[a].restPos+p.bones[c].restPos)*.5f;
        if(Len(bend)<1e-4f) bend={0,0,1};
        pole=pa+bend;
      }
      Quat foot=s.worldRot[c];
      Vec3 aa=pa,bb=pb,cc=pc;
      SolveTwoBone(aa,bb,cc,goal,pole,true);
      ContactSetWorld(p,s,a,NormQ(Quat::FromTo(pb-pa,bb-aa)*s.worldRot[a]));
      ContactSetWorld(p,s,b,NormQ(Quat::FromTo(s.worldPos[c]-s.worldPos[b],cc-bb)*s.worldRot[b]));
      ContactSetWorld(p,s,c,foot);
      if(Len(s.worldPos[c]-goal)>.005f) ++result.limited;
      result.active[side]=true;
    } else result.active[side]=o.slope && before<.035f;
    result.penetration=(std::max)(result.penetration,(std::max)(0.f,-clearance()));
  }
  return result;
}
} // namespace mmd
