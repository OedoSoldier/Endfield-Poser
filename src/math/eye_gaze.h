#pragma once
#include "math/quat_math.h"
#include <algorithm>

namespace eye_gaze {
enum class Mode { Follow, Manual, Camera };
struct Limits { float left=20,right=20,up=10,down=15; };
// Camera alignment is independent from manual direction and from the authored
// neutral pose. It is measured per model, not inferred from an iris bone pivot.
struct Profile {
  float cameraYaw=0,cameraPitch=0;
  Limits limits;
  float response=1,focusDepth=0,smoothing=.04f;
  // In eye-separation units, in the calibrated face basis. X mirrors outward.
  // These adjust the sight origins, never the authored neutral eye rotations.
  Vec3 centerOffset;
};
struct Settings { Mode mode=Mode::Follow; float yaw=0,pitch=0,strength=1; Profile profile; };
struct Basis { Vec3 right,up,forward; bool ready=false; };
inline bool Finite(Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline float Limit(float v,float amount) {return std::isfinite(v)?(std::max)(-amount,(std::min)(amount,v)):0;}
inline float SafeLimit(float v,float fallback,float maximum) {
  return std::isfinite(v)?(std::max)(0.f,(std::min)(maximum,v)):fallback;
}
inline Profile Sanitize(Profile p) {
  p.cameraYaw=Limit(p.cameraYaw,30);p.cameraPitch=Limit(p.cameraPitch,20);
  p.limits.left=SafeLimit(p.limits.left,20,30);p.limits.right=SafeLimit(p.limits.right,20,30);
  p.limits.up=SafeLimit(p.limits.up,10,20);p.limits.down=SafeLimit(p.limits.down,15,20);
  p.response=SafeLimit(p.response,1,2);p.focusDepth=std::isfinite(p.focusDepth)?(std::max)(-2.f,(std::min)(10.f,p.focusDepth)):0;
  p.smoothing=SafeLimit(p.smoothing,.04f,.2f);
  p.centerOffset={Limit(p.centerOffset.x,.45f),Limit(p.centerOffset.y,.5f),Limit(p.centerOffset.z,.5f)};
  return p;
}
inline void BoundAngles(float &yaw,float &pitch,const Limits &limits) {
  Profile p;p.limits=limits;const auto l=Sanitize(p).limits;
  yaw=std::isfinite(yaw)?(std::max)(-l.left,(std::min)(l.right,yaw)):0;
  pitch=std::isfinite(pitch)?(std::max)(-l.down,(std::min)(l.up,pitch)):0;
  // An ellipse also constrains diagonals: two individually safe extremes must
  // not combine into a larger, unsafe corner rotation.
  float h=yaw<0?l.left:l.right,v=pitch<0?l.down:l.up;
  float x=h>0?yaw/h:0,y=v>0?pitch/v:0,r=std::sqrt(x*x+y*y);
  if(r>1){yaw/=r;pitch/=r;}
}
inline Vec3 Direction(const Basis &b,float yaw,float pitch) {
  constexpr float rad=3.14159265358979f/180.f;yaw*=rad;pitch*=rad;
  return b.forward*(std::cos(pitch)*std::cos(yaw))+
      b.right*(std::cos(pitch)*std::sin(yaw))+b.up*std::sin(pitch);
}
inline void Angles(const Basis &b,Vec3 d,float &yaw,float &pitch) {
  constexpr float deg=180.f/3.14159265358979f;
  float x=Dot(d,b.right),y=Dot(d,b.up),z=Dot(d,b.forward);
  yaw=std::atan2(x,z)*deg;pitch=std::atan2(y,std::sqrt(x*x+z*z))*deg;
}
inline Quat ClampDelta(const Basis &b,Quat delta,const Limits &limits) {
  if(!b.ready)return {};
  if(!std::isfinite(QuatLen(delta))||QuatLen(delta)<1e-6f)return {};
  delta=NormQ(delta);Vec3 d=delta*b.forward;
  float yaw,pitch;Angles(b,d,yaw,pitch);BoundAngles(yaw,pitch,limits);
  // Keep the authored twist when only the gaze direction needs correction.
  Vec3 bounded=Direction(b,yaw,pitch);
  // Quat::FromTo intentionally ignores sub-degree rotations for body IK. Eyes
  // need a precise correction or the final limit can leak on small overshoots.
  float dot=(std::max)(-1.f,(std::min)(1.f,Dot(Norm(d),bounded)));
  Vec3 cross=Cross(d,bounded);
  Quat correction=dot>-.99999f?NormQ(Quat{cross.x,cross.y,cross.z,1+dot}):Quat::FromTo(d,bounded);
  return NormQ(correction*delta);
}
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
inline Quat DirectionDelta(const Basis &b,float yaw,float pitch) {
  const auto d=Direction(b,yaw,pitch),c=Cross(b.forward,d);
  // The body FromTo epsilon discards small visible eye movements (~0.25 deg).
  return NormQ({c.x,c.y,c.z,1+Dot(b.forward,d)});
}
inline float Visibility(const Basis &b,Vec3 direction) {
  if(!Finite(direction)||Len(direction)<1e-5f)return 0;
  // Ease back to neutral between 60 and 90 degrees off the face forward axis.
  // At the side/rear boundary both the weight and its derivative are zero.
  float t=SafeLimit(Dot(Norm(direction),b.forward)*2,0,1);
  return t*t*(3-2*t);
}
inline Quat Aim(const Basis &b,const Settings &settings,Vec3 targetDirection,bool validTarget) {
  if(!b.ready)return {};
  float yaw=0,pitch=0;
  auto profile=Sanitize(settings.profile);
  if(settings.mode==Mode::Camera&&validTarget&&Finite(targetDirection)&&Len(targetDirection)>1e-5f) {
    const float visibility=Visibility(b,targetDirection);
    if(visibility>0) {
      Angles(b,targetDirection,yaw,pitch);
      yaw=(yaw+profile.cameraYaw)*profile.response;pitch=(pitch+profile.cameraPitch)*profile.response;
      BoundAngles(yaw,pitch,profile.limits);yaw*=visibility;pitch*=visibility;
    }
  }
  if(settings.mode==Mode::Manual){yaw=Limit(settings.yaw,30);pitch=Limit(settings.pitch,20);}
  BoundAngles(yaw,pitch,profile.limits);
  return DirectionDelta(b,yaw,pitch);
}
inline void AimEyes(const Basis &b,const Settings &settings,Vec3 centerToCamera,
                    const Vec3 (&eyeOffsets)[2],Quat (&out)[2],Vec3 cameraForward={}) {
  if(settings.mode!=Mode::Camera||!b.ready||!Finite(eyeOffsets[0])||!Finite(eyeOffsets[1])) {
    out[0]=out[1]=Aim(b,settings,centerToCamera,false);return;
  }
  const auto profile=Sanitize(settings.profile);
  const float span=Len(eyeOffsets[1]-eyeOffsets[0]);
  Vec3 offsets[2];
  for(int i=0;i<2;++i)offsets[i]=eyeOffsets[i]+(b.right*(profile.centerOffset.x*(i?1.f:-1.f))+
      b.up*profile.centerOffset.y+b.forward*profile.centerOffset.z)*span;
  const auto center=(offsets[0]+offsets[1])*.5f;
  // Positive depth places the target behind the lens along its optical axis.
  if(Finite(cameraForward)&&Len(cameraForward)>.5f)centerToCamera=centerToCamera-Norm(cameraForward)*profile.focusDepth;
  centerToCamera=centerToCamera-center;
  for(auto &offset:offsets)offset=offset-center;
  const float visibility=Visibility(b,centerToCamera);
  out[0]=out[1]=Aim(b,settings,centerToCamera,visibility>0);
  if(visibility<=0||!std::isfinite(span)||span<1e-5f)return;
  // Apply small binocular convergence on top of the authored neutral eyes.
  // Iris centers are offset from the rotation pivots on stylized characters:
  // pivot->iris is NOT an independent optical axis to forcibly straighten.
  // Keep a minimum focus distance proportional to eye separation so a camera
  // pushed into the face cannot create excessive convergence.
  float distance=(std::max)(Len(centerToCamera),(std::max)(span,Len(offsets[1]-offsets[0]))*10.f);
  if(!std::isfinite(distance)||distance<1e-5f)return;
  Vec3 focus=(out[0]*b.forward)*distance;
  float commonYaw,commonPitch;Angles(b,out[0]*b.forward,commonYaw,commonPitch);
  for(int i=0;i<2;++i) {
    float yaw,pitch;Angles(b,focus-offsets[i],yaw,pitch);
    yaw=commonYaw+(yaw-commonYaw)*profile.response*visibility;
    pitch=commonPitch+(pitch-commonPitch)*profile.response*visibility;
    BoundAngles(yaw,pitch,profile.limits);out[i]=DirectionDelta(b,yaw,pitch);
  }
}
struct Smoothing {
  bool ready=false;double time=0;float yaw[2]={},pitch[2]={};
};
inline void SmoothEyes(const Basis &b,const Profile &profile,double now,Smoothing &state,Quat (&eyes)[2]) {
  if(!b.ready||!std::isfinite(now))return;
  const auto p=Sanitize(profile);
  // Exponential response depends on elapsed time, not render rate or number of
  // SMC/body/camera callbacks. First acquisition is immediate; manual bypasses.
  double dt=state.ready?(std::max)(0.,now-state.time):0;
  float alpha=!state.ready||p.smoothing<=0?1.f:float(-std::expm1(-dt/p.smoothing));
  for(int i=0;i<2;++i) {
    float yaw,pitch;Angles(b,eyes[i]*b.forward,yaw,pitch);
    state.yaw[i]+=(yaw-state.yaw[i])*alpha;state.pitch[i]+=(pitch-state.pitch[i])*alpha;
    BoundAngles(state.yaw[i],state.pitch[i],p.limits);
    eyes[i]=DirectionDelta(b,state.yaw[i],state.pitch[i]);
  }
  state.time=state.ready?(std::max)(state.time,now):now;state.ready=true;
}
} // namespace eye_gaze
