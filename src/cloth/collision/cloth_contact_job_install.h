#pragma once
#include "../native/cloth_contact_native_profile.h"
static bool ClothContactCodeHash(void *code,size_t bytes,uint64_t &hash) {
  MEMORY_BASIC_INFORMATION region{};hash=0;
  if(!code || !bytes || !VirtualQuery(code,&region,sizeof(region)) || region.AllocationBase!=hGA ||
      region.State!=MEM_COMMIT || (region.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
      uintptr_t(code)<uintptr_t(region.BaseAddress) ||
      bytes>region.RegionSize-(uintptr_t(code)-uintptr_t(region.BaseAddress))) return false;
  hash=eiem_cloth_input::Fingerprint((unsigned char*)code,bytes);return true;
}
static bool ClothContactCode(void *code,size_t bytes,uint64_t hash) {
  uint64_t actual=0;return ClothContactCodeHash(code,bytes,actual) && actual==hash;
}
static bool ClothContactInstallReject(const char *check) {
  _snprintf_s(s_clothContactInstallIssue,_TRUNCATE,"native-contact-adapter-%s",check);
  Log("[CLOTH-BONE-CONTACT-JOB] stage=install-refused check=%s originalUntouched=1",check);return false;
}
static bool ClothContactRequireCode(const char *label,void *code,size_t bytes,uint64_t expected) {
  uint64_t actual=0;const bool readable=ClothContactCodeHash(code,bytes,actual);
  if(readable && actual==expected)return true;
  Log("[CLOTH-BONE-CONTACT-ABI] check=code method=%s code=%p bytes=%zu readable=%d expected=%016llX actual=%016llX",
      label,code,bytes,int(readable),(unsigned long long)expected,(unsigned long long)actual);
  return ClothContactInstallReject(label);
}
static bool ClothContactProfileCode(void *code,poser_cloth_contact_native::Code check) {
  return poser_cloth_contact_native::AddressMatches(uintptr_t(hGA),uintptr_t(code),check) &&
      ClothContactCode(code,check.bytes,check.hash);
}
static bool ClothContactRequireProfile(const char *label,void *code,poser_cloth_contact_native::Code check) {
  if(!poser_cloth_contact_native::AddressMatches(uintptr_t(hGA),uintptr_t(code),check))
    return ClothContactInstallReject(label);
  return ClothContactRequireCode(label,code,check.bytes,check.hash);
}
static bool ClothContactSize(void *cls,int expected) {
  uint32_t align=0;const int actual=cls?il2cpp_class_value_size(cls,&align):-1;
  if(actual==expected)return true;
  Log("[CLOTH-BONE-CONTACT-ABI] check=value-size type=%s expected=%d actual=%d alignment=%u",
      cls?il2cpp_class_get_name(cls):"missing",expected,actual,align);
  return ClothContactInstallReject("value-size-mismatch");
}
static bool ClothContactOffset(void *cls,const char *name,const char *type,int stride,int bytes,int expected) {
  auto field=cls?CollisionFieldInfo(cls,name,type):nullptr;
  const size_t raw=field?il2cpp_field_get_offset(field):SIZE_MAX;
  const int flags=field&&il2cpp_field_get_flags?il2cpp_field_get_flags(field):0x10;
  if(field && !(flags&0x10) && raw>=16 && raw<=size_t(stride+16-bytes) && raw-16==size_t(expected))return true;
  Log("[CLOTH-BONE-CONTACT-ABI] check=field type=%s field=%s expectedType=%s expectedOffset=%d boxedOffset=%zu flags=%X",
      cls?il2cpp_class_get_name(cls):"missing",name,type,expected,raw,flags);
  if(cls)for(void *it=nullptr,*f=nullptr;(f=il2cpp_class_get_fields(cls,&it));) {
    if(strcmp(il2cpp_field_get_name(f),name))continue;
    auto actual=il2cpp_type_get_name(il2cpp_field_get_type(f));
    Log("[CLOTH-BONE-CONTACT-ABI] field=%s actualType=%s boxedOffset=%zu",name,actual?actual:"missing",il2cpp_field_get_offset(f));
    if(actual)s_clothFreeName((void*)actual);
  }
  return ClothContactInstallReject(name);
}
static void *ClothContactUniqueCall(unsigned char *code,size_t bytes,size_t targetBytes,uint64_t hash,
                                   const char *label,void **returnAddress=nullptr) {
  void *found=nullptr,*site=nullptr;unsigned matches=0;
  for(size_t n=0;n+5<=bytes;++n) {
    if(code[n]!=0xe8)continue;int32_t relative=0;memcpy(&relative,code+n+1,4);
    auto target=(unsigned char*)(uintptr_t(code+n+5)+intptr_t(relative));
    if(!ClothContactCode(target,targetBytes,hash))continue;
    found=target;site=code+n+5;++matches;
  }
  if(matches!=1) {
    Log("[CLOTH-BONE-CONTACT-ABI] check=direct-call method=%s matches=%u",label,matches);
    ClothContactInstallReject(label);return nullptr;
  }
  if(returnAddress)*returnAddress=site;return found;
}
static void *ClothContactValueType(const char *name,const char *full,void **asms,size_t count) {
  auto parent=FindClass("BeyondDynamicBone","SelfCollisionConstraint",asms,count);
  if(!parent || !il2cpp_class_get_nested_types) {ClothContactInstallReject("declaring-type-unavailable");return nullptr;}
  void *found=nullptr;
  for(void *it=nullptr,*cls=nullptr;(cls=il2cpp_class_get_nested_types(parent,&it));) {
      if(!cls || strcmp(il2cpp_class_get_name(cls),name))continue;
      if(!CollisionType(il2cpp_class_get_type(cls),full)) {
        auto actual=il2cpp_type_get_name(il2cpp_class_get_type(cls));
        Log("[CLOTH-BONE-CONTACT-ABI] check=declaring-type expected=%s actual=%s",full,actual?actual:"missing");
        if(actual)s_clothFreeName((void*)actual);continue;
      }
      if(found){ClothContactInstallReject("ambiguous-declaring-type");return nullptr;}found=cls;
  }
  if(!found){Log("[CLOTH-BONE-CONTACT-ABI] check=type-not-found expected=%s",full);ClothContactInstallReject(name);}
  return found;
}
static bool ClothContactJobLayout(int kind,void **asms,size_t count,
    const poser_cloth_contact_native::Profile &profile) {
  const char *name=kind?"UpdatePointTriangleBroadPhaseCrossFrameJob":"UpdateEdgeEdgeBroadPhaseCrossFrameJob";
  const char *full=kind?"BeyondDynamicBone.SelfCollisionConstraint.UpdatePointTriangleBroadPhaseCrossFrameJob":
      "BeyondDynamicBone.SelfCollisionConstraint.UpdateEdgeEdgeBroadPhaseCrossFrameJob";
  const char *listName=kind?"pointTriangleContactList":"edgeEdgeContactList";
  const char *listType=kind?"Unity.Collections.NativeList<BeyondDynamicBone.SelfCollisionConstraint.PointTriangleContact>":
      "Unity.Collections.NativeList<BeyondDynamicBone.SelfCollisionConstraint.EdgeEdgeContact>";
  const char *pointerType=kind?"Unity.Collections.LowLevel.Unsafe.UnsafeList<BeyondDynamicBone.SelfCollisionConstraint.PointTriangleContact>*":
      "Unity.Collections.LowLevel.Unsafe.UnsafeList<BeyondDynamicBone.SelfCollisionConstraint.EdgeEdgeContact>*";
  auto cls=ClothContactValueType(name,full,asms,count);
  if(!cls || !ClothContactSize(cls,64) ||
      !ClothContactOffset(cls,"indexCount","Unity.Collections.NativeReference<System.Int32>",64,16,0) ||
      !ClothContactOffset(cls,"nextPosArray","Unity.Collections.NativeArray<Unity.Mathematics.double3>",64,16,16) ||
      !ClothContactOffset(cls,"oldPosArray","Unity.Collections.NativeArray<Unity.Mathematics.double3>",64,16,32) ||
      !ClothContactOffset(cls,listName,listType,64,16,48)) return false;
  auto listField=CollisionFieldInfo(cls,listName,listType);
  auto refField=CollisionFieldInfo(cls,"indexCount","Unity.Collections.NativeReference<System.Int32>");
  auto list=il2cpp_class_from_type(il2cpp_field_get_type(listField));
  auto ref=il2cpp_class_from_type(il2cpp_field_get_type(refField));
  if(!ClothContactSize(list,16) || !ClothContactSize(ref,16) ||
      !ClothContactOffset(list,"m_ListData",pointerType,16,8,0) ||
      !ClothContactOffset(ref,"m_Data","System.Void*",16,8,0) ||
      !ClothContactOffset(ref,"m_AllocatorLabel","Unity.Collections.AllocatorManager.AllocatorHandle",16,4,8)) return false;
  auto execute=SurfaceMethod(cls,"Execute","System.Void");
  if(!ClothContactRequireProfile(name,execute?((MInfo*)execute)->mp:nullptr,profile.execute[kind]))return false;
  const char *queueName=kind?"PointTriangleToListJob":"EdgeEdgeToListJob";
  const char *queueFull=kind?"BeyondDynamicBone.SelfCollisionConstraint.PointTriangleToListJob":
      "BeyondDynamicBone.SelfCollisionConstraint.EdgeEdgeToListJob";
  auto queue=ClothContactValueType(queueName,queueFull,asms,count);
  if(!queue || !ClothContactSize(queue,40) || !ClothContactOffset(queue,listName,listType,40,16,24))return false;
  auto queueField=CollisionFieldInfo(queue,listName,listType);
  if(il2cpp_class_from_type(il2cpp_field_get_type(queueField))!=list)return ClothContactInstallReject("queue-list-type-identity");
  auto toList=SurfaceMethod(queue,"Execute","System.Void");
  auto code=toList?(unsigned char*)((MInfo*)toList)->mp:nullptr;
  if(!ClothContactRequireProfile(queueName,code,profile.queue[kind]))return false;
  auto clear=ClothContactUniqueCall(code,profile.queue[kind].bytes,profile.clear.bytes,
      profile.clear.hash,"queue-list-Clear");
  return clear && ClothContactRequireProfile("queue-list-Clear-address",clear,profile.clear);
}
struct ClothContactInstallState {
  bool attempted=false;
  const poser_cloth_contact_native::Profile *profile=nullptr;
  unsigned char *updateCode=nullptr,*targets[2]{};
  uint64_t installedHash[2]{};
} static s_clothContactInstall;

static double ClothInstallClockMs() {
  LARGE_INTEGER now{},frequency{};QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
  return frequency.QuadPart?double(now.QuadPart)*1000.0/double(frequency.QuadPart):0;
}
static bool ClothEnableHookGroup(unsigned char **targets,unsigned count) {
  for(unsigned n=0;n<count;++n)if(MH_QueueEnableHook(targets[n])!=MH_OK)return false;
  return MH_ApplyQueued()==MH_OK;
}

static bool ClothInstallContactJobsMainThread() {
  auto &s=s_clothContactInstall;
  auto &updateCode=s.updateCode;auto &targets=s.targets;auto &installedHash=s.installedHash;
  if(!ClothOnMainThread() || !s_clothSurfaceAtBoundary || s_clothInputUpdateDepth!=1 || !s_clothInputHooks) return false;
  if(s_clothContactJobs.installed) {
    const bool valid=s.profile && ClothContactProfileCode(updateCode,s.profile->update) &&
        ClothContactCode(targets[0],s.profile->schedule[0].bytes,installedHash[0]) &&
        ClothContactCode(targets[1],s.profile->schedule[1].bytes,installedHash[1]);
    return valid?true:ClothContactInstallReject("installed-code-changed");
  }
  if(s.attempted)return false;s.attempted=true;s_clothContactInstallIssue[0]=0;
  const double started=ClothInstallClockMs();
  size_t count=0;auto asms=il2cpp_domain_get_assemblies(il2cpp_domain_get(),&count);
  auto cls=FindClass("BeyondDynamicBone","SelfCollisionConstraint",asms,count);
  auto method=SurfaceMethod(cls,"UpdateBroadPhase","Unity.Jobs.JobHandle","Unity.Jobs.JobHandle");
  auto untyped=FindClass("Unity.Collections.LowLevel.Unsafe","UntypedUnsafeList",asms,count);
  updateCode=method?(unsigned char*)((MInfo*)method)->mp:nullptr;
  for(auto profile:{&poser_cloth_contact_native::Legacy,&poser_cloth_contact_native::Native163})
    if(ClothContactProfileCode(updateCode,profile->update)) {s.profile=profile;break;}
  if(!s.profile)return ClothContactInstallReject("UpdateBroadPhase-profile");
  const auto &profile=*s.profile;
  if(!ClothContactSize(untyped,32) ||
      !ClothContactOffset(untyped,"m_length","System.Int32",32,4,8) ||
      !ClothContactOffset(untyped,"Ptr","System.Void*",32,8,0) ||
      !ClothContactOffset(untyped,"m_capacity","System.Int32",32,4,12) ||
      !ClothContactRequireProfile("UpdateBroadPhase",updateCode,profile.update) ||
      !ClothContactJobLayout(0,asms,count,profile) || !ClothContactJobLayout(1,asms,count,profile))return false;
  for(const auto &support:profile.support)if(support.bytes &&
      !ClothContactRequireProfile("schedule-support",(unsigned char*)hGA+support.rva,support))return false;
  for(int kind=0;kind<2;++kind) {
    const auto &schedule=profile.schedule[kind];
    targets[kind]=(unsigned char*)ClothContactUniqueCall(updateCode,profile.update.bytes,schedule.bytes,schedule.hash,
        kind?"PointTriangle-schedule":"EdgeEdge-schedule",&s_clothContactJobs.callsites[kind]);
    if(!targets[kind] || !ClothContactRequireProfile("schedule-address",targets[kind],schedule))return false;
    if(profile.scheduleCall[kind] && s_clothContactJobs.callsites[kind]!=updateCode+profile.scheduleCall[kind]+5)
      return ClothContactInstallReject("schedule-callsite");
  }
  if(targets[0]==targets[1])return ClothContactInstallReject("schedule-target-identity");
  const double validated=ClothInstallClockMs();
  void *detours[]{(void*)ClothContactScheduleEdge,(void*)ClothContactSchedulePoint};
  bool created[2]{};bool ok=true;
  for(int n=0;n<2&&ok;++n) {
    const auto status=MH_CreateHook(targets[n],detours[n],(void**)&s_clothContactJobs.original[n]);
    created[n]=status==MH_OK;ok=created[n];
    if(!ok)Log("[CLOTH-BONE-CONTACT-ABI] check=create-hook kind=%d status=%d",n,int(status));
  }
  const double createdAt=ClothInstallClockMs();
  if(ok)ok=ClothEnableHookGroup(targets,2);
  if(!ok) {
    for(int n=0;n<2;++n)if(created[n]){MH_DisableHook(targets[n]);MH_RemoveHook(targets[n]);}
    s_clothContactJobs={};return ClothContactInstallReject("hook-install-failed");
  }
  for(int n=0;n<2;++n)installedHash[n]=eiem_cloth_input::Fingerprint(targets[n],profile.schedule[n].bytes);
  s_clothContactJobs.lengthOffset=8;s_clothContactJobs.installed=true;
  Log("[CLOTH-INSTALL-COST] adapter=contact validateMs=%g createMs=%g enableMs=%g enableBatches=1",
      validated-started,createdAt-validated,ClothInstallClockMs()-createdAt);
  Log("[CLOTH-BONE-CONTACT-JOB] stage=installed profile=%s jobs=2 scope=leased-contact-list-and-exact-callsite counter=live-native-list-length clearProof=native-queue-call noWorkerHook=1 nativeArrayWrites=0 dependencyWrites=0",profile.name);
  return true;
}
