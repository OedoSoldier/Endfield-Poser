#pragma once
#include "math/cloth_turn_motion.h"
#include "math/cloth_attachment.h"
#include <chrono>

static bool s_clothTurnEnabled=true;
static float s_clothTurnStrength=1;
static float s_clothHairStrength=1;
static float s_clothLightness=0;
static float s_clothRibbonStrength=1,s_clothBeltStrength=1,s_clothAccessoryStrength=1;
static bool ClothTurnAnyGain(){return s_clothTurnStrength>0||s_clothHairStrength>0||s_clothRibbonStrength>0||
    s_clothBeltStrength>0||s_clothAccessoryStrength>0||s_clothLightness>0;}
struct ClothTurnState {
  uint64_t session=0,submissions=0,enhancedSubmissions=0,shapeRevisions=0;
  cloth_turn::Tracker body,head,hips;
  cloth_turn::Mailbox mailbox;
  void *driverBones[55]{};cloth_turn::Pose driverPoses[55]{};bool driverRead[55]{},driverValid[55]{};
  struct ShapeEntry {
    eiem_cloth::ObjectId id{};cloth_turn::Shape shape;
    uintptr_t process=0;unsigned command=0;int team=0;bool enhanced=false;
    double nextReadback=0;int lastReadback=0;
    cloth_turn::Part part=cloth_turn::Part::None;
    cloth_turn::Attachment attachment;cloth_turn::Tracker tracker;cloth_turn::Mailbox motion;
    ClothRef mounts[8]{};
  };
  ShapeEntry shapes[ClothCapacity+eiem_cloth_rebuild::BatchCapacity]{};
  unsigned shapeCount=0,clothing=0,hair=0,tail=0,ears=0,accessories=0,shaped=0,enhancedCount=0;
  float peakForce=0,peakVerticalForce=0;
  double nextReadback=0;
  uint64_t readbacks=0,readbackFailures=0;
  const char *nativeStatus=u8"等待物理受力核对";
  bool fault=false;
  const char *status=u8"开始播放后随身体运动";
};
static ClothActorBank<ClothTurnState> s_clothTurnActors;
#define s_clothTurn (s_clothTurnActors.Get())
static double ClothTurnNow() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
// Only motion derivatives/mailbox expiry follow the recording clock. Resource
// discovery and restore deadlines continue to use real time.
static double (*s_clothTurnMotionClock)()=nullptr;
static double ClothTurnMotionNow(){return s_clothTurnMotionClock?s_clothTurnMotionClock():ClothTurnNow();}
static void ClothTurnDropShape(ClothTurnState::ShapeEntry &entry) {
  for(auto &mount:entry.mounts)ClothFree(mount);
  entry={};
}
static void ClothTurnClear() {
  for(int n=0;n<s_cloth.count;++n)ClothRestoreRibbonResponse(s_cloth.instances[n]);
  for(auto &entry:s_clothTurn.shapes)ClothTurnDropShape(entry);
  s_clothTurn={};
}
static bool ClothTurnPose(void *t,cloth_turn::Pose &out) {
  Vector3 p{};Quaternion q{};
  if(!t||!UnityObjAlive(t)||!ClothValue(g_transform_get_position,t,p)||!ClothValue(g_transform_get_rotation,t,q))return false;
  out={{p.x,p.y,p.z},{q.x,q.y,q.z,q.w}};return cloth_turn::Finite(out.position)&&cloth_turn::Finite(out.rotation);
}
static bool ClothTurnDriver(int role,cloth_turn::Pose &out) {
  auto &s=s_clothTurn;if(role<0||role>=55)return false;
  if(!s.driverRead[role]) {s.driverRead[role]=true;s.driverValid[role]=ClothTurnPose(s.driverBones[role],s.driverPoses[role]);}
  out=s.driverPoses[role];return s.driverValid[role];
}
static bool ClothTurnMountPose(const ClothTurnState::ShapeEntry &entry,cloth_turn::Pose &out) {
  return entry.attachment.pose([&](int role,cloth_turn::Pose &p) {
    if(role<55)return ClothTurnDriver(role,p);
    const int index=role-55;
    return index>=0&&index<8&&ClothTurnPose(ClothTarget(entry.mounts[index]),p);
  },out);
}
static void ClothTurnAttachment(ClothTurnState::ShapeEntry &entry,const ClothInstance *source,const ClothBoneRuntime *enhanced) {
  std::vector<void*> roots;
  if(enhanced) {
    const auto &candidate=ClothBoneCandidate(*enhanced);
    if(candidate.rootCount<1||candidate.rootCount>128)return;
    for(int k=0;k<candidate.rootCount;++k) {
      int index=candidate.roots[k];if(index<0||size_t(index)>=enhanced->bones.size())return;
      auto t=ClothTarget(enhanced->bones[index].bone);if(!t)return;roots.push_back(t);
    }
  } else if(source) {
    auto bbc=ClothTarget(source->ref);void *data=nullptr,*list=nullptr;
    if(!bbc||!ClothInvoke(source->api.serialize,bbc,nullptr,data)||!ClothField(data,"rootBones","System.Collections.Generic.List<UnityEngine.Transform>",list))return;
    int count=CollisionCount(list);if(count<1||count>128)return;
    for(int k=0;k<count;++k)if(auto t=CollisionItem(list,k,"UnityEngine.Transform"))roots.push_back(t);
  }
  cloth_turn::Attachment attachment;
  const unsigned count=(std::min)(unsigned(roots.size()),8u);
  for(unsigned n=0;n<count;++n) {
    auto root=roots[(2*n+1)*roots.size()/(2*count)],parent=CollisionParent(root);int role=-1;
    const auto mount=parent;
    // A root hanging from another component follows that completed parent
    // pose. Never feed a descendant of this component back into its force.
    bool ownOutput=false;
    for(auto other:roots){auto t=mount;for(int depth=0;t&&depth<64;++depth) {
      if(t==other){ownOutput=true;break;}
      auto next=CollisionParent(t);if(next==t)break;t=next;
    }}
    if(ownOutput)continue;
    for(int depth=0;parent&&depth<64;++depth) {
      for(int i=0;i<55;++i)if(s_clothTurn.driverBones[i]==parent){role=i;break;}
      if(role>=0)break;auto next=CollisionParent(parent);if(next==parent)break;parent=next;
    }
    cloth_turn::Pose body,point;
    if(!ClothTurnPose(root,point))continue;
    // Direct parents retain intermediate cloth/strap motion. Detached helper
    // rigs are also valid when they belong to the same actor hierarchy.
    if(role>=0&&mount==s_clothTurn.driverBones[role]&&ClothTurnDriver(role,body)) {
      attachment.add(role,body,point); // Shared per-frame humanoid pose cache.
    } else if(mount&&ClothUnderAnimator(mount,ClothTarget(s_cloth.animator))&&ClothTurnPose(mount,body)) {
      const unsigned index=attachment.count;
      auto ref=ClothProtect(mount);
      if(ref.handle&&attachment.add(55+int(index),body,point))entry.mounts[index]=ref;
      else ClothFree(ref);
    } else if(role>=0&&ClothTurnDriver(role,body))attachment.add(role,body,point);
  }
  entry.attachment=attachment;
}
static ClothTurnState::ShapeEntry *ClothTurnShape(const ClothRef &ref,cloth_turn::Part part,
    void *process,const ClothInstance *source,const ClothBoneRuntime *enhanced,unsigned &budget) {
  auto &s=s_clothTurn;
  unsigned index=s.shapeCount;
  for(unsigned i=0;i<s.shapeCount;++i)if(s.shapes[i].id==ref.id){index=i;break;}
  const auto processKey=reinterpret_cast<uintptr_t>(process);
  const unsigned command=enhanced?enhanced->command:0;const int team=enhanced?enhanced->team[1]:0;
  if(index<s.shapeCount) {
    auto &e=s.shapes[index];
    if(e.process==processKey&&e.enhanced==bool(enhanced)&&e.command==command&&e.team==team)return &e;
  }
  // The component survives a rebuild; its Process and simulated bones do not.
  // Never reuse a source cache while a replacement waits for the capture budget.
  if(!budget||index==std::size(s.shapes)||!s.mailbox.count)return nullptr;
  --budget;if(index==s.shapeCount)++s.shapeCount;
  auto &entry=s.shapes[index];ClothTurnDropShape(entry);entry.id=ref.id;entry.process=processKey;
  entry.enhanced=bool(enhanced);entry.command=command;entry.team=team;++s.shapeRevisions;
  entry.part=part;ClothTurnAttachment(entry,source,enhanced);
  auto pose=cloth_turn::Mailbox::select(s.mailbox.frames[s.mailbox.count-1],part).pose;
  if(entry.attachment.count&&!ClothTurnMountPose(entry,pose))entry.attachment={};
  auto add=[&](const ClothRef &bone) {
    auto t=ClothTarget(bone);Vector3 p{};
    if(t&&ClothValue(g_transform_get_position,t,p))entry.shape.add({p.x,p.y,p.z},pose);
  };
  // Reuse retained skeletal samples. No tree walk, new handles or mesh reads.
  if(enhanced) {
    const auto &candidate=ClothBoneCandidate(*enhanced);
    // Select across moving points, including newly added panels/tips. The
    // first eight references may all be old fixed waist/body anchors.
    int selected[ClothBoneMaxIdentities]{};unsigned count=0;
    if(candidate.bones&&candidate.boneCount>0&&candidate.boneCount<=ClothBoneMaxIdentities&&
        enhanced->bones.size()==size_t(candidate.boneCount)) {
      for(int n=0;n<candidate.boneCount;++n)
        if(candidate.CandidateAttribute(n)==2&&!candidate.Passive(n))selected[count++]=n;
      const unsigned samples=(std::min)(count,8u);
      for(unsigned n=0;n<samples;++n)add(enhanced->bones[selected[(2*n+1)*count/(2*samples)]].bone);
    }
  } else if(source) {
    // Roots plus first children miss almost the entire lever of a long strip.
    // Capture terminal points once, with bounded skeletal traversal only.
    std::vector<void*> pending;std::vector<cloth_turn::Pose> tips;
    for(int i=0;i<source->poseProbeCount;i+=2)if(auto t=ClothTarget(source->poseProbes[i].ref))pending.push_back(t);
    unsigned visited=0;
    while(!pending.empty()&&visited++<128) {
      auto t=pending.back();pending.pop_back();int children=0;
      if(!t||!UnityObjAlive(t)||!ClothValue(s_clothUnity.childCount,t,children)||children<0||children>32)continue;
      if(!children){cloth_turn::Pose tip;if(ClothTurnPose(t,tip))tips.push_back(tip);continue;}
      for(int k=0;k<children;++k){void *child=nullptr,*args[]{&k};if(ClothInvoke(s_clothUnity.child,t,args,child)&&child)pending.push_back(child);}
    }
    const unsigned samples=(std::min)(unsigned(tips.size()),8u);
    for(unsigned k=0;k<samples;++k)entry.shape.add(tips[(2*k+1)*tips.size()/(2*samples)].position,pose);
    if(!entry.shape.count)for(int i=0;i<source->poseProbeCount;++i)add(source->poseProbes[i].ref);
  }
  Log("[CLOTH-TURN-SHAPE] actorSlot=%u session=%llu instance=%d points=%u part=%d route=%s process=%p team=%d command=%u meshReads=0",
      s_clothActorIndex,(unsigned long long)s.session,ref.id.instance,entry.shape.count,int(part),
      enhanced?"enhanced-moving-points":"original-chain-tips",process,team,command);
  Log("[CLOTH-ATTACHMENT] component=%s anchors=%u driverRoles=%d,%d policy=external-mount-parent no-self-output-feedback=1",
      source?source->name:"enhanced",entry.attachment.count,entry.attachment.count?entry.attachment.roots[0].role:-1,
      entry.attachment.count>1?entry.attachment.roots[1].role:-1);
  return &entry;
}
static bool ClothTurnNeeded() {
  return s_clothTurnEnabled&&ClothTurnAnyGain()&&!s_clothTurn.fault&&s_cloth.active&&!s_cloth.releasing&&
      s_clothRequested&&s_clothTurn.session==s_cloth.owner.session&&s_clothTurn.mailbox.count;
}
struct ClothTurnApi {void *force=nullptr,*center=nullptr,*valid=nullptr,*process=nullptr;int mode=0,enhancedMode=0;};
static ClothTurnApi ClothTurnResolve(void *cls) {
  static std::map<void*,ClothTurnApi> cache;
  auto it=cache.find(cls);if(it!=cache.end())return it->second;
  ClothTurnApi api;
  api.center=ClothMethod(cls,"GetCenterPosition","UnityEngine.Vector3");
  api.valid=ClothMethod(cls,"IsValid","System.Boolean");
  api.process=ClothMethod(cls,"get_Process","BeyondDynamicBone.ClothProcess");
  if(s_clothMethodFlags&&il2cpp_class_from_type&&il2cpp_class_value_size) {
    void *iterator=nullptr;
    for(void *m=il2cpp_class_get_methods(cls,&iterator);m;m=il2cpp_class_get_methods(cls,&iterator)) {
      uint32_t flags=0;
      if(strcmp(il2cpp_method_get_name(m),"AddForce")||il2cpp_method_get_param_count(m)!=3||
         (s_clothMethodFlags(m,&flags)&0x10)||!ClothTypeIs(il2cpp_method_get_return_type(m),"System.Void")||
         !ClothTypeIs(il2cpp_method_get_param(m,0),"UnityEngine.Vector3")||
         !ClothTypeIs(il2cpp_method_get_param(m,1),"System.Single")||
         !ClothTypeIs(il2cpp_method_get_param(m,2),"BeyondDynamicBone.ClothForceMode"))continue;
      auto mode=il2cpp_class_from_type(il2cpp_method_get_param(m,2));uint32_t align=0;
      if(mode&&il2cpp_class_value_size(mode,&align)==4&&CollisionEnumValue(mode,"VelocityAdd",api.mode)) {
        api.force=m;CollisionEnumValue(mode,"VelocityAddWithoutDepth",api.enhancedMode);
      }
    }
  }
  Log("[CLOTH-TURN-ABI] class=%s addForce=%p center=%p valid=%p process=%p depthWeightedMode=%d enhancedMode=%d nativeInput=acceleration",
      il2cpp_class_get_name(cls),api.force,api.center,api.valid,api.process,api.mode,api.enhancedMode);
  cache.emplace(cls,api);return api;
}
static void ClothTurnReadback(const ClothRef &ref,void *process,int expectedMode,Vec3 expected,double now) {
  // At the completed-jobs boundary only. Inspect one Team snapshot, never
  // meshes/particle arrays; at most four probes per second per actor and one
  // per component every 30 seconds. Log only the first result or a change.
  auto &s=s_clothTurn;
  if(!s_clothInputManagerClass||now<s.nextReadback)return;
  ClothTurnState::ShapeEntry *entry=nullptr;
  for(unsigned n=0;n<s.shapeCount;++n)if(s.shapes[n].id==ref.id){entry=&s.shapes[n];break;}
  if(!entry||entry->process!=uintptr_t(process)||now<entry->nextReadback)return;
  entry->nextReadback=now+30;s.nextReadback=now+.25;
  int team=0,mode=0;Vec3 force{};void *box=nullptr;
  const bool known=ClothValue(ClothMethod(il2cpp_object_get_class(process),"get_TeamId","System.Int32"),process,team)&&
      ClothBoneContactTeam(team,process,box)&&
      ClothInputTeamField(box,"forceMode","BeyondDynamicBone.ClothForceMode",mode)&&
      ClothInputTeamField(box,"impactForce","Unity.Mathematics.float3",force)&&cloth_turn::Finite(force);
  const bool matched=known&&mode==expectedMode&&Len(force-expected)<.0001f;
  if(matched)++s.readbacks;else ++s.readbackFailures;
  s.nativeStatus=matched?u8"已核对：摆动力已写入物理系统":known?u8"摆动力写入结果不符，已记录日志":u8"受力已提交，暂无法核对物理读回";
  int result=matched?1:known?2:3;
  if(entry->lastReadback==result)return;
  entry->lastReadback=result;
  float weight=NAN,blend=NAN,ratio=NAN,scale=NAN;bool running=false,culled=false,skip=false;
  if(box) {
    ClothInputTeamField(box,"clothSimulateWeight","System.Single",weight);
    ClothInputTeamField(box,"blendWeight","System.Single",blend);
    ClothInputTeamField(box,"animationPoseRatio","System.Single",ratio);
    ClothInputTeamField(box,"scaleRatio","System.Single",scale);
    auto cls=il2cpp_object_get_class(box);
    ClothValue(ClothMethod(cls,"get_IsRunning","System.Boolean"),(char*)box+16,running);
    ClothValue(ClothMethod(cls,"get_IsCullingInvisible","System.Boolean"),(char*)box+16,culled);
  }
  ClothValue(ClothMethod(il2cpp_object_get_class(process),"IsSkipWriting","System.Boolean"),process,skip);
  Log("[CLOTH-TURN-NATIVE] actorSlot=%u session=%llu instance=%d enhanced=%d team=%d process=%p known=%d matched=%d mode=%d expectedMode=%d force=(%g,%g,%g) expected=(%g,%g,%g) running=%d culled=%d skipWriting=%d weight=%g blend=%g animationRatio=%g scale=%g unit=acceleration meshReads=0 visualVerified=0",
      s_clothActorIndex,(unsigned long long)s.session,ref.id.instance,int(entry->enhanced),team,process,int(known),int(matched),mode,expectedMode,
      force.x,force.y,force.z,expected.x,expected.y,expected.z,int(running),int(culled),int(skip),weight,blend,ratio,scale);
}
// Apply at the existing audited boundary, after prior jobs finish and before
// TeamManager consumes external forces. No job buffer or mesh access.
static bool ClothTurnForce(void *obj,const ClothRef &ref,cloth_turn::Part part,double now,
    const ClothInstance *source,const ClothBoneRuntime *enhanced,unsigned &budget) {
  const float strength=part==cloth_turn::Part::Hair?s_clothHairStrength:
      part==cloth_turn::Part::Ribbon?s_clothRibbonStrength:part==cloth_turn::Part::Belt?s_clothBeltStrength:
      part==cloth_turn::Part::Accessory?s_clothAccessoryStrength:s_clothTurnStrength;
  if(source&&!enhanced&&part==cloth_turn::Part::Ribbon)for(int n=0;n<s_cloth.count;++n)
    if(s_cloth.instances[n].ref.id==ref.id&&!ClothRibbonResponse(s_cloth.instances[n],strength>0))return false;
  if((part==cloth_turn::Part::Hair||cloth_turn::AttachmentPart(part))&&(!std::isfinite(strength)||strength<=0))return false;
  auto api=ClothTurnResolve(il2cpp_object_get_class(obj));
  if(!api.force||!api.center||!api.valid||!api.process||(enhanced&&!api.enhancedMode)) {s_clothTurn.status=u8"当前 BBC 不支持运动受力接口";return false;}
  bool enabled=false,valid=false;Vector3 center{};void *process=nullptr;
  if(!ClothValue(s_clothUnity.getEnabled,obj,enabled)||!enabled||
     !ClothValue(api.valid,obj,valid)||!valid||!ClothInvoke(api.process,obj,nullptr,process)||!process||
     (enhanced&&process!=CollisionGc(enhanced->process[1]))||!ClothValue(api.center,obj,center))return false;
  auto entry=ClothTurnShape(ref,part,process,source,enhanced,budget);
  if(!entry)return false;
  // An unresolved attachment retains native physics, not an invented chest driver.
  if(cloth_turn::AttachmentPart(part)&&!entry->attachment.count)return false;
  const auto &mailbox=entry->attachment.count?entry->motion:s_clothTurn.mailbox;
  // BBC adds impactForce to gravity/wind, then multiplies by its own fixed
  // simulationDeltaTime. Native API's historical velocity label is misleading
  // for this game build. Supply acceleration, not our sample's delta velocity.
  Vec3 force=mailbox.force(part,{center.x,center.y,center.z},strength,now,s_clothLightness,&entry->shape);
  const float magnitude=Len(force);
  if(!std::isfinite(magnitude))return false;
  // Zero motion must not erase native wind or another system's force.
  if(magnitude>1e-5f) {
    Vector3 direction{force.x/magnitude,force.y/magnitude,force.z/magnitude};float amount=magnitude;
    // Generated chains and short native attachment chains attenuate the
    // depth-weighted mode differently. Apply the
    // bounded supplemental force uniformly to moving points; fixed particles
    // are still excluded by the native solver, all constraints remain active.
    int mode=(enhanced||cloth_turn::AttachmentPart(part))&&api.enhancedMode?api.enhancedMode:api.mode;
    void *args[]{&direction,&amount,&mode},*unused=nullptr;
    if(!ClothInvoke(api.force,obj,args,unused)) {
      s_clothTurn.fault=true;s_clothTurn.status=u8"运动受力调用失败，停止重播后可重试";return false;
    }
    ++s_clothTurn.submissions;if(enhanced)++s_clothTurn.enhancedSubmissions;
    s_clothTurn.peakForce=(std::max)(s_clothTurn.peakForce,magnitude);
    s_clothTurn.peakVerticalForce=(std::max)(s_clothTurn.peakVerticalForce,std::fabs(force.y));
    ClothTurnReadback(ref,process,mode,force,now);
  }
  if(enhanced)++s_clothTurn.enhancedCount;
  if(entry->shape.count)++s_clothTurn.shaped;
  switch(part) {
    case cloth_turn::Part::Hair:++s_clothTurn.hair;break;
    case cloth_turn::Part::Tail:++s_clothTurn.tail;break;
    case cloth_turn::Part::Ear:++s_clothTurn.ears;break;
    case cloth_turn::Part::Accessory:case cloth_turn::Part::Ribbon:case cloth_turn::Part::Belt:++s_clothTurn.accessories;break;
    default:++s_clothTurn.clothing;break;
  }
  return true;
}
static void ClothTurnBoundary() {
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||!ClothTurnNeeded())return;
  if(!ClothOwns(s_cloth.owner)) {ClothTurnClear();return;}
  auto &s=s_clothTurn;s.clothing=s.hair=s.tail=s.ears=s.accessories=s.shaped=s.enhancedCount=0;s.status=u8"当前没有可驱动的衣物、头发等部件";
  const double now=ClothTurnMotionNow();
  void *seen[ClothCapacity+eiem_cloth_rebuild::BatchCapacity]{};unsigned count=0,budget=1;
  auto submit=[&](const ClothRef &ref,const char *name,const ClothInstance *source,const ClothBoneRuntime *enhanced) {
    if(s.fault)return;
    auto part=cloth_turn::Classify(name);if(part==cloth_turn::Part::None)return;
    void *obj=ClothTarget(ref);if(!obj)return;
    for(unsigned n=0;n<count;++n)if(seen[n]==obj)return;
    if(count==std::size(seen))return;
    seen[count++]=obj;ClothTurnForce(obj,ref,part,now,source,enhanced,budget);
  };
  // Enhancement replaces the Process on the SAME BBC. Its current graph must
  // win deduplication; otherwise the original-probe path always masks it.
  for(const auto &slot:s_clothBoneSlots)if(ClothBoneAppliedKind(slot,s_cloth.owner)&&slot.teamModeConfirmed) {
    const ClothInstance *source=nullptr;
    for(int n=0;n<s_cloth.count;++n)if(s_cloth.instances[n].ref.id==slot.bbc.id){source=&s_cloth.instances[n];break;}
    submit(slot.bbc,slot.profile->component,source,&slot);
  }
  for(int n=0;n<s_cloth.count;++n) {
    const auto &i=s_cloth.instances[n];bool leased=false;
    for(const auto &slot:s_clothBoneSlots)if(slot.lease&&slot.bbc.id==i.ref.id){leased=true;break;}
    // During build/restore, do not fall back to stale source state or bones.
    if(!leased&&i.last.state.active&&!i.last.state.culled&&!i.last.state.paused&&!i.last.state.skip)
      submit(i.ref,i.name,&i,nullptr);
  }
  if(!s.fault&&(s.clothing||s.hair||s.tail||s.ears||s.accessories))s.status=u8"衣物、头发等部件随身体运动产生惯性";
  s.mailbox.clear(); // All native substeps use this frame's force; never replay stale samples.
  for(unsigned n=0;n<s.shapeCount;++n)s.shapes[n].motion.clear();
}
template<class Profile,class Bones>
static void ClothTurnSubmit(const Profile &profile,const Bones &bones,double cursor,uint64_t epoch,bool playing,bool bodyMotion) {
  if(!ClothOnMainThread())return;
  auto &s=s_clothTurn;
  if(!s_clothTurnEnabled||!ClothTurnAnyGain()||!bodyMotion||!s_clothRequested||!s_cloth.active||s_cloth.releasing) {
    ClothTurnClear();s.status=u8"衣物惯性物理增强未启用";return;
  }
  if(s.session!=s_cloth.owner.session) {ClothTurnClear();s.session=s_cloth.owner.session;}
  if(s.fault)return;
  bool changed=false;
  for(int role=0;role<55;++role) {
    int index=profile.roles[role];auto t=index>=0&&index<int(bones.size())?bones[index].transform:nullptr;
    if(s.driverBones[role]&&s.driverBones[role]!=t)changed=true;
    s.driverBones[role]=t;s.driverRead[role]=s.driverValid[role]=false;
  }
  if(changed){for(auto &e:s.shapes)ClothTurnDropShape(e);s.shapeCount=0;}
  auto read=[&](std::initializer_list<int> roles,cloth_turn::Pose &out) {
    for(int role:roles) {
      int i=profile.roles[role];if(i<0||i>=int(bones.size()))continue;
      auto t=bones[i].transform;Vector3 p{};Quaternion q{};
      if(t&&UnityObjAlive(t)&&ClothValue(g_transform_get_position,t,p)&&ClothValue(g_transform_get_rotation,t,q)) {
        out={{p.x,p.y,p.z},{q.x,q.y,q.z,q.w}};return true;
      }
    }
    return false;
  };
  cloth_turn::Pose body,head,hips;
  if(!read({8,7,0},body)||!read({10,9,8,7,0},head)) {
    ClothTurnClear();s.status=u8"无法读取身体或头部姿态";return;
  }
  if(!read({0,7,8},hips))hips=body;
  if(playing&&s.body.ready&&s.body.playing&&s.body.epoch==epoch&&s.body.cursor==cursor)return;
  double now=ClothTurnMotionNow();
  s.mailbox.push(s.body.step(body,now,cursor,epoch,playing),s.head.step(head,now,cursor,epoch,playing),now,
      s.hips.step(hips,now,cursor,epoch,playing));
  for(unsigned n=0;n<s.shapeCount;++n) {
    auto &e=s.shapes[n];cloth_turn::Pose pose;
    if(!ClothTurnMountPose(e,pose)){e.tracker={};e.motion.clear();continue;}
    auto motion=e.tracker.step(pose,now,cursor,epoch,playing,cloth_turn::Material(e.part).smoothing);
    e.motion.push(motion,motion,now,motion);
  }
  if(!playing)s.status=u8"已暂停，衣物惯性自然收敛";
  else if(!s_clothSurfaceHook)s.status=u8"等待 BBC 物理更新接口";
  if(s_clothInputHookInstaller)s_clothInputHookInstaller();
}
