#pragma once
#include "math/mmd_retarget.h"
#include <memory>

namespace mmd {
// An .epmotion is already in the actor's local axes. Only the extra VMD
// eyes need retargeting, before applying the native pose's joint calibration.
struct EditedEyes {
  std::unique_ptr<Retargeter> overlay;
  void bind(const RigDefinition &rig,const MotionClip &clip,const RetargetProfile &rest,
            const std::map<int,std::string> &roles,const std::map<std::string,std::string> &tracks) {
    overlay.reset();
    if(clip.bones.empty())return;
    overlay=std::make_unique<Retargeter>();
    overlay->bind(rig,clip,rest,roles,tracks);
    if(!overlay->drivesEye(0,false)&&!overlay->drivesEye(1,false)&&
       !overlay->drivesEye(0,true)&&!overlay->drivesEye(1,true))overlay.reset();
  }
  void compose(const RetargetProfile &rest,RetargetProfile &pose,
               std::vector<Quat> &rotations,std::vector<bool> &writes,
               Quat basis,std::vector<bool> &scales,bool mirror,
               bool preview,double frame,IkMode ikMode) {
    if(preview)return; // Blender supplies the final, already-composed pose.
    std::array<bool,2> owned{};
    for(int e=0;e<2;++e) {
      int idx=rest.roles[21+(mirror?1-e:e)];
      owned[e]=idx>=0&&writes[idx];
    }
    MirrorNative(rest,pose,rotations,writes,basis,&scales,mirror);
    if(overlay) {
      overlay->sample(frame,1,false,0,ikMode,{}, {},mirror);
      for(int e=0;e<2;++e)if(overlay->output.eyeDriven[e]) {
        const int idx=rest.roles[21+e];
        rotations[idx]=overlay->output.localRot[idx];owned[e]=true;
      }
    }
    // MirrorNative marks both sides for old pose cleanup. That doesn't give
    // the body player ownership of the unauthored eye.
    for(int e=0;e<2;++e)if(rest.roles[21+e]>=0)writes[rest.roles[21+e]]=owned[e];
  }
};
} // namespace mmd
