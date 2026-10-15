#pragma once
struct ClothElasticInstallState {
  bool attempted=false,profile163=false;
  unsigned char *producer=nullptr,*target=nullptr;
  uint64_t installedHash=0;
} static s_clothElasticInstall;
static bool ClothInstallElasticMainThread() {
  using namespace poser_cloth_boundary;
  auto &state=s_clothElasticInstall;auto &attempted=state.attempted;
  auto &producer=state.producer;auto &target=state.target;auto &installedHash=state.installedHash;
  if(!ClothOnMainThread() || !s_clothSurfaceAtBoundary || s_clothInputUpdateDepth!=1)return false;
  if(s_clothTetherConvert)return state.profile163?
      MappedMethodProfile(hGA,producer,ElasticProducer163)&&
      MappedCodeRange(hGA,target,{0,42,installedHash})&&MappedCodeRange(hGA,target,ElasticConvertRanges163[1]):
      ClothContactCode(producer,617,0x1126293357658d32ULL)&&ClothContactCode(target,42,installedHash);
  if(attempted)return false;attempted=true;
  const double started=ClothInstallClockMs();
  auto data=SurfaceClass("BeyondDynamicBone","ClothSerializeData");
  auto parameters=SurfaceClass("BeyondDynamicBone","ClothParameters");
  auto f=parameters?CollisionFieldInfo(parameters,"tetherConstraint","BeyondDynamicBone.TetherConstraint.TetherConstraintParams"):nullptr;
  auto cls=f?il2cpp_class_from_type(il2cpp_field_get_type(f)):nullptr;
  auto method=SurfaceMethod(cls,"Convert","System.Void","BeyondDynamicBone.TetherConstraint.SerializeData","BeyondDynamicBone.ClothProcess.ClothType");
  auto get=SurfaceMethod(data,"GetClothParameters","BeyondDynamicBone.ClothParameters");
  auto cf=data?CollisionFieldInfo(data,"clothType","BeyondDynamicBone.ClothProcess.ClothType"):nullptr;
  auto cc=cf?il2cpp_class_from_type(il2cpp_field_get_type(cf)):nullptr;
  producer=get?(unsigned char*)((MInfo*)get)->mp:nullptr;
  target=method?(unsigned char*)((MInfo*)method)->mp:nullptr;
  const bool layout=ClothContactSize(cls,8) &&
      ClothContactOffset(cls,"compressionLimit","System.Single",8,4,0) &&
      ClothContactOffset(cls,"stretchLimit","System.Single",8,4,4) &&
      cc && CollisionEnumValue(cc,"BoneCloth",s_clothTetherBoneType) && s_clothTetherBoneType>=0 && s_clothTetherBoneType<=1;
  bool code=false;
  if(s_clothInputBoundary163) {
    code=MappedMethodProfile(hGA,producer,ElasticProducer163)&&MappedMethodProfile(hGA,target,ElasticConvert163)&&
        DirectCallAt(producer,617,ElasticCall163,target)&&DirectCallCount(producer,617,target)==1;
    if(code)s_clothTetherCallsite=producer+ElasticCall163+5;
  } else code=ClothContactRequireCode("elastic-GetClothParameters",producer,617,0x1126293357658d32ULL)&&
      ClothContactRequireCode("elastic-TetherConvert",target,42,0xaa4d91b218cd4585ULL)&&
      ClothContactUniqueCall(producer,617,42,0xaa4d91b218cd4585ULL,"elastic-tether-call",&s_clothTetherCallsite)==target;
  if(!layout||!code) {
    Log("[CLOTH-BONE-ELASTIC] stage=install-refused reason=native-signature-layout-or-code-unconfirmed originalUntouched=1");return false;
  }
  const double validated=ClothInstallClockMs();
  const HookFunctions hooks{
      [](void *at,void *detour,void **original){return int(MH_CreateHook(at,detour,original));},
      [](void *at){return int(MH_QueueEnableHook(at));},[](){return int(MH_ApplyQueued());},
      [](void *at){return int(MH_DisableHook(at));},[](void *at){return int(MH_RemoveHook(at));}};
  void *targets[]{target},*detours[]{(void*)ClothBoneElasticConvert};
  void **originals[]{(void**)&s_clothTetherConvert};unsigned rollbackFailed=0;
  if(!InstallHooks(targets,detours,originals,1,hooks,rollbackFailed)) {
    s_clothTetherCallsite=nullptr; // A retained detour can only forward the original.
    Log("[CLOTH-BONE-ELASTIC] stage=install-refused reason=hook-install-failed rollbackFailed=%u originalUntouched=1",rollbackFailed);return false;
  }
  const double createdAt=ClothInstallClockMs();
  state.profile163=s_clothInputBoundary163;
  installedHash=eiem_cloth_input::Fingerprint(target,42);
  Log("[CLOTH-INSTALL-COST] adapter=elastic validateMs=%g createMs=%g enableMs=%g enableBatches=1",
      validated-started,createdAt-validated,ClothInstallClockMs()-createdAt);
  Log("[CLOTH-BONE-ELASTIC] stage=installed scope=main-thread-private-tether-and-exact-producer-call resultBytes=8 nativeArrayWrites=0 solverChanges=0");return true;
}
