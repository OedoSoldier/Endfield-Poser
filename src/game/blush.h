#pragma once
#include "game/mmd_visibility.h"
#include "math/blush.h"
#include <fstream>
#include <filesystem>
#include <map>

// Called only on the game thread under g_poseMutex. UI edits profiles, never
// Unity objects. The native EmotionShaderSyncMono uses renderer-wide blocks.
// Preserve existing indexed overrides too, without creating new overrides that
// bypass the game's renderer-wide material updates. Shared assets stay intact.
namespace poser_blush {
static std::map<std::string,blush::Profile> profiles;
static std::map<std::string,std::string> labels;
static bool enabled=true;
static std::string profileError;
struct Info {std::string status=u8"等待播放或手动预览";int materials=0,verified=0;float weight=0;};
static std::map<std::string,Info> info;
static std::string previewModel;
static float preview=0;
static void *previewActor=nullptr;
static bool previewEnabled=false;
inline blush::Profile ProfileFor(const std::string &key) {auto it=profiles.find(key);return it==profiles.end()?blush::Profile{}:it->second;}
inline void Load() {
  try {
    std::filesystem::path path(PoserFilePath(L"mmd\\blush.json"));if(!std::filesystem::exists(path))return;
    if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Blush file too large");
    std::ifstream file(path);nlohmann::json j;file>>j;
    if(j.value("version",0)!=1||!j.at("models").is_object()||j.at("models").size()>1024)throw std::runtime_error("Invalid blush settings");
    std::map<std::string,blush::Profile> parsed;
    for(auto it=j.at("models").begin();it!=j.at("models").end();++it) {
      if(it.key().empty()||it.key().size()>256||character_face::ModelKey(it.key())!=it.key())throw std::runtime_error("Invalid blush model key");
      parsed.emplace(it.key(),blush::Read(it.value()));
    }
    profiles=std::move(parsed);profileError.clear();
  }catch(const std::exception &e){profileError=u8"脸红设置读取失败，暂用默认值";Log("[BLUSH] %s",e.what());}
}
inline bool Save() {
  try {
    std::filesystem::path path(PoserFilePath(L"mmd\\blush.json")),temp=path;temp+=L".tmp";
    std::filesystem::create_directories(path.parent_path());
    nlohmann::json j={{"version",1},{"models",nlohmann::json::object()}};
    for(const auto &entry:profiles)j["models"][entry.first]=blush::Write(entry.second);
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<j.dump(2);f.flush();if(!f)throw std::runtime_error("Blush write failed");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Blush replace failed");
    profileError.clear();return true;
  }catch(const std::exception &e){profileError=u8"脸红设置未保存，请重试";Log("[BLUSH] %s",e.what());return false;}
}
struct Api {
  using EmptyNative=bool(__cdecl*)(void*);
  void *materials=nullptr,*shader=nullptr,*has=nullptr,*blockClass=nullptr,*ctor=nullptr,*empty=nullptr;
  void *getGlobal=nullptr,*setGlobal=nullptr,*getSlot=nullptr,*setSlot=nullptr,*setFloat=nullptr,*getFloat=nullptr,*propertyId=nullptr;
  void *materialFloat=nullptr,*texture=nullptr,*keyword=nullptr;
  EmptyNative emptyNative=nullptr;
  int index=0,blend=0,useMap=0,emotionMap=0;
} static api;
static std::string apiIssue;
inline bool Call(void *m,void *o,void **args=nullptr) {void *r=nullptr;return mmd_api::Call(m,o,args,r);}
inline bool ResolveEmpty(Api &next) {
  // The game strips the managed property but keeps this registered Unity
  // internal call. Its argument is the managed MPB, not its native pointer.
  if(next.empty)return true;
  if(!RuntimeClosing()&&il2cpp_resolve_icall)
    next.emptyNative=reinterpret_cast<Api::EmptyNative>(il2cpp_resolve_icall("UnityEngine.MaterialPropertyBlock::get_isEmpty"));
  return next.emptyNative!=nullptr;
}
inline bool Empty(void *block,bool &value) {
  if(!block||RuntimeClosing())return false;
  if(api.empty)return mmd_api::Value(api.empty,block,value);
  __try {if(!api.emptyNative)return false;value=api.emptyNative(block);return true;}
  __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool ApiUnavailable(const char *method) {
  if(apiIssue!=method){apiIssue=method;Log("[BLUSH] unavailable material interface: %s",method);}return false;
}
inline bool Ready() {
  if(api.materials)return true;
  if(!mmd_visibility::Ready()||!il2cpp_object_new||!il2cpp_string_new)return ApiUnavailable("renderer/object lifetime API");
  auto renderer=mmd_api::Class("UnityEngine","Renderer"),material=mmd_api::Class("UnityEngine","Material"),shader=mmd_api::Class("UnityEngine","Shader");
  Api next;next.blockClass=mmd_api::Class("UnityEngine","MaterialPropertyBlock");
  next.materials=mmd_api::Method(renderer,"get_sharedMaterials","UnityEngine.Material[]");
  next.shader=mmd_api::Method(material,"get_shader","UnityEngine.Shader");
  next.has=mmd_api::Method(material,"HasProperty","System.Boolean",{"System.Int32"});
  next.ctor=mmd_api::Method(next.blockClass,".ctor","System.Void");
  next.empty=mmd_api::Method(next.blockClass,"get_isEmpty","System.Boolean");
  if(!ResolveEmpty(next))return ApiUnavailable("MaterialPropertyBlock.isEmpty");
  next.getGlobal=mmd_api::Method(renderer,"GetPropertyBlock","System.Void",{"UnityEngine.MaterialPropertyBlock"});
  next.setGlobal=mmd_api::Method(renderer,"SetPropertyBlock","System.Void",{"UnityEngine.MaterialPropertyBlock"});
  next.getSlot=mmd_api::Method(renderer,"GetPropertyBlock","System.Void",{"UnityEngine.MaterialPropertyBlock","System.Int32"});
  next.setSlot=mmd_api::Method(renderer,"SetPropertyBlock","System.Void",{"UnityEngine.MaterialPropertyBlock","System.Int32"});
  next.setFloat=mmd_api::Method(next.blockClass,"SetFloat","System.Void",{"System.Int32","System.Single"});
  next.getFloat=mmd_api::Method(next.blockClass,"GetFloat","System.Single",{"System.Int32"});
  next.materialFloat=mmd_api::Method(material,"GetFloat","System.Single",{"System.Int32"});
  next.texture=mmd_api::Method(material,"GetTexture","UnityEngine.Texture",{"System.Int32"});
  next.keyword=mmd_api::Method(material,"IsKeywordEnabled","System.Boolean",{"System.String"});
  next.propertyId=mmd_api::Method(shader,"PropertyToID","System.Int32",{"System.String"},true);
  const std::pair<void*,const char*> required[]{
    {next.materials,"Renderer.sharedMaterials"},{next.shader,"Material.shader"},{next.has,"Material.HasProperty(int)"},
    {next.ctor,"MaterialPropertyBlock.ctor"},{next.getGlobal,"Renderer.GetPropertyBlock(block)"},
    {next.setGlobal,"Renderer.SetPropertyBlock(block)"},{next.getFloat,"MaterialPropertyBlock.GetFloat(int)"},
    {next.materialFloat,"Material.GetFloat(int)"},{next.texture,"Material.GetTexture(int)"},{next.keyword,"Material.IsKeywordEnabled"},
    {next.getSlot,"Renderer.GetPropertyBlock(block,int)"},{next.setSlot,"Renderer.SetPropertyBlock(block,int)"},
    {next.setFloat,"MaterialPropertyBlock.SetFloat(int,float)"},{next.propertyId,"Shader.PropertyToID"}};
  for(const auto &entry:required)if(!entry.first)return ApiUnavailable(entry.second);
  auto id=[&](const char *name,int &out){void *r=nullptr,*a[]={il2cpp_string_new(name)};return mmd_api::Call(next.propertyId,nullptr,a,r)&&r&&mmd_api::Copy((char*)r+16,&out,4);};
  if(!id("_EmotionIndex",next.index)||!id("_EmotionBlend",next.blend)||!id("_UseEmotionMap",next.useMap)||
      !id("_EmotionMap",next.emotionMap))return ApiUnavailable("emotion property identifiers");
  api=next;apiIssue.clear();Log("[BLUSH] native material API ready empty=%s",api.empty?"managed":"registered-icall");return true;
}
struct Block {
  uint32_t handle=0;
  ~Block(){mmd_visibility::Free(handle);}
  Block()=default;Block(const Block&)=delete;Block &operator=(const Block&)=delete;
  Block(Block &&v) noexcept:handle(v.handle){v.handle=0;}
  void *get() const {return handle&&il2cpp_gchandle_get_target?il2cpp_gchandle_get_target(handle):nullptr;}
  bool create() {auto object=il2cpp_object_new(api.blockClass);if(!object)return false;
    handle=il2cpp_gchandle_new(object,false);return handle&&Call(api.ctor,object);}
};
struct Slot {
  mmd_visibility::Ref renderer,material;
  Block original,working;
  int index=0;bool global=false,originallyEmpty=true,applied=false,verified=false;
  float readWeight=0,readStyle=0,previousWeight=0;
  double nextRead=0;
  std::string rendererName,materialName;
};
struct Actor {
  mmd_visibility::Ref owner,root;
  std::string key;
  std::vector<std::unique_ptr<Slot>> slots;
  double nextScan=0;
};
static std::map<void*,std::unique_ptr<Actor>> actors;
static double nextApiAttempt=0;
inline bool Array(void *array,std::vector<void*> &out,size_t limit) {
  size_t count=0;if(!array||!mmd_api::Copy((char*)array+IL2CPP_ARRAY_LEN,&count,sizeof(count))||count>limit)return false;
  out.resize(count);return !count||mmd_api::Copy((char*)array+IL2CPP_ARRAY_DATA,out.data(),count*sizeof(void*));
}
inline bool Materials(void *renderer,std::vector<void*> &out) {void *a=nullptr;return mmd_api::Call(api.materials,renderer,nullptr,a)&&Array(a,out,64);}
inline std::string Name(void *object) {
  void *name=nullptr;char text[192]{};
  if(mmd_api::Call(g_object_get_name,object,nullptr,name))ReadStr(name,text,sizeof(text));return text;
}
inline bool Float(void *method,void *object,int id,float &value) {
  void *args[]={&id},*result=nullptr;
  return mmd_api::Call(method,object,args,result)&&result&&mmd_api::Copy((char*)result+16,&value,sizeof(value))&&std::isfinite(value);
}
inline bool Compatible(void *material) {
  void *shader=nullptr,*name=nullptr;
  if(!UnityObjAlive(material)||!mmd_api::Call(api.shader,material,nullptr,shader)||!shader||
      !mmd_api::Call(g_object_get_name,shader,nullptr,name))return false;
  char text[160]{};ReadStr(name,text,sizeof(text));if(strcmp(text,"HGRP/CharacterNPR_Skin"))return false;
  for(int id:{api.index,api.blend}) {void *r=nullptr,*args[]={&id};bool has=false;
    if(!mmd_api::Call(api.has,material,args,r)||!r||!mmd_api::Copy((char*)r+16,&has,sizeof(has))||!has)return false;}
  // Body/eyelash materials share this shader but have no emotion atlas. A
  // successful SetFloat on those is not evidence of a supported blush surface.
  float use=0;bool keyword=false;void *texture=nullptr,*result=nullptr;
  void *texArgs[]={&api.emotionMap},*keyArgs[]={il2cpp_string_new("_EMOTION_MAP")};
  return Float(api.materialFloat,material,api.useMap,use)&&use>.5f&&
    mmd_api::Call(api.keyword,material,keyArgs,result)&&result&&mmd_api::Copy((char*)result+16,&keyword,sizeof(keyword))&&keyword&&
    mmd_api::Call(api.texture,material,texArgs,texture)&&UnityObjAlive(texture);
}
inline bool RestoreSlot(Slot &s,void *root) {
  if(!s.applied)return true;
  void *renderer=nullptr;auto life=s.renderer.inspect(renderer);
  if(life==mmd_visibility::Life::Unavailable)return false;
  if(life==mmd_visibility::Life::Dead||!root)return true;
  int scope=mmd_visibility::Scope(renderer,root);if(scope<0)return false;if(!scope)return true;
  std::vector<void*> materials;if(!Materials(renderer,materials))return false;
  if(!s.global&&s.index>=int(materials.size()))return true;
  // A replacement material must not receive the old material's block.
  void *originalMaterial=nullptr;auto materialLife=s.material.inspect(originalMaterial);
  if(materialLife==mmd_visibility::Life::Unavailable)return false;
  bool same=s.global||(materialLife==mmd_visibility::Life::Alive&&materials[s.index]==originalMaterial);
  void *original=s.originallyEmpty||!same?nullptr:s.original.get();
  if(!s.originallyEmpty&&same&&!original)return false;
  void *args[]={original,&s.index};
  if(!Call(s.global?api.setGlobal:api.setSlot,renderer,args))return false;s.applied=false;return true;
}
inline bool Restore(Actor &a) {
  void *root=nullptr;auto life=a.root.inspect(root);
  if(life==mmd_visibility::Life::Unavailable)return false;
  if(life==mmd_visibility::Life::Dead)root=nullptr;
  for(auto it=a.slots.begin();it!=a.slots.end();)if(RestoreSlot(**it,root))it=a.slots.erase(it);else ++it;
  return a.slots.empty();
}
inline void RestoreAll() {
  previewEnabled=false;previewActor=nullptr;preview=0;
  for(auto it=actors.begin();it!=actors.end();)if(Restore(*it->second)) {
    info[it->second->key].weight=0;info[it->second->key].status=u8"已恢复原有脸红";it=actors.erase(it);
  }else ++it;
}
inline bool Scan(Actor &a,void *root,double now) {
  if(now<a.nextScan)return true;a.nextScan=now+2;
  void *go=nullptr,*array=nullptr;bool inactive=true;
  void *args[]={mmd_visibility::api.rendererType,&inactive};std::vector<void*> renderers;
  if(!mmd_api::Call(g_component_get_gameObject,root,nullptr,go)||!go||
      !mmd_api::Call(mmd_visibility::api.query,go,args,array)||!Array(array,renderers,256))return false;
  for(void *renderer:renderers) {
    if(!UnityObjAlive(renderer)||mmd_visibility::Scope(renderer,root)!=1)continue;
    std::vector<void*> materials;if(!Materials(renderer,materials))return false;
    for(int i=0;i<int(materials.size());++i) {
      bool have=false,haveGlobal=false;for(const auto &s:a.slots) {
        void *held=nullptr;auto life=s->renderer.inspect(held);
        if(life==mmd_visibility::Life::Unavailable)return false;
        if(life==mmd_visibility::Life::Alive&&held==renderer){
          haveGlobal|=s->global;have|=!s->global&&s->index==i;
        }
      }
      if(have||!Compatible(materials[i]))continue;
      if(a.slots.size()>=64)return false;
      auto s=std::make_unique<Slot>();s->index=i;
      if(!s->renderer.capture(renderer)||!s->material.capture(materials[i])||!s->original.create()||!s->working.create())return false;
      void *read[]={s->original.get(),&i};
      if(!Call(api.getSlot,renderer,read)||!Empty(s->original.get(),s->originallyEmpty))return false;
      s->rendererName=Name(renderer);s->materialName=Name(materials[i]);
      // An existing per-material block takes priority over the global block.
      // Retain it and update it as well; never introduce a new indexed block.
      if(!s->originallyEmpty)a.slots.push_back(std::move(s));
      if(!haveGlobal) {
        auto global=std::make_unique<Slot>();global->index=i;global->global=true;
        if(!global->renderer.capture(renderer)||!global->material.capture(materials[i])||
            !global->original.create()||!global->working.create())return false;
        void *readGlobal[]={global->original.get()};
        if(!Call(api.getGlobal,renderer,readGlobal)||!Empty(global->original.get(),global->originallyEmpty))return false;
        global->rendererName=Name(renderer);global->materialName=Name(materials[i]);
        a.slots.push_back(std::move(global));
        Log("[BLUSH] model=%s face=%s material=%s route=native-global atlas=1 keyword=1",a.key.c_str(),Name(renderer).c_str(),Name(materials[i]).c_str());
      }
    }
  }
  return true;
}
inline bool Apply(Actor &a,void *root,float weight,int style,double now) {
  bool ok=true;
  for(auto it=a.slots.begin();it!=a.slots.end();) {
    auto &s=**it;void *renderer=nullptr;auto life=s.renderer.inspect(renderer);
    if(life==mmd_visibility::Life::Unavailable){ok=false;++it;continue;}
    int scope=life==mmd_visibility::Life::Alive?mmd_visibility::Scope(renderer,root):0;
    if(scope<0){ok=false;++it;continue;}
    if(life==mmd_visibility::Life::Dead||!scope){it=a.slots.erase(it);continue;}
    std::vector<void*> materials;
    if(!Materials(renderer,materials)){ok=false;++it;continue;}
    void *material=nullptr;auto materialLife=s.material.inspect(material);
    if(materialLife==mmd_visibility::Life::Unavailable){ok=false;++it;continue;}
    if(s.index>=int(materials.size())||materialLife==mmd_visibility::Life::Dead||materials[s.index]!=material) {
      if(RestoreSlot(s,root)){it=a.slots.erase(it);a.nextScan=0;}else{ok=false;++it;}
      continue;
    }
    void *block=s.working.get(),*read[]={block,&s.index};
    if(!block||!Call(s.global?api.getGlobal:api.getSlot,renderer,read)){ok=false;++it;continue;}
    bool verify=now>=s.nextRead;
    if(verify)Float(api.getFloat,block,api.blend,s.previousWeight);
    float index=float(style);void *blendArgs[]={&api.blend,&weight},*indexArgs[]={&api.index,&index};
    if(!Call(api.setFloat,block,indexArgs)||!Call(api.setFloat,block,blendArgs)){ok=false;++it;continue;}
    s.applied=true; // retain on failures, including partial native writes
    if(!Call(s.global?api.setGlobal:api.setSlot,renderer,read)){s.verified=false;ok=false;}
    else if(verify) {
      s.nextRead=now+.5;
      s.verified=Call(s.global?api.getGlobal:api.getSlot,renderer,read)&&
        Float(api.getFloat,block,api.blend,s.readWeight)&&Float(api.getFloat,block,api.index,s.readStyle)&&
        std::abs(s.readWeight-weight)<.0001f&&std::abs(s.readStyle-index)<.0001f;
      if(!s.verified)ok=false;
    }
    ++it;
  }
  return ok;
}
struct Request {void *owner=nullptr,*root=nullptr;std::string key;float value=0,master=1;bool active=false;};
// HTTP/UI may read cached diagnostics under g_poseMutex, never invoke Unity.
inline nlohmann::json Diagnostics(const std::string &key) {
  auto rows=nlohmann::json::array();
  for(const auto &a:actors)if(a.second->key==key)for(const auto &s:a.second->slots)
    rows.push_back({{"renderer",s->rendererName},{"material",s->materialName},
      {"route",s->global?"native-global":"existing-indexed"},{"slot",s->index},
      {"verified",s->verified},{"weight",s->readWeight},{"style",s->readStyle},{"previous_weight",s->previousWeight}});
  return rows;
}
inline void Tick(const std::vector<Request> &requests,double now) {
  // Retire old instances first. Failed restoration remains owned for retry.
  for(auto it=actors.begin();it!=actors.end();) {
    bool keep=false;for(const auto &r:requests)if(r.active&&r.owner==it->first&&r.root==it->second->root.target()&&
        it->second->owner.target()==r.owner&&it->second->key==r.key&&ProfileFor(r.key).enabled){keep=true;break;}
    if(!keep&&Restore(*it->second)) {
      info[it->second->key].weight=0;info[it->second->key].status=u8"已恢复原有脸红";it=actors.erase(it);
    }else ++it;
  }
  for(const auto &r:requests) {
    if(!r.active||r.key.empty()||!r.owner||!r.root)continue;
    auto p=ProfileFor(r.key);auto &ui=info[r.key];
    if(!p.enabled){ui.status=u8"此角色已关闭脸红映射";continue;}
    if(!api.materials) {
      if(now<nextApiAttempt){ui.status=apiIssue.empty()?u8"等待游戏材质接口":std::string(u8"脸红接口不可用：")+apiIssue;continue;}
      nextApiAttempt=now+2;
      if(!Ready()){ui.status=std::string(u8"脸红接口不可用：")+apiIssue;continue;}
    }
    auto found=actors.find(r.owner);
    if(found==actors.end()) {
      if(actors.size()>=12){ui.status=u8"等待旧角色脸红恢复";continue;}
      auto a=std::make_unique<Actor>();a->key=r.key;
      if(!a->owner.capture(r.owner)||!a->root.capture(r.root)){ui.status=u8"等待角色就绪";continue;}
      found=actors.emplace(r.owner,std::move(a)).first;
    }
    auto &a=*found->second;
    if(a.owner.target()!=r.owner||a.root.target()!=r.root||a.key!=r.key){ui.status=u8"等待旧角色脸红恢复";continue;}
    float weight=blush::Weight(r.value,r.master,p);
    bool ok=Scan(a,r.root,now)&&Apply(a,r.root,weight,p.style,now);
    ui.materials=0;ui.verified=0;for(const auto &s:a.slots){if(s->global)++ui.materials;if(s->verified)++ui.verified;}ui.weight=weight;
    ui.status=!ok?u8"脸红参数写入或读回失败，等待重试":a.slots.empty()?u8"当前模型没有启用原生脸红贴图":u8"脸红参数已写入并读回";
  }
}
} // namespace poser_blush
