#pragma once
#include "math/quat_math.h"
#include <algorithm>

namespace eye_gaze {
enum class Mode { Follow, Manual, Camera };
struct Settings { Mode mode=Mode::Follow; float yaw=0,pitch=0; };
struct Basis { Vec3 right,up,forward; bool ready=false; };
inline bool Finite(Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Limit(float v,float amount) {return std::isfinite(v)?(std::max)(-amount,(std::min)(amount,v)):0;}
// Landmarks are in the same neutral head space. Do not assume the eye bone's
// local axes, or a world-up direction: those differ between character rigs.
inline Basis Calibrate(Vec3 left,Vec3 right,Vec3 mouth,Vec3 head,Vec3 optical={}) {
  Basis b;if(!Finite(left)||!Finite(right)||!Finite(mouth)||!Finite(head))return b;
  Vec3 center=(left+right)*.5f;
  b.right=Norm(right-left);
  if(Finite(optical)&&Len(optical)>1e-5f) {
    b.forward=Norm(optical-b.right*Dot(optical,b.right));
    b.up=Norm(Cross(b.forward,b.right));
    if(Dot(b.up,center-mouth)<0)b.up=b.up*-1.f;
    b.ready=Len(right-left)>1e-5f&&Len(b.forward)>.9f&&Len(b.up)>.9f;
    return b;
  }
  Vec3 up=center-mouth;b.up=Norm(up-b.right*Dot(up,b.right));
  b.forward=Norm(Cross(b.right,b.up));
  float depth=Dot(center-head,b.forward);
  if(Len(right-left)<1e-5f||Len(b.up)<.9f||std::fabs(depth)<1e-5f)return b;
  if(depth<0)b.forward=b.forward*-1.f;
  b.ready=true;return b;
}
inline Quat Aim(const Basis &b,const Settings &settings,Vec3 targetDirection,bool validTarget) {
  if(!b.ready)return {};
  constexpr float rad=3.14159265358979f/180.f;
  float yaw=0,pitch=0;
  if(settings.mode==Mode::Camera&&validTarget&&Finite(targetDirection)&&Len(targetDirection)>1e-5f) {
    auto d=Norm(targetDirection);float x=Dot(d,b.right),y=Dot(d,b.up),z=Dot(d,b.forward);
    // A camera behind the head is not a valid fixation. Return to forward
    // instead of flipping the eyes.
    if(z>0) {yaw=std::atan2(x,z)/rad;pitch=std::atan2(y,std::sqrt(x*x+z*z))/rad;}
  }
  yaw=Limit(yaw+Limit(settings.yaw,30),30)*rad;
  pitch=Limit(pitch+Limit(settings.pitch,20),20)*rad;
  Vec3 direction=b.forward*(std::cos(pitch)*std::cos(yaw))+
      b.right*(std::cos(pitch)*std::sin(yaw))+b.up*std::sin(pitch);
  return Quat::FromTo(b.forward,direction);
}
inline void AimEyes(const Basis &b,const Settings &settings,Vec3 centerToCamera,
                    const Vec3 (&eyeOffsets)[2],Quat (&out)[2]) {
  const bool front=Finite(centerToCamera)&&Len(centerToCamera)>1e-5f&&Dot(centerToCamera,b.forward)>0;
  out[0]=out[1]=Aim(b,settings,centerToCamera,front);
  if(settings.mode!=Mode::Camera||!b.ready||!front||
     !Finite(eyeOffsets[0])||!Finite(eyeOffsets[1]))return;
  // Apply small binocular convergence on top of the authored neutral eyes.
  // Iris centers are offset from the rotation pivots on stylized characters:
  // pivot->iris is NOT an independent optical axis to forcibly straighten.
  // Keep a minimum focus distance proportional to eye separation so a camera
  // pushed into the face cannot create excessive convergence.
  float span=Len(eyeOffsets[1]-eyeOffsets[0]);
  float distance=(std::max)(Len(centerToCamera),span*10.f);
  if(!std::isfinite(distance)||distance<1e-5f)return;
  Vec3 focus=(out[0]*b.forward)*distance;
  Settings centered;centered.mode=Mode::Camera;
  for(int i=0;i<2;++i)out[i]=Aim(b,centered,focus-eyeOffsets[i],true);
}
} // namespace eye_gaze
