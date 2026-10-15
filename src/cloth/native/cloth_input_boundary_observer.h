#pragma once
// Included after the completed-input observer and its state declarations.
static bool ClothInputBoundaryIdentityMatches(void *team) {
  poser_cloth_boundary::Identity current{};
  il2cpp_field_static_get_value(s_clothInputStaticCloth,&current.cloth);
  il2cpp_field_static_get_value(s_clothInputStaticTeam,&current.team);
  il2cpp_field_static_get_value(s_clothInputStaticBone,&current.bone);
  return s_clothInputBoundaryContext.IdentityMatches(current,team);
}
static void *__fastcall ClothInputNativeGetBone(void *method) {
  void *caller=_ReturnAddress();
  void *result=s_clothInputOriginalGetBone(method);
  if(ClothOnMainThread()&&s_clothInputBoundary163&&s_clothInputUpdateDepth==1&&
      caller==s_clothInputBoneCallsite)s_clothInputBoundaryContext.RecordBone(result);
  return result;
}
static __declspec(noinline) void ClothInputObserveTeamActors(void *manager,void *caller) {
  for(unsigned actor=0;actor<ClothActorCount;++actor) {
    if(!s_clothActors.values[actor])continue;
    ClothActorScope scope(actor);ClothInputCompleted(manager,caller);
  }
}
static __declspec(noinline) void ClothInputObserveTeamGetterUnchecked(void *result,void *caller) {
  auto &context=s_clothInputBoundaryContext;
  if(caller==s_clothInputEarlyTeamCallsite) {context.RecordTeam(result);return;}
  if(caller!=s_clothInputCallsite)return;
  poser_cloth_boundary::Identity current{};
  il2cpp_field_static_get_value(s_clothInputStaticCloth,&current.cloth);
  il2cpp_field_static_get_value(s_clothInputStaticTeam,&current.team);
  il2cpp_field_static_get_value(s_clothInputStaticBone,&current.bone);
  const bool fingerprint=s_clothInputUpdateCode&&
      eiem_cloth_input::Fingerprint(s_clothInputUpdateCode,s_clothInputAuditedBytes)==s_clothInputPatchedFingerprint;
  if(!context.BeginObservation(current,result,true,s_clothInputUpdateDepth,true,
      ClothInputStaticBool("UseAnimatorTransform"),ClothInputStaticBool("UseCrossFrameJob"),fingerprint))return;
  ClothInputObserveTeamActors(context.observed.bone,caller);
}
static void ClothInputObserveTeamGetter(void *result,void *caller) noexcept {
  if(!ClothOnMainThread()||!s_clothInputBoundary163||!s_clothInputHooks||s_clothInputUpdateDepth!=1||
      s_clothInputBoundaryContext.observing||s_clothInputBoundaryContext.consumed)return;
  // Keep C++ object lifetimes out of the SEH function (MSVC C2712).
  // A fault in observation must not alter the game's getter result or handle.
  __try {
    __try { ClothInputObserveTeamGetterUnchecked(result,caller); }
    __finally { s_clothInputBoundaryContext.EndObservation(); }
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    s_clothInputBoundaryContext.EndObservation();
    ClothInputIssue("completed-input-identity-observer-fault",true);
  }
}
static void *__fastcall ClothInputNativeGetTeam(void *method) {
  void *caller=_ReturnAddress();
  void *result=s_clothInputOriginalGetTeam(method);
  ClothInputObserveTeamGetter(result,caller);
  return result;
}
