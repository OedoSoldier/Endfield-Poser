#pragma once
#include "cloth_turn_motion.h"

namespace cloth_turn {
// A bounded virtual spring shapes the extra force, not the rendered bones.
// BBC remains responsible for integration, gravity, lengths and collisions.
// Like secondary-body motion, this follows actual mount acceleration and
// angular acceleration and is discarded across pause/seek/owner changes.
struct SleeveResponse {
  Vec3 lag,velocity,output;
  double stamp=-1;
  void clear(){*this={};}
  Vec3 step(const Mailbox &box,Vec3 center,float strength,double now,float lightness,const Shape &shape) {
    if(!std::isfinite(strength)||strength<=0||!std::isfinite(now)||!Finite(center)) {clear();return {};}
    Vec3 integrated;double duration=0,latest=-1;
    for(unsigned n=0;n<box.count;++n) {
      const auto &f=box.frames[n];const auto &m=f.body;
      if(now<f.time||now-f.time>.12||!std::isfinite(m.dt)||m.dt<=0||m.dt>.12f)continue;
      const auto lever=shape.lever(m.pose.rotation,center-m.pose.position);
      // Stronger lateral and vertical lag than general cloth, including arm
      // rotation at a stationary shoulder. No artificial force in a still pose.
      const Vec3 linear=m.acceleration*(-.85f)+Vec3{0,-.50f*m.acceleration.y,0};
      const Vec3 rotational=Cross(m.alpha,lever)*(-.85f)
          +Cross(m.omega,Cross(m.omega,lever))*(-.75f);
      const auto air=m.airResponse(m.pose.position+lever,lightness,Part::Cloth);
      integrated=integrated+Limited((linear+rotational+air)*std::clamp(strength,0.f,2.f),12.f)*m.dt;
      duration+=m.dt;latest=f.time;
    }
    if(duration<=1e-6||!Finite(integrated)) {clear();return {};}
    if(latest==stamp)return output; // Never integrate the same published sample twice.
    if(latest<stamp||latest-stamp>.12){lag=velocity=output={};}
    stamp=latest;
    const auto target=Limited(integrated*float(1/duration),12.f);
    // 2.5 Hz, damping ratio .65: responsive start, short bounded follow-through.
    constexpr float omega=6.2831853f*2.5f,damping=2*.65f*omega;
    const int steps=(std::max)(1,int(std::ceil(duration*240)));
    const float h=float(duration/steps);
    for(int n=0;n<steps;++n) {
      velocity=velocity+((target-lag)*(omega*omega)-velocity*damping)*h;
      lag=lag+velocity*h;
      if(Len(lag)>12.f){lag=Limited(lag,12.f);const auto normal=Norm(lag);const auto out=Dot(velocity,normal);if(out>0)velocity=velocity-normal*out;}
    }
    output=Limited(target*.65f+lag*.35f,12.f);
    return output;
  }
};
}
