#pragma once
#include "math/blender_bridge.h"
#include <set>

// Offline cut metadata repair. No game objects, inferred camera poses or file IO.
namespace camera_repair {
using Json = nlohmann::json;
struct Reference {
  std::vector<uint32_t> frames, cuts;
  bool dense = false;
};
inline Reference ReadReference(mmd::Reader &r) {
  if(r.remaining()>mmd::MaxVmdFileBytes)throw std::runtime_error(u8"参考 VMD 超过 1 GiB");
  auto signature=r.raw(30);
  bool old=signature.rfind("Vocaloid Motion Data file",0)==0;
  if(!old&&signature.rfind("Vocaloid Motion Data 0002",0)!=0)
    throw std::runtime_error(u8"请选择有效的 VMD 镜头文件");
  r.skip(old?10:20);
  // Bone/morph names are irrelevant to cut timing. Skip their bounded records
  // without decoding or allocating a large body motion.
  auto n=r.count(111,8000000);r.skip(size_t(n)*111);
  if(!r.remaining())throw std::runtime_error(u8"VMD 没有镜头轨道");
  n=r.count(23,8000000);r.skip(size_t(n)*23);
  if(!r.remaining())throw std::runtime_error(u8"VMD 没有镜头轨道");
  n=r.count(61,2000000);
  if(n<2)throw std::runtime_error(u8"参考 VMD 至少需要两个镜头关键帧");
  Reference out;out.frames.reserve(n);
  for(uint32_t i=0;i<n;++i) {
    auto frame=r.read<uint32_t>();
    if(std::fabs(r.number())>1e6f)throw std::runtime_error(u8"VMD 镜头距离异常");
    r.vec();r.vec();r.skip(24);
    auto fov=r.read<uint32_t>();auto projection=r.read<uint8_t>();
    if(fov<1||fov>=180||projection>1)throw std::runtime_error(u8"VMD 镜头参数异常");
    out.frames.push_back(frame);
  }
  std::sort(out.frames.begin(),out.frames.end());
  out.frames.erase(std::unique(out.frames.begin(),out.frames.end()),out.frames.end());
  for(size_t i=1;i<out.frames.size();++i)
    if(uint64_t(out.frames[i])-out.frames[i-1]==1)out.cuts.push_back(out.frames[i]);
  out.dense=out.cuts.size()>=10&&out.cuts.size()*2>=out.frames.size()-1;
  return out;
}
struct Cut {
  uint32_t vmdFrame=0;
  size_t index=0;
  double requested=0,actual=0;
  bool existing=false;
};
struct Plan {
  std::vector<Cut> cuts;
  size_t totalFrames=0,existing=0,outside=0,rounded=0,merged=0;
  double duration=0,referenceDuration=0,offset=0;
  bool dense=false;
};
inline Plan Analyze(const Reference &reference,const Json &doc,double offset) {
  if(!std::isfinite(offset)||std::abs(offset)>86400)
    throw std::runtime_error(u8"时间偏移必须在 -86400 至 86400 秒之间");
  if(!doc.is_object()||doc.value("format",std::string{})!="endfield-blender-camera")
    throw std::runtime_error(u8"请选择 .epcamera 镜头文件，不能使用 .epmotion");
  const auto clip=blender_bridge::Clip::Parse(doc);
  if(clip.frames.size()<2)throw std::runtime_error(u8"旧镜头至少需要两个采样帧");
  Plan plan;plan.offset=offset;plan.dense=reference.dense;
  plan.totalFrames=clip.frames.size();plan.duration=clip.duration();
  if(!reference.frames.empty())plan.referenceDuration=reference.frames.back()/30.;
  for(const auto &f:clip.frames)plan.existing+=f.camera.cut;
  std::set<size_t> mapped;
  for(auto frame:reference.cuts) {
    double t=frame/30.+offset;
    if(t<=1e-7||t>plan.duration+1e-7) {++plan.outside;continue;}
    // A cut starts at its destination sample. Never choose the earlier sample
    // or invent a pose when the source and export FPS differ.
    auto it=std::lower_bound(clip.frames.begin(),clip.frames.end(),t-1e-7,
      [](const blender_bridge::Frame &f,double value){return f.time<value;});
    if(it==clip.frames.end()){++plan.outside;continue;}
    const size_t index=it-clip.frames.begin();
    if(!mapped.insert(index).second){++plan.merged;continue;}
    bool rounded=std::abs(it->time-t)>1e-7;plan.rounded+=rounded;
    plan.cuts.push_back({frame,index,t,it->time,it->camera.cut});
  }
  return plan;
}
inline Json Repair(const Json &doc,const Plan &plan,const std::vector<bool> &selected) {
  if(selected.size()!=plan.cuts.size()||doc.at("frames").size()!=plan.totalFrames)
    throw std::runtime_error(u8"修复选项与分析结果不一致，请重新分析");
  Json output=doc;size_t added=0;
  for(size_t i=0;i<selected.size();++i)if(selected[i]) {
    const auto &cut=plan.cuts[i];
    if(cut.index==0||cut.index>=plan.totalFrames||
       std::abs(output["frames"][cut.index]["time"].get<double>()-cut.actual)>1e-9)
      throw std::runtime_error(u8"镜头时间轴已变化，请重新分析");
    if(!output["frames"][cut.index]["camera"].value("cut",false))++added;
    output["frames"][cut.index]["camera"]["cut"]=true;
  }
  if(!added)throw std::runtime_error(u8"没有需要新增的切镜点，请检查参考文件、时间偏移或勾选项");
  output["version"]=3;
  return output;
}
}
