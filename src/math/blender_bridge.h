#pragma once
#include "math/quat_math.h"
#include "math/mmd_camera.h"
#include "nlohmann/json.hpp"
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

// Wire values use Unity local coordinates. Blender owns conversion, including
// edit-bone roll; this protocol never guesses Euler axes or anatomical names.
namespace blender_bridge {
using Json=nlohmann::json;
constexpr int Protocol=1;
constexpr size_t MaxPacket=4*1024*1024;
struct Bone {int index=-1;Vec3 position;Quat rotation;};
struct Camera {bool active=false,perspective=true;Vec3 position,target;Quat rotation;float fov=45,size=5;};
struct Frame {
  uint64_t session=0,sequence=0;double time=0;
  Vec3 root;bool visible=true,preserveFace=false;
  Quat anchor;bool hasAnchor=false;
  std::vector<Bone> bones;std::map<std::string,float> faces;Camera camera;
};
inline mmd::CameraPose PlaceCamera(const Frame &frame,const mmd::CameraSettings &settings,
                                  Vec3 start,Quat anchor,Vec3 delta,Vec3 correction={}) {
  const auto &c=frame.camera;
  const Quat basis=NormQ(anchor*Quat::AxisAngle({0,1,0},settings.yaw*.01745329252f)*Conj(frame.hasAnchor?frame.anchor:Quat{}));
  Vec3 origin=start;
  if(settings.origin==mmd::CameraOrigin::Follow) {auto follow=delta-correction;if(!settings.followVertical)follow.y=0;origin=origin+follow;}
  if(settings.followCorrection)origin=origin+correction;
  mmd::CameraPose pose;pose.target=origin+basis*c.target*settings.heightScale+anchor*settings.offset;
  pose.position=pose.target+basis*(c.position-c.target)*(settings.heightScale*settings.distanceScale);
  pose.rotation=NormQ(basis*c.rotation);pose.fov=mmd::Clamp(c.fov+settings.fovOffset,1,179);
  pose.orthoSize=c.size*settings.heightScale*settings.distanceScale;pose.perspective=c.perspective;
  return pose;
}
inline double Number(const Json &v,double lo,double hi) {
  if(!v.is_number())throw std::runtime_error("Expected a number");
  double n=v.get<double>();
  if(!std::isfinite(n)||n<lo||n>hi)throw std::runtime_error("Value outside supported range");
  return n;
}
inline Vec3 Vector(const Json &v,float limit=10000) {
  if(!v.is_array()||v.size()!=3)throw std::runtime_error("Expected XYZ");
  return {float(Number(v[0],-limit,limit)),float(Number(v[1],-limit,limit)),float(Number(v[2],-limit,limit))};
}
inline Quat Rotation(const Json &v) {
  if(!v.is_array()||v.size()!=4)throw std::runtime_error("Expected XYZW quaternion");
  Quat q{float(Number(v[0],-1.01,1.01)),float(Number(v[1],-1.01,1.01)),float(Number(v[2],-1.01,1.01)),float(Number(v[3],-1.01,1.01))};
  float l=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
  if(l<.5f||l>1.5f)throw std::runtime_error("Invalid quaternion length");
  return NormQ(q);
}
inline uint64_t Id(const Json &v) {
  if(!v.is_number_unsigned()&&!(v.is_number_integer()&&v.get<int64_t>()>=0))throw std::runtime_error("Invalid sequence/session");
  return v.get<uint64_t>();
}
inline Frame ParseFrame(const Json &j,size_t count) {
  Frame f;f.session=Id(j.at("session"));f.sequence=Id(j.at("sequence"));
  f.time=Number(j.at("time"),0,86400);f.root=Vector(j.at("root"));f.visible=j.value("visible",true);
  f.preserveFace=j.value("preserve_face",false);
  if(j.contains("anchor_rotation")){f.anchor=Rotation(j["anchor_rotation"]);f.hasAnchor=true;}
  auto &bones=j.at("bones");
  if(!bones.is_array()||bones.size()>count||count>4096)throw std::runtime_error("Invalid bone count");
  std::vector<bool> seen(count);
  for(const auto &b:bones) {
    int i=int(Number(b.at("i"),0,count?count-1:0));
    if(i>=int(count)||seen[i])throw std::runtime_error("Duplicate/unknown bone");
    seen[i]=true;f.bones.push_back({i,Vector(b.at("p"),100),Rotation(b.at("q"))});
  }
  const auto &faces=j.at("faces");
  if(!faces.is_object()||faces.size()>1024)throw std::runtime_error("Invalid face count");
  for(auto it=faces.begin();it!=faces.end();++it) {
    if(it.key().size()>192)throw std::runtime_error("Face name is too long");
    f.faces[it.key()]=float(Number(it.value(),0,1));
  }
  if(j.contains("camera")&&!j["camera"].is_null()) {
    auto &c=j["camera"];f.camera.active=true;
    f.camera.position=Vector(c.at("p"));f.camera.rotation=Rotation(c.at("q"));f.camera.target=Vector(c.at("target"));
    f.camera.fov=float(Number(c.at("fov"),1,179));f.camera.size=float(Number(c.value("size",5.),.001,10000));
    f.camera.perspective=c.value("perspective",true);
  }
  return f;
}
struct Clip {
  bool cameraOnly=false,hasFaces=false;
  std::string model;std::vector<std::string> names;std::vector<int> parents;std::vector<Frame> frames;
  double duration() const{return frames.empty()?0:frames.back().time;}
  static Clip Parse(const Json &j) {
    const auto format=j.value("format","");const int version=j.value("version",0);
    if((format!="endfield-blender-motion"&&format!="endfield-blender-camera")||(version!=1&&version!=2))throw std::runtime_error("Unsupported Blender motion format");
    if(format=="endfield-blender-camera") {
      if(version!=2)throw std::runtime_error("Unsupported Blender camera format");
      Clip result;result.cameraOnly=true;
      const auto &frames=j.at("frames");
      if(!frames.is_array()||frames.empty()||frames.size()>108001)throw std::runtime_error("Invalid camera frame count");
      for(const auto &value:frames) {
        Json wire=value;wire["session"]=0;wire["sequence"]=0;wire["root"]={0,0,0};wire["bones"]=Json::array();wire["faces"]=Json::object();
        auto f=ParseFrame(wire,0);
        if(!f.camera.active||(result.frames.empty()?f.time!=0:f.time<=result.frames.back().time))throw std::runtime_error("Invalid camera timeline");
        result.frames.push_back(std::move(f));
      }
      return result;
    }
    Clip clip;clip.model=j.at("model").get<std::string>();clip.names=j.at("bone_names").get<std::vector<std::string>>();
    clip.parents=j.at("bone_parents").get<std::vector<int>>();
    if(clip.model.size()>256||clip.names.empty()||clip.names.size()>4096)throw std::runtime_error("Invalid skeleton");
    if(clip.parents.size()!=clip.names.size())throw std::runtime_error("Missing parent hierarchy");
    for(size_t i=0;i<clip.names.size();++i)if(clip.names[i].size()>256||clip.parents[i]<-1||clip.parents[i]>=int(i))throw std::runtime_error("Invalid parent hierarchy");
    const auto &frames=j.at("frames");
    if(!frames.is_array()||frames.empty()||frames.size()>108001)throw std::runtime_error("Invalid frame count");
    size_t values=0;
    for(const auto &value:frames) {
      Json wire=value;
      if(version==2) {wire["session"]=0;wire["sequence"]=0;if(!wire.contains("faces"))wire["faces"]=Json::object();wire.erase("camera");wire.erase("visible");}
      if(value.contains("faces"))clip.hasFaces=true;
      auto frame=ParseFrame(wire,clip.names.size());
      values+=frame.bones.size()+frame.faces.size();
      if(values>12000000)throw std::runtime_error("Motion exceeds decoded budget");
      if(!clip.frames.empty()) {
        auto &prev=clip.frames.back();
        if(frame.time<=prev.time||frame.bones.size()!=prev.bones.size()||frame.faces.size()!=prev.faces.size())throw std::runtime_error("Inconsistent frame tracks");
        for(size_t k=0;k<frame.bones.size();++k)if(frame.bones[k].index!=prev.bones[k].index)throw std::runtime_error("Bone order changed");
        for(auto &f:frame.faces)if(!prev.faces.count(f.first))throw std::runtime_error("Face tracks changed");
      } else if(frame.time!=0)throw std::runtime_error("Motion must start at zero");
      clip.frames.push_back(std::move(frame));
    }
    return clip;
  }
  Frame sample(double time) const {
    if(frames.empty())return {};
    auto it=std::upper_bound(frames.begin(),frames.end(),time,[](double t,const Frame &f){return t<f.time;});
    if(it==frames.begin())return frames.front();
    if(it==frames.end())return frames.back();
    const auto &a=*(it-1),&b=*it;float t=float((time-a.time)/(b.time-a.time));Frame f=a;f.time=time;
    auto lerp=[&](Vec3 p,Vec3 q){return p+(q-p)*t;};
    f.root=lerp(a.root,b.root);
    for(size_t i=0;i<f.bones.size();++i) {
      f.bones[i].position=lerp(a.bones[i].position,b.bones[i].position);
      f.bones[i].rotation=Quat::Slerp(a.bones[i].rotation,b.bones[i].rotation,t);
    }
    for(auto &face:f.faces)face.second+=(b.faces.at(face.first)-face.second)*t;
    if(a.camera.active&&b.camera.active) {
      f.camera.position=lerp(a.camera.position,b.camera.position);f.camera.target=lerp(a.camera.target,b.camera.target);
      f.camera.rotation=Quat::Slerp(a.camera.rotation,b.camera.rotation,t);
      f.camera.fov+=(b.camera.fov-a.camera.fov)*t;f.camera.size+=(b.camera.size-a.camera.size)*t;
    }
    return f;
  }
};
}
