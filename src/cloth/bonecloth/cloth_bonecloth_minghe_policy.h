#pragma once
// Minghe PMX 0d9c32aecb96: upper five rows use 0.1 PMX-unit box
// half-thickness, middle rows 0.3, lower rows up to 1.0. Matching coat
// landmarks give 0.08 game units / PMX unit. BBC has one depth curve,
// so use the maximum thickness within each row, retaining lower coverage.
inline float MingheCoatContactRadius(float depth) {
  constexpr float rows[]{.008f,.008f,.008f,.008f,.008f,.024f,.024f,.024f,.024f,.08f,.08f,.08f,.08f,.08f,.08f};
  const float x=std::clamp(depth,0.f,1.f)*14.f;const int n=(std::min)(13,int(x));
  return rows[n]+(rows[n+1]-rows[n])*(x-n);
}
static bool ClothMingheRadiusRead(void *box,float (&curve)[16]) {
  if(!box||!ClothInputTeamField(box,"radiusCurveData","Unity.Mathematics.float4x4",curve))return false;
  for(float v:curve)if(!std::isfinite(v)||v<=0||v>.2f)return false;
  return true;
}
static bool ClothMingheRadiusFrom(void *data,float (&curve)[16]) {
  void *box=nullptr;
  return data&&ClothInvoke(SurfaceMethod(il2cpp_object_get_class(data),"GetClothParameters",
      "BeyondDynamicBone.ClothParameters"),data,nullptr,box)&&ClothMingheRadiusRead(box,curve);
}
static bool ClothMingheRadiusSame(const float (&a)[16],const float (&b)[16]) {
  for(int n=0;n<16;++n)if(!std::isfinite(a[n])||!std::isfinite(b[n])||fabsf(a[n]-b[n])>1e-6f)return false;
  return true;
}
static bool ClothBoneMingheRadiusConfigure(void *data,void *original) {
  auto &s=ClothBoneState();if(!s.profile||!eiem_cloth_asset::SourceMingheCoatSelection(*s.profile))return true;
  if(!ClothOnMainThread()||!s_clothSurfaceAtBoundary||s_clothInputUpdateDepth!=1||!ClothOwns(s.owner)||
      s.stopRequested||s.mingheRadiusConfigured||!ClothBoneForkCoat(s)||
      !data||!original||data==original||data!=CollisionGc(s.candidateData)||original!=CollisionGc(s.data))return false;
  constexpr auto ct="BeyondDynamicBone.CurveSerializeData";
  void *source=nullptr,*oldKeys=nullptr,*copy=nullptr,*check=nullptr;
  float value=0,afterValue=0,sourceCurve[16]{},initial[16]{},after[16]{},actual[16]{},expected[16]{};
  bool use=false,afterUse=false;
  if(!ClothField(original,"radius",ct,source)||!source||
      !ClothField(source,"value","System.Single",value)||fabsf(value-.1f)>1e-6f||
      !ClothField(source,"useCurve","System.Boolean",use)||!use||
      !ClothField(source,"curve","UnityEngine.AnimationCurve",oldKeys)||!oldKeys||
      !ClothMingheRadiusFrom(original,sourceCurve)||!ClothMingheRadiusFrom(data,initial)||
      !ClothMingheRadiusSame(sourceCurve,initial))return false;
  auto cls=il2cpp_object_get_class(oldKeys);
  auto ctor=SurfaceMethod(cls,".ctor","System.Void");
  auto add=SurfaceMethod(cls,"AddKey","System.Int32","System.Single","System.Single");
  auto length=SurfaceMethod(cls,"get_length","System.Int32");
  if(!ctor||!add||!length||!il2cpp_object_new)return false;
  // Never mutate the shared source AnimationCurve. Unity creates and owns the
  // new key storage; no native struct layout or conversion hook is involved.
  void *keys=il2cpp_object_new(cls),*unused=nullptr;
  if(!ClothBoneHold(keys)||!ClothInvoke(ctor,keys,nullptr,unused))return false;
  for(int n=0;n<16;++n){float t=n/15.f;expected[n]=(std::min)(MingheCoatContactRadius(t),sourceCurve[n]);
    float normalized=expected[n]/.08f;void *args[]{&t,&normalized},*box=nullptr;int index=-1;
    if(!ClothInvoke(add,keys,args,box)||!box)return false;
    memcpy(&index,(char*)box+16,sizeof(index));if(index!=n)return false;
  }
  int count=0;
  if(!ClothValue(length,keys,count)||count!=16||
      !SurfaceCloneField(data,original,"radius",ct)||!ClothField(data,"radius",ct,copy)||!copy||copy==source||
      !SurfaceReference(copy,"curve","UnityEngine.AnimationCurve",keys)||
      !SurfaceScalar(copy,"value","System.Single",.08f)||!SurfaceScalar(copy,"useCurve","System.Boolean",true)||
      !ClothMingheRadiusFrom(data,actual)||!ClothMingheRadiusSame(actual,expected)||
      !ClothMingheRadiusFrom(original,after)||!ClothMingheRadiusSame(sourceCurve,after)||
      !ClothField(original,"radius",ct,check)||check!=source||
      !ClothField(source,"value","System.Single",afterValue)||value!=afterValue||
      !ClothField(source,"useCurve","System.Boolean",afterUse)||use!=afterUse||
      !ClothField(source,"curve","UnityEngine.AnimationCurve",check)||check!=oldKeys)return false;
  s.mingheRadiusSource=ClothBoneHold(source);s.mingheRadiusCandidate=ClothBoneHold(copy);
  if(!s.mingheRadiusSource||!s.mingheRadiusCandidate)return false;
  memcpy(s.mingheRadiusOriginal,sourceCurve,sizeof(sourceCurve));memcpy(s.mingheRadiusExpected,expected,sizeof(expected));
  s.mingheRadiusConfigured=true;
  Log("[CLOTH-MINGHE] stage=contact-envelope-configured upperRadius=%g/%g lowerRadius=%g/%g basis=PMX-row-thickness sourceCurveUntouched=1 bodyAndTailCollidersRetained=1 TeamReadbackPending=1 visualVerified=0",
      expected[0],sourceCurve[0],expected[15],sourceCurve[15]);
  return true;
}
static bool ClothBoneMingheRadiusMatches(const ClothBoneRuntime &s,int slot,void *box) {
  if(!s.profile||!eiem_cloth_asset::SourceMingheCoatSelection(*s.profile))return !s.mingheRadiusConfigured;
  if(slot<0||slot>2)return false;
  if(!s.mingheRadiusConfigured)return slot!=1;
  if(slot==1&&!ClothOwns(s.owner))return false;
  const auto data=CollisionGc(slot==1?s.candidateData:s.data);
  const auto expectedObject=CollisionGc(slot==1?s.mingheRadiusCandidate:s.mingheRadiusSource);
  void *object=nullptr;float actual[16]{};
  return expectedObject&&ClothField(data,"radius","BeyondDynamicBone.CurveSerializeData",object)&&object==expectedObject&&
      ClothMingheRadiusRead(box,actual)&&ClothMingheRadiusSame(actual,slot==1?s.mingheRadiusExpected:s.mingheRadiusOriginal);
}
