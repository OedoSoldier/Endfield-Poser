#pragma once
#include "cloth_turn_motion.h"
#include <array>

namespace cloth_turn {
inline float RibbonDamping(float value) {
  // Near-unit damping on animated sleeve ribbons prevents supplemental motion.
  // Other authored material responses, including rigid pendants, stay intact.
  return std::isfinite(value)&&value>.8f&&value<=1.f?.35f:value;
}
// Resolve against animated body bones, never a physics result from the same
// cloth. Each component may have several fixed roots (e.g. both shoulders).
struct Attachment {
  struct Root {int role=-1;Vec3 offset;Quat rotation;};
  std::array<Root,8> roots{};unsigned count=0;
  bool add(int role,Pose body,Pose root) {
    if(role<0||count==roots.size()||!Finite(body.position)||!Finite(body.rotation)||
        !Finite(root.position)||!Finite(root.rotation))return false;
    auto inverse=Conj(NormQ(body.rotation));auto offset=inverse*(root.position-body.position);
    if(Len(offset)>2.5f)return false;
    roots[count++]={role,offset,NormQ(inverse*root.rotation)};return true;
  }
  template<class Read> bool pose(Read read,Pose &out) const {
    if(!count)return false;out={};Quat sum{0,0,0,0},first;
    for(unsigned n=0;n<count;++n) {
      Pose body;if(!read(roots[n].role,body)||!Finite(body.position)||!Finite(body.rotation))return false;
      auto q=NormQ(body.rotation*roots[n].rotation);
      if(!n)first=q;
      if(q.x*first.x+q.y*first.y+q.z*first.z+q.w*first.w<0)q={-q.x,-q.y,-q.z,-q.w};
      sum={sum.x+q.x,sum.y+q.y,sum.z+q.z,sum.w+q.w};
      out.position=out.position+body.position+NormQ(body.rotation)*roots[n].offset;
    }
    out.position=out.position*(1.f/count);out.rotation=NormQ(sum);return true;
  }
};
// A narrow, one-way contact relation requires authored/hierarchy evidence.
// Existing surface partners and ambiguous/reverse pairs are not augmented.
inline bool LayerPair(bool attachment,bool sameOwner,bool related,bool reverse,
    bool existingSurfacePair,unsigned candidates) {
  return attachment&&sameOwner&&related&&!reverse&&!existingSurfacePair&&candidates>0&&candidates<=8;
}
}
