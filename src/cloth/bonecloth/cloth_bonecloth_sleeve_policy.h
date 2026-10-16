#pragma once
#include "math/cloth_sleeve_material.h"
static bool ClothBoneSleevePolicy(const ClothBoneRuntime &s) {
  return s.profile&&eiem_cloth_asset::JsspsiSleeveSource(*s.profile)&&s.local.requested&&
      s.local.recipe&&s.profile->generatedLocal==s.local.recipe&&s.local.recipe->SharedSupplier();
}
static bool ClothBoneSleeveShapeParameters(void *box,ClothSleeveNativeAngle &angle,ClothSleeveNativeTether &tether) {
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
      ClothValueOffset(ac,"restorationVelocityAttenuation","System.Single",148,4)!=offsetof(ClothSleeveNativeAngle,velocity)||
      ClothValueOffset(ac,"restorationGravityFalloff","System.Single",148,4)!=offsetof(ClothSleeveNativeAngle,gravity)||
      ClothValueOffset(ac,"useAngleLimit","System.Boolean",148,1)!=offsetof(ClothSleeveNativeAngle,limit)||
      ClothValueOffset(ac,"limitCurveData","Unity.Mathematics.float4x4",148,64)!=offsetof(ClothSleeveNativeAngle,limitCurve)||
      ClothValueOffset(ac,"limitstiffness","System.Single",148,4)!=offsetof(ClothSleeveNativeAngle,limitStiffness)||
      ClothValueOffset(tc,"compressionLimit","System.Single",8,4)!=0||
      ClothValueOffset(tc,"stretchLimit","System.Single",8,4)!=4||
      !ClothInputTeamField(box,"angleConstraint",at,angle)||!ClothInputTeamField(box,"tetherConstraint",tt,tether))return false;
  if(!std::isfinite(angle.limitStiffness)||!std::isfinite(angle.velocity)||!std::isfinite(angle.gravity)||
      !std::isfinite(tether.compression)||!std::isfinite(tether.stretch))return false;
  for(float v:angle.stiffness)if(!std::isfinite(v))return false;
  for(float v:angle.limitCurve)if(!std::isfinite(v))return false;
  return true;
}
static bool ClothBoneSleeveInertiaRead(void *box,ClothSleeveNativeInertia &out) {
  constexpr auto type="BeyondDynamicBone.InertiaConstraint.InertiaConstraintParams";
  static_assert(sizeof(out)==56&&offsetof(ClothSleeveNativeInertia,teleportMode)==44,"native sleeve inertia layout");
  auto field=box?CollisionFieldInfo(il2cpp_object_get_class(box),"inertiaConstraint",type):nullptr;
  auto cls=field?il2cpp_class_from_type(il2cpp_field_get_type(field)):nullptr;
  // IL2CPP metadata is immutable. Avoid repeating fourteen layout scans for
  // every Team readback; never cache a failed or differently typed layout.
  static void *verified=nullptr;
  if(!cls)return false;
  if(cls!=verified) {
    uint32_t alignment=0;if(il2cpp_class_value_size(cls,&alignment)!=56)return false;
    const char *names[]{"anchorInertia","worldInertia","movementInertiaSmoothing","movementSpeedLimit","rotationSpeedLimit",
        "localInertia","localMovementSpeedLimit","localRotationSpeedLimit","depthInertia","centrifualAcceleration",
        "particleSpeedLimit","teleportMode","teleportDistance","teleportRotation"};
    for(int n=0;n<14;++n)if(ClothValueOffset(cls,names[n],n==11?
        "BeyondDynamicBone.InertiaConstraint.TeleportMode":"System.Single",56,4)!=n*4)return false;
    verified=cls;
  }
  if(!ClothInputTeamField(box,"inertiaConstraint",type,out))return false;
  for(int n=0;n<14;++n)if(n!=11){float v;memcpy(&v,(char*)&out+n*4,4);if(!std::isfinite(v))return false;}
  return out.worldInertia>=0&&out.worldInertia<=1&&out.localInertia>=0&&out.localInertia<=1;
}
// Compare BBC's own converted source parameters, not guessed scalar-to-solver
// conversions. Disabled angle limits and nonuniform native curves are valid.
static bool ClothBoneSleeveRead(void *box,ClothSleeveMaterial &m) {
  constexpr auto bt="BeyondDynamicBone.TriangleBendingConstraint.TriangleBendingConstraintParams";
  auto f=box?CollisionFieldInfo(il2cpp_object_get_class(box),"triangleBendingConstraint",bt):nullptr;
  auto bc=f?il2cpp_class_from_type(il2cpp_field_get_type(f)):nullptr;uint32_t alignment=0;
  if(!bc||il2cpp_class_value_size(bc,&alignment)!=8||
      ClothValueOffset(bc,"method","BeyondDynamicBone.TriangleBendingConstraint.Method",8,4)!=0||
      ClothValueOffset(bc,"stiffness","System.Single",8,4)!=4||
      !ClothInputTeamField(box,"triangleBendingConstraint",bt,m.bending)||
      !ClothInputTeamField(box,"gravity","System.Single",m.gravity)||
      !ClothInputTeamField(box,"worldGravityDirection","Unity.Mathematics.float3",m.gravityDirection)||
      !ClothInputTeamField(box,"gravityFalloff","System.Single",m.falloff)||
      !ClothInputTeamField(box,"dampingCurveData","Unity.Mathematics.float4x4",m.damping)||
      !ClothBoneSleeveShapeParameters(box,m.angle,m.tether)||!ClothBoneSleeveInertiaRead(box,m.inertia)||
      !ClothBoneLocalDistanceParameters(box,m.distance,m.attenuation))return false;
  if(!std::isfinite(m.gravity)||!std::isfinite(m.falloff)||!std::isfinite(m.attenuation)||
      !std::isfinite(m.gravityDirection.x)||!std::isfinite(m.gravityDirection.y)||!std::isfinite(m.gravityDirection.z)||
      !std::isfinite(m.bending.stiffness))return false;
  for(float v:m.damping)if(!std::isfinite(v))return false;
  for(float v:m.distance)if(!std::isfinite(v))return false;
  return true;
}
static bool ClothBoneSleeveSame(const ClothSleeveMaterial &a,const ClothSleeveMaterial &b) {
  // Avoid comparing padding bytes in the native boolean-containing struct.
  auto eq=[](float x,float y){return std::isfinite(x)&&std::isfinite(y)&&fabsf(x-y)<=1e-6f;};
  if(!eq(a.gravity,b.gravity)||!eq(a.falloff,b.falloff)||!eq(a.attenuation,b.attenuation)||
      !eq(a.gravityDirection.x,b.gravityDirection.x)||!eq(a.gravityDirection.y,b.gravityDirection.y)||
      !eq(a.gravityDirection.z,b.gravityDirection.z)||
      a.bending.method!=b.bending.method||!eq(a.bending.stiffness,b.bending.stiffness)||
      a.angle.restore!=b.angle.restore||a.angle.limit!=b.angle.limit||
      !eq(a.angle.velocity,b.angle.velocity)||!eq(a.angle.gravity,b.angle.gravity)||
      !eq(a.angle.limitStiffness,b.angle.limitStiffness)||
      !eq(a.tether.compression,b.tether.compression)||!eq(a.tether.stretch,b.tether.stretch))return false;
  if(a.inertia.teleportMode!=b.inertia.teleportMode)return false;
  for(int n=0;n<14;++n)if(n!=11){float x,y;memcpy(&x,(const char*)&a.inertia+n*4,4);
    memcpy(&y,(const char*)&b.inertia+n*4,4);if(!eq(x,y))return false;}
  for(int n=0;n<16;++n)if(!eq(a.damping[n],b.damping[n])||!eq(a.distance[n],b.distance[n])||
      !eq(a.angle.stiffness[n],b.angle.stiffness[n])||!eq(a.angle.limitCurve[n],b.angle.limitCurve[n]))return false;
  return true;
}
static bool ClothBoneSleeveMaterialFrom(void *data,ClothSleeveMaterial &m) {
  void *box=nullptr;
  return data&&ClothInvoke(SurfaceMethod(il2cpp_object_get_class(data),"GetClothParameters",
      "BeyondDynamicBone.ClothParameters"),data,nullptr,box)&&ClothBoneSleeveRead(box,m);
}
static bool ClothBoneSleevePmxConfigure(void *data,void *original,const ClothSleeveMaterial &source) {
  // Only edit cloned configuration objects. Original animation curves remain
  // untouched and survive stop, cancellation and actor replacement.
  constexpr auto ct="BeyondDynamicBone.CurveSerializeData";
  constexpr auto rt="BeyondDynamicBone.AngleConstraint.RestorationSerializeData";
  constexpr auto lt="BeyondDynamicBone.AngleConstraint.LimitSerializeData";
  constexpr auto tt="BeyondDynamicBone.TetherConstraint.SerializeData";
  constexpr auto it="BeyondDynamicBone.InertiaConstraint.SerializeData";
  void *rest=nullptr,*limit=nullptr,*oldLimit=nullptr,*angle=nullptr,*tether=nullptr,*damping=nullptr,*inertia=nullptr;
  if(!SurfaceCloneField(data,original,"angleRestorationConstraint",rt)||
      !ClothField(data,"angleRestorationConstraint",rt,rest)||!rest||
      !SurfaceScalar(rest,"useAngleRestoration","System.Boolean",false)||
      !SurfaceCloneField(data,original,"angleLimitConstraint",lt)||
      !ClothField(data,"angleLimitConstraint",lt,limit)||!limit||
      !ClothField(original,"angleLimitConstraint",lt,oldLimit)||!oldLimit||
      !SurfaceCloneField(limit,oldLimit,"limitAngle",ct)||
      !ClothField(limit,"limitAngle",ct,angle)||!angle||
      !SurfaceScalar(limit,"useAngleLimit","System.Boolean",true)||
      !SurfaceScalar(limit,"stiffness","System.Single",cloth_sleeve_material::LimitStiffness())||
      !SurfaceScalar(angle,"useCurve","System.Boolean",false)||
      !SurfaceScalar(angle,"value","System.Single",cloth_sleeve_material::LimitDegrees())||
      !SurfaceCloneField(data,original,"tetherConstraint",tt)||
      !ClothField(data,"tetherConstraint",tt,tether)||!tether||
      !SurfaceScalar(tether,"distanceCompression","System.Single",1.f)||
      !SurfaceCloneField(data,original,"damping",ct)||
      !ClothField(data,"damping",ct,damping)||!damping||
      !SurfaceScalar(damping,"useCurve","System.Boolean",false)||
      !SurfaceScalar(damping,"value","System.Single",cloth_sleeve_material::DampingScalar())||
      !SurfaceScalar(data,"gravity","System.Single",cloth_sleeve_material::Gravity())||
      !SurfaceScalar(data,"gravityFalloff","System.Single",cloth_sleeve_material::GravityFalloff())||
      !SurfaceCloneField(data,original,"inertiaConstraint",it)||
      !ClothField(data,"inertiaConstraint",it,inertia)||!inertia||
      !SurfaceScalar(inertia,"worldInertia","System.Single",(std::min)(source.inertia.worldInertia,cloth_sleeve_material::MaximumInertia()))||
      !SurfaceScalar(inertia,"localInertia","System.Single",(std::min)(source.inertia.localInertia,cloth_sleeve_material::MaximumInertia())))return false;
  // Segment distance remains native stiffness=1. Root tether compression=1
  // removes the global straightening chord while retaining the native reach
  // guard (3% extension). No extra native conversion hook is needed.
  return true;
}
static bool ClothBoneSleeveConfigure(void *data,void *original) {
  auto &s=ClothBoneState();if(!ClothBoneSleevePolicy(s))return true;
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||!ClothOwns(s.owner)||
      !data||!original||data==original||s.local.elasticTether||s.surfaceBending)return false;
  // First require an exact import, then apply the PMX-inspired local-joint
  // policy. Separate source/candidate snapshots also validate restoration.
  ClothSleeveMaterial source{},candidate{},after{};
  if(!ClothBoneSleeveMaterialFrom(original,source)||!ClothBoneSleeveMaterialFrom(data,candidate)||
      !ClothBoneSleeveSame(source,candidate)||!ClothBoneSleevePmxConfigure(data,original,source)||
      !ClothBoneSleeveMaterialFrom(data,candidate)||!ClothBoneSleeveMaterialFrom(original,after)||
      !ClothBoneSleeveSame(source,after))return false;
  if(candidate.angle.restore||!candidate.angle.limit||candidate.tether.compression!=1.f||
      candidate.falloff!=cloth_sleeve_material::GravityFalloff()||
      fabsf(candidate.angle.limitStiffness-cloth_sleeve_material::LimitStiffness())>1e-6f||
      candidate.gravity!=cloth_sleeve_material::Gravity()||candidate.tether.stretch!=source.tether.stretch)return false;
  for(int n=0;n<16;++n)if(fabsf(candidate.damping[n]-.2f*cloth_sleeve_material::DampingScalar())>1e-6f||
      candidate.distance[n]!=source.distance[n])return false;
  auto expected=candidate;expected.inertia=source.inertia;expected.gravityDirection=source.gravityDirection;
  expected.inertia.worldInertia=(std::min)(source.inertia.worldInertia,cloth_sleeve_material::MaximumInertia());
  expected.inertia.localInertia=(std::min)(source.inertia.localInertia,cloth_sleeve_material::MaximumInertia());
  if(!ClothBoneSleeveSame(candidate,expected))return false; // All other inertia/speed/teleport settings stay native.
  s.sleeveNativeMaterial=source;s.sleevePmxMaterial=candidate;s.sleeveNativeConfigured=true;
  Log("[CLOTH-SLEEVE] stage=pmx-material-confirmed component=%s particles=30 fixed=2 material=first-thirty-bounded gravity=%g falloff=%g worldGravity=(%g,%g,%g) damping=%g/%g restoration=off angleLimit=on coneDegrees=%g limitStiffness=%g compression=1 stretch=%g worldInertia=%g/%g localInertia=%g/%g extraForce=optional-short-response sourceUntouched=1 TeamReadbackPending=1 massAndTwistMapped=0 visualVerified=0",
      s.profile->component,candidate.gravity,candidate.falloff,candidate.gravityDirection.x,candidate.gravityDirection.y,candidate.gravityDirection.z,candidate.damping[0],candidate.damping[15],
      cloth_sleeve_material::LimitDegrees(),candidate.angle.limitStiffness,candidate.tether.stretch,
      candidate.inertia.worldInertia,source.inertia.worldInertia,candidate.inertia.localInertia,source.inertia.localInertia);
  // Generated bones have no game animation tracks: reconstruct their reference
  // from the moving upper-arm mount (ratio=0), never feed solver output back as
  // animation input. This structural change is separate from material settings.
  return true;
}
static bool ClothBoneSleeveMatches(const ClothBoneRuntime &s,int slot,void *box) {
  if(!ClothBoneSleevePolicy(s))return true;
  if(slot<0||slot>2)return false;
  // Source adoption can precede candidate configuration.
  if(!s.sleeveNativeConfigured)return slot!=1;
  ClothSleeveMaterial actual{};
  return ClothBoneSleeveRead(box,actual)&&ClothBoneSleeveSame(slot==1?s.sleevePmxMaterial:s.sleeveNativeMaterial,actual);
}
