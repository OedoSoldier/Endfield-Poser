#pragma once
#include "mmd_secondary_motion.h"

namespace secondary_body {
using mmd_secondary::Finite;
using mmd_secondary::Limited;
struct Settings {
  float strength=1,vertical=1,lateral=1,depth=1,frequency=2.5f,damping=.65f;
  float angleLimit=.3141593f,offsetLimit=.012f;
};
inline float Clamp(float v,float lo,float hi,float fallback) {
  return std::isfinite(v)?std::clamp(v,lo,hi):fallback;
}
struct Sample {Vec3 position;Quat rotation;};
struct Result {Vec3 angle,offset;};
// World-space derivatives, body-space spring state. The sampled anchor is
// reconstructed from the parent and its original offset, never our output.
struct Spring {
  bool ready=false,wasPlaying=false,velocityReady=false;
  uint64_t epoch=0;double now=0,cursor=0;
  Sample previous;Vec3 velocity,angularVelocity,filtered,filteredTorque;
  Vec3 angleSpeed,offsetSpeed;Result result;
  void rebase(const Sample &s,double clock,double time,uint64_t rev,bool playing,bool clear) {
    ready=true;previous=s;now=clock;cursor=time;epoch=rev;wasPlaying=playing;
    velocityReady=false;velocity=angularVelocity=filtered=filteredTorque={};
    if(clear){result={};angleSpeed=offsetSpeed={};}
  }
  Result step(const Sample &s,double clock,double time,uint64_t rev,bool playing,const Settings &c) {
    if(!Finite(s.position)||!Finite(s.rotation)||!std::isfinite(clock)||!std::isfinite(time)) {
      *this={};return result;
    }
    double dt=clock-now;
    if(!ready||epoch!=rev||time<cursor-1e-5||Len(s.position-previous.position)>1.5f||
       Quat::Angle(NormQ(s.rotation),NormQ(previous.rotation))>1.5f||dt<0||dt>.25) {
      rebase(s,clock,time,rev,playing,true);return result;
    }
    if(!playing||!wasPlaying) {rebase(s,clock,time,rev,playing,false);return result;}
    if(dt<1e-5||time==cursor)return result;
    Vec3 v=(s.position-previous.position)*float(1/dt);
    Quat dq=NormQ(s.rotation*Conj(previous.rotation));if(dq.w<0)dq={-dq.x,-dq.y,-dq.z,-dq.w};
    Vec3 xyz{dq.x,dq.y,dq.z};float l=Len(xyz);
    Vec3 av=l>1e-7f?xyz*float(2*std::atan2(l,dq.w)/(l*dt)):Vec3{};
    Vec3 acceleration,torque;
    if(velocityReady) {
      acceleration=Conj(NormQ(s.rotation))*Limited((v-velocity)*float(-1/dt),70.f);
      acceleration.x*=Clamp(c.lateral,0,2,1);acceleration.y*=Clamp(c.vertical,0,2,1);acceleration.z*=Clamp(c.depth,0,2,1);
      // A small forward/down lever gives lateral roll/yaw and vertical pitch;
      // depth acceleration also has a bounded translational response.
      Vec3 lever{0,-.035f,.10f};
      torque=Cross(lever,acceleration)*(1/Dot(lever,lever))-
        (Conj(NormQ(s.rotation))*Limited((av-angularVelocity)*float(1/dt),100.f))*.4f;
    }
    velocityReady=true;velocity=v;angularVelocity=av;previous=s;now=clock;cursor=time;
    const float strength=Clamp(c.strength,0,3,0),omega=6.2831853f*Clamp(c.frequency,1,6,2.5f);
    const float damping=2*Clamp(c.damping,.25f,1.5f,.65f)*omega;
    const float maxAngle=Clamp(c.angleLimit,0,.6f,.3141593f),maxOffset=Clamp(c.offsetLimit,0,.025f,.012f);
    const int count=(std::max)(1,int(std::ceil(dt*240)));float h=float(dt/count),filter=1-std::exp(-h/.04f);
    for(int i=0;i<count;++i) {
      filtered=filtered+(acceleration-filtered)*filter;filteredTorque=filteredTorque+(torque-filteredTorque)*filter;
      angleSpeed=angleSpeed+(filteredTorque*strength-result.angle*(omega*omega)-angleSpeed*damping)*h;
      offsetSpeed=offsetSpeed+(filtered*(strength*.3f)-result.offset*(omega*omega)-offsetSpeed*damping)*h;
      result.angle=result.angle+angleSpeed*h;result.offset=result.offset+offsetSpeed*h;
      auto bound=[](Vec3 &p,Vec3 &v,float limit) {
        if(Len(p)>limit) {p=Limited(p,limit);Vec3 n=Norm(p);float outward=Dot(v,n);if(outward>0)v=v-n*outward;}
      };
      bound(result.angle,angleSpeed,maxAngle);bound(result.offset,offsetSpeed,maxOffset);
    }
    if(strength==0){result={};angleSpeed=offsetSpeed={};}
    return result;
  }
};
inline Quat RotationVector(Vec3 r) {float n=Len(r);return n>1e-7f?Quat::AxisAngle(r*(1/n),n):Quat{};}
} // namespace secondary_body
