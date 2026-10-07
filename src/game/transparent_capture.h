#pragma once
#include "game/mmd_visibility.h"
#include "core/plugin_paths.h"
#include "math/transparent_capture.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <algorithm>

// One shot through the game's HG pipeline. No camera.Render(), custom shaders,
// mesh readback, or CPU model export. All Unity operations run in game callbacks.
namespace poser_capture {
using Ref=mmd_visibility::Ref;
using Life=mmd_visibility::Life;
struct Color {float r=0,g=0,b=0,a=0;};
struct Rect {float x=0,y=0,w=0,h=0;};
struct Matrix {float v[16]{};};
static std::atomic<bool> requested{false},cancelled{false},busy{false};
static std::atomic<bool> externalOwner{false}; // Recorder leases the extraction resources.
static std::atomic<bool> requestedFollowAA{true};
static std::atomic<double> requestedAt{0};
static std::shared_ptr<const std::string> message=std::make_shared<const std::string>(u8"保存当前角色的透明 PNG（实验功能）");
static std::future<std::string> writer;
inline void Status(std::string text) {std::atomic_store(&message,std::make_shared<const std::string>(std::move(text)));}
inline std::string Status() {return *std::atomic_load(&message);}
inline std::wstring Directory() {return PoserFilePath(L"screenshots");}
inline bool Request(double now,bool followGameAA=true) {
  if(busy.exchange(true))return false;
  requestedAt=now;requestedFollowAA=followGameAA;cancelled=false;requested=true;Status(u8"等待游戏相机，准备人物透明截图…");return true;
}
inline void Cancel(const char *reason=u8"截图已取消") {
  if(!cancelled.exchange(true)&&busy)Status(reason);
  requested=false;
}
struct Api {
  void *all=nullptr,*goType=nullptr,*getComponent=nullptr,*getLayer=nullptr,*setLayer=nullptr;
  void *maskGet=nullptr,*maskSet=nullptr,*width=nullptr,*height=nullptr;
  void *transformType=nullptr,*worldMatrix=nullptr,*viewMatrix=nullptr,*projectionMatrix=nullptr;
  void *hgCameraGet=nullptr,*extractRegister=nullptr,*extractUnregister=nullptr,*wrap=nullptr,*unwrap=nullptr;
  void *rtClass=nullptr,*rtCtor=nullptr,*rtCreate=nullptr,*rtRelease=nullptr,*activeGet=nullptr,*activeSet=nullptr;
  void *texClass=nullptr,*texCtor=nullptr,*read=nullptr,*pixels=nullptr,*loadPixels=nullptr,*encode=nullptr,*destroy=nullptr,*glClear=nullptr;
  void *hgType=nullptr,*alpha=nullptr,*background=nullptr,*clear=nullptr,*depth=nullptr,*dynamic=nullptr,*aa=nullptr,*historyReset=nullptr,*frameCount=nullptr;
  int colorMode=0,noAA=0,fxAA=0,extractionType=0,extractionDuration=0;
} static api;
inline void Require(bool ok,const char *what) {if(!ok)throw std::runtime_error(what);}
inline void *Field(void *cls,const char *name,const char *type=nullptr) {
  void *it=nullptr;
  while(auto f=il2cpp_class_get_fields(cls,&it))if(!strcmp(il2cpp_field_get_name(f),name)&&
      !(il2cpp_field_get_flags(f)&0x10)&&(!type||mmd_api::Type(il2cpp_field_get_type(f),type)))return f;
  return nullptr;
}
inline int EnumClass(void *cls,const char *name) {
  Require(cls&&il2cpp_field_static_get_value,"Missing enum metadata");
  void *it=nullptr;uint32_t align=0;
  Require(cls&&il2cpp_class_value_size(cls,&align)==4,"Invalid enum size");
  while(auto f=il2cpp_class_get_fields(cls,&it))if(!strcmp(il2cpp_field_get_name(f),name)&&
      (il2cpp_field_get_flags(f)&0x10)) {int value=0;il2cpp_field_static_get_value(f,&value);return value;}
  throw std::runtime_error(std::string("Missing enum value: ")+name);
}
inline int Enum(void *field,const char *name) {
  Require(field&&il2cpp_class_from_type,"Missing enum metadata");
  return EnumClass(il2cpp_class_from_type(il2cpp_field_get_type(field)),name);
}
inline void *Type(void *cls) {
  auto t=cls?il2cpp_class_get_type(cls):nullptr;return t?il2cpp_type_get_object(t):nullptr;
}
inline void Resolve() {
  if(api.encode)return;
  Require(mmd_visibility::Ready()&&il2cpp_object_new&&il2cpp_class_get_type&&il2cpp_type_get_object&&
    il2cpp_class_get_fields&&il2cpp_field_get_offset&&il2cpp_field_get_flags&&il2cpp_field_get_type&&
    il2cpp_field_get_name&&il2cpp_class_value_size,"Object lifetime/metadata API unavailable");
  Api a;
  auto object=mmd_api::Class("UnityEngine","Object"),camera=mmd_api::Class("UnityEngine","Camera");
  auto component=mmd_api::Class("UnityEngine","Component"),go=mmd_api::Class("UnityEngine","GameObject");
  auto hg=mmd_api::Class("HG.Rendering.Runtime","HGAdditionalCameraData");
  a.rtClass=mmd_api::Class("UnityEngine","RenderTexture");a.texClass=mmd_api::Class("UnityEngine","Texture2D");
  Require(object&&camera&&component&&go&&hg&&a.rtClass&&a.texClass,"HG camera/texture types unavailable");
  a.goType=Type(go);a.hgType=Type(hg);
  a.all=mmd_api::Method(object,"FindObjectsOfType","UnityEngine.Object[]",{"System.Type","System.Boolean"},true);
  a.getComponent=mmd_api::Method(component,"GetComponent","UnityEngine.Component",{"System.Type"});
  a.getLayer=mmd_api::Method(go,"get_layer","System.Int32");
  a.setLayer=mmd_api::Method(go,"set_layer","System.Void",{"System.Int32"});
  a.maskGet=mmd_api::Method(camera,"get_cullingMask","System.Int32");
  a.maskSet=mmd_api::Method(camera,"set_cullingMask","System.Void",{"System.Int32"});
  auto transform=mmd_api::Class("UnityEngine","Transform");a.transformType=Type(transform);
  a.worldMatrix=mmd_api::Method(transform,"get_localToWorldMatrix","UnityEngine.Matrix4x4");
  a.viewMatrix=mmd_api::Method(camera,"get_worldToCameraMatrix","UnityEngine.Matrix4x4");
  a.projectionMatrix=mmd_api::Method(camera,"get_nonJitteredProjectionMatrix","UnityEngine.Matrix4x4");
  auto hgCamera=mmd_api::Class("HG.Rendering.Runtime","HGCamera");
  a.hgCameraGet=mmd_api::Method(hgCamera,"TryGet","HG.Rendering.Runtime.HGCamera",{"UnityEngine.Camera","System.Int32"},true);
  a.extractRegister=mmd_api::Method(hgCamera,"RegisterRTExtraction","System.Void",{"HG.Rendering.Runtime.RTExtractionType","HG.Rendering.Runtime.RTExtractionDuration","UnityEngine.Rendering.RTHandle"});
  a.extractUnregister=mmd_api::Method(hgCamera,"UnRegisterRTExtraction","System.Void",{"HG.Rendering.Runtime.RTExtractionType","HG.Rendering.Runtime.RTExtractionDuration","UnityEngine.Rendering.RTHandle"});
  a.wrap=mmd_api::Method(mmd_api::Class("UnityEngine.Rendering","RTHandles"),"Alloc","UnityEngine.Rendering.RTHandle",{"UnityEngine.RenderTexture"},true);
  a.unwrap=mmd_api::Method(mmd_api::Class("UnityEngine.Rendering","RTHandle"),"SetRenderTexture","System.Void",{"UnityEngine.RenderTexture"});
  a.width=mmd_api::Method(camera,"get_pixelWidth","System.Int32");a.height=mmd_api::Method(camera,"get_pixelHeight","System.Int32");
  a.rtCtor=mmd_api::Method(a.rtClass,".ctor","System.Void",{"System.Int32","System.Int32","System.Int32","UnityEngine.RenderTextureFormat","UnityEngine.RenderTextureReadWrite"});
  a.rtCreate=mmd_api::Method(a.rtClass,"Create","System.Boolean");a.rtRelease=mmd_api::Method(a.rtClass,"Release","System.Void");
  a.activeGet=mmd_api::Method(a.rtClass,"get_active","UnityEngine.RenderTexture",{},true);
  a.activeSet=mmd_api::Method(a.rtClass,"set_active","System.Void",{"UnityEngine.RenderTexture"},true);
  a.texCtor=mmd_api::Method(a.texClass,".ctor","System.Void",{"System.Int32","System.Int32","UnityEngine.TextureFormat","System.Boolean","System.Boolean"});
  a.read=mmd_api::Method(a.texClass,"ReadPixels","System.Void",{"UnityEngine.Rect","System.Int32","System.Int32","System.Boolean"});
  a.pixels=mmd_api::Method(a.texClass,"GetPixels32","UnityEngine.Color32[]");
  a.loadPixels=mmd_api::Method(a.texClass,"LoadRawTextureData","System.Void",{"System.IntPtr","System.Int32"});
  a.encode=mmd_api::Method(mmd_api::Class("UnityEngine","ImageConversion"),"EncodeToPNG","System.Byte[]",{"UnityEngine.Texture2D"},true);
  a.destroy=mmd_api::Method(object,"Destroy","System.Void",{"UnityEngine.Object"},true);
  a.glClear=mmd_api::Method(mmd_api::Class("UnityEngine","GL"),"Clear","System.Void",{"System.Boolean","System.Boolean","UnityEngine.Color","System.Single"},true);
  a.frameCount=mmd_api::Method(mmd_api::Class("UnityEngine","Time"),"get_frameCount","System.Int32",{},true);
  a.alpha=Field(hg,"enableAlpha","System.Boolean");a.background=Field(hg,"backgroundColorHDR","UnityEngine.Color");
  a.clear=Field(hg,"clearColorMode");a.depth=Field(hg,"clearDepth","System.Boolean");
  a.dynamic=Field(hg,"allowDynamicResolution","System.Boolean");a.aa=Field(hg,"antialiasing");
  a.historyReset=Field(hgCamera,"resetPostProcessingHistory","System.Boolean");
  const std::pair<void*,const char*> required[]={{a.all,"Object.FindObjectsOfType"},{a.goType,"GameObject type"},{a.hgType,"HG camera type"},
    {a.getComponent,"Component.GetComponent"},{a.getLayer,"GameObject.layer getter"},{a.setLayer,"GameObject.layer setter"},
    {a.maskGet,"Camera.cullingMask getter"},{a.maskSet,"Camera.cullingMask setter"},
    {a.transformType,"Transform type"},{a.worldMatrix,"Transform.localToWorldMatrix"},
    {a.viewMatrix,"Camera.worldToCameraMatrix"},{a.projectionMatrix,"Camera.nonJitteredProjectionMatrix"},
    {a.hgCameraGet,"HGCamera.TryGet"},{a.extractRegister,"HGCamera.RegisterRTExtraction"},{a.extractUnregister,"HGCamera.UnRegisterRTExtraction"},
    {a.wrap,"RTHandles.Alloc"},{a.unwrap,"RTHandle.SetRenderTexture"},
    {a.width,"Camera.pixelWidth"},{a.height,"Camera.pixelHeight"},{a.rtCtor,"RenderTexture constructor"},{a.rtCreate,"RenderTexture.Create"},{a.rtRelease,"RenderTexture.Release"},
    {a.activeGet,"RenderTexture.active getter"},{a.activeSet,"RenderTexture.active setter"},{a.texCtor,"Texture2D constructor"},{a.read,"Texture2D.ReadPixels"},
    {a.pixels,"Texture2D.GetPixels32"},{a.loadPixels,"Texture2D.LoadRawTextureData"},{a.encode,"ImageConversion.EncodeToPNG"},{a.destroy,"Object.Destroy"},{a.glClear,"GL.Clear"},{a.frameCount,"Time.frameCount"},
    {a.alpha,"HG enableAlpha"},{a.background,"HG backgroundColorHDR"},{a.clear,"HG clearColorMode"},{a.depth,"HG clearDepth"},{a.dynamic,"HG allowDynamicResolution"},{a.aa,"HG antialiasing"},
    {a.historyReset,"HG resetPostProcessingHistory"}};
  for(auto &p:required)Require(p.first,p.second);
  a.colorMode=Enum(a.clear,"Color");a.noAA=Enum(a.aa,"None");a.fxAA=Enum(a.aa,"FastApproximateAntialiasing");
  // These are the game's own GetScreenCaptureWithoutUI output stage and a
  // persistent lease we explicitly unregister before destroying its texture.
  a.extractionType=EnumClass(mmd_api::Class("HG.Rendering.Runtime","RTExtractionType"),"SceneColorPS");
  a.extractionDuration=EnumClass(mmd_api::Class("HG.Rendering.Runtime","RTExtractionDuration"),"Persistent");api=a;
}
inline bool Call(void *method,void *object,void **args=nullptr) {void *result=nullptr;return mmd_api::Call(method,object,args,result);}
inline bool SetInt(void *method,void *object,int value) {void *args[]{&value};return Call(method,object,args);}
inline bool SetObject(void *method,void *object,void *value) {void *args[]{value};return Call(method,object,args);}
inline std::vector<void*> Array(void *array,size_t limit) {
  size_t count=0;Require(array&&mmd_api::Copy((char*)array+IL2CPP_ARRAY_LEN,&count,sizeof(count))&&count<=limit,"Invalid object array");
  std::vector<void*> values(count);Require(!count||mmd_api::Copy((char*)array+IL2CPP_ARRAY_DATA,values.data(),count*sizeof(void*)),"Cannot read object array");return values;
}
struct FieldValue {
  size_t offset=0;std::vector<unsigned char> original,next;
  template<class T> void Capture(void *object,void *field,T value) {
    offset=il2cpp_field_get_offset(field);Require(offset>=16&&offset<65536,"Invalid HG field offset");
    original.resize(sizeof(T));next.resize(sizeof(T));memcpy(next.data(),&value,sizeof(T));
    Require(mmd_api::Copy((char*)object+offset,original.data(),original.size()),"Cannot save HG field");
  }
  bool Write(void *object,bool restore) const {
    const auto &value=restore?original:next;
    return mmd_api::Copy(value.data(),(char*)object+offset,value.size());
  }
};
struct Layer {Ref object;int original=0;};
struct TransformState {Ref object;Matrix original;};
struct Shot {
  Ref camera,hg,actor,root;
  std::vector<Layer> layers;std::vector<FieldValue> fields;
  uint32_t rt=0,tex=0,hgCamera=0,rtHandle=0;int mask=0,layer=0,width=0,height=0,frames=0,lastFrame=-1;
  bool changed=false,reading=false,registered=false;Ref previousActive;
  FieldValue historyReset;bool followAA=true,resetForCapture=false,resetForScene=false;int settleFrames=2,sceneSettleFrames=2;
  bool mattePhase=false;std::vector<unsigned char> scene;
  std::vector<TransformState> transforms;Matrix view,projection;
  double deadline=0;
};
static std::unique_ptr<Shot> shot;
inline void *Target(uint32_t handle) {return handle?il2cpp_gchandle_get_target(handle):nullptr;}
inline void *Create(void *cls,void *ctor,void **args,uint32_t &handle) {
  void *object=il2cpp_object_new(cls);Require(object,"Texture allocation failed");
  handle=il2cpp_gchandle_new(object,false);Require(handle!=0,"Cannot retain texture");
  Require(Call(ctor,object,args)&&UnityObjAlive(object),"Texture creation failed");return object;
}
inline bool Destroy(uint32_t &handle,bool renderTexture) {
  if(!handle)return true;
  void *object=Target(handle);
  if(UnityObjAlive(object)) {
    if(renderTexture&&!Call(api.rtRelease,object))return false;
    if(!SetObject(api.destroy,nullptr,object))return false;
  }
  mmd_visibility::Free(handle);handle=0;return true;
}
inline bool Restore() {
  if(!shot)return true;
  auto &s=*shot;
  if(RuntimeClosing()) {shot.reset();return true;}
  bool ok=true;void *obj=nullptr;
  if(s.reading) {
    auto life=s.previousActive.inspect(obj);
    if(life==Life::Unavailable||!SetObject(api.activeSet,nullptr,life==Life::Alive?obj:nullptr))ok=false;
    else s.reading=false;
  }
  if(s.changed) {
    auto life=s.camera.inspect(obj);
    if(life==Life::Unavailable)ok=false;
    if(life==Life::Alive) {
      if(!SetInt(api.maskSet,obj,s.mask))ok=false;
    }
    life=s.hg.inspect(obj);if(life==Life::Unavailable)ok=false;
    if(life==Life::Alive)for(auto &f:s.fields)ok=f.Write(obj,true)&&ok;
    for(auto &l:s.layers) {
      life=l.object.inspect(obj);if(life==Life::Unavailable)ok=false;
      if(life==Life::Alive)ok=SetInt(api.setLayer,obj,l.original)&&ok;
    }
  }
  if(s.registered) {
    void *args[]{&api.extractionType,&api.extractionDuration,Target(s.rtHandle)};
    if(!Target(s.hgCamera)||!Target(s.rtHandle)||!Call(api.extractUnregister,Target(s.hgCamera),args))ok=false;
    else s.registered=false;
  }
  if(!ok)return false;
  // Request a fresh normal-scene history after removing the isolated actor
  // image. This is a one-shot engine flag, not a persistent setting to restore.
  if(s.changed&&!s.resetForScene) {
    if(!Target(s.hgCamera)||!s.historyReset.Write(Target(s.hgCamera),false))return false;
    s.resetForScene=true;
  }
  // RTHandle.Release owns/destroys its texture. Detach our own RT instead so
  // the ordinary cleanup/retry path remains its sole native-resource owner.
  if(s.rtHandle) {
    if(!SetObject(api.unwrap,Target(s.rtHandle),nullptr))return false;
    mmd_visibility::Free(s.rtHandle);s.rtHandle=0;
  }
  if(!Destroy(s.tex,false)||!Destroy(s.rt,true))return false;
  mmd_visibility::Free(s.hgCamera);s.hgCamera=0;
  shot.reset();return true;
}
inline void CaptureAlignment();
inline void Prepare(void *camera,void *actor,void *root,double now,bool isolate=true) {
  Resolve();shot=std::make_unique<Shot>();auto &s=*shot;s.deadline=now+8;
  Require(s.camera.capture(camera)&&s.actor.capture(actor)&&s.root.capture(root),"Cannot retain character/camera");
  void *hg=nullptr,*args[]{api.hgType};
  Require(mmd_api::Call(api.getComponent,camera,args,hg)&&s.hg.capture(hg),"HG camera data unavailable");
  Require(mmd_api::Value(api.width,camera,s.width)&&mmd_api::Value(api.height,camera,s.height)&&
    s.width>0&&s.height>0&&s.width<=8192&&s.height<=8192&&uint64_t(s.width)*s.height<=33554432,"Unsupported screenshot resolution");
  Require(mmd_api::Value(api.maskGet,camera,s.mask),"Cannot save camera mask");
  int eye=0;void *hgCamera=nullptr,*hgArgs[]{camera,&eye};
  Require(mmd_api::Call(api.hgCameraGet,nullptr,hgArgs,hgCamera)&&hgCamera,"Active HG camera unavailable");
  s.hgCamera=il2cpp_gchandle_new(hgCamera,false);Require(s.hgCamera,"Cannot retain HG camera");
  s.historyReset.Capture(hgCamera,api.historyReset,true);
  s.followAA=requestedFollowAA.load();
  FieldValue aaState,dynamicState;aaState.Capture(hg,api.aa,0);dynamicState.Capture(hg,api.dynamic,false);
  int originalAA=0;bool originalDynamic=false;
  memcpy(&originalAA,aaState.original.data(),sizeof(originalAA));memcpy(&originalDynamic,dynamicState.original.data(),sizeof(originalDynamic));
  // Temporal reconstruction needs fresh scene samples. Never reset history
  // every callback: let the game's own jitter/history converge between resets.
  s.sceneSettleFrames=originalAA!=api.noAA&&originalAA!=api.fxAA?16:2;
  s.settleFrames=s.followAA?s.sceneSettleFrames:2;
  // Find a genuinely unused scene layer instead of assuming layer 30/31 is free.
  bool inactive=true;
  if(isolate) {
  void *all=nullptr,*query[]{api.goType,&inactive};uint32_t used=0;
  Require(mmd_api::Call(api.all,nullptr,query,all),"Cannot inspect scene layers");
  for(void *go:Array(all,262144))if(UnityObjAlive(go)) {
    int layer=0;Require(mmd_api::Value(api.getLayer,go,layer)&&layer>=0&&layer<32,"Cannot inspect object layer");used|=uint32_t(1)<<layer;
  }
  s.layer=transparent_capture::UnusedLayer(used);Require(s.layer>=0,"No unused rendering layer is available");
  void *go=nullptr,*renderers=nullptr,*children[]{mmd_visibility::api.rendererType,&inactive};
  Require(mmd_api::Call(g_component_get_gameObject,root,nullptr,go)&&mmd_api::Call(mmd_visibility::api.query,go,children,renderers),"Cannot enumerate character renderers");
  std::set<void*> seen;
  for(void *renderer:Array(renderers,1024))if(UnityObjAlive(renderer)) {
    Require(mmd_visibility::Scope(renderer,root)==1,"Character renderer changed");
    void *object=nullptr;Require(mmd_api::Call(g_component_get_gameObject,renderer,nullptr,object)&&UnityObjAlive(object),"Invalid character renderer object");
    if(!seen.insert(object).second)continue;
    Layer l;Require(l.object.capture(object)&&mmd_api::Value(api.getLayer,object,l.original),"Cannot save character layer");s.layers.push_back(std::move(l));
  }
  Require(!s.layers.empty(),"No character renderers found");
  }
  auto field=[&](void *f,auto value){FieldValue v;v.Capture(hg,f,value);s.fields.push_back(std::move(v));};
  field(api.alpha,true);field(api.background,Color{});field(api.clear,api.colorMode);field(api.depth,true);
  if(!s.followAA){field(api.dynamic,false);field(api.aa,api.fxAA);}
  // Native ScreenCaptureUtils uses R8G8B8A8_UNorm, not an sRGB render target.
  // SceneColorPS already contains display colour: never encode it a second time.
  int depth=0,format=0,readWrite=1;void *rtArgs[]{&s.width,&s.height,&depth,&format,&readWrite}; // ARGB32, Linear
  void *rt=Create(api.rtClass,api.rtCtor,rtArgs,s.rt);bool created=false;
  Require(mmd_api::Value(api.rtCreate,rt,created)&&created,"RenderTexture.Create failed");
  // A fresh GPU allocation has undefined pixels. Clear it so an extraction
  // that never runs is rejected as empty, never accepted as random alpha.
  void *active=nullptr;Require(mmd_api::Call(api.activeGet,nullptr,nullptr,active)&&(!active||s.previousActive.capture(active)),"Cannot save active render target");
  s.reading=true;Require(SetObject(api.activeSet,nullptr,rt),"Cannot clear capture target");
  bool clear=true;Color color;float farDepth=1;void *clearArgs[]{&clear,&clear,&color,&farDepth};
  Require(Call(api.glClear,nullptr,clearArgs),"Cannot clear capture texture");
  Require(SetObject(api.activeSet,nullptr,active),"Cannot restore active render target");s.reading=false;
  s.previousActive=Ref{};
  void *wrapper=nullptr,*wrapArgs[]{rt};Require(mmd_api::Call(api.wrap,nullptr,wrapArgs,wrapper)&&wrapper,"Cannot wrap extraction texture");
  s.rtHandle=il2cpp_gchandle_new(wrapper,false);Require(s.rtHandle,"Cannot retain extraction texture");
  if(isolate)CaptureAlignment();
  Log("[CAPTURE] prepared %dx%d characterRenderers=%zu isolatedLayer=%d source=SceneColorPS(postprocess,noUI) target=Linear aa=%s originalAA=%d dynamic=%d settleFrames=%d",
    s.width,s.height,s.layers.size(),s.layer,s.followAA?"game":"FXAA",originalAA,int(originalDynamic),s.settleFrames);
}
inline void RegisterExtraction() {
  auto &s=*shot;
  if(!s.registered) {
    void *args[]{&api.extractionType,&api.extractionDuration,Target(s.rtHandle)};
    s.registered=true; // Unregister even if a managed call partially succeeds.
    Require(Call(api.extractRegister,Target(s.hgCamera),args),"Cannot register postprocessed screenshot");
  }
}
inline void Apply() {
  auto &s=*shot;void *camera=s.camera.target(),*hg=s.hg.target();
  Require(camera&&hg&&s.actor.target()&&s.root.target(),"Character/camera changed during capture");
  RegisterExtraction();
  if(!s.mattePhase)return; // The colour pass must keep the entire scene and original quality settings.
  s.changed=true;
  for(auto &l:s.layers)Require(SetInt(api.setLayer,l.object.target(),s.layer),"Cannot isolate character renderer");
  for(auto &f:s.fields)Require(f.Write(hg,false),"Cannot enable HG alpha rendering");
  Require(SetInt(api.maskSet,camera,int(uint32_t(1)<<s.layer)),"Cannot isolate game camera");
  if(!s.resetForCapture) {
    Require(s.historyReset.Write(Target(s.hgCamera),false),"Cannot reset screenshot history");s.resetForCapture=true;
  }
}
inline void CaptureAlignment() {
  auto &s=*shot;
  Require(mmd_api::Value(api.viewMatrix,s.camera.target(),s.view)&&mmd_api::Value(api.projectionMatrix,s.camera.target(),s.projection),"Cannot save screenshot camera alignment");
  bool inactive=true;void *go=nullptr,*list=nullptr,*args[]{api.transformType,&inactive};
  Require(mmd_api::Call(g_component_get_gameObject,s.root.target(),nullptr,go)&&mmd_api::Call(mmd_visibility::api.query,go,args,list),"Cannot inspect screenshot pose");
  for(void *transform:Array(list,8192))if(UnityObjAlive(transform)) {
    TransformState state;Require(state.object.capture(transform)&&mmd_api::Value(api.worldMatrix,transform,state.original),"Cannot save screenshot pose");s.transforms.push_back(std::move(state));
  }
  Require(!s.transforms.empty(),"No screenshot pose transforms");
}
inline void CheckAlignment() {
  auto &s=*shot;Matrix view,projection;
  Require(mmd_api::Value(api.viewMatrix,s.camera.target(),view)&&mmd_api::Value(api.projectionMatrix,s.camera.target(),projection)&&
    transparent_capture::SameTransform(s.view.v,view.v,16)&&transparent_capture::SameTransform(s.projection.v,projection.v,16),u8"截图期间镜头发生变化，请保持镜头不动后重试");
  for(auto &state:s.transforms) {
    Matrix current;Require(state.object.target()&&mmd_api::Value(api.worldMatrix,state.object.target(),current)&&transparent_capture::SameTransform(state.original.v,current.v,16),
      u8"截图期间人物发生变化，请冻结角色、暂停动作后重试");
  }
}
inline std::vector<unsigned char> ReadPixels() {
  auto &s=*shot;int format=4;bool mip=false,linear=true; // RGBA32, raw display bytes from the native screenshot output
  void *texArgs[]{&s.width,&s.height,&format,&mip,&linear};void *tex=s.tex?Target(s.tex):Create(api.texClass,api.texCtor,texArgs,s.tex);
  void *active=nullptr;Require(mmd_api::Call(api.activeGet,nullptr,nullptr,active)&&(!active||s.previousActive.capture(active)),"Cannot save active render target");
  s.reading=true;Require(SetObject(api.activeSet,nullptr,Target(s.rt)),"Cannot read capture target");
  Rect rect{0,0,float(s.width),float(s.height)};int zero=0;void *readArgs[]{&rect,&zero,&zero,&mip};
  Require(Call(api.read,tex,readArgs),"GPU screenshot readback failed");
  Require(SetObject(api.activeSet,nullptr,active),"Cannot restore active render target");s.reading=false;
  s.previousActive=Ref{};
  void *pixels=nullptr;size_t count=0;
  Require(mmd_api::Call(api.pixels,tex,nullptr,pixels)&&pixels&&mmd_api::Copy((char*)pixels+IL2CPP_ARRAY_LEN,&count,sizeof(count))&&count==size_t(s.width)*s.height,"Invalid screenshot pixels");
  std::vector<unsigned char> rgba(count*4);Require(mmd_api::Copy((char*)pixels+IL2CPP_ARRAY_DATA,rgba.data(),rgba.size()),"Cannot read screenshot pixels");return rgba;
}
inline std::vector<unsigned char> Read() {
  auto &s=*shot;CheckAlignment();auto rgba=ReadPixels();
  auto stats=transparent_capture::Inspect(rgba.data(),rgba.size(),s.width,s.height);
  Log("[CAPTURE] alpha clear=%zu solid=%zu partial=%zu",stats.clear,stats.solid,stats.partial);
  Require(transparent_capture::Usable(stats),stats.clear?"HG returned an empty image; character-only rendering is unavailable":
    (s.followAA?u8"当前游戏抗锯齿未输出透明通道，请切换 FXAA 兼容模式重试":"HG returned an opaque image; transparent output is unavailable"));
  auto edges=transparent_capture::ApplyMatte(s.scene,rgba,s.width,s.height);
  void *data=s.scene.data();int dataBytes=int(s.scene.size());void *loadArgs[]{&data,&dataBytes};
  Require(Call(api.loadPixels,Target(s.tex),loadArgs),"Cannot combine final scene colour and character alpha");
  Log("[CAPTURE] composed original scene RGB + character alpha; defringe corrected=%zu unresolved=%zu (opaque RGB and alpha unchanged)",edges.corrected,edges.unresolved);
  void *png=nullptr,*pngArgs[]{Target(s.tex)};size_t bytes=0;
  Require(mmd_api::Call(api.encode,nullptr,pngArgs,png)&&png&&mmd_api::Copy((char*)png+IL2CPP_ARRAY_LEN,&bytes,sizeof(bytes))&&bytes>32&&bytes<=256*1024*1024,"PNG encoding failed");
  std::vector<unsigned char> out(bytes);Require(mmd_api::Copy((char*)png+IL2CPP_ARRAY_DATA,out.data(),bytes),"Cannot copy PNG");return out;
}
inline std::string Save(std::vector<unsigned char> png,std::wstring directory) {
  std::filesystem::path dir(directory);Require(!directory.empty(),"Screenshot directory unavailable");
  std::filesystem::create_directories(dir);SYSTEMTIME time{};GetLocalTime(&time);wchar_t name[128];
  swprintf_s(name,L"character-%04u%02u%02u-%02u%02u%02u-%03u-%llu.png",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetTickCount64());
  auto file=dir/name;HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  Require(handle!=INVALID_HANDLE_VALUE,"Cannot create screenshot file");DWORD written=0;
  bool ok=WriteFile(handle,png.data(),DWORD(png.size()),&written,nullptr)&&written==png.size();CloseHandle(handle);
  if(!ok){DeleteFileW(file.c_str());throw std::runtime_error("Cannot write screenshot file (check free space)");}
  Log("[CAPTURE] saved %zu bytes",png.size());return file.u8string();
}
inline void AfterCamera(void *camera,void *actor,void *root,double now,bool allowed) {
  if(externalOwner)return;
  if(!allowed)Cancel();
  if(cancelled){Restore();return;}
  try {
    if(requested.exchange(false))Prepare(camera,actor,root,now);
    if(!shot)return;
    auto &s=*shot;
    Require(now<=s.deadline&&camera==s.camera.target()&&actor==s.actor.target()&&root==s.root.target(),"Capture timed out or character/camera changed");
    int frame=-1;Require(mmd_api::Value(api.frameCount,nullptr,frame)&&frame>=0,"Cannot observe rendered frame progress");
    if(frame==s.lastFrame){Apply();return;}
    s.lastFrame=frame;
    if(!s.mattePhase) {
      if(s.frames++<2){Apply();Status(u8"正在读取完整场景的颜色与反光…");return;}
      CheckAlignment();s.scene=ReadPixels();
      Require(std::any_of(s.scene.begin(),s.scene.end(),[](unsigned char v){return v!=0;}),"Full-scene screenshot extraction returned empty");
      s.mattePhase=true;s.frames=0;
      Log("[CAPTURE] full-scene colour captured; extracting character alpha");
    }
    if(s.frames++<s.settleFrames) {
      Apply();Status(s.settleFrames>2?std::string(u8"正在等待游戏抗锯齿稳定（")+std::to_string(s.frames)+"/"+std::to_string(s.settleFrames)+u8"）…":u8"正在渲染人物透明截图…");return;
    }
    auto png=Read();Require(Restore(),"Restoring screenshot state; please wait");
    Status(u8"透明通道检查通过，正在保存 PNG…");
    writer=std::async(std::launch::async,Save,std::move(png),Directory());
  } catch(const std::exception &e) {
    Log("[CAPTURE] failed: %s",e.what());Status(std::string(u8"人物透明截图失败：")+e.what());cancelled=true;Restore();
  }
}
inline void Maintenance(double now,bool allowed) {
  if(externalOwner)return;
  if(!allowed)Cancel();
  else if(busy&&requested&&now-requestedAt>10)Cancel(u8"截图失败：未等到可用游戏相机");
  else if(shot&&now>shot->deadline)Cancel(u8"截图超时，正在恢复原画面");
  if(cancelled)Restore();
  if(writer.valid()&&writer.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
    try {auto path=writer.get();Status(std::string(u8"已保存人物透明截图：")+path);}
    catch(const std::exception &e){Status(std::string(u8"截图保存失败：")+e.what());}
  }
  if(!shot&&!requested&&!writer.valid()) {
    busy=false;
  }
}
}
