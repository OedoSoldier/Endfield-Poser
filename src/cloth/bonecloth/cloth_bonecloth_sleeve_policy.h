#pragma once
static bool ClothBoneSleevePolicy(const ClothBoneRuntime &s) {
  return s.profile&&eiem_cloth_asset::JsspsiSleeveSource(*s.profile)&&s.local.requested&&
      s.local.recipe&&s.profile->generatedLocal==s.local.recipe&&s.local.recipe->SharedSupplier();
}
static bool ClothBoneSleeveElasticPolicy(const ClothBoneRuntime &s) {
  const auto r=s.local.recipe;
  if(!ClothBoneSleevePolicy(s)||s.supportCreated||s.contactConsumer>=0||s.contactPartner>=0||
      r->distanceStiffness!=ClothSleeveDistanceStiffness||r->tetherStretch!=ClothSleeveTetherStretch||
      r->bendingStiffness!=0||!r->distanceCurve)return false;
  for(int n=0;n<16;++n)if(!std::isfinite(r->distanceCurve[n])||
      fabsf(r->distanceCurve[n]-ClothSleeveDistanceAt(float(n)/15.f))>1e-6f)return false;
  return true;
}
// Use a private Unity curve, converted by BBC itself. Never overwrite shared
// keyframes or native Team arrays, and never create curves during simulation.
static bool ClothBoneSleeveDistanceCurve(void *curve) {
  void *keys=nullptr,*linear=nullptr,*copy=nullptr;
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||
      !s_clothMethodFlags||!ClothField(curve,"curve","UnityEngine.AnimationCurve",keys)||!keys)return false;
  auto cls=il2cpp_object_get_class(keys);
  for(void *it=nullptr,*m=nullptr;(m=il2cpp_class_get_methods(cls,&it));) {
    uint32_t flags=0;
    if(strcmp(il2cpp_method_get_name(m),"Linear")||il2cpp_method_get_param_count(m)!=4||
        !(s_clothMethodFlags(m,&flags)&0x10)||
        !CollisionType(il2cpp_method_get_return_type(m),"UnityEngine.AnimationCurve"))continue;
    bool match=true;for(int n=0;n<4;++n)match&=CollisionType(il2cpp_method_get_param(m,n),"System.Single");
    if(match){if(linear)return false;linear=m;}
  }
  float start=0,root=1,end=1,tip=ClothSleeveDistanceTip/ClothSleeveDistanceStiffness;
  void *args[]{&start,&root,&end,&tip};
  if(!linear||!ClothInvoke(linear,nullptr,args,copy)||!copy||copy==keys||
      il2cpp_object_get_class(copy)!=cls)return false;
  auto evaluate=SurfaceMethod(cls,"Evaluate","System.Single","System.Single");
  for(int n=0;n<16;++n) {
    float t=float(n)/15.f,value=NAN;void *sample=nullptr,*sampleArgs[]{&t};
    if(!ClothInvoke(evaluate,copy,sampleArgs,sample)||!sample)return false;
    memcpy(&value,static_cast<char*>(sample)+16,sizeof(value));
    if(!std::isfinite(value)||fabsf(value*ClothSleeveDistanceStiffness-ClothSleeveDistanceAt(t))>1e-6f)return false;
  }
  return SurfaceReference(curve,"curve","UnityEngine.AnimationCurve",copy)&&
      SurfaceScalar(curve,"useCurve","System.Boolean",true);
}
// The sleeve reference has angular limits but zero joint springs. BBC angle
// restoration instead rotates every edge back towards its reference direction;
// even a small value opposes gravity continuously and makes a raised sleeve
// settle like a stiff board. Keep its parent-relative limit, not this bias.
constexpr float ClothSleeveGravity=4.f,ClothSleeveGravityFalloff=.35f,ClothSleeveRestoration=0.f;
constexpr float ClothSleeveDamping=.16f;
constexpr float ClothSleeveCompression=.85f;
// PMX: 28 moving joints at +/-5 degrees. This six-link BBC approximation
// distributes that bending budget over a symmetric cone, not a 6DOF joint.
constexpr float ClothSleeveLimitDegrees=28.f*5.f/6.f,ClothSleeveLimitStiffness=.6f;
static bool ClothBoneSleeveLimit(void *data,void *original) {
  constexpr auto type="BeyondDynamicBone.AngleConstraint.LimitSerializeData";
  constexpr auto ct="BeyondDynamicBone.CurveSerializeData";
  void *source=nullptr,*sourceCurve=nullptr,*keys=nullptr,*copy=nullptr,*curve=nullptr,*check=nullptr,*afterKeys=nullptr;
  float value=NAN,stiffness=NAN,after=NAN;bool enabled=false,use=false,afterBool=false;
  if(!data||!original||data==original||
      !ClothField(original,"angleLimitConstraint",type,source)||!source||
      !ClothField(source,"useAngleLimit","System.Boolean",enabled)||
      !ClothField(source,"stiffness","System.Single",stiffness)||!std::isfinite(stiffness)||stiffness<0||stiffness>1||
      !ClothField(source,"limitAngle",ct,sourceCurve)||!sourceCurve||
      !ClothField(sourceCurve,"value","System.Single",value)||!std::isfinite(value)||value<0||value>180||
      !ClothField(sourceCurve,"useCurve","System.Boolean",use)||
      !ClothField(sourceCurve,"curve","UnityEngine.AnimationCurve",keys)||
      !SurfaceCloneField(data,original,"angleLimitConstraint",type)||
      !ClothField(data,"angleLimitConstraint",type,copy)||!copy||copy==source||
      !SurfaceCloneField(copy,source,"limitAngle",ct)||!ClothField(copy,"limitAngle",ct,curve)||!curve||curve==sourceCurve||
      !SurfaceScalar(copy,"useAngleLimit","System.Boolean",true)||
      !SurfaceScalar(copy,"stiffness","System.Single",ClothSleeveLimitStiffness)||
      !SurfaceScalar(curve,"useCurve","System.Boolean",false)||
      !SurfaceScalar(curve,"value","System.Single",ClothSleeveLimitDegrees))return false;
  return ClothField(original,"angleLimitConstraint",type,check)&&check==source&&
      ClothField(source,"limitAngle",ct,check)&&check==sourceCurve&&
      ClothField(source,"useAngleLimit","System.Boolean",afterBool)&&afterBool==enabled&&
      ClothField(source,"stiffness","System.Single",after)&&after==stiffness&&
      ClothField(sourceCurve,"useCurve","System.Boolean",afterBool)&&afterBool==use&&
      ClothField(sourceCurve,"value","System.Single",after)&&after==value&&
      ClothField(sourceCurve,"curve","UnityEngine.AnimationCurve",afterKeys)&&afterKeys==keys;
}
// Allow folding towards the attachment; the material stage separately bounds
// root reach while preserving a supported attachment and softer free links.
static bool ClothBoneSleeveTether(void *data,void *original) {
  constexpr auto type="BeyondDynamicBone.TetherConstraint.SerializeData";
  void *source=nullptr,*copy=nullptr,*check=nullptr;float before=NAN,after=NAN;
  if(!ClothField(original,"tetherConstraint",type,source)||!source||
      !ClothField(source,"distanceCompression","System.Single",before)||!std::isfinite(before)||before<0||before>1||
      !SurfaceCloneField(data,original,"tetherConstraint",type)||
      !ClothField(data,"tetherConstraint",type,copy)||!copy||copy==source||
      !SurfaceScalar(copy,"distanceCompression","System.Single",ClothSleeveCompression))return false;
  return ClothField(original,"tetherConstraint",type,check)&&check==source&&
      ClothField(source,"distanceCompression","System.Single",after)&&after==before;
}
struct ClothSleeveNativeAngle {
  bool restore;unsigned char pad[3];float stiffness[16],velocity,gravity;
  bool limit;unsigned char pad2[3];float limitCurve[16],limitStiffness;
};
struct ClothSleeveNativeTether {float compression,stretch;};
static bool ClothBoneSleeveShapeParameters(void *box,ClothSleeveNativeAngle &angle,ClothSleeveNativeTether &tether,float stretch=.03f) {
  constexpr auto at="BeyondDynamicBone.AngleConstraint.AngleConstraintParams";
  constexpr auto tt="BeyondDynamicBone.TetherConstraint.TetherConstraintParams";
  auto cls=box?il2cpp_object_get_class(box):nullptr;
  auto af=cls?CollisionFieldInfo(cls,"angleConstraint",at):nullptr;
  auto tf=cls?CollisionFieldInfo(cls,"tetherConstraint",tt):nullptr;
  auto ac=af?il2cpp_class_from_type(il2cpp_field_get_type(af)):nullptr;
  auto tc=tf?il2cpp_class_from_type(il2cpp_field_get_type(tf)):nullptr;uint32_t align=0;
  static_assert(sizeof(angle)==148&&sizeof(tether)==8,"native sleeve parameter layout");
  if(!ac||!tc||il2cpp_class_value_size(ac,&align)!=sizeof(angle)||il2cpp_class_value_size(tc,&align)!=sizeof(tether)||
      ClothValueOffset(ac,"useAngleRestoration","System.Boolean",148,1)!=offsetof(ClothSleeveNativeAngle,restore)||
      ClothValueOffset(ac,"restorationStiffness","Unity.Mathematics.float4x4",148,64)!=offsetof(ClothSleeveNativeAngle,stiffness)||
      ClothValueOffset(ac,"useAngleLimit","System.Boolean",148,1)!=offsetof(ClothSleeveNativeAngle,limit)||
      ClothValueOffset(ac,"limitCurveData","Unity.Mathematics.float4x4",148,64)!=offsetof(ClothSleeveNativeAngle,limitCurve)||
      ClothValueOffset(ac,"limitstiffness","System.Single",148,4)!=offsetof(ClothSleeveNativeAngle,limitStiffness)||
      ClothValueOffset(tc,"compressionLimit","System.Single",8,4)!=0||
      ClothValueOffset(tc,"stretchLimit","System.Single",8,4)!=4||
      !ClothInputTeamField(box,"angleConstraint",at,angle)||!ClothInputTeamField(box,"tetherConstraint",tt,tether))return false;
  if(angle.restore||!angle.limit||!std::isfinite(angle.limitStiffness)||angle.limitStiffness<=0||angle.limitStiffness>1||
      !std::isfinite(tether.compression)||fabsf(tether.compression-ClothSleeveCompression)>1e-6f||
      !std::isfinite(tether.stretch)||fabsf(tether.stretch-stretch)>1e-6f)return false;
  for(float v:angle.stiffness)if(!std::isfinite(v)||fabsf(v)>1e-7f)return false;
  for(float v:angle.limitCurve)if(!std::isfinite(v)||v<=0||fabsf(v-angle.limitCurve[0])>1e-6f)return false;
  return true;
}
static bool ClothBoneSleeveDamping(void *data,void *original) {
  constexpr auto type="BeyondDynamicBone.CurveSerializeData";
  void *source=nullptr,*copy=nullptr,*keys=nullptr,*afterKeys=nullptr,*check=nullptr;
  float value=NAN,after=NAN;bool use=false,afterUse=false;
  if(!data||!original||data==original||
      !ClothField(original,"damping",type,source)||!source||
      !ClothField(source,"value","System.Single",value)||!std::isfinite(value)||value<0||value>1||
      !ClothField(source,"useCurve","System.Boolean",use)||
      !ClothField(source,"curve","UnityEngine.AnimationCurve",keys)||
      !SurfaceCloneField(data,original,"damping",type)||
      !ClothField(data,"damping",type,copy)||!copy||copy==source||
      !SurfaceScalar(copy,"useCurve","System.Boolean",false)||
      !SurfaceScalar(copy,"value","System.Single",ClothSleeveDamping))return false;
  // Only the private sleeve parameters change. The native depth curve remains
  // intact for stopping, replacing the actor, and playing without enhancement.
  return ClothField(original,"damping",type,check)&&check==source&&
      ClothField(source,"value","System.Single",after)&&after==value&&
      ClothField(source,"useCurve","System.Boolean",afterUse)&&afterUse==use&&
      ClothField(source,"curve","UnityEngine.AnimationCurve",afterKeys)&&afterKeys==keys;
}
static bool ClothBoneSleeveConfigure(void *data,void *original) {
  auto &s=ClothBoneState();if(!ClothBoneSleevePolicy(s))return true;
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||!ClothOwns(s.owner)||
      !data||!original||data==original)return false;
  constexpr auto type="BeyondDynamicBone.AngleConstraint.RestorationSerializeData";
  constexpr auto curveType="BeyondDynamicBone.CurveSerializeData";
  void *source=nullptr,*oldCurve=nullptr,*copy=nullptr,*curve=nullptr,*check=nullptr,*oldKeys=nullptr,*keys=nullptr;
  float gravity=NAN,falloff=NAN,value=NAN,after=NAN;bool use=false,afterUse=false,enabled=false;
  if(!ClothField(original,"gravity","System.Single",gravity)||!ClothField(original,"gravityFalloff","System.Single",falloff)||
      !std::isfinite(gravity)||!std::isfinite(falloff)||
      !ClothField(original,"angleRestorationConstraint",type,source)||!source||
      !ClothField(source,"useAngleRestoration","System.Boolean",enabled)||!enabled||
      !ClothField(source,"stiffness",curveType,oldCurve)||!oldCurve||
      !ClothField(oldCurve,"value","System.Single",value)||!std::isfinite(value)||value<=0||value>1||
      !ClothField(oldCurve,"useCurve","System.Boolean",use)||!use||
      !ClothField(oldCurve,"curve","UnityEngine.AnimationCurve",oldKeys)||!oldKeys||
      !SurfaceCloneField(data,original,"angleRestorationConstraint",type)||
      !ClothField(data,"angleRestorationConstraint",type,copy)||!copy||copy==source||
      !SurfaceScalar(copy,"useAngleRestoration","System.Boolean",false)||
      !SurfaceCloneField(copy,source,"stiffness",curveType)||!ClothField(copy,"stiffness",curveType,curve)||!curve||curve==oldCurve||
      !SurfaceScalar(curve,"useCurve","System.Boolean",true)||!SurfaceScalar(curve,"value","System.Single",ClothSleeveRestoration)||
      !SurfaceScalar(data,"gravity","System.Single",ClothSleeveGravity)||
      !SurfaceScalar(data,"gravityFalloff","System.Single",ClothSleeveGravityFalloff)||
      !ClothBoneSleeveDamping(data,original)||!ClothBoneSleeveTether(data,original)||!ClothBoneSleeveLimit(data,original))return false;
  // The original remains the exact restoration target; never edit shared curves.
  if(!ClothField(original,"gravity","System.Single",after)||after!=gravity||
      !ClothField(original,"gravityFalloff","System.Single",after)||after!=falloff||
      !ClothField(original,"angleRestorationConstraint",type,check)||check!=source||
      !ClothField(source,"useAngleRestoration","System.Boolean",afterUse)||afterUse!=enabled||
      !ClothField(source,"stiffness",curveType,check)||check!=oldCurve||
      !ClothField(oldCurve,"value","System.Single",after)||after!=value||
      !ClothField(oldCurve,"useCurve","System.Boolean",afterUse)||afterUse!=use||
      !ClothField(oldCurve,"curve","UnityEngine.AnimationCurve",keys)||keys!=oldKeys)return false;
  // BBC converts authored damping to solver units. Capture its own converter's
  // result once instead of equating the UI scalar to native dampingCurveData.
  if(!ClothBoneDampingParameters(data,s.sleeveDampingCurve))return false;
  for(float sample:s.sleeveDampingCurve)if(!std::isfinite(sample)||sample<0||sample>1)return false;
  void *box=nullptr;ClothSleeveNativeAngle angles{};ClothSleeveNativeTether tether{};
  if(!ClothInvoke(SurfaceMethod(il2cpp_object_get_class(data),"GetClothParameters","BeyondDynamicBone.ClothParameters"),data,nullptr,box)||
      !ClothBoneSleeveShapeParameters(box,angles,tether))return false;
  std::copy(std::begin(angles.stiffness),std::end(angles.stiffness),s.sleeveRestorationCurve);
  std::copy(std::begin(angles.limitCurve),std::end(angles.limitCurve),s.sleeveLimitCurve);
  s.sleeveLimitStiffness=angles.limitStiffness;
  s.sleeveDampingConfigured=true;
  Log("[CLOTH-SLEEVE] stage=drape-configured component=%s gravity=%g->%g falloff=%g->%g restoration=%g angleRestoration=off nativeRestoration=%g->%g tetherCompression=%g stretchBeforeMaterial=%g damping=%g inertia=spring-force coneLimitDegrees=%g limitStiffness=%g nativeLimit=%g fixedRoots=1 arms=4 innerBodyCapsules=5 materialPending=1 originalUntouched=1 visualVerified=0",
      s.profile->component,gravity,ClothSleeveGravity,falloff,ClothSleeveGravityFalloff,ClothSleeveRestoration,
      angles.stiffness[0],angles.stiffness[15],tether.compression,tether.stretch,ClothSleeveDamping,
      ClothSleeveLimitDegrees,ClothSleeveLimitStiffness,angles.limitCurve[0]);
  return true;
}
static bool ClothBoneSleeveMatches(const ClothBoneRuntime &s,int slot,void *box) {
  if(!ClothBoneSleevePolicy(s))return true;
  auto data=CollisionGc(slot==1?s.candidateData:s.data);float gravity=NAN,falloff=NAN,actual=NAN;
  if(!data||!ClothField(data,"gravity","System.Single",gravity)||!ClothField(data,"gravityFalloff","System.Single",falloff)||
      !ClothInputTeamField(box,"gravity","System.Single",actual)||actual!=gravity||
      !ClothInputTeamField(box,"gravityFalloff","System.Single",actual)||actual!=falloff)return false;
  if(slot!=1)return true;
  void *privateTether=nullptr;
  if(!s.local.elasticTether||!s.local.elasticConversions||
      !ClothField(data,"tetherConstraint","BeyondDynamicBone.TetherConstraint.SerializeData",privateTether)||
      privateTether!=CollisionGc(s.local.elasticTether))return false;
  void *angle=nullptr,*curve=nullptr,*damping=nullptr;bool use=true;float restoration=NAN,value=NAN,native[16]{};
  if(!ClothField(data,"damping","BeyondDynamicBone.CurveSerializeData",damping)||!damping||
      !ClothField(damping,"useCurve","System.Boolean",use)||use||
      !ClothField(damping,"value","System.Single",value)||value!=ClothSleeveDamping||
      !ClothInputTeamField(box,"dampingCurveData","Unity.Mathematics.float4x4",native))return false;
  if(!s.sleeveDampingConfigured)return false;
  for(int n=0;n<16;++n)if(!std::isfinite(native[n])||fabsf(native[n]-s.sleeveDampingCurve[n])>1e-5f)return false;
  ClothSleeveNativeAngle angles{};ClothSleeveNativeTether tether{};
  if(!ClothBoneSleeveElasticPolicy(s)||!ClothBoneSleeveShapeParameters(box,angles,tether,ClothSleeveTetherStretch))return false;
  float distance[16]{},attenuation=NAN;
  if(!ClothBoneLocalDistanceParameters(box,distance,attenuation)||!std::isfinite(attenuation))return false;
  for(int n=0;n<16;++n)if(!std::isfinite(distance[n])||fabsf(distance[n]-ClothSleeveDistanceAt(float(n)/15.f))>1e-6f)return false;
  for(int n=0;n<16;++n)if(fabsf(angles.stiffness[n]-s.sleeveRestorationCurve[n])>1e-5f)return false;
  for(int n=0;n<16;++n)if(fabsf(angles.limitCurve[n]-s.sleeveLimitCurve[n])>1e-5f)return false;
  if(fabsf(angles.limitStiffness-s.sleeveLimitStiffness)>1e-5f)return false;
  void *limit=nullptr,*limitCurve=nullptr;bool enabled=false;
  if(!ClothField(data,"angleLimitConstraint","BeyondDynamicBone.AngleConstraint.LimitSerializeData",limit)||!limit||
      !ClothField(limit,"useAngleLimit","System.Boolean",enabled)||!enabled||
      !ClothField(limit,"stiffness","System.Single",value)||value!=ClothSleeveLimitStiffness||
      !ClothField(limit,"limitAngle","BeyondDynamicBone.CurveSerializeData",limitCurve)||!limitCurve||
      !ClothField(limitCurve,"useCurve","System.Boolean",use)||use||
      !ClothField(limitCurve,"value","System.Single",value)||value!=ClothSleeveLimitDegrees)return false;
  return gravity==ClothSleeveGravity&&falloff==ClothSleeveGravityFalloff&&
      ClothField(data,"angleRestorationConstraint","BeyondDynamicBone.AngleConstraint.RestorationSerializeData",angle)&&angle&&
      ClothField(angle,"useAngleRestoration","System.Boolean",use)&&!use&&
      ClothField(angle,"stiffness","BeyondDynamicBone.CurveSerializeData",curve)&&curve&&
      ClothField(curve,"useCurve","System.Boolean",use)&&use&&
      ClothField(curve,"value","System.Single",restoration)&&restoration==ClothSleeveRestoration;
}
