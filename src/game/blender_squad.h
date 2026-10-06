#pragma once
// Included inside poser_blender; all access runs on the existing game thread.
struct SquadEditActor {
  Json schema;
  std::vector<bool> editable;
  std::set<std::string> faces;
  std::vector<MmdSavedTransform> connected;
  SMCMotionFrame face;
};
static std::array<SquadEditActor,4> squadEdit;
static blender_bridge::Frame squadCamera;
static bool SquadLive() {
  if(!g_blenderEditing||!g_squad.active||!g_squad.editing||g_squad.cameraSession!=session)return false;
  auto roster=poser_squad::Read();
  if(!roster.valid||!g_squad.identity.matches(MmdSquadIdentity(roster)))return false;
  for(const auto &a:g_squad.actors)if(a) {
    if(!UnityObjAlive(a->saved.animator)||!UnityObjAlive(a->saved.root))return false;
    for(const auto &b:a->bones)if(!UnityObjAlive(b.transform))return false;
  }
  return true;
}
static void SquadEnd() {
  const bool resume=adoptedMotion&&SquadLive()&&!RuntimeClosing();
  if(resume) {
    for(int n=0;n<4;++n)if(g_squad.actors[n]) {
      auto &a=*g_squad.actors[n];auto &edit=squadEdit[n];
      for(size_t i=0;i<edit.connected.size();++i) {
        auto b=edit.connected[i];b.scaleOwned=i<a.saved.transforms.size()&&a.saved.transforms[i].scaleOwned;
        MmdRestoreTransform(b);
      }
      if(a.member.animator==g_charAnimator)CapturePoseSnapshot();
      {SMCActorScope scope(a.face.get());SMCMotionPublish(edit.face);}
      // Native preview keeps its own profile alive. Rebind the original VMD on
      // its next sample, inside the player's guarded update, never in restoration.
      MmdApplyVisibility(a.saved,g_squad.slots[n].clip,savedTimeline.seconds*30);
      ++a.saved.terrain.epoch;
    }
    g_squad.editing=false;g_squad.previewReady=false;g_blenderEditing=false;
    g_squad.timeline=savedTimeline;g_squad.timeline.lastNow=MmdNow();
    mmd_camera::Publish(connectedCamera);MmdSquadSyncAudio();
    if(restoreFixed&&g_charAnimator==fixedActor&&s_bonesRev==fixedRevision&&UnityObjAlive(fixedActor))
      mmd_camera::SetFixed(true,fixedActor);
    restoreFixed=false;fixedReferences.reset();
  } else MmdSquadStop();
  g_blenderEditing=false;squadSession=false;adoptedMotion=false;hasFrame=false;
  squadEdit={};mmd_camera::observe=false;
  status=u8"Blender 小队已断开，各队员已恢复";
}
static Json SquadScene() {
  Json members=Json::array();for(int n=0;n<4;++n)if(g_squad.actors[n])members.push_back(squadEdit[n].schema);
  Json camera=nullptr;
  if(auto observed=std::atomic_load(&mmd_camera::observed))camera=CameraJson(*observed,g_squad.anchor.origin);
  Json cuts=Json::array();
  if(g_mmd.editedCamera) {
    for(const auto &f:g_mmd.editedCamera->frames)if(f.camera.cut)cuts.push_back(f.time+g_mmd.cameraSettings.timeOffset);
  } else {
    const auto &keys=MmdCameraKeys();const auto &settings=g_mmd.cameraSettings;
    for(size_t i=1;i<keys.size();++i)if((settings.cutMode==mmd::CameraCutMode::Adjacent&&keys[i].frame-keys[i-1].frame==1)||
        (settings.cutMode==mmd::CameraCutMode::Manual&&std::find(settings.cutFrames.begin(),settings.cutFrames.end(),keys[i].frame)!=settings.cutFrames.end()))
      cuts.push_back(keys[i].frame/30.+settings.timeOffset);
  }
  for(auto &member:members){member["camera"]=camera;member["initial"]["camera"]=camera;member["camera_cuts"]=cuts;}
  return {{"ok",true},{"protocol",blender_bridge::Protocol},{"mode","squad"},{"session",session},
    {"members",members},{"camera",camera},{"camera_cuts",cuts},{"anchor_rotation",Q(g_squad.anchor.basis)},
    {"duration",g_squad.timeline.duration},{"time",savedTimeline.seconds},{"fps",30}};
}
static Json SquadBegin() {
  if(g_blenderEditing||g_squad.loading||g_squad.pending.active||g_mmd.session.active||g_mmd.loading||g_mmd.preview)
    return Error(u8"请结束单人播放、校准或当前导入，再连接小队");
  adoptedMotion=g_squad.active;savedTimeline=g_squad.timeline;connectedCamera=mmd_camera::request;
  if(!g_squad.active&&!MmdSquadStart(true)) {
    MmdSquadCancelStart();g_squad.editing=false;
    return {{"ok",false},{"retryable",true},{"error",g_squad.status}};
  }
  g_squad.editing=true;g_squad.previewReady=false;g_squad.timeline.state=mmd::PlayState::Paused;
  g_squad.timeline.lastNow=MmdNow();MmdSquadCancelStart();MmdSquadSyncAudio();
  squadSession=true;g_blenderEditing=true;session=g_squad.cameraSession;sequence=0;hasFrame=false;touched=MmdNow();
  restoreFixed=mmd_camera::fixedEnabled.load();fixedActor=g_charAnimator;fixedRevision=s_bonesRev;
  fixedReferences=restoreFixed?g_squad.cameraReferences:nullptr;
  mmd_camera::observe=true;squadEdit={};
  for(int n=0;n<4;++n)if(g_squad.actors[n]) {
    auto &a=*g_squad.actors[n];auto &edit=squadEdit[n];
    Json bones=Json::array(),faces=Json::array(),initialBones=Json::array(),initialFaces=Json::object();
    auto shift=mmd::TRS(GetBoneWorldPos(a.saved.root)*-1.f,{});
    edit.editable.resize(a.bones.size(),false);
    for(size_t i=0;i<a.bones.size();++i) {
      const auto &b=a.bones[i];mmd::Matrix world;
      if(!MmdReadMatrix(g_transform_get_localToWorldMatrix,b.transform,&world))throw std::runtime_error("Cannot read squad skeleton transform");
      int role=a.profile.bones[i].role;
      edit.editable[i]=(role>=0||(i<a.mapper.output.write.size()&&a.mapper.output.write[i]))&&role!=Jaw&&b.transform!=a.saved.root;
      auto p=GetBoneLocalPos(b.transform);auto q=GetBoneLocalRot(b.transform);auto scale=MmdLocalScale(b.transform);
      bones.push_back({{"i",i},{"name",b.name},{"parent",b.parentIdx},{"role",role},{"editable",edit.editable[i]},
        {"p",V(p)},{"q",Q(q)},{"scale",V(scale)},{"world",Matrix(shift*world)}});
      if(edit.editable[i])initialBones.push_back({{"i",i},{"p",V(p)},{"q",Q(q)},{"s",V(scale)}});
      edit.connected.push_back({b.transform,p,q,scale});
    }
    edit.connected.push_back({a.saved.root,GetBoneLocalPos(a.saved.root),GetBoneLocalRot(a.saved.root)});
    {SMCActorScope scope(a.face.get());AcquireSRWLockShared(&s_motionFaceLock);edit.face=s_motionFaceMailbox;ReleaseSRWLockShared(&s_motionFaceLock);}
    auto catalog=mmd_face_controls::Build(a.faceProfile.get(),SMCManualCatalog());
    for(const auto &track:g_squad.slots[n].clip.morphs)
      if(std::none_of(catalog.begin(),catalog.end(),[&](const auto &c){return c.name==track.first;}))catalog.push_back({track.first,-1,-1,4});
    for(size_t i=0;i<catalog.size();++i) {const auto &c=catalog[i];edit.faces.insert(c.name);
      faces.push_back({{"name",c.name},{"label",mmd_face_controls::Label(c.name,c.panel,int(i))},{"panel",c.panel}});initialFaces[c.name]=0.f;}
    if(adoptedMotion) {
      const auto &slot=g_squad.slots[n];auto sampled=slot.edited?slot.edited->sample(savedTimeline.seconds):blender_bridge::Frame{};
      for(const auto &f:slot.clip.morphs)initialFaces[f.first]=slot.edited&&slot.edited->hasFaces&&!slot.faceOverrides.count(f.first)?sampled.faces.at(f.first):mmd::SampleMorph(f.second,savedTimeline.seconds*30);
    } else if(a.member.animator==g_charAnimator&&s_mmdFaceMode&&s_manualFace.applied)
      for(size_t i=0;i<s_manualFace.controls.size();++i)if(edit.faces.count(s_manualFace.controls[i].name))initialFaces[s_manualFace.controls[i].name]=s_manualFace.weights[i];
    const auto rootQ=Q(GetBoneWorldRot(a.saved.root));
    Json initial={{"time",adoptedMotion?savedTimeline.seconds:0},{"root",V(GetBoneWorldPos(a.saved.root)-g_squad.anchor.origin)},
      {"root_rotation",rootQ},{"bones",initialBones},{"faces",initialFaces},{"visible",!a.saved.visibility.lease},{"camera",nullptr}};
    edit.schema={{"ok",true},{"protocol",blender_bridge::Protocol},{"slot",n},{"session",session},{"model",a.profile.model},
      {"bones",bones},{"faces",faces},{"initial",initial},{"root_rotation",rootQ},{"anchor_rotation",Q(g_squad.anchor.basis)},
      {"bone_scale",bool(g_transform_get_localScale&&g_transform_set_localScale)},{"duration",g_squad.timeline.duration},{"fps",30},{"has_motion",g_squad.slots[n].content()}};
  }
  status=u8"Blender 已连接小队；共用时间轴，按队员独立编辑";return SquadScene();
}
static Json SquadSample(double seconds) {
  auto &s=g_squad;Json members=Json::array();std::array<Vec3,4> offsets{};
  // Sampling never writes game objects, even after preview frames were sent.
  bool ready=s.previewReady;s.previewReady=false;
  try {
    for(int n=0;n<4;++n)if(s.actors[n]) {
      auto &a=*s.actors[n];auto &edit=squadEdit[n];Json bones=Json::array(),faces=Json::object();
      MmdSquadSampleActor(n,seconds*30);auto placement=MmdSquadPlacement(n);
      bool body=s.slots[n].body();offsets[n]=body?placement.position-s.anchor.origin:blender_bridge::Vector(edit.schema["initial"]["root"]);
      for(size_t i=0;i<a.bones.size();++i)if(edit.editable[i]) {
        auto b=edit.connected[i];
        if(body&&i<a.mapper.output.write.size()&&a.mapper.output.write[i]) {
          b.pos=(s.slots[n].edited?a.playbackProfile:a.profile).bones[i].localPos;b.rot=a.mapper.output.localRot[i];
          if(s.slots[n].edited)b.scale=a.playbackProfile.bones[i].localScale;
        }
        bones.push_back({{"i",i},{"p",V(b.pos)},{"q",Q(b.rot)},{"s",V(b.scale)}});
      }
      for(const auto &f:edit.faces)faces[f]=0.f;
      const auto &slot=s.slots[n];
      for(const auto &f:slot.clip.morphs)faces[f.first]=slot.edited&&slot.edited->hasFaces&&!slot.faceOverrides.count(f.first)?a.sample.faces.at(f.first):mmd::SampleMorph(f.second,seconds*30);
      members.push_back({{"slot",n},{"time",seconds},{"root",V(offsets[n])},{"root_rotation",body?Q(placement.rotation):edit.schema["root_rotation"]},
        {"bones",bones},{"faces",faces},{"visible",mmd::SampleVisibility(slot.clip,seconds*30)},{"camera",nullptr}});
    }
  } catch(...) {s.previewReady=ready;throw;}
  s.previewReady=ready;
  Json camera=nullptr;const auto &settings=g_mmd.cameraSettings;
  Vec3 delta=s.cameraFollow>=0&&s.cameraFollow<4?offsets[s.cameraFollow]:Vec3{};
  if(g_mmd.editedCamera)camera=CameraJson(blender_bridge::PlaceCamera(g_mmd.editedCamera->sample(seconds-settings.timeOffset),settings,s.anchor.origin,s.anchor.basis,delta),s.anchor.origin);
  else if(!MmdCameraKeys().empty())camera=CameraJson(mmd::PlaceCamera(mmd::SampleCamera(MmdCameraKeys(),mmd::CameraFrame(seconds,settings),settings),settings,s.anchor.origin,s.anchor.basis,delta,s.scale,s.cameraHeight,mmd::CameraSourceHeight(s.rig),{}),s.anchor.origin);
  return {{"time",seconds},{"members",members},{"camera",camera}};
}
static Json SquadFrame(const Json &j) {
  auto &s=g_squad;const auto seq=blender_bridge::Id(j.at("sequence"));
  if(seq<=sequence)return Error("Stale preview frame");
  const auto &members=j.at("members");
  if(!members.is_array()||members.empty()||members.size()>4)return Error("Invalid squad frame");
  std::array<blender_bridge::Frame,4> frames;std::array<bool,4> seen{};
  for(const auto &member:members) {
    auto slotNumber=blender_bridge::Number(member.at("slot"),0,3);
    if(slotNumber!=std::floor(slotNumber))return Error("Squad slot must be an integer");
    int slot=int(slotNumber);
    if(seen[slot]||!s.actors[slot])return Error("Duplicate or unknown squad slot");seen[slot]=true;
    auto wire=member;wire["session"]=session;wire["sequence"]=seq;wire["time"]=j.at("time");wire.erase("camera");
    auto f=blender_bridge::ParseFrame(wire,squadEdit[slot].editable.size());
    for(const auto &b:f.bones) {
      if(!squadEdit[slot].editable[b.index])return Error("Bone is not an editable body joint");
      if(b.hasScale&&(!g_transform_get_localScale||!g_transform_set_localScale))return Error("Bone scale API unavailable");
    }
    for(const auto &face:f.faces)if(!squadEdit[slot].faces.count(face.first))return Error("Unknown face control");
    frames[slot]=std::move(f);
  }
  for(int n=0;n<4;++n)if(bool(s.actors[n])!=seen[n])return Error("Squad frame must contain every connected member");
  Json view=j;view["root"]={0,0,0};view["bones"]=Json::array();view["faces"]=Json::object();
  auto camera=blender_bridge::ParseFrame(view,0);bool playing=j.value("playing",false);
  if(camera.time<s.timeline.seconds||camera.time-s.timeline.seconds>.2)for(auto &a:s.actors)if(a)++a->saved.terrain.epoch;
  s.previewFrames=std::move(frames);squadCamera=camera;s.timeline.seconds=camera.time;s.previewPlaying=playing;
  sequence=seq;s.previewReady=true;hasFrame=true;return {{"ok",true},{"sequence",sequence}};
}
static bool SquadTick() {
  if(!SquadLive()||MmdNow()-touched>8) {SquadEnd();return false;}
  if(!ClothOnMainThread())return true;
  if(g_squad.previewReady) {
    MmdSquadApply();if(!g_squad.active)return false;
    if(g_squad.timeline.clockHeld)return true;
    const auto &f=squadCamera;
    if(f.camera.active) {
      auto pose=blender_bridge::PlaceCamera(f,mmd::CameraSettings{},g_squad.anchor.origin,g_squad.anchor.basis,{});
      mmd_camera::SetFixed(false);mmd_camera::Publish({true,session,g_squad.cameraOwner,pose,nullptr,0,f.time*30});
    } else mmd_camera::Stop();
  }
  return true;
}
