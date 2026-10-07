#pragma once
#include "game/mmd_squad.h"
#include "game/transparent_capture.h"
#include "game/recording_readback.h"
#include "game/recording_mask_probe.h"
#include "math/mmd_recording.h"
#include "math/recording_png.h"
#include "math/recording_queue.h"
#include "core/recording_display.h"
#include <wincodec.h>
#pragma comment(lib,"windowscodecs.lib")

// Main-thread, bounded-memory offline recording. Samples remain held across
// render passes; bounded CPU encoders and an ordered writer overlap later renders.
namespace poser_recording {
using poser_capture::Require;
using poser_capture::Ref;
using poser_capture::Life;
using mmd_recording::Kind;
static std::atomic<bool> busy{false},cancelled{false};
static mmd_recording::Options options;
static std::string status=u8"从动作首帧录制无 UI 的图像序列",lastDirectory;
static uint32_t completed=0,total=0;
inline std::wstring Directory(){return PoserFilePath(L"recordings");}
struct Actor {int slot=0;Ref animator,root;};
struct Renderer {int slot=0;Ref object,go;bool hidden=false;int layer=0;};
struct Bone {Ref transform;void *held=nullptr;Vec3 position,scale;Quat rotation;};
struct CameraPose {Ref transform;Vec3 position;Quat rotation;float fov=60,size=5;bool ortho=false;};
struct Clock {
  void *getScale=nullptr,*setScale=nullptr,*getDelta=nullptr,*setDelta=nullptr;
  float scale=1,delta=0;bool owned=false;
  static bool Set(void *method,float value){void *args[]{&value};return poser_capture::Call(method,nullptr,args);}
  void acquire(int fps) {
    auto cls=mmd_api::Class("UnityEngine","Time");
    getScale=mmd_api::Method(cls,"get_timeScale","System.Single",{},true);
    setScale=mmd_api::Method(cls,"set_timeScale","System.Void",{"System.Single"},true);
    getDelta=mmd_api::Method(cls,"get_captureDeltaTime","System.Single",{},true);
    setDelta=mmd_api::Method(cls,"set_captureDeltaTime","System.Void",{"System.Single"},true);
    Require(getScale&&setScale&&getDelta&&setDelta&&mmd_api::Value(getScale,nullptr,scale)&&
      mmd_api::Value(getDelta,nullptr,delta)&&std::isfinite(scale)&&scale>0&&std::isfinite(delta),u8"游戏时间接口不可用或游戏正在暂停");
    owned=true;Require(Set(setDelta,1.f/fps)&&Set(setScale,1),"Cannot set recording time step");
  }
  void run(bool enabled) {Require(Set(setScale,enabled?1.f:0.f),"Cannot hold recording time");}
  void verify(bool running,int fps) {
    float currentScale=-1,currentDelta=-1;
    Require(mmd_api::Value(getScale,nullptr,currentScale)&&mmd_api::Value(getDelta,nullptr,currentDelta)&&
      std::abs(currentScale-(running?1.f:0.f))<1e-6f&&std::abs(currentDelta-1.f/fps)<1e-6f,
      u8"游戏时间设置被改变，已停止录制；请避免录制中打开暂停菜单");
  }
  bool restore() {
    if(!owned)return true;
    if(RuntimeClosing()){owned=false;return true;}
    // Always try to restore timeScale even if restoring captureDeltaTime fails.
    const bool a=Set(setDelta,delta),b=Set(setScale,scale);
    if(a&&b)owned=false;return !owned;
  }
};
// Armed/disarmed only by game-thread recording callbacks. The pre-lock pulse
// also runs on that thread: UI contention cannot let time keep advancing while
// the recorder waits to apply/acknowledge its next sample. No Job access here.
struct StepClock {
  void *setScale=nullptr;
  int armedFrame=-1,heldFrame=-1;
  bool armed=false,failed=false;
  void arm(void *method,int frame){setScale=method;armedFrame=frame;heldFrame=-1;failed=false;armed=true;}
  void pulse(int frame) {
    if(!armed||frame<0||frame==armedFrame||RuntimeClosing())return;
    if(Clock::Set(setScale,0)){heldFrame=frame;armed=false;}else failed=true;
  }
  void clear(){armed=false;setScale=nullptr;heldFrame=-1;failed=false;}
} static stepClock;
inline void FramePulse(int frame){stepClock.pulse(frame);}
using Pixels=std::vector<unsigned char>;
struct Packet {
  std::string layer;Pixels rgba,matte;bool fastPng=true,jpeg=false;int jpegQuality=95;
  std::shared_ptr<const Pixels> sharedRgba;
};
struct EncodedPacket {std::string layer;Pixels bytes;bool jpeg=false;double processMs=0,encodeMs=0;};
struct FrameFiles {
  std::vector<std::filesystem::path> paths;
  void rollback(){for(auto &p:paths)DeleteFileW(p.c_str());paths.clear();}
};
enum class Phase { Start, Warm, Step, Pass, Write, Finish, Readback, SceneQueue, SceneDrain };
struct SceneReadback {
  recording_readback::Request request;
  recording_queue::Work work;
  std::vector<unsigned char> rgba;
  uint32_t index=0;int width=0,height=0;
  double started=0;
};
struct Job {
  Phase phase=Phase::Start;
  mmd_recording::Options opt;
  mmd_recording::Plan plan;
  std::vector<mmd_recording::Pass> passes;
  std::vector<Actor> actors;
  std::vector<Renderer> renderers;
  std::vector<Bone> bones;
  CameraPose cameraPose;
  Clock clock;
  std::filesystem::path directory;
  nlohmann::json manifest;
  std::shared_ptr<FrameFiles> files=std::make_shared<FrameFiles>();
  std::vector<unsigned char> beauty;
  std::shared_ptr<const Pixels> sceneColour;
  std::future<void> writer;
  std::unique_ptr<recording_queue::Writer> pipeline;
  recording_queue::Work pendingWrite;
  recording_readback::Request readback;
  std::deque<SceneReadback> sceneReads;
  mmd_recording::Samples samples;
  uint64_t session=0;
  uint32_t index=0;
  size_t pass=0;
  int stepFrame=-1,sampledFrame=-1,preparedFrame=-1,lastCamera=-1,replayFrame=-1,targetSamples=2;
  int restoredFrame=-1,cleanFrame=-1,cleanRenders=0;
  double started=0,warmStarted=0,lastActivity=0,simulationNow=0,oldSpeed=1,readStarted=0;
  bool oldLoop=false,transportOwned=false,sceneChanged=false,finalWrite=false,failed=false,alphaVerified=false,stepSampled=false,maskProbed=false,scenePipeline=false;
  bool displayNeedsClean=false;
  bool reuseColour=false;
  uint64_t renderedSamples=0,readbacks=0,reusedColours=0;
  double passStarted=0,renderMs=0,readbackMs=0,queueWaitStarted=0,queueWaitMs=0;
  std::string result;
};
// Worker futures are never destructed/waited under the DLL loader lock.
static auto &job=*new std::unique_ptr<Job>;
inline mmd::Timeline &Timeline(){return job->opt.squad?g_squad.timeline:g_mmd.timeline;}
inline bool Active(){return job->opt.squad?g_squad.active:g_mmd.session.active;}
inline uint64_t Session(){return job->opt.squad?g_squad.cameraSession:g_mmd.session.cameraSession;}
inline void Cancel(){if(busy)cancelled=true;}
inline bool Request(mmd_recording::Options opt) {
  if(busy||poser_capture::busy)return false;
  if(g_blenderEditing||g_mmd.loading||g_squad.loading||g_mmd.preview) {
    status=u8"请先完成动作导入／校准，并断开 Blender 实时编辑";return false;
  }
  if((opt.squad&&g_mmd.session.active)||(!opt.squad&&g_squad.active)) {
    status=u8"请先停止另一播放器，再录制所选的单人或多人动作";return false;
  }
  if(first_person::active){status=u8"请先关闭第一人称镜头，再开始录制";return false;}
  options=opt;completed=total=0;cancelled=false;busy=true;
  recording_display::Begin(opt.layered&&opt.hideIntermediate);
  poser_capture::busy=true;poser_capture::externalOwner=true;poser_capture::cancelled=false;
  g_mmdOfflineNow=MmdNow();
  g_mmdOfflineRecording=true;g_captureRenderActive=true;status=u8"正在回到动作首帧…";return true;
}
inline void Finish(std::string reason,bool failed=false) {
  if(!job)return;
  if(failed)Log("[MMD-RECORD] failure phase=%d frame=%u pass=%zu samples=%d prepared=%d camera=%d render=%d: %s",
    int(job->phase),job->index,job->pass,job->samples.count,job->preparedFrame,job->lastCamera,job->replayFrame,reason.c_str());
  job->result=std::move(reason);job->failed=failed;job->phase=Phase::Finish;
  stepClock.clear();recording_display::restoring=true;
  status=job->result+u8"；正在恢复画面与游戏时间…";
}
inline std::vector<void*> Children(void *root,void *type,size_t limit) {
  bool inactive=true;void *go=nullptr,*array=nullptr,*args[]{type,&inactive};
  Require(mmd_api::Call(g_component_get_gameObject,root,nullptr,go)&&
    mmd_api::Call(mmd_visibility::api.query,go,args,array),"Cannot enumerate recording actor");
  return poser_capture::Array(array,limit);
}
inline void Actors() {
  auto &j=*job;
  auto add=[&](int slot,void *animator,void *root) {
    Actor a;a.slot=slot;Require(a.animator.capture(animator)&&a.root.capture(root),"Cannot retain recording actor");j.actors.push_back(std::move(a));
  };
  if(j.opt.squad)for(int i=0;i<4;++i) {if(g_squad.actors[i])add(i,g_squad.actors[i]->saved.animator,g_squad.actors[i]->saved.root);}
  else add(0,g_mmd.session.animator,g_mmd.session.root);
  std::vector<int> slots;for(auto &a:j.actors)slots.push_back(a.slot);
  j.passes=mmd_recording::Passes(j.opt,slots);
}
inline void Validate() {
  auto &j=*job;
  Require(Active()&&Session()==j.session,u8"动作已停止或播放角色发生变化");
  Require(std::abs(Timeline().seconds-j.plan.time(j.index))<1e-6,u8"时间轴被修改，录制已停止");
  Require(!g_blenderEditing&&!first_person::active,u8"镜头或编辑模式发生变化");
  for(auto &a:j.actors)Require(a.animator.target()&&a.root.target(),u8"录制角色已离开场景");
  if(j.opt.squad) {
    auto roster=poser_squad::Read();
    Require(roster.valid&&g_squad.identity.matches(MmdSquadIdentity(roster)),u8"队员顺序或实例发生变化");
  } else Require(g_mmd.session.animator==g_charAnimator&&g_mmd.session.revision==s_bonesRev,u8"操控角色骨架发生变化");
}
inline void Sample(double time,bool playing) {
  auto &j=*job;auto &t=Timeline();t.seconds=time;t.lastNow=MmdNow();t.state=playing?mmd::PlayState::Playing:mmd::PlayState::Paused;
  g_mmdOfflineNow=j.simulationNow;
  if(j.opt.squad)MmdSquadApply();else MmdApplyFrame();
  Require(Active(),u8"动作应用失败，录制已停止");
}
inline void WriteBytes(const std::filesystem::path &path,const unsigned char *data,size_t size) {
  Require(size<=MAXDWORD,"Recording file too large");
  HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  Require(file!=INVALID_HANDLE_VALUE,"Cannot create recording file");DWORD written=0;
  const bool ok=WriteFile(file,data,DWORD(size),&written,nullptr)&&written==size;CloseHandle(file);
  if(!ok){DeleteFileW(path.c_str());throw std::runtime_error("Cannot save recording (check free space)");}
}
inline void Manifest(const std::filesystem::path &directory,const nlohmann::json &value) {
  auto temp=directory/L"recording.json.tmp",file=directory/L"recording.json";auto text=value.dump(2);
  WriteBytes(temp,reinterpret_cast<const unsigned char*>(text.data()),text.size());
  if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());throw std::runtime_error("Cannot update recording manifest");
  }
}
inline std::vector<unsigned char> EncodeWorker(std::vector<unsigned char> &rgba,int width,int height,bool jpeg=false,int quality=95) {
  using Microsoft::WRL::ComPtr;
  Require(width>0&&height>0&&uint64_t(width)*height<=33554432&&rgba.size()==uint64_t(width)*height*4&&quality>=60&&quality<=100,"Invalid recording image dimensions/quality");
  struct Com {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~Com(){if(SUCCEEDED(hr))CoUninitialize();}} com;
  Require(SUCCEEDED(com.hr),"Cannot initialize image worker");
  ComPtr<IWICImagingFactory> factory;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IStream> stream;ComPtr<IPropertyBag2> settings;
  Require(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&
    SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))&&SUCCEEDED(factory->CreateEncoder(jpeg?GUID_ContainerFormatJpeg:GUID_ContainerFormatPng,nullptr,&encoder))&&
    SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))&&SUCCEEDED(encoder->CreateNewFrame(&frame,&settings)),"Cannot initialize image encoder");
  if(jpeg) {
    PROPBAG2 properties[2]{};VARIANT values[2]{};
    properties[0].pstrName=const_cast<wchar_t*>(L"ImageQuality");values[0].vt=VT_R4;values[0].fltVal=quality/100.f;
    properties[1].pstrName=const_cast<wchar_t*>(L"JpegYCrCbSubsampling");values[1].vt=VT_UI1;values[1].bVal=WICJpegYCrCbSubsampling444;
    Require(SUCCEEDED(settings->Write(2,properties,values)),"Cannot set JPEG quality");
  }
  Require(SUCCEEDED(frame->Initialize(jpeg?settings.Get():nullptr))&&SUCCEEDED(frame->SetSize(width,height)),"Cannot initialize image frame");
  const auto wanted=jpeg?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat32bppBGRA;WICPixelFormatGUID format=wanted;
  Require(SUCCEEDED(frame->SetPixelFormat(&format))&&IsEqualGUID(format,wanted),"Unsupported image pixel format");
  // Unity ReadPixels is bottom-up RGBA. WIC writes top-down BGR or straight BGRA.
  const size_t stride=size_t(width)*(jpeg?3:4);
  if(jpeg) {
    for(size_t i=0;i<size_t(width)*height;++i) {auto r=rgba[4*i],g=rgba[4*i+1],b=rgba[4*i+2];rgba[3*i]=b;rgba[3*i+1]=g;rgba[3*i+2]=r;}
    rgba.resize(stride*height);
  } else for(size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
  for(int y=0;y<height/2;++y)for(size_t x=0;x<stride;++x)std::swap(rgba[size_t(y)*stride+x],rgba[size_t(height-y-1)*stride+x]);
  Require(SUCCEEDED(frame->WritePixels(height,UINT(stride),UINT(rgba.size()),rgba.data()))&&
    SUCCEEDED(frame->Commit())&&SUCCEEDED(encoder->Commit()),"Image encoding failed");
  STATSTG stat{};Require(SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME))&&stat.cbSize.QuadPart>32&&stat.cbSize.QuadPart<=256*1024*1024,"Invalid encoded image size");
  LARGE_INTEGER zero{};Require(SUCCEEDED(stream->Seek(zero,STREAM_SEEK_SET,nullptr)),"Cannot read encoded image");
  std::vector<unsigned char> out(size_t(stat.cbSize.QuadPart));ULONG read=0;
  Require(SUCCEEDED(stream->Read(out.data(),ULONG(out.size()),&read))&&read==out.size(),"Cannot copy encoded image");return out;
}
inline EncodedPacket EncodeLayer(Packet packet,int width,int height) {
  const double started=MmdNow();
  const bool opaque=packet.matte.empty();
  Require(!packet.jpeg||(opaque&&packet.layer=="background"),"JPEG is only supported for opaque background layers");
  // Shared scene colour is immutable. Copy only on a CPU encoder that needs to
  // alter it (alpha extraction / WIC), never while the game renders a layer.
  if(packet.sharedRgba&&(!opaque||!packet.fastPng||packet.jpeg))packet.rgba=*packet.sharedRgba;
  if(!opaque)transparent_capture::ApplyMatte(packet.rgba,packet.matte,width,height);
  else if(!packet.fastPng&&!packet.jpeg)for(size_t i=3;i<packet.rgba.size();i+=4)packet.rgba[i]=255;
  Pixels{}.swap(packet.matte);
  EncodedPacket out;out.layer=packet.layer;out.jpeg=packet.jpeg;
  const double encoding=MmdNow();out.processMs=(encoding-started)*1000;
  const auto &colour=packet.sharedRgba&&opaque&&packet.fastPng&&!packet.jpeg?*packet.sharedRgba:packet.rgba;
  out.bytes=packet.jpeg?EncodeWorker(packet.rgba,width,height,true,packet.jpegQuality):
    packet.fastPng?recording_png::EncodeFast(colour,width,height,2,opaque):EncodeWorker(packet.rgba,width,height);
  out.encodeMs=(MmdNow()-encoding)*1000;return out;
}
inline void SaveEncodedLayer(std::filesystem::path directory,uint32_t index,EncodedPacket packet,
                      std::shared_ptr<FrameFiles> files,nlohmann::json manifest,bool commit) {
  const double writing=MmdNow();
  try {
    auto temp=directory/packet.layer/mmd_recording::FrameName(index,packet.jpeg);temp+=L".tmp";
    WriteBytes(temp,packet.bytes.data(),packet.bytes.size());files->paths.push_back(temp);
    const double committing=MmdNow();
    if(commit) {
      for(auto &path:files->paths) {
        auto file=path;file.replace_extension();
        Require(MoveFileExW(path.c_str(),file.c_str(),MOVEFILE_WRITE_THROUGH),"Cannot commit recording frame");path=file;
      }
      manifest["completed_frames"]=index+1;Manifest(directory,manifest);
      files->paths.clear();
    }
    if(index%60==0)Log("[MMD-RECORD] worker frame=%u layer=%s format=%s process=%.1fms encode=%.1fms write=%.1fms commit=%.1fms bytes=%zu",
      index,packet.layer.c_str(),packet.jpeg?"JPEG":"PNG",packet.processMs,packet.encodeMs,(committing-writing)*1000,(MmdNow()-committing)*1000,packet.bytes.size());
  } catch(...) {files->rollback();throw;}
}
inline void SaveLayer(std::filesystem::path directory,uint32_t index,Packet packet,int width,int height,
                      std::shared_ptr<FrameFiles> files,nlohmann::json manifest,bool commit) {
  try {SaveEncodedLayer(std::move(directory),index,EncodeLayer(std::move(packet),width,height),files,std::move(manifest),commit);}
  catch(...) {files->rollback();throw;}
}
inline void NewDirectory() {
  auto &j=*job;auto base=Directory();Require(!base.empty(),"Recording directory unavailable");
  std::filesystem::create_directories(base);SYSTEMTIME t{};GetLocalTime(&t);wchar_t name[128];
  swprintf_s(name,L"mmd-%04u%02u%02u-%02u%02u%02u-%03u-%llu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetTickCount64());
  j.directory=std::filesystem::path(base)/name;
  Require(std::filesystem::create_directory(j.directory),"Recording directory already exists");lastDirectory=j.directory.u8string();
  nlohmann::json layers=nlohmann::json::array(),layerFiles=nlohmann::json::object();std::set<std::string> unique;
  for(auto &p:j.passes)if(p.kind!=Kind::Beauty&&unique.insert(p.layer).second) {
    std::filesystem::create_directory(j.directory/p.layer);layers.push_back(p.layer);
    layerFiles[p.layer]=mmd_recording::Jpeg(j.opt,p.kind)?"%06d.jpg":"%06d.png";
  }
  j.manifest={{"format","EndfieldPoser image sequence"},{"version",2},{"fps",j.plan.fps},{"planned_frames",j.plan.frames},
    {"completed_frames",0},{"duration_seconds",j.plan.duration},{"first_frame",0},{"filename",j.opt.layered&&j.opt.jpegBackground?"%06d":"%06d.png"},
    {"sample_time","index / fps; interval [0, duration)"},{"warmup_seconds",j.opt.warmup},{"layers",layers},{"layer_files",layerFiles},
    {"state","recording"},{"alpha","straight; character layers only"},{"ui",false},
    {"png_compression",j.opt.fastPng?"rgba_deflate1_up_or_rgb_stored":"standard"},{"writer_queue_layers",4},
    {"encoder_threads",2},{"writer_memory_budget_bytes",512ull*1024*1024},
    {"gpu_readback_slots",j.scenePipeline?2:1},
    {"music_file",g_mmd.musicFile},{"music_offset_seconds",g_mmd.musicOffset},{"music_enabled",g_mmd.musicEnabled},
    {"audio_recorded",false},{"camera_file",g_mmd.cameraFile},{"antialiasing",j.opt.followAA?"game":"FXAA matte"}};
  nlohmann::json actors=nlohmann::json::array();
  if(j.opt.layered)j.manifest["background_format"]=j.opt.jpegBackground?"jpeg":"png";
  if(j.opt.layered&&j.opt.jpegBackground){j.manifest["jpeg_quality"]=j.opt.jpegQuality;j.manifest["jpeg_subsampling"]="444";}
  for(auto &a:j.actors)actors.push_back({{"slot",a.slot+1},{"motion",j.opt.squad?g_squad.slots[a.slot].file:g_mmd.file}});
  j.manifest["actors"]=actors;Manifest(j.directory,j.manifest);
}
inline bool RestoreScene() {
  auto &j=*job;if(!j.sceneChanged)return true;bool ok=true;
  for(auto &r:j.renderers) {
    void *obj=nullptr;auto life=r.object.inspect(obj);
    if(life==Life::Unavailable)ok=false;
    if(life==Life::Alive)ok=mmd_visibility::Write(obj,r.hidden)&&ok;
    life=r.go.inspect(obj);if(life==Life::Unavailable)ok=false;
    if(life==Life::Alive)ok=poser_capture::SetInt(poser_capture::api.setLayer,obj,r.layer)&&ok;
  }
  if(poser_capture::shot) {
    auto &s=*poser_capture::shot;void *obj=nullptr;auto life=s.camera.inspect(obj);
    if(life==Life::Unavailable)ok=false;
    if(life==Life::Alive)ok=poser_capture::SetInt(poser_capture::api.maskSet,obj,s.mask)&&ok;
    life=s.hg.inspect(obj);if(life==Life::Unavailable)ok=false;
    if(life==Life::Alive)for(auto &f:s.fields)ok=f.Write(obj,true)&&ok;
  }
  if(ok)j.sceneChanged=false;return ok;
}
inline void CapturePose(void *camera) {
  auto &j=*job;
  if(!j.opt.layered)return; // Full-scene recording never enumerates the skeleton.
  auto &api=poser_capture::api;
  if(j.bones.empty()) {
  for(auto &a:j.actors) {
    for(void *t:Children(a.root.target(),api.transformType,8192))if(UnityObjAlive(t)) {
      Bone b;Require(b.transform.capture(t),"Cannot retain recording pose");b.held=t;
      j.bones.push_back(std::move(b));
    }
    if(j.opt.layered)for(void *r:Children(a.root.target(),mmd_visibility::api.rendererType,1024))if(UnityObjAlive(r)) {
      Renderer entry;entry.slot=a.slot;void *go=nullptr;
      Require(mmd_visibility::Scope(r,a.root.target())==1&&entry.object.capture(r)&&
        mmd_api::Value(mmd_visibility::api.get,r,entry.hidden)&&mmd_api::Call(g_component_get_gameObject,r,nullptr,go)&&
        entry.go.capture(go)&&mmd_api::Value(api.getLayer,go,entry.layer),"Cannot hold actor renderers");j.renderers.push_back(std::move(entry));
    }
  }
  Log("[MMD-RECORD] retained bones=%zu renderers=%zu; background excludes actor layer=%d",j.bones.size(),j.renderers.size(),poser_capture::shot->layer);
  }
  for(auto &b:j.bones) {
    auto t=b.transform.target();Require(t&&mmd_api::Value(g_transform_get_localPosition,t,b.position)&&
      mmd_api::Value(g_transform_get_localRotation,t,b.rotation)&&mmd_api::Value(g_transform_get_localScale,t,b.scale),"Cannot sample recording pose");
  }
  for(auto &r:j.renderers)Require(r.object.target()&&mmd_api::Value(mmd_visibility::api.get,r.object.target(),r.hidden),"Cannot sample actor visibility");
  j.cameraPose=CameraPose{};
  auto &p=j.cameraPose;void *transform=nullptr;
  Require(mmd_api::Call(g_component_get_transform,camera,nullptr,transform)&&p.transform.capture(transform)&&
    mmd_api::Value(g_transform_get_position,transform,p.position)&&mmd_api::Value(g_transform_get_rotation,transform,p.rotation)&&
    mmd_api::Value(mmd_camera::getFov,camera,p.fov)&&mmd_api::Value(mmd_camera::getOrtho,camera,p.ortho)&&
    mmd_api::Value(mmd_camera::getSize,camera,p.size),"Cannot hold recording camera");
}
inline void Replay() {
  if(!job||job->phase!=Phase::Pass||!poser_capture::shot)return;
  auto &j=*job;
  if(!j.opt.layered)return;
  for(auto &b:j.bones) {
    // The strong GC handle stays owned for the recording; validate identity at
    // each output sample, check native liveness before each replay.
    auto t=b.held;Require(UnityObjAlive(t)&&mmd_camera::Write(g_transform_set_localPosition,t,b.position)&&
      mmd_camera::Write(g_transform_set_localRotation,t,b.rotation)&&mmd_camera::Write(g_transform_set_localScale,t,b.scale),"Recording pose changed");
  }
  auto &p=j.cameraPose;auto t=p.transform.target();auto camera=poser_capture::shot->camera.target();
  Require(t&&camera&&mmd_camera::Write(g_transform_set_position,t,p.position)&&mmd_camera::Write(g_transform_set_rotation,t,p.rotation)&&
    mmd_camera::Write(mmd_camera::setOrtho,camera,p.ortho)&&mmd_camera::Write(mmd_camera::setFov,camera,p.fov)&&
    mmd_camera::Write(mmd_camera::setSize,camera,p.size),"Recording camera changed");
}
inline void ApplyPassVisibility() {
  auto &j=*job;auto &s=*poser_capture::shot;auto &p=j.passes[j.pass];
  if(p.kind!=Kind::Scene) {
    j.sceneChanged=true;s.changed=true;
    for(auto &r:j.renderers) {
      const bool selected=p.actor<0||p.actor==r.slot;
      const bool hide=r.hidden||p.kind==Kind::Background||!selected;
      Require(r.object.target()&&mmd_visibility::Write(r.object.target(),hide),"Cannot set recording actor visibility");
      // HG character draws can survive forceRenderingOff. Exclude hidden
      // actors from camera culling as well, on a verified unused scene layer.
      const int wanted=(p.kind==Kind::Matte?(selected&&!r.hidden):hide)?s.layer:r.layer;
      auto go=r.go.target();int current=0;
      Require(go&&mmd_api::Value(poser_capture::api.getLayer,go,current)&&
        (current==wanted||poser_capture::SetInt(poser_capture::api.setLayer,go,wanted)),"Cannot isolate recording actor");
    }
    if(p.kind==Kind::Matte) {
      for(auto &f:s.fields)Require(f.Write(s.hg.target(),false),"Cannot enable recording alpha");
      Require(poser_capture::SetInt(poser_capture::api.maskSet,s.camera.target(),int(uint32_t(1)<<s.layer)),"Cannot isolate recording camera");
    } else {
      Require(poser_capture::SetInt(poser_capture::api.maskSet,s.camera.target(),int(uint32_t(s.mask)&~(uint32_t(1)<<s.layer))),"Cannot exclude recording actors");
    }
  }
}
inline void ApplyPass(bool resetHistory) {
  auto &s=*poser_capture::shot;
  Require(RestoreScene(),"Cannot restore previous recording pass");
  ApplyPassVisibility();
  poser_capture::RegisterExtraction();
  if(resetHistory)Require(s.historyReset.Write(poser_capture::Target(s.hgCamera),false),"Cannot reset layer rendering history");
}
inline void BeginPass(int frame) {
  auto &j=*job;j.samples.reset();j.preparedFrame=frame;j.phase=Phase::Pass;
  j.passStarted=MmdNow();
  j.displayNeedsClean=j.displayNeedsClean||j.opt.layered;
  j.targetSamples=!j.opt.layered?(j.index==0?2:1):(j.passes[j.pass].kind==Kind::Matte?poser_capture::shot->settleFrames:poser_capture::shot->sceneSettleFrames);
  ApplyPass(j.opt.layered);
}
inline bool CanReuseSceneColour() {
  auto &j=*job;auto &s=*poser_capture::shot;
  if(!j.opt.layered||j.renderers.empty())return false;
  bool grouped=false;for(auto &p:j.passes)if(p.kind==Kind::Beauty&&p.actor<0)grouped=true;
  if(!grouped)return false;
  // Grouped Beauty only differs from Scene by camera-excluding force-hidden
  // renderers. Some native HG draws ignore forceRenderingOff, so fall back to
  // the separate colour pass if an originally visible camera layer has one.
  for(auto &r:j.renderers)if(r.layer<0||r.layer>=32||
    (r.hidden&&(uint32_t(s.mask)&(uint32_t(1)<<r.layer))))return false;
  return true;
}
inline void BeginFrame(void *camera,int frame) {
  auto &j=*job;
  if(!poser_capture::shot) {
    poser_capture::requestedFollowAA=j.opt.followAA;
    poser_capture::Prepare(camera,j.actors.front().animator.target(),j.actors.front().root.target(),MmdNow(),j.opt.layered);
    // The recorder holds the entire multi-actor pose, not the one-shot checker.
    poser_capture::shot->transforms.clear();
    j.manifest["width"]=poser_capture::shot->width;j.manifest["height"]=poser_capture::shot->height;
  }
  CapturePose(camera);j.reuseColour=CanReuseSceneColour();j.sceneColour.reset();
  if(j.index%60==0)Log("[MMD-RECORD] colour reuse frame=%u enabled=%d renderers=%zu",j.index,int(j.reuseColour),j.renderers.size());
  j.clock.run(false);j.pass=0;j.beauty.clear();BeginPass(frame);
}
inline void CheckCamera(void *camera) {
  Require(UnityObjAlive(camera),"Recording camera unavailable");
  if(!poser_capture::shot)return;
  auto &s=*poser_capture::shot;int w=0,h=0;
  Require(camera==s.camera.target()&&mmd_api::Value(poser_capture::api.width,camera,w)&&mmd_api::Value(poser_capture::api.height,camera,h)&&w==s.width&&h==s.height,u8"镜头或分辨率已改变，录制已停止");
}
inline void FinishStep(void *camera,int frame) {
  auto &j=*job;
  Require(j.stepSampled&&j.sampledFrame>=0&&frame>=j.sampledFrame&&stepClock.heldFrame>=0&&!stepClock.failed,
    u8"录制步进时钟未就绪");
  Validate();CheckCamera(camera);j.clock.verify(false,j.plan.fps);
  Require(mmd_camera::SyncRecording(camera,j.simulationNow),u8"无法同步当前采样的镜头，录制已停止");
  BeginFrame(camera,frame);j.lastActivity=MmdNow();
}
inline void RenderPose(int frame) {
  if(!busy||!job||frame<0||cancelled)return;
  try {
    if(job->phase==Phase::Step&&job->stepSampled&&poser_capture::shot) {
      // TailLateTick may be suppressed by timeScale=0. SRP is the final safe
      // rendezvous for body+camera, before any capture pass is rendered.
      FinishStep(poser_capture::shot->camera.target(),frame);
    }
  } catch(const std::exception &e){Finish(e.what(),true);return;}
  if(job->phase!=Phase::Pass)return;
  if(job->replayFrame==frame)return;job->replayFrame=frame;
  try {
    Replay();
    // Game visibility/camera updates can overwrite the pass between renders.
    // Reassert only render state here; history must continue accumulating.
    Require(bool(poser_capture::shot),"Recording render target unavailable");ApplyPassVisibility();
    // timeScale=0 suppresses CameraManager.TailLateTick in this game. The
    // held pose/camera remain valid: prepare every actual SRP render without
    // waiting for another gameplay-camera update to acknowledge this pass.
    job->preparedFrame=frame;
  }
  catch(const std::exception &e){Finish(e.what(),true);}
}
inline void AfterCamera(void *camera) {
  if(!busy||!job||job->phase==Phase::Finish)return;
  try {
    auto &j=*job;int frame=ReadUnityFrameCount();if(frame<0)return;
    if(j.phase==Phase::Start)return;
    if(j.phase==Phase::SceneQueue||j.phase==Phase::SceneDrain)return;
    if(j.phase==Phase::Step&&!j.stepSampled)return;
    Validate();CheckCamera(camera);
    if(frame==j.lastCamera)return;j.lastCamera=frame;
    if(j.phase==Phase::Warm) {
      j.lastActivity=MmdNow();
      bool holding=j.opt.squad?MmdSquadClothHolding():g_clothPlaybackGate.Holding(1u,s_clothRequestGeneration);
      if(holding)j.warmStarted=MmdNow();
      j.clock.verify(true,j.plan.fps);
      if(j.opt.layered&&j.opt.hideIntermediate) {
        Require(!recording_display::failed,u8"录制进度遮罩无法显示，请改用窗口／无边框模式或关闭遮罩");
        if(!recording_display::ready){status=u8"等待录制进度遮罩就绪…";return;}
      }
      if(!holding&&MmdNow()-j.warmStarted>=j.opt.warmup)BeginFrame(camera,frame);
      else status=u8"首帧稳定中（"+std::to_string(j.opt.warmup)+u8" 秒）…";
    } else if(j.phase==Phase::Step&&frame!=j.stepFrame) {
      FinishStep(camera,frame);
    }
  } catch(const std::exception &e){Finish(e.what(),true);}
}
inline recording_queue::Work MakeWrite(Packet packet,uint32_t index,int width,int height,std::shared_ptr<FrameFiles> files,bool commit) {
  auto &j=*job;recording_queue::Work work;
  // Reserve raw colour/matte plus encoder scratch/output. Count shared colour
  // in each job conservatively. A maximum-size image may run alone over budget.
  work.bytes=(packet.sharedRgba?packet.sharedRgba->size():packet.rgba.size())+packet.matte.size()+size_t(width)*height*16;
  struct Buffers {Packet raw;EncodedPacket encoded;};
  auto buffers=std::make_shared<Buffers>();buffers->raw=std::move(packet);
  work.prepare=[buffers,width,height]{buffers->encoded=EncodeLayer(std::move(buffers->raw),width,height);};
  work.run=[directory=j.directory,index,buffers,files,manifest=j.manifest,commit]() mutable {
    SaveEncodedLayer(directory,index,std::move(buffers->encoded),files,std::move(manifest),commit);
  };
  work.rollback=[files]{files->rollback();};work.commits=commit?index+1:0;return work;
}
inline void NextSample() {
  auto &j=*job;j.phase=Phase::Step;j.stepFrame=ReadUnityFrameCount();j.sampledFrame=-1;j.stepSampled=false;
  Require(j.stepFrame>=0,u8"无法读取录制步进帧号");
  stepClock.arm(j.clock.setScale,j.stepFrame);j.clock.run(true);
  status=u8"正在录制第 "+std::to_string(j.index+1)+" / "+std::to_string(j.plan.frames)+u8" 帧…";
}
inline void QueuePendingWrite(int frame) {
  auto &j=*job;
  if(!j.pipeline->submit(j.pendingWrite)){if(!j.queueWaitStarted)j.queueWaitStarted=MmdNow();return;}
  if(j.queueWaitStarted){j.queueWaitMs+=(MmdNow()-j.queueWaitStarted)*1000;j.queueWaitStarted=0;}
  j.lastActivity=MmdNow();
  ++j.pass;
  if(j.pass<j.passes.size()&&j.reuseColour&&j.sceneColour&&j.passes[j.pass].kind==Kind::Beauty&&j.passes[j.pass].actor<0) {
    ++j.pass;++j.reusedColours;
    if(j.index%60==0)Log("[MMD-RECORD] reused full-scene colour frame=%u; skipped grouped beauty render/readback",j.index);
  }
  if(j.pass<j.passes.size()) {BeginPass(frame);j.preparedFrame=-1;return;}
  Require(RestoreScene(),"Cannot restore recorded scene");
  ++j.index;j.files=std::make_shared<FrameFiles>();
  if(j.index>=j.plan.frames)Finish(u8"录制完成");
  else NextSample();
}
inline void SubmitPixels(std::vector<unsigned char> rgba,int frame) {
    auto &j=*job;
    auto &p=j.passes[j.pass];auto &s=*poser_capture::shot;
    if(p.kind==Kind::Beauty) {
      j.beauty=std::move(rgba);++j.pass;BeginPass(frame);j.preparedFrame=-1;return;
    }
    else {
      Packet packet;packet.layer=p.layer;packet.fastPng=j.opt.fastPng;
      packet.jpeg=mmd_recording::Jpeg(j.opt,p.kind);packet.jpegQuality=j.opt.jpegQuality;
      if(p.kind==Kind::Matte) {
        const auto alpha=transparent_capture::Inspect(rgba.data(),rgba.size(),s.width,s.height);
        if(alpha.clear>0)j.alphaVerified=true;
        Require(j.alphaVerified,u8"当前抗锯齿未输出透明通道，请使用 FXAA 兼容模式重试");
        // All-clear is valid when a visibility key hides this actor/off-screen.
        packet.matte=std::move(rgba);
        if(j.reuseColour&&p.actor<0)packet.sharedRgba=std::move(j.sceneColour);
        else packet.rgba=std::move(j.beauty);
      } else if(p.kind==Kind::Scene&&j.reuseColour) {
        j.sceneColour=std::make_shared<const Pixels>(std::move(rgba));packet.sharedRgba=j.sceneColour;
      } else packet.rgba=std::move(rgba);
      const bool commit=j.pass+1==j.passes.size();
      if(!j.pipeline)j.pipeline=std::make_unique<recording_queue::Writer>();
      j.pendingWrite=MakeWrite(std::move(packet),j.index,s.width,s.height,j.files,commit);
    }
    j.lastActivity=MmdNow();
    j.phase=Phase::Write;
    status=u8"等待保存缓冲，第 "+std::to_string(j.index+1)+" / "+std::to_string(j.plan.frames)+u8" 帧…";
    QueuePendingWrite(frame);
}
inline void QueueSceneReadback() {
  auto &j=*job;auto &s=*poser_capture::shot;
  Require(j.sceneReads.size()<2,"Full-scene GPU queue overflow");
  // Own the request before submitting it; an allocation failure must never
  // leave an untracked GPU copy referring to a texture we then destroy.
  j.sceneReads.emplace_back();auto &r=j.sceneReads.back();
  r.index=j.index;r.width=s.width;r.height=s.height;r.started=MmdNow();
  r.request.start(poser_capture::Target(s.rt),s.width,s.height);
  if(j.index%60==0)Log("[MMD-RECORD] async scene queued frame=%u pending=%zu",j.index,j.sceneReads.size());
  ++j.index;j.lastActivity=MmdNow();
  if(j.index==j.plan.frames)j.phase=Phase::SceneDrain;
  else if(j.sceneReads.size()==2)j.phase=Phase::SceneQueue;
  else NextSample();
}
inline void ServiceSceneReadbacks() {
  auto &j=*job;
  // Requests can finish out of order. Copy *every* fulfilled request in its
  // valid frame, even if an earlier request or the disk queue is still blocked.
  for(auto &r:j.sceneReads)if(r.request.pending&&r.request.take(r.rgba)) {
    ++j.readbacks;j.readbackMs+=(MmdNow()-r.started)*1000;
    j.lastActivity=MmdNow();
    if(r.index%60==0)Log("[MMD-RECORD] async scene ready frame=%u latency=%.1fms",r.index,(MmdNow()-r.started)*1000);
  }
  while(!j.sceneReads.empty()) {
    auto &r=j.sceneReads.front();if(r.request.pending)break;
    if(!r.work.run) {
      Require(!r.rgba.empty(),"Empty full-scene GPU result");
      Packet packet;packet.layer="full";packet.fastPng=j.opt.fastPng;packet.rgba=std::move(r.rgba);
      r.work=MakeWrite(std::move(packet),r.index,r.width,r.height,std::make_shared<FrameFiles>(),true);
    }
    if(!j.pipeline)j.pipeline=std::make_unique<recording_queue::Writer>();
    if(!j.pipeline->submit(r.work))break;
    j.sceneReads.pop_front();j.lastActivity=MmdNow();
  }
}
inline bool DrainSceneReadbacks() {
  bool done=true;for(auto &r:job->sceneReads)if(!r.request.drain())done=false;
  if(done)job->sceneReads.clear();return done;
}
inline void Rendered(int frame) {
  if(busy&&job&&job->phase==Phase::Finish&&job->restoredFrame>=0&&frame>job->restoredFrame&&frame!=job->cleanFrame) {
    job->cleanFrame=frame;++job->cleanRenders;return;
  }
  if(!busy||!job||job->phase!=Phase::Pass||cancelled)return;
  try {
    auto &j=*job;if(frame!=j.preparedFrame||frame<0||frame==j.samples.last)return;
    j.lastActivity=MmdNow();
    ++j.renderedSamples;
    if(!j.samples.rendered(frame,j.targetSamples))return;
    j.renderMs+=(MmdNow()-j.passStarted)*1000;
    Validate();Require(bool(poser_capture::shot),"Recording render target unavailable");
    auto &s=*poser_capture::shot;
    CheckCamera(s.camera.target());j.clock.verify(false,j.plan.fps);j.readStarted=MmdNow();
    if(!j.maskProbed&&(j.passes[j.pass].kind==Kind::Scene||j.passes[j.pass].kind==Kind::Beauty)) {
      j.maskProbed=true;recording_mask_probe::Inspect();
    }
    if(recording_readback::api.ready) {
      if(j.scenePipeline){QueueSceneReadback();return;}
      j.readback.start(poser_capture::Target(s.rt),s.width,s.height);j.phase=Phase::Readback;
      if(j.index%60==0)Log("[MMD-RECORD] async readback queued frame=%u pass=%zu submit=%.1fms",j.index,j.pass,(MmdNow()-j.readStarted)*1000);
      return;
    }
    auto rgba=poser_capture::ReadPixels();
    ++j.readbacks;j.readbackMs+=(MmdNow()-j.readStarted)*1000;
    if(j.index%60==0)Log("[MMD-RECORD] readback frame=%u pass=%zu gpu+copy=%.1fms",j.index,j.pass,(MmdNow()-j.readStarted)*1000);
    SubmitPixels(std::move(rgba),frame);
  } catch(const std::exception &e){Finish(e.what(),true);}
}
inline void Tick(double now,bool allowed) {
  if(!busy)return;
  try {
    if(!job) {job=std::make_unique<Job>();job->opt=options;job->started=job->lastActivity=now;job->simulationNow=now;}
    auto &j=*job;
    recording_display::done=completed;recording_display::total=total;
    if(stepClock.failed&&j.phase!=Phase::Finish)Finish(u8"无法暂停录制步进时钟",true);
    if(j.pipeline) {
      const auto saved=j.pipeline->completed();
      if(completed!=saved){completed=saved;j.manifest["completed_frames"]=saved;j.lastActivity=now;}
      if(j.phase!=Phase::Finish)j.pipeline->check();
    }
    if((!allowed||cancelled||poser_capture::cancelled)&&j.phase!=Phase::Finish)Finish(u8"录制已取消");
    if(j.scenePipeline&&j.phase!=Phase::Finish)ServiceSceneReadbacks();
    if(j.phase!=Phase::Finish&&now-j.lastActivity>20)Finish(u8"录制超时，未收到相机或保存进度",true);
    if(j.phase==Phase::Start) {
      Require(FrameRenderReady()&&mmd_camera::ready,u8"渲染完成回调未就绪，无法安全录制");
      poser_capture::Resolve();
      const bool async=recording_readback::Resolve();
      j.scenePipeline=async&&!j.opt.layered;
      Require(g_transform_get_localScale&&g_transform_set_localScale,"Transform scale API unavailable");
      if(!(j.opt.squad?MmdSquadStart():MmdStart())) {
        status=std::string(u8"正在准备录制：")+(j.opt.squad?g_squad.status:g_mmd.status);
        Require(now-j.started<15,j.opt.squad?g_squad.status.c_str():g_mmd.status.c_str());return;
      }
      MmdCancelStart();MmdSquadCancelStart();j.session=Session();
      auto &t=Timeline();j.oldLoop=t.loop;j.oldSpeed=t.speed;j.transportOwned=true;t.loop=false;t.speed=1;
      j.plan=mmd_recording::MakePlan(j.opt,t.duration);total=j.plan.frames;
      t.seek(0,now);g_mmd.audio.stop();Actors();
      if(j.opt.squad)for(auto &a:g_squad.actors){if(a)++a->saved.terrain.epoch;}
      else ++g_mmd.session.terrain.epoch;
      NewDirectory();j.clock.acquire(j.plan.fps);j.warmStarted=now;j.phase=Phase::Warm;
      Sample(0,false);Log("[MMD-RECORD] start fps=%d frames=%u layers=%zu actors=%zu warmup=%d asyncGPU=%d fastPNG=%d directory=%s",j.plan.fps,j.plan.frames,j.passes.size(),j.actors.size(),j.opt.warmup,int(async),int(j.opt.fastPng),lastDirectory.c_str());
    } else if(j.phase==Phase::Warm) {
      Validate();Require(now-j.started<30,u8"人物或衣物预热超时");
      j.simulationNow=now;Sample(0,false);
    } else if(j.phase==Phase::Step&&!j.stepSampled&&ReadUnityFrameCount()!=j.stepFrame) {
      // Apply the next body/camera sample in the frame that will actually be
      // captured. Applying it while the previous frame finishes writing would
      // render it once too early and destroy the new frame's motion vectors.
      const int frame=ReadUnityFrameCount();stepClock.pulse(frame);
      Require(stepClock.heldFrame>=0&&!stepClock.failed,u8"无法确认录制步进时钟");
      j.clock.verify(false,j.plan.fps);
      j.simulationNow+=1./j.plan.fps;Sample(j.plan.time(j.index),true);j.stepSampled=true;j.sampledFrame=frame;
      if(frame>j.stepFrame+1)Log("[MMD-RECORD] held delayed sample=%u armed=%d held=%d sampled=%d",j.index,j.stepFrame,stepClock.heldFrame,frame);
    } else if(j.phase==Phase::Readback) {
      std::vector<unsigned char> rgba;const double copiedAt=MmdNow();
      if(j.readback.take(rgba)) {
        ++j.readbacks;j.readbackMs+=(MmdNow()-j.readStarted)*1000;
        if(j.index%60==0)Log("[MMD-RECORD] async readback ready frame=%u pass=%zu latency=%.1fms copy=%.1fms",j.index,j.pass,(MmdNow()-j.readStarted)*1000,(MmdNow()-copiedAt)*1000);
        Validate();CheckCamera(poser_capture::shot->camera.target());j.clock.verify(false,j.plan.fps);
        SubmitPixels(std::move(rgba),ReadUnityFrameCount());
      }
    } else if(j.phase==Phase::Write) {
      QueuePendingWrite(-1);
    } else if(j.phase==Phase::SceneQueue&&j.sceneReads.size()<2) {
      NextSample();
    } else if(j.phase==Phase::SceneDrain&&j.sceneReads.empty()) {
      Finish(u8"录制完成");
    }
    if(j.phase==Phase::Finish) {
      MmdCancelStart();MmdSquadCancelStart();
      // Restore time even if a dead renderer or a failed extraction needs retry.
      const bool clockOK=j.clock.restore(),sceneOK=RuntimeClosing()||RestoreScene();
      // Keep the source RT alive until any queued GPU copy has completed.
      const bool captureOK=sceneOK&&j.readback.drain()&&DrainSceneReadbacks()&&poser_capture::Restore();
      if(j.transportOwned) {
        auto &t=Timeline();t.loop=j.oldLoop;t.speed=j.oldSpeed;t.lastNow=now;
        if(Active()&&Session()==j.session)t.state=mmd::PlayState::Paused;
        j.transportOwned=false;
      }
      if(!clockOK||!sceneOK||!captureOK)return;
      if(j.restoredFrame<0)j.restoredFrame=ReadUnityFrameCount();
      if(j.pipeline) {
        if(j.pipeline->pending())return;
        completed=j.pipeline->completed();j.manifest["completed_frames"]=completed;
        try {j.pipeline->check();}
        catch(const std::exception &e){j.failed=true;j.result=std::string(u8"保存失败：")+e.what();}
        const auto stats=j.pipeline->statistics();
        j.manifest["performance"]={{"render_samples",j.renderedSamples},{"render_wait_ms",j.renderMs},
          {"readbacks",j.readbacks},{"readback_ms",j.readbackMs},{"reused_colour_frames",j.reusedColours},
          {"save_queue_wait_ms",j.queueWaitMs},{"encode_worker_ms",stats.prepareMs},{"write_worker_ms",stats.writeMs},
          {"queue_peak_items",stats.peakItems},{"queue_peak_reserved_bytes",stats.peakBytes}};
        Log("[MMD-RECORD] timing frames=%u renders=%llu reusedColours=%llu renderWait=%.1fms readback=%.1fms queueWait=%.1fms encodeWorkers=%.1fms writeWorker=%.1fms peakJobs=%zu peakReservedMiB=%.1f",
          completed,j.renderedSamples,j.reusedColours,j.renderMs,j.readbackMs,j.queueWaitMs,stats.prepareMs,stats.writeMs,stats.peakItems,stats.peakBytes/1048576.);
        j.pipeline.reset();j.pendingWrite={};
      }
      if(j.writer.valid()) {
        if(j.writer.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        try {j.writer.get();}
        catch(const std::exception &e){j.failed=true;j.result=std::string(u8"保存失败：")+e.what();}
      }
      if(!j.directory.empty()&&!j.finalWrite) {
        j.manifest["state"]=j.failed?"failed":(completed==j.plan.frames?"complete":"cancelled");j.manifest["message"]=j.result;
        j.finalWrite=true;
        j.writer=std::async(std::launch::async,[files=j.files,directory=j.directory,manifest=j.manifest](){files->rollback();Manifest(directory,manifest);});return;
      }
      // Do not reveal the last matte render on removal. Normal scene renders
      // run behind the cover after restoration, including temporal convergence.
      if(j.displayNeedsClean&&recording_display::requested&&recording_display::ready&&j.cleanRenders<16&&!RuntimeClosing()&&!poser_close::Closing())return;
      Log("[MMD-RECORD] finished frames=%u/%u failed=%d result=%s",completed,total,int(j.failed),j.result.c_str());
      status=j.result+u8"，已保存 "+std::to_string(completed)+u8" 帧";
      g_mmdOfflineRecording=false;g_captureRenderActive=false;poser_capture::externalOwner=false;poser_capture::busy=false;poser_capture::cancelled=false;
      job.reset();busy=false;cancelled=false;
      recording_display::End();
    }
  } catch(const std::exception &e) {Finish(e.what(),true);}
}
}
