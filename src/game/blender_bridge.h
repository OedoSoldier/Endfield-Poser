#pragma once
#include "math/blender_bridge.h"
#include <deque>
#include <future>
#include <mutex>

// HTTP only enqueues immutable commands. Unity reads/writes, capture and restore
// happen in GameFrameTick, including timeout/character-switch cleanup.
namespace poser_blender {
using Json=nlohmann::json;
struct Command {std::string path;Json body;std::promise<Json> result;std::atomic<bool> cancelled{false};};
static std::mutex queueMutex;
static std::deque<std::shared_ptr<Command>> queue;
static uint64_t session=0,sequence=0;
static double touched=0;
static Json scene;
static std::vector<bool> editable;
static std::map<std::string,mmd_face_controls::Control> controls;
static blender_bridge::Frame current;
static bool hasFrame=false;
static bool adoptedMotion=false;
static mmd::Timeline savedTimeline;
static std::vector<MmdSavedTransform> connectedPose;
static SMCMotionFrame connectedFace;
static mmd_camera::Request connectedCamera;
static bool loading=false;
static std::string loadedFile;
static auto &loader=*new std::future<std::shared_ptr<blender_bridge::Clip>>;
static std::atomic<int> localCommand{0};
static std::atomic<HWND> fileDialog{nullptr};
static std::atomic<bool> closing{false};
static bool restoreFixed=false;
static void *fixedActor=nullptr;
static int fixedRevision=0;
static std::shared_ptr<GripReferences> fixedReferences;
static std::string status=u8"未连接 Blender";
static Json V(Vec3 v){return Json::array({v.x,v.y,v.z});}
static Json Q(Quat q){return Json::array({q.x,q.y,q.z,q.w});}
static Json Matrix(const mmd::Matrix &m){return std::vector<float>(m.m,m.m+16);}
static Json Error(const std::string &message){return {{"ok",false},{"error",message}};}
static bool Live() {
  auto &s=g_mmd.session;
  return g_blenderEditing&&s.active&&s.cameraSession==session&&s.animator==g_charAnimator&&
    s.revision==s_bonesRev&&UnityObjAlive(s.animator)&&UnityObjAlive(s.root);
}
static void End() {
  if(adoptedMotion&&Live()&&!RuntimeClosing()) {
    for(const auto &b:connectedPose)MmdRawPose(b.transform,b.pos,b.rot);
    CapturePoseSnapshot();SMCMotionPublish(connectedFace);
    mmd_camera::Publish(connectedCamera);
    g_mmd.timeline=savedTimeline;g_mmd.timeline.lastNow=MmdNow();
    MmdApplyVisibility(g_mmd.session,g_mmd.clip,savedTimeline.seconds*30);
    g_blenderEditing=false;mmd_camera::observe=false;MmdSyncAudio();
  } else if(g_blenderEditing)MmdStop();
  adoptedMotion=false;connectedPose.clear();connectedFace={};
  hasFrame=false;status=u8"Blender 已断开，已恢复游戏状态";
}
static Json CameraJson(const mmd::CameraPose &c,Vec3 origin) {
  return {{"p",V(c.position-origin)},{"q",Q(c.rotation)},{"target",V(c.target-origin)},
    {"fov",c.fov},{"size",c.orthoSize},{"perspective",c.perspective}};
}
static Json BuildScene() {
  auto &m=g_mmd;auto &s=m.session;Json bones=Json::array(),faces=Json::array();
  mmd::Matrix shift=mmd::TRS(GetBoneWorldPos(s.root)*-1.f,{});
  editable.assign(s_allBones.size(),false);controls.clear();
  for(size_t i=0;i<s_allBones.size();++i) {
    auto &b=s_allBones[i];mmd::Matrix world;
    if(!MmdReadMatrix(g_transform_get_localToWorldMatrix,b.transform,&world))throw std::runtime_error("Cannot read skeleton transform");
    int role=-1;for(int h=0;h<s_humanBoneCount;++h)if(s_humanBones[h].transform==b.transform)role=s_humanBones[h].humanBone;
    bool driven=role>=0;
    if(MmdHasBody()&&i<m.mapper.output.write.size())driven=driven||m.mapper.output.write[i];
    // The jaw is driven by the expression snapshot after body posing. Writing
    // it from both editors causes the displayed bone and rendered mouth to fight.
    editable[i]=driven&&role!=Jaw&&b.transform!=s.root;
    bones.push_back({{"i",i},{"name",b.name},{"parent",b.parentIdx},{"role",role},{"editable",editable[i]},
      {"p",V(GetBoneLocalPos(b.transform))},{"q",Q(GetBoneLocalRot(b.transform))},
      {"scale",V(MmdLocalScale(b.transform))},{"world",Matrix(shift*world)}});
  }
  auto catalog=mmd_face_controls::Build(s_characterProfile.get(),SMCManualCatalog());
  // Preserve authored spelling and manual combination bindings. Full/half-width
  // VMD aliases may differ from the character catalog but already have a valid
  // mapping in the player; dropping them would silently lose expressions.
  for(const auto &track:m.clip.morphs)
    if(std::none_of(catalog.begin(),catalog.end(),[&](const auto &c){return c.name==track.first;}))
      catalog.push_back({track.first,-1,-1,4});
  for(size_t i=0;i<catalog.size();++i) {
    const auto &c=catalog[i];controls[c.name]=c;
    faces.push_back({{"name",c.name},{"label",mmd_face_controls::Label(c.name,c.panel,int(i))},{"panel",c.panel}});
  }
  Json camera=nullptr;
  if(auto observed=std::atomic_load(&mmd_camera::observed))camera=CameraJson(*observed,s.anchorWorld.position());
  Json currentBones=Json::array(),currentFaces=Json::object();
  for(const auto &b:bones)if(b["editable"].get<bool>())currentBones.push_back({{"i",b["i"]},{"p",b["p"]},{"q",b["q"]}});
  for(const auto &c:catalog)currentFaces[c.name]=0.f;
  if(adoptedMotion) {
    const auto faces=m.editedBody&&m.editedBody->hasFaces?m.editedBody->sample(savedTimeline.seconds).faces:std::map<std::string,float>{};
    for(const auto &track:m.clip.morphs)currentFaces[track.first]=m.editedBody&&m.editedBody->hasFaces&&!m.editedFaceOverrides.count(track.first)?faces.at(track.first):mmd::SampleMorph(track.second,savedTimeline.seconds*30);
  }
  else if(s_mmdFaceMode&&s_manualFace.applied&&s_manualFace.owner==s.animator)
    for(size_t i=0;i<s_manualFace.controls.size();++i)currentFaces[s_manualFace.controls[i].name]=s_manualFace.weights[i];
  Json initial={{"time",adoptedMotion?savedTimeline.seconds:0},{"root",V(GetBoneWorldPos(s.root)-s.anchorWorld.position())},
    {"bones",currentBones},{"faces",currentFaces},{"camera",camera},{"visible",!s.visibility.lease}};
  return {{"ok",true},{"protocol",blender_bridge::Protocol},{"session",session},{"revision",s_bonesRev},
    {"model",CurrentCharModelKey()},{"bones",bones},{"faces",faces},{"camera",camera},{"initial",initial},
    {"anchor_rotation",Q(mmd::Rotation(s.anchorWorld))},
    {"duration",m.timeline.duration},{"fps",30},{"motion_file",m.file},{"has_motion",MmdHasContent()}};
}
static Json Begin(bool useMotion=true) {
  if(g_blenderEditing)return Error("Blender already owns this character; disconnect first");
  if(MmdSquadBusy()||g_mmd.loading||g_mmd.preview)return Error("Stop squad playback/calibration and wait for loading first");
  if(!MmdCharacterReady())return Error("Character skeleton is not ready");
  auto &m=g_mmd;
  if(m.session.active&&(m.session.animator!=g_charAnimator||m.session.revision!=s_bonesRev))return Error("Character changed; wait for the skeleton to settle");
  adoptedMotion=m.session.active;
  savedTimeline=m.timeline;connectedCamera=mmd_camera::request;
  AcquireSRWLockShared(&s_motionFaceLock);connectedFace=s_motionFaceMailbox;ReleaseSRWLockShared(&s_motionFaceLock);
  if(!adoptedMotion&&useMotion&&MmdHasBody()&&!MmdPrepareProfile())return Error(m.calibrationStatus);
  if(!useMotion||!MmdHasBody())m.profile=MmdCurrentProfile();
  m.playbackProfile=m.profile;
  if(useMotion&&!m.editedBody&&!m.clip.bones.empty()) {
    MmdPrepareThumbs(m.playbackProfile,m.adaptation.characterThumbs);
    m.mapper.bind(m.rig,m.clip,m.playbackProfile,mmd::AdaptedRoles(m.adaptation),m.adaptation.tracks);
    if(m.autoScale)m.scale=m.mapper.suggestedScale;
    m.mapper.sample(0,m.scale,m.inPlace,m.height,m.ikMode,m.amplitude,m.motionCalibration);
  }
  if(useMotion&&m.editedBody)MmdSampleBody(0);
  if(!adoptedMotion) {m.preview=true;MmdCaptureSession(true);m.preview=false;}
  else if(!m.session.bodyOwned) {
    // Camera-only playback also needs a body snapshot once joint editing starts.
    auto previous=m.session;
    m.preview=true;MmdCaptureSession(true);m.preview=false;
    m.session.cameraSession=previous.cameraSession;m.session.anchorWorld=previous.anchorWorld;
    m.session.cameraBasis=previous.cameraBasis;m.session.visibility=previous.visibility;
  }
  // Hold exactly the last rendered MMD frame, without advancing or restoring it.
  m.timeline.state=mmd::PlayState::Paused;m.timeline.lastNow=MmdNow();MmdSyncAudio();
  connectedPose.clear();
  for(const auto &b:s_allBones)connectedPose.push_back({b.transform,GetBoneLocalPos(b.transform),GetBoneLocalRot(b.transform)});
  connectedPose.push_back({m.session.root,GetBoneLocalPos(m.session.root),GetBoneLocalRot(m.session.root)});
  if(std::none_of(m.session.transforms.begin(),m.session.transforms.end(),[&](const auto &b){return b.transform==m.session.root;}))
    m.session.transforms.push_back({m.session.root,m.session.rootPos,m.session.rootRot});
  g_blenderEditing=true;InterlockedExchange(&g_mmdOwnsPose,1);
  session=m.session.cameraSession;sequence=0;hasFrame=false;touched=MmdNow();
  restoreFixed=mmd_camera::fixedEnabled.load();fixedActor=g_charAnimator;fixedRevision=s_bonesRev;
  fixedReferences=restoreFixed?m.session.references:nullptr;
  mmd_camera::observe=true;
  MmdUpdateDuration();scene=BuildScene();status=u8"Blender 已连接；使用 Blender 时间轴预览";
  return scene;
}
static Json Sample(double seconds) {
  auto &m=g_mmd;auto &s=m.session;
  Json bones=Json::array(),faces=Json::object();Vec3 root{};
  if(MmdHasBody()) {
    MmdSampleBody(seconds);
    root=mmd::Rotation(s.anchorWorld)*m.mapper.output.rootOffset;
  }
  for(size_t i=0;i<editable.size();++i)if(editable[i]) {
    Vec3 p=s.transforms[i].pos;Quat q=s.transforms[i].rot;
    if(MmdHasBody()&&i<m.mapper.output.write.size()&&m.mapper.output.write[i]) {
      p=m.editedBody?m.playbackProfile.bones[i].localPos:m.profile.bones[i].localPos;q=m.mapper.output.localRot[i];
    }
    bones.push_back({{"i",i},{"p",V(p)},{"q",Q(q)}});
  }
  for(auto &c:controls)faces[c.first]=0;
  for(auto &track:m.clip.morphs)if(controls.count(track.first))faces[track.first]=m.editedBody&&m.editedBody->hasFaces&&!m.editedFaceOverrides.count(track.first)?m.editedFaces[track.first]:mmd::SampleMorph(track.second,seconds*30);
  Json camera=nullptr;auto &keys=MmdCameraKeys();
  if(m.editedCamera) {
    auto pose=blender_bridge::PlaceCamera(m.editedCamera->sample(seconds-m.cameraSettings.timeOffset),
      m.cameraSettings,s.anchorWorld.position(),mmd::Rotation(s.anchorWorld),root,{});
    camera=CameraJson(pose,s.anchorWorld.position());
  } else if(!keys.empty()) {
    auto pose=mmd::PlaceCamera(mmd::SampleCamera(keys,mmd::CameraFrame(seconds,m.cameraSettings),m.cameraSettings),
      m.cameraSettings,s.anchorWorld.position(),s.cameraBasis,root,m.scale,s.cameraHeight,mmd::CameraSourceHeight(m.rig),{});
    camera=CameraJson(pose,s.anchorWorld.position());
  }
  return {{"time",seconds},{"root",V(root)},{"bones",bones},{"faces",faces},{"camera",camera},
    {"visible",mmd::SampleVisibility(m.clip,seconds*30)}};
}
static void Apply(const blender_bridge::Frame &f) {
  auto &s=g_mmd.session;
  mmd_camera::SetFixed(false);
  Quat basis=f.hasAnchor?NormQ(mmd::Rotation(s.anchorWorld)*Conj(f.anchor)):Quat{};
  mmd_camera::Write(g_transform_set_position,s.root,s.anchorWorld.position()+basis*f.root);
  for(const auto &b:f.bones) {
    if(!editable[b.index])continue;
    MmdRawPose(s_allBones[b.index].transform,b.position,b.rotation);
  }
  CapturePoseSnapshot();
  SMCMotionFrame face;face.active=true;face.animator=s.animator;face.generation=s_faceGeneration;
  face.profile=s_characterProfile;face.settings=g_mmd.faceSettings;
  for(const auto &item:f.faces) {
    auto mapped=g_mmd.morphMap.find(item.first);
    if(mapped!=g_mmd.morphMap.end()) {
      mmd_face_bindings::Apply(mapped->second,item.second,face,[&](int id) {
        return face.profile&&s_characterBinding.ready&&s_characterBindingGeneration==s_faceGeneration&&
          id<int(s_characterBinding.usable.size())&&s_characterBinding.usable[id];
      });continue;
    }
    auto it=controls.find(item.first);if(it==controls.end())continue;const auto &c=it->second;
    bool calibrated=c.morph>=0&&s_characterBinding.ready&&s_characterBindingGeneration==s_faceGeneration&&
      c.morph<int(s_characterBinding.usable.size())&&s_characterBinding.usable[c.morph];
    if(calibrated)face.expressions[c.morph]=(std::max)(face.expressions[c.morph],item.second);
    else if(c.native>=0&&c.native<SMC_NUM_MOUTH+s_extraMorphCount)face.fallbackWeights[c.native]=mmd::Clamp(face.fallbackWeights[c.native]+item.second,0,1);
  }
  for(const auto &b:f.bones)for(int e=0;e<2;++e)
    if(g_mmd.profile.roles[21+e]==b.index) {
      face.eyeDriven[e]=true;face.eyes[e]=s_allBones[b.index].transform;face.eyeRotation[e]=b.rotation;
    }
  if(!f.preserveFace)SMCMotionPublish(face);
  mmd::MotionClip visibility;visibility.visibility.push_back({0,f.visible});MmdApplyVisibility(s,visibility,0);
  if(f.camera.active) {
    const auto &c=f.camera;mmd::CameraPose p;p.position=s.anchorWorld.position()+basis*c.position;
    p.target=s.anchorWorld.position()+basis*c.target;p.rotation=NormQ(basis*c.rotation);p.fov=c.fov;p.orthoSize=c.size;p.perspective=c.perspective;
    mmd_camera::Publish({true,s.cameraSession,s.animator,p,nullptr,0,f.time*30});
  } else mmd_camera::Stop();
}
static bool Tick() {
  if(!g_blenderEditing)return false;
  if(!Live()||MmdNow()-touched>8) {End();return false;}
  if(hasFrame)ClothRequestPlayback(!g_mmd.freezeCloth);
  const bool applied=hasFrame&&!ClothBlockFirstBodyPose("blender-preparation",MmdClothMayAdjustAnchor);
  if(applied)Apply(current);
  // Complete the same Prepared -> Ready handshake as MMD playback. Without
  // the submitted-pose notification the prepared garment times out after two
  // minutes, blocking every later body/camera preview despite successful HTTP.
  ClothService(applied,MmdClothMayAdjustAnchor,applied?current.time*30:NAN);
  if(hasFrame)MmdHideProps();return true;
}
static Json Execute(const std::string &path,const Json &j) {
  if(!poser_agreement::Allowed())return Error("Accept the agreement in the game panel first");
  if(path=="status")return {{"ok",true},{"protocol",blender_bridge::Protocol},{"active",Live()},{"session",session},{"status",status},{"loading",g_mmd.loading}};
  if(path=="load_vmd") {
    if(g_mmd.session.active||MmdSquadBusy()||g_mmd.loading)return Error("Disconnect and stop playback before loading a VMD");
    auto file=std::filesystem::path(mmd::Wide(j.at("path").get<std::string>()));
    if(file.extension()!=L".vmd")return Error("Choose a .vmd file");
    MmdBeginLoad(j.value("camera",false)?6:0,file);return {{"ok",true}};
  }
  if(path=="begin")return Begin();
  if(!Live()||blender_bridge::Id(j.at("session"))!=session)return Error("Session expired or character changed; reconnect");
  touched=MmdNow();
  if(path=="end"){End();return {{"ok",true}};}
  if(path=="scene"){scene=BuildScene();return scene;}
  if(path=="ping")return {{"ok",true},{"sequence",sequence}};
  if(path=="sample") {
    double start=blender_bridge::Number(j.at("start"),0,86400);
    int count=int(blender_bridge::Number(j.at("count"),1,8));
    double fps=blender_bridge::Number(j.value("fps",30.),1,120);
    Json frames=Json::array();for(int i=0;i<count;++i)frames.push_back(Sample(start+i/fps));
    return {{"ok",true},{"frames",frames}};
  }
  if(path=="frame") {
    auto next=blender_bridge::ParseFrame(j,editable.size());
    if(next.sequence<=sequence)return Error("Stale preview frame");
    for(auto &b:next.bones)if(!editable[b.index])return Error("Bone is not an editable body joint");
    for(auto &f:next.faces)if(!controls.count(f.first))return Error("Unknown face control");
    current=std::move(next);sequence=current.sequence;hasFrame=true;
    return {{"ok",true},{"sequence",sequence}};
  }
  return Error("Unknown Blender command");
}
static void Service() {
  g_blenderTick=Tick;
  if(restoreFixed&&!g_blenderEditing) {
    if(!closing&&!MmdOwnsPose()&&g_charAnimator==fixedActor&&s_bonesRev==fixedRevision&&UnityObjAlive(fixedActor))mmd_camera::SetFixed(true,fixedActor);
    restoreFixed=false;fixedReferences.reset();
  }
  if(loading&&loader.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
    loading=false;
    try {
      auto clip=loader.get();
      if(clip) {
        if(g_mmd.session.active||MmdSquadBusy())throw std::runtime_error("Stop playback before loading an edited file");
        if(clip->cameraOnly) {
          g_mmd.editedCamera=clip;g_mmd.editedCameraFile=loadedFile;
          g_mmd.cameraSettings.enabled=true;status=u8"镜头已载入，使用 MMD 时间轴播放";
        } else {
          // Legacy combined files retain expression tracks. Camera is now a
          // separately selected source and cannot replace the chosen lens here.
          g_mmd.editedBody=clip;g_mmd.editedBodyFile=loadedFile;g_mmd.editedFaceOverrides.clear();
          g_mmd.clip.bones.clear();
          if(clip->hasFaces) {
            g_mmd.clip.morphs.clear();
            for(const auto &face:clip->frames.front().faces)g_mmd.clip.morphs[face.first]={{0,face.second}};
            MmdMapMorphs();
          }
          mmd::Recount(g_mmd.clip);
          if(g_mmdSquadBridge.selectSingle)g_mmdSquadBridge.selectSingle();
          status=u8"动作与表情已载入，共用 MMD 播放、映射、动作校准和物理设置";
        }
        g_mmd.timeline.stop();MmdUpdateDuration();g_mmd.status=status;
      }
    } catch(const std::exception &e){status=e.what();}
  }
  if(localCommand.exchange(0)==3)End();
  for(int i=0;i<2;++i) {
    std::shared_ptr<Command> cmd;
    {std::lock_guard<std::mutex> lock(queueMutex);if(queue.empty())break;cmd=queue.front();queue.pop_front();}
    if(cmd->cancelled)continue;
    try {cmd->result.set_value(Execute(cmd->path,cmd->body));}
    catch(const std::exception &e){if(cmd->path=="begin")End();cmd->result.set_value(Error(e.what()));}
  }
}
static void OpenEditedMotion(bool camera=false) {
  if(loading||RuntimeClosing())return;loading=true;closing=false;HWND owner=g_gameHwnd;
  loader=std::async(std::launch::async,[owner,camera]() -> std::shared_ptr<blender_bridge::Clip> {
    wchar_t path[32768]={};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;
    dialog.lpstrFilter=camera?L"Blender 镜头 (*.epcamera)\0*.epcamera\0":L"Blender 骨架动作 (*.epmotion)\0*.epmotion\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER|OFN_ENABLEHOOK;
    dialog.lpfnHook=[](HWND window,UINT message,WPARAM,LPARAM)->UINT_PTR {
      if(message==WM_INITDIALOG){HWND parent=GetParent(window);fileDialog=parent;if(closing)PostMessageW(parent,WM_CLOSE,0,0);}
      return 0;
    };
    BOOL chosen=GetOpenFileNameW(&dialog);fileDialog=nullptr;
    if(!chosen||closing)return {};
    if(std::filesystem::file_size(path)>256*1024*1024)throw std::runtime_error("Edited motion file is too large");
    std::ifstream stream{std::filesystem::path(path)};Json data;stream>>data;
    auto clip=std::make_shared<blender_bridge::Clip>(blender_bridge::Clip::Parse(data));
    if(clip->cameraOnly!=camera)throw std::runtime_error("Select the matching motion/camera file type");
    loadedFile=mmd::Utf8(path);return clip;
  });
}
static void Shutdown() {
  closing=true;
  if(HWND dialog=fileDialog.load())PostMessageW(dialog,WM_CLOSE,0,0);
  {std::lock_guard<std::mutex> lock(queueMutex);
    for(auto &cmd:queue){cmd->cancelled=true;cmd->result.set_value(Error("Plugin is closing"));}queue.clear();}
  if(loading&&loader.valid()){loader.wait();loading=false;}
}
static Json Request(const std::string &path,const Json &j) {
  if(RuntimeClosing()||closing)return Error("Game is closing");
  auto cmd=std::make_shared<Command>();cmd->path=path;cmd->body=j;auto done=cmd->result.get_future();
  {std::lock_guard<std::mutex> lock(queueMutex);if(queue.size()>=8)return {{"ok",false},{"retryable",true},{"error","Blender request queue is full"}};queue.push_back(cmd);}
  if(done.wait_for(std::chrono::seconds(2))!=std::future_status::ready) {cmd->cancelled=true;return {{"ok",false},{"retryable",true},{"error","Game update timed out; enter a playable scene"}};}
  return done.get();
}
}
