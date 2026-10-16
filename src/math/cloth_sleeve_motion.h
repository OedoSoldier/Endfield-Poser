#pragma once
#include "cloth_turn_motion.h"
#include "cloth_sleeve_material.h"

namespace cloth_turn {
// A bounded short response shapes the extra force, not the rendered bones.
// BBC remains responsible for integration, gravity, lengths and collisions.
// Like secondary-body motion, this follows actual mount acceleration and
// angular acceleration and is discarded across pause/seek/owner changes.
struct SleeveResponse {
  // The mount tracker already filters acceleration (50 ms). A second 1.6 Hz
  // two-pole filter kept pushing the old way well into a reversed arm motion.
  // Use a short, monotone response; BBC itself supplies the cloth's lag.
  static constexpr float responseSeconds=.035f;
  // BBC already responds to the moving attachment. This is only a supplement;
  // preserve the user's strength control without doubling the native swing.
  static constexpr float forceGain=.3f,accelerationPerStrength=3.f;
  Vec3 lag,output;
  double stamp=-1;
  void clear(){*this={};}
  Vec3 step(const Mailbox &box,Vec3 center,float strength,double now,float lightness,const Shape &shape) {
    if(!std::isfinite(strength)||strength<=0||!std::isfinite(now)||!Finite(center)) {clear();return {};}
    // Even maximum upward assistance must leave a downward acceleration.
    // The source's native inertia is still active; this is a small supplement.
    const float gain=std::clamp(strength,0.f,2.f);
    const float limit=(std::min)(accelerationPerStrength*gain,cloth_sleeve_material::Gravity()*.5f);
    Vec3 integrated;double duration=0,latest=-1;
    for(unsigned n=0;n<box.count;++n) {
      const auto &f=box.frames[n];const auto &m=f.body;
      if(now<f.time||now-f.time>.12||!std::isfinite(m.dt)||m.dt<=0||m.dt>.12f)continue;
      const auto lever=shape.lever(m.pose.rotation,center-m.pose.position);
      // Include rotation at a stationary shoulder as well as translation.
      // Scale the complete supplement, including optional moving-air lift.
      // No artificial force in a still pose.
      const Vec3 linear=m.acceleration*(-.85f)+Vec3{0,-.50f*m.acceleration.y,0};
      const Vec3 rotational=Cross(m.alpha,lever)*(-.85f)
          +Cross(m.omega,Cross(m.omega,lever))*(-.75f);
      const auto air=m.airResponse(m.pose.position+lever,lightness,Part::Cloth);
      integrated=integrated+Limited((linear+rotational+air)*(gain*forceGain),limit)*m.dt;
      duration+=m.dt;latest=f.time;
    }
    if(duration<=1e-6||!Finite(integrated)) {clear();return {};}
    // Never integrate the same published sample twice. Lowering strength must
    // still tighten the bound immediately, even before the next pose arrives.
    if(latest==stamp){lag=output=Limited(output,limit);return output;}
    if(latest<stamp||latest-stamp>.12){lag=output={};}
    stamp=latest;
    const auto target=Limited(integrated*float(1/duration),limit);
    // Exact first-order response to this averaged input. Convex interpolation
    // cannot overshoot or add a spring rebound. Do not filter the cloth pose.
    const float blend=1-std::exp(-float(duration)/responseSeconds);
    lag=Limited(lag+(target-lag)*blend,limit);
    output=lag;
    return output;
  }
};
}
