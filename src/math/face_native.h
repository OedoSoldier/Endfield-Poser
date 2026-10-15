#pragma once
#include "math/quat_math.h"
#include <algorithm>

// SkeletalMorph stores Maya Euler degrees, not Unity Euler increments.
// Keep its authored neutral Euler branch: adding a delta after converting the
// neutral pose to a quaternion loses the original rotation axes.
namespace face_native {
struct Reference { Vec3 euler; bool valid=false; };
struct Delta { Vec3 position,rotation,scale; };
inline Quat FromMaya(Vec3 degrees) {
  constexpr float radians=3.14159265358979f/180.f;
  return NormQ(Quat::AxisAngle({0,0,1},-degrees.z*radians)*
               Quat::AxisAngle({0,1,0},-degrees.y*radians)*
               Quat::AxisAngle({1,0,0}, degrees.x*radians));
}
inline Vec3 ToMaya(Quat q) {
  q=NormQ(q);constexpr float degrees=180.f/3.14159265358979f;
  float y=(std::max)(-1.f,(std::min)(1.f,2*(q.w*q.y-q.x*q.z)));
  return {std::atan2(2*(q.w*q.x+q.y*q.z),1-2*(q.x*q.x+q.y*q.y))*degrees,
          -std::asin(y)*degrees,
          -std::atan2(2*(q.w*q.z+q.x*q.y),1-2*(q.y*q.y+q.z*q.z))*degrees};
}
inline Vec3 ToMayaNear(Quat q,Vec3 reference) {
  auto nearest=[](float value,float anchor){return value+360.f*std::round((anchor-value)/360.f);};
  auto a=ToMaya(q),b=Vec3{a.x+180,180-a.y,a.z+180};
  auto align=[&](Vec3 v){return Vec3{nearest(v.x,reference.x),nearest(v.y,reference.y),nearest(v.z,reference.z)};};
  a=align(a);b=align(b);return Len(a-reference)<=Len(b-reference)?a:b;
}
inline Quat Correction(const Reference &reference,Quat neutral,Vec3 delta) {
  if(Len(delta)<1e-8f)return {};
  const Vec3 base=reference.valid?reference.euler:ToMaya(neutral);
  return NormQ(FromMaya(base+delta)*Conj(FromMaya(base)));
}
inline Vec3 BoundedScale(Vec3 scale) {
  // Degenerate transforms cannot be inverted for regional composition.
  auto bound=[](float v){return std::isfinite(v)?(std::max)(.01f,(std::min)(4.f,v)):1.f;};
  return {bound(scale.x),bound(scale.y),bound(scale.z)};
}
inline Vec3 AddScale(Vec3 base,Vec3 delta) {
  auto add=[](float a,float b){
    if(!std::isfinite(b)||b==0)return a;
    const float sign=a<0?-1.f:1.f;
    return sign*(std::max)(.01f,(std::min)(4.f,sign*(a+b)));
  };
  return {add(base.x,delta.x),add(base.y,delta.y),add(base.z,delta.z)};
}
inline Vec3 Product(Vec3 a,Vec3 b) {return {a.x*b.x,a.y*b.y,a.z*b.z};}
inline Vec3 Ratio(Vec3 a,Vec3 b) {
  auto ratio=[](float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(b)>1e-6f?a/b:1.f;};
  return {ratio(a.x,b.x),ratio(a.y,b.y),ratio(a.z,b.z)};
}
}
