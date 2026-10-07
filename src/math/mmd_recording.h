#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace mmd_recording {
struct Options {
  int fps=60, warmup=2;
  bool squad=false, layered=false, separate=false, followAA=true,fastPng=true;
  bool jpegBackground=true;
  bool hideIntermediate=true;
  int jpegQuality=95;
};
struct Plan {
  int fps=60;
  uint32_t frames=0;
  double duration=0;
  double time(uint32_t index) const {return double(index)/fps;}
};
inline Plan MakePlan(const Options &o,double duration) {
  if((o.fps!=30&&o.fps!=60)||o.warmup<0||o.warmup>2||o.jpegQuality<60||o.jpegQuality>100||
      !std::isfinite(duration)||duration<=0||duration>3600)
    throw std::runtime_error("Invalid recording options or duration");
  // Half-open interval [0,duration): no duplicate terminal frame, no cumulative
  // floating point drift. 60 FPS samples VMD's 30 FPS keys at half-frame times.
  return {o.fps,uint32_t((std::max)(1.,std::ceil(duration*o.fps-1e-8))),duration};
}
enum class Kind { Scene, Background, Beauty, Matte };
inline bool Jpeg(const Options &o,Kind kind) {return o.layered&&o.jpegBackground&&kind==Kind::Background;}
struct Pass {Kind kind;int actor=-1;std::string layer;};
inline std::vector<Pass> Passes(const Options &o,const std::vector<int> &slots) {
  if(slots.empty())throw std::runtime_error("No recording actors");
  if(!o.layered)return {{Kind::Scene,-1,"full"}};
  // Save the actual final scene before isolating any layers. Compositing the
  // layers cannot recover native shadows, reflections or actor occlusion.
  // All passes share one held sample and commit as a single output frame.
  std::vector<Pass> out{{Kind::Scene,-1,"full"},{Kind::Background,-1,"background"}};
  if(!o.separate||slots.size()==1) {
    out.push_back({Kind::Beauty,-1,"characters"});out.push_back({Kind::Matte,-1,"characters"});
  } else for(int slot:slots) {
    auto name="character_"+std::to_string(slot+1);
    out.push_back({Kind::Beauty,slot,name});out.push_back({Kind::Matte,slot,name});
  }
  return out;
}
inline std::string FrameName(uint32_t index,bool jpeg=false) {
  auto s=std::to_string(index);return std::string(s.size()<6?6-s.size():0,'0')+s+(jpeg?".jpg":".png");
}
// Only acknowledged rendered frames count. Duplicate callbacks, missed render
// callbacks, and disk backpressure must never move the motion cursor.
struct Samples {
  int last=-1,count=0;
  void reset(){last=-1;count=0;}
  bool rendered(int frame,int target) {
    if(frame<0||frame==last)return false;
    last=frame;return ++count>=target;
  }
};
}
