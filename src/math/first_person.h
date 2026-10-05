#pragma once
#include "mmd_camera.h"
#include "mmd_retarget.h"

namespace first_person {
// Defaults and the native free-look mode follow honxi1/Endfield-Poser.
// Playback additionally supports a calibrated head basis and squad selection.
struct Settings {
  bool enabled=false, mmdEnabled=false, hideHead=true, followHead=true;
  int member=-1; // -1: controlled actor; 0..3: participating squad slot.
  Vec3 offset{0,.07f,-.045f}; // View right/up/forward, in world metres.
  float pitch=0, fov=60;
};
inline bool Finite(Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool Valid(const Settings &s) {
  return s.member>=-1&&s.member<4&&Finite(s.offset)&&std::fabs(s.offset.x)<=.6f&&
    std::fabs(s.offset.y)<=.6f&&std::fabs(s.offset.z)<=.6f&&std::isfinite(s.pitch)&&
    std::fabs(s.pitch)<=45&&std::isfinite(s.fov)&&s.fov>=25&&s.fov<=110;
}
inline bool HeadBasis(const mmd::RetargetProfile &p,Quat &headToView) {
  for(int r:{0,10,13,14})if(p.roles[r]<0||p.roles[r]>=int(p.bones.size())||!p.bones[p.roles[r]].calibrated)return false;
  auto pos=[&](int r){return p.bones[p.roles[r]].restPos;};
  const Vec3 across=pos(13)-pos(14),up=pos(10)-pos(0);
  const Quat head=p.bones[p.roles[10]].restRot;
  if(!Finite(across)||!Finite(up)||Len(across)<1e-4f||Len(up)<1e-4f||
      Len(Cross(across,up))<1e-5f||!std::isfinite(QuatLen(head))||QuatLen(head)<.5f)return false;
  // Retarget body +X is anatomical left, +Z is back. Unity camera looks +Z.
  const Quat view=NormQ(mmd::BodyBasis(pos(13),pos(14),pos(0),pos(10))*Quat::AxisAngle({0,1,0},3.14159265359f));
  headToView=NormQ(Conj(NormQ(head))*view);return true;
}
inline bool Solve(Vec3 head,Quat view,const Settings &s,mmd::CameraPose &out) {
  if(!Valid(s)||!Finite(head)||!std::isfinite(QuatLen(view))||QuatLen(view)<.5f)return false;
  view=NormQ(view*Quat::AxisAngle({1,0,0},s.pitch*.01745329252f));
  out={};out.rotation=view;out.position=head+view*s.offset;
  out.target=out.position+view*Vec3{0,0,3};out.fov=s.fov;out.perspective=true;
  return Finite(out.position)&&Finite(out.target);
}
} // namespace first_person
