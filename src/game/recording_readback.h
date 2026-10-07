#pragma once
#include "game/transparent_capture.h"

// Poll only on Unity's thread. A fulfilled request's bytes are valid for one
// frame, so copy them immediately; workers never hold a Unity-owned pointer.
namespace recording_readback {
template<class T> inline bool CallValue(void *method,void *self,T &value,void **args) {
  void *box=nullptr;return mmd_api::Call(method,self,args,box)&&box&&mmd_api::Copy((char*)box+16,&value,sizeof(T));
}
struct Value {void *pointer=nullptr;int version=0,padding=0;};
struct Api {
  void *request=nullptr,*done=nullptr,*error=nullptr,*bytes=nullptr,*layers=nullptr,*data=nullptr;
  int format=0;
  bool tried=false,ready=false;
} static api;
inline bool Unavailable(const char *reason) {
  Log("[MMD-RECORD] async GPU unavailable: %s; using synchronous readback",reason);return false;
}
inline void *FindRequest(void *readback) {
  // IL2CPP may retain the generic arity (`1) in the reflected delegate name.
  // Match both spellings, still checking every argument and the return type.
  for(const char *callback:{"System.Action`1<UnityEngine.Rendering.AsyncGPUReadbackRequest>",
                           "System.Action<UnityEngine.Rendering.AsyncGPUReadbackRequest>"}) {
    auto method=mmd_api::Method(readback,"Request","UnityEngine.Rendering.AsyncGPUReadbackRequest",
      {"UnityEngine.Texture","System.Int32","UnityEngine.Experimental.Rendering.GraphicsFormat",callback},true);
    if(method)return method;
  }
  using Free=void(*)(void*);auto release=hGA?reinterpret_cast<Free>(GetProcAddress(hGA,"il2cpp_free")):nullptr;
  if(readback&&release&&il2cpp_type_get_name&&il2cpp_class_get_methods&&il2cpp_method_get_name&&il2cpp_method_get_param_count&&il2cpp_method_get_param) {
    void *it=nullptr;int logged=0;
    while(auto method=il2cpp_class_get_methods(readback,&it)) {
      auto name=il2cpp_method_get_name(method);
      if(!name||strcmp(name,"Request")||il2cpp_method_get_param_count(method)!=4)continue;
      auto format=il2cpp_type_get_name(il2cpp_method_get_param(method,2));
      auto callback=il2cpp_type_get_name(il2cpp_method_get_param(method,3));
      Log("[MMD-RECORD] async Request candidate format=%s callback=%s",format?format:"?",callback?callback:"?");
      if(format)release(const_cast<char*>(format));if(callback)release(const_cast<char*>(callback));
      if(++logged==8)break;
    }
  }
  return nullptr;
}
inline bool Resolve() {
  if(api.tried)return api.ready;api.tried=true;
  auto info=mmd_api::Class("UnityEngine","SystemInfo");bool supported=false;
  auto device=mmd_api::Method(info,"get_graphicsDeviceType","UnityEngine.Rendering.GraphicsDeviceType",{},true);
  int backend=-1;if(device&&mmd_api::Value(device,nullptr,backend))Log("[MMD-RECORD] Unity graphics backend=%d",backend);
  auto supports=mmd_api::Method(info,"get_supportsAsyncGPUReadback","System.Boolean",{},true);
  if(!supports)return Unavailable("SystemInfo support method missing");
  if(!mmd_api::Value(supports,nullptr,supported))return Unavailable("SystemInfo support query failed");
  if(!supported)return Unavailable("graphics backend reports unsupported");
  auto request=mmd_api::Class("UnityEngine.Rendering","AsyncGPUReadbackRequest");
  auto readback=mmd_api::Class("UnityEngine.Rendering","AsyncGPUReadback");uint32_t align=0;
  if(!request||!readback||!il2cpp_class_value_size)return Unavailable("request metadata missing");
  const int size=il2cpp_class_value_size(request,&align);
  if(size!=sizeof(Value)){Log("[MMD-RECORD] async request size=%d alignment=%u",size,align);return Unavailable("request layout mismatch");}
  auto ptr=poser_capture::Field(request,"m_Ptr","System.IntPtr"),version=poser_capture::Field(request,"m_Version","System.Int32");
  if(!ptr||!version||il2cpp_field_get_offset(ptr)!=16||il2cpp_field_get_offset(version)!=24)return Unavailable("request fields mismatch");
  api.request=FindRequest(readback);
  if(!api.request)return Unavailable("GraphicsFormat Request overload missing");
  // SceneColorPS already contains display bytes. The TextureFormat overload
  // chooses sRGB from project colour space; request explicit UNorm instead.
  try {api.format=poser_capture::EnumClass(mmd_api::Class("UnityEngine.Experimental.Rendering","GraphicsFormat"),"R8G8B8A8_UNorm");}
  catch(...){return Unavailable("RGBA8 UNorm format unavailable");}
  api.done=mmd_api::Method(request,"get_done","System.Boolean");
  api.error=mmd_api::Method(request,"get_hasError","System.Boolean");
  api.bytes=mmd_api::Method(request,"get_layerDataSize","System.Int32");
  api.layers=mmd_api::Method(request,"get_layerCount","System.Int32");
  api.data=mmd_api::Method(request,"GetDataRaw","System.IntPtr",{"System.Int32"});
  api.ready=api.request&&api.done&&api.error&&api.bytes&&api.layers&&api.data;
  if(!api.ready)return Unavailable("request polling/data methods missing");
  Log("[MMD-RECORD] async GPU ready: RGBA8 UNorm, polling on game thread");
  return api.ready;
}
struct Request {
  Value value;
  bool pending=false;
  int width=0,height=0;
  void start(void *texture,int w,int h) {
    using poser_capture::Require;
    Require(api.ready&&!pending&&texture&&w>0&&h>0,"Invalid asynchronous capture request");
    int mip=0;void *args[]{texture,&mip,&api.format,nullptr};
    Require(CallValue(api.request,nullptr,value,args)&&value.pointer,"Cannot request asynchronous GPU capture");
    pending=true;width=w;height=h;
  }
  bool finished() {
    if(!pending)return true;
    if(RuntimeClosing()){pending=false;return true;}
    bool done=false,error=false;
    poser_capture::Require(mmd_api::Value(api.done,&value,done)&&mmd_api::Value(api.error,&value,error),"Cannot poll asynchronous GPU capture");
    return done||error;
  }
  bool take(std::vector<unsigned char> &out) {
    using poser_capture::Require;
    if(!finished())return false;
    bool error=true;int bytes=0,layers=0,layer=0;void *data=nullptr,*args[]{&layer};
    Require(pending&&mmd_api::Value(api.error,&value,error)&&!error,"Asynchronous GPU capture failed or expired");
    Require(mmd_api::Value(api.bytes,&value,bytes)&&mmd_api::Value(api.layers,&value,layers)&&layers==1&&
      int64_t(bytes)==int64_t(width)*height*4&&CallValue(api.data,&value,data,args)&&data,"Invalid asynchronous capture pixels");
    out.resize(size_t(bytes));Require(mmd_api::Copy(data,out.data(),out.size()),"Cannot copy asynchronous capture pixels");
    pending=false;return true;
  }
  bool drain() {if(!finished())return false;pending=false;return true;}
};
}
