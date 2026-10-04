#pragma once
#include "cloth_collision_math.h"
namespace eiem_collision {
// Conservative bone-length envelope; existing authored shapes take precedence.
inline Capsule CalfEnvelope(V foot) {
  Capsule c;const auto length=Length(foot);
  if(!Finite(foot)||length<.15||length>.9)return c;
  c.a=foot*.14;c.b=foot*.88;c.ra=length*.16;c.rb=length*.105;c.valid=true;return c;
}
inline bool CalfSpan(const Capsule &c,V knee,V ankle) {
  const auto leg=ankle-knee,axis=c.b-c.a;const auto len=Length(leg),span=Length(axis);
  if(!c.valid||!Finite(knee)||!Finite(ankle)||len<.15||len>.9||span<len*.35||
      !std::isfinite(c.ra)||!std::isfinite(c.rb)||c.ra<=0||c.rb<=0)return false;
  return std::abs(Dot(axis,leg))/(len*span)>.8&&SegmentDistance((c.a+c.b)*.5,knee,ankle)<len*.30;
}
}
