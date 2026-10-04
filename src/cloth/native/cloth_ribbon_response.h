#pragma once
#include "math/cloth_attachment.h"

static bool ClothAttachmentLeased(void *bbc);
static bool ClothRestoreRibbonResponse(ClothInstance &i) {
  if(!i.ribbonAdjusted)return true;
  auto data=CollisionGc(i.ribbonData),original=CollisionGc(i.ribbonOriginal),copy=CollisionGc(i.ribbonCopy);
  void *current=nullptr;
  constexpr auto type="BeyondDynamicBone.CurveSerializeData";
  if(!data||!original||!copy||!ClothField(data,"damping",type,current))return false;
  if(current==copy&&!SurfaceReference(data,"damping",type,original))return false;
  auto bbc=ClothTarget(i.ref);void *live=nullptr;
  if(bbc&&(!ClothInvoke(i.api.serialize,bbc,nullptr,live)||
      (live==data&&!SurfaceCall(bbc,"SetParameterChange"))))return false;
  i.ribbonAdjusted=false;i.ribbonChecked=false;
  Log("[CLOTH-RIBBON-RESPONSE] component=%s restored=1 originalCurveRetained=1",i.name);
  return true;
}
static void ClothFreeRibbonResponse(ClothInstance &i) {
  for(auto h:{&i.ribbonData,&i.ribbonOriginal,&i.ribbonCopy}){if(*h)il2cpp_gchandle_free(*h);*h=0;}
}
static bool ClothRibbonResponse(ClothInstance &i,bool wanted) {
  if(!wanted)return ClothRestoreRibbonResponse(i);
  if(i.ribbonAdjusted)return true;
  if(i.ribbonChecked)return true;
  auto bbc=ClothTarget(i.ref);void *data=nullptr,*curve=nullptr,*copy=nullptr;
  constexpr auto type="BeyondDynamicBone.CurveSerializeData";
  if(!bbc||ClothAttachmentLeased(bbc))return true;
  i.ribbonChecked=true;
  float value=0;
  if(!ClothInvoke(i.api.serialize,bbc,nullptr,data)||!data||
      !ClothField(data,"damping",type,curve)||!curve||!ClothField(curve,"value","System.Single",value))return true;
  const float adjusted=cloth_turn::RibbonDamping(value);
  if(!std::isfinite(value)||adjusted==value)return true;
  if(!ClothInvoke(SurfaceMethod(il2cpp_object_get_class(curve),"Clone",type),curve,nullptr,copy)||!copy||copy==curve||
      !SurfaceScalar(copy,"value","System.Single",adjusted))return true;
  ClothFreeRibbonResponse(i);
  i.ribbonData=il2cpp_gchandle_new(data,false);i.ribbonOriginal=il2cpp_gchandle_new(curve,false);i.ribbonCopy=il2cpp_gchandle_new(copy,false);
  if(!i.ribbonData||!i.ribbonOriginal||!i.ribbonCopy){ClothFreeRibbonResponse(i);return true;}
  i.ribbonAdjusted=true; // Capture before any write, including a failed write.
  if(!SurfaceReference(data,"damping",type,copy)||!SurfaceCall(bbc,"SetParameterChange"))return ClothRestoreRibbonResponse(i);
  Log("[CLOTH-RIBBON-RESPONSE] component=%s damping=%g->%g originalCurveRetained=1 sourceSkinRetained=1 visualVerified=0",i.name,value,adjusted);
  return true;
}
