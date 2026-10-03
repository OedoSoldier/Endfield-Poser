#pragma once
#include "quat_math.h"
#include <algorithm>
#include <cstdint>

namespace mmd_secondary {
inline bool Finite(Vec3 v) {return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
inline bool Finite(Quat q) {return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w)&&DotQ(q,q)>.0001f;}
inline Vec3 Limited(Vec3 v,float limit) {float l=Len(v);return l>limit?v*(limit/l):v;}
struct Sample { Vec3 position; Quat rotation; Vec3 up{0,1,0},axis{1,0,0}; };
struct Settings {float strength=1,frequency=2.5f,limit=.35f;};
// A damped angular spring excited by the acceleration of the *posed body*.
// No clip names, fixed idle wave, or feedback from the simulated bone.
struct Spring {
  bool ready=false,wasPlaying=false,velocityReady=false;
  uint64_t epoch=0;
  double now=0,time=0;
  Sample previous;
  Vec3 velocity,angularVelocity;
  float acceleration=0,angle=0,speed=0;
  void rebase(const Sample &s,double clock,double cursor,uint64_t revision,bool playing,bool clear) {
    ready=true;previous=s;now=clock;time=cursor;epoch=revision;wasPlaying=playing;
    velocityReady=false;velocity={};angularVelocity={};acceleration=0;
    if(clear)angle=speed=0;
  }
  float step(const Sample &s,double clock,double cursor,uint64_t revision,bool playing,const Settings &cfg) {
    if(!Finite(s.position)||!Finite(s.rotation)||!Finite(s.up)||!Finite(s.axis)||
       !std::isfinite(clock)||!std::isfinite(cursor)) {ready=false;angle=speed=0;return 0;}
    double dt=clock-now;
    if(!ready||epoch!=revision||cursor<time-1e-5||
       Len(s.position-previous.position)>1.5f||Quat::Angle(s.rotation,previous.rotation)>1.5f) {
      rebase(s,clock,cursor,revision,playing,true);return angle;
    }
    if(!playing) {rebase(s,clock,cursor,revision,false,false);return angle;}
    if(!wasPlaying) {rebase(s,clock,cursor,revision,true,false);return angle;}
    if(dt<0||dt>.25) {rebase(s,clock,cursor,revision,true,true);return angle;}
    // Repeated samples in one render frame do not consume physical time.
    if(dt<1e-5||cursor==time)return angle;
    Vec3 v=(s.position-previous.position)*float(1/dt);
    Quat dq=NormQ(s.rotation*Conj(previous.rotation));
    if(dq.w<0)dq={-dq.x,-dq.y,-dq.z,-dq.w};
    Vec3 xyz{dq.x,dq.y,dq.z};float l=Len(xyz);
    Vec3 av=l>1e-7f?xyz*float(2*std::atan2(l,dq.w)/(l*dt)):Vec3{};
    float drive=0;
    if(velocityReady) {
      Vec3 a=Limited((v-velocity)*float(1/dt),100.f);
      Vec3 aa=Limited((av-angularVelocity)*float(1/dt),150.f);
      drive=-Dot(a,Norm(s.up))/0.16f-Dot(aa,Norm(s.axis))*.45f;
    }
    velocityReady=true;velocity=v;angularVelocity=av;previous=s;now=clock;time=cursor;
    const float strength=std::isfinite(cfg.strength)?std::clamp(cfg.strength,0.f,3.f):0;
    const float omega=6.2831853f*(std::isfinite(cfg.frequency)?std::clamp(cfg.frequency,1.f,6.f):2.5f);
    const float limit=std::isfinite(cfg.limit)?std::clamp(cfg.limit,0.f,.7f):.35f;
    const int steps=(std::max)(1,int(std::ceil(dt*240)));
    const float h=float(dt/steps),filter=1-std::exp(-h/.035f);
    for(int i=0;i<steps;++i) {
      acceleration+=(drive-acceleration)*filter;
      speed+=(acceleration*strength-omega*omega*angle-2*.65f*omega*speed)*h;
      angle+=speed*h;
      if(std::fabs(angle)>limit) {angle=std::clamp(angle,-limit,limit);if(angle*speed>0)speed=0;}
    }
    return angle;
  }
};
}
