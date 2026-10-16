#pragma once
#include "cloth_input_boundary.h"
#include "cloth_native_memory.h"
#include <iterator>

namespace poser_cloth_boundary {
// Version 1.6.3: managed entry signatures are still bound by reflection.
// These ranges also cover cold code and the synchronous setup producers.
// This profile does not certify optional injected solver/Burst jobs.
struct NativeMethodProfile { const char *type,*name; size_t rva; const CodeRange *ranges; size_t count; };
inline constexpr CodeRange ElasticProducerRanges163[]{
  {0,617,0x328cb49bf4e95ae4ULL},{0x189e156,8,0x501406cca23f3812ULL}};
inline constexpr CodeRange ElasticConvertRanges163[]{
  {0,42,0x15128fda4949631aULL},{0x189dae6,21,0xf21c7e465b5305d5ULL}};
inline constexpr NativeMethodProfile ElasticProducer163{"ClothSerializeData","GetClothParameters",0x5d79140,
    ElasticProducerRanges163,std::size(ElasticProducerRanges163)};
inline constexpr NativeMethodProfile ElasticConvert163{"TetherConstraintParams","Convert",0x5d797c0,
    ElasticConvertRanges163,std::size(ElasticConvertRanges163)};
inline constexpr size_t ElasticCall163=0x17b;
inline constexpr CodeRange Native163_0[]{
  {0x0,84,0x4c13a4bce9e33352ULL},
  {0x1667040,25,0xc59bf334455497b6ULL},
};
inline constexpr CodeRange Native163_1[]{
  {0x0,62,0x155a649675b9c2c7ULL},
};
inline constexpr CodeRange Native163_2[]{
  {0x0,990,0xdb8c3d5124f4a71ULL},
};
inline constexpr CodeRange Native163_3[]{
  {0x0,1582,0x39a56060f9c8002ULL},
  {0x189d746,1727,0x93ab43846ae6292aULL},
};
inline constexpr CodeRange Native163_4[]{
  {0x0,9268,0x80010bb386afde1fULL},
};
inline constexpr CodeRange Native163_5[]{
  {0x0,10780,0x656ece73456e058ULL},
};
inline constexpr CodeRange Native163_6[]{
  {0x0,248,0xbdbfb715c3c51bfdULL},
};
inline constexpr CodeRange Native163_7[]{
  {0x0,334,0x3a757a0e838bf80ULL},
};
inline constexpr CodeRange Native163_8[]{
  {0x0,57,0xd75b60475e4b28f5ULL},
  {0x39,167,0xab067d4d91f74aa1ULL},
  {0xe0,16,0xac6d20a5ece762d8ULL},
  {0xf0,6,0x5114f08aebaf8c7bULL},
  {0xf6,64,0x632892e5a01b1efbULL},
};
inline constexpr CodeRange Native163_9[]{
  {0x0,146,0xa41d501e86229f3aULL},
};
inline constexpr CodeRange Native163_10[]{
  {0x0,7026,0x7961e0056c450dceULL},
};
inline constexpr CodeRange Native163_11[]{
  {0x0,382,0xc8f4d1cbdf56ca97ULL},
  {0x14463d4,58,0xd4445e750ab06aacULL},
};
inline constexpr CodeRange Native163_12[]{
  {0x0,11313,0x9a2c6835b3d8479ULL},
};
inline constexpr CodeRange Native163_13[]{
  {0x0,139,0x98e1d00c9a403ef9ULL},
};
inline constexpr CodeRange Native163_14[]{
  {0x0,38,0xe8bc29a99f81dafbULL},
  {0x26,205,0x4383b27fd885b741ULL},
  {0xf3,24,0xbddecd858580cb5aULL},
  {0xc92400,10,0x453caf1c6f9077caULL},
};
inline constexpr CodeRange Native163_15[]{
  {0x0,22,0x2f6f727f9e6fb2deULL},
  {0x16,116,0x655e467e966a9b35ULL},
  {0x8a,72,0xe3f689bfb078cc00ULL},
  {0xd2,6,0x313e301c2ee39176ULL},
  {0x1af0d70,493,0xdb394cdce3d4e933ULL},
};
inline constexpr CodeRange Native163_16[]{
  {0x0,353,0x4e9372ffbfd497ULL},
};
inline constexpr CodeRange Native163_17[]{
  {0x0,775,0xd50cb0a8ccd50e01ULL},
};
inline constexpr CodeRange Native163_18[]{
  {0x0,2885,0x96cabff8b3197185ULL},
};
inline constexpr CodeRange Native163_19[]{
  {0x0,837,0x49639f94ccc81e38ULL},
};
inline constexpr CodeRange Native163_20[]{
  {0x0,224,0x73ad1797efb700c5ULL},
  {0x1937bb8,23,0xbd74e61b72f1775ULL},
};
inline constexpr CodeRange Native163_21[]{
  {0x0,65,0x7efbfc93a247932bULL},
};
inline constexpr CodeRange Native163_22[]{
  {0x0,12,0x934a3fc2f8136c18ULL},
};
inline constexpr CodeRange Native163_23[]{
  {0x0,231,0x951aa420dd3f846aULL},
};
inline constexpr CodeRange Native163_24[]{
  {0x0,112,0x4aa08342604ed212ULL},
  {0xcf0e28,34,0xb084f250bebdaa8ULL},
};
inline constexpr CodeRange Native163_25[]{
  {0x0,99,0x94f66291aa2753c8ULL},
  {0x63,60,0x5440fa54f78639fcULL},
  {0x9f,35,0x7ca615b7170bca41ULL},
  {0xc2,24,0x2c26b58d09e1d5c4ULL},
  {0xda,68,0xdd393359c08b36deULL},
  {0x11e,20,0xf5e4758c639dbe56ULL},
  {0x132,176,0x3c08ada600602704ULL},
  {0x1e2,24,0xfe7929d07c43b418ULL},
  {0x200a56e,17,0x4269164ebe0613ffULL},
};
inline constexpr CodeRange Native163_26[]{
  {0x0,57,0x2ca7d162a33dcf2cULL},
  {0x39,112,0x7226b1471cdf2806ULL},
  {0xa9,80,0x3587a5968a3b5255ULL},
  {0xf9,67,0x8358d817433354ebULL},
};
inline constexpr NativeMethodProfile RequiredMethods163[]{
  {"BeyondBoneCloth","ResetCloth",0x89d136c,Native163_23,std::size(Native163_23)},
  {"BeyondBoneCloth","DisposeTeleportResources",0x5ff13c0,Native163_0,std::size(Native163_0)},
  {"BeyondBoneCloth","Initialize",0x613ce70,Native163_1,std::size(Native163_1)},
  {"BeyondBoneCloth","BuildAndRun",0x89cf140,Native163_2,std::size(Native163_2)},
  {"ClothProcess","Init",0x5d79c10,Native163_3,std::size(Native163_3)},
  {"ClothProcess","PreBuildDataConstruction",0x61837b0,Native163_4,std::size(Native163_4)},
  {"ClothProcess","DisposeInternal",0x5ff2950,Native163_5,std::size(Native163_5)},
  {"ClothProcess","GenerateInitialization",0x89d21e0,Native163_6,std::size(Native163_6)},
  {"ClothManager","RemoveCloth",0x5ff1590,Native163_7,std::size(Native163_7)},
  {"PreBuildManager","RegisterPreBuildData",0x6183670,Native163_8,std::size(Native163_8)},
  {"PreBuildManager","UnregisterPreBuildData",0x61832e0,Native163_9,std::size(Native163_9)},
  {"RenderSetupData","ReadTransformInformation",0x5b17cf0,Native163_10,std::size(Native163_10)},
  {"TransformData","ShareDeserialize",0x6262490,Native163_11,std::size(Native163_11)},
  {"VirtualMesh","ImportBoneType",0x54541e0,Native163_12,std::size(Native163_12)},
};
inline constexpr NativeMethodProfile RequiredSupport163[]{
  {"support","BeyondDynamicBone.BeyondBoneCloth.IsValid",0x6a713f0,Native163_24,std::size(Native163_24)},
  {"support","BeyondDynamicBone.TeamManager.GetTeamDataRef",0x54d7710,Native163_25,std::size(Native163_25)},
  {"support","BeyondDynamicBone.ClothProcess.UpdateRendererUse",0x6c183f0,Native163_26,std::size(Native163_26)},
  {"support","BeyondDynamicBone.BeyondBoneCloth.get_Process",0x54d81c0,Native163_13,std::size(Native163_13)},
  {"support","BeyondDynamicBone.ClothProcess.CreateBoneRenderSetupData",0x6ad9960,Native163_14,std::size(Native163_14)},
  {"support","BeyondDynamicBone.ClothProcess.StartRuntimeBuild",0x5ae18a0,Native163_15,std::size(Native163_15)},
  {"support","BeyondDynamicBone.ClothProcess.Dispose",0x5ff1420,Native163_16,std::size(Native163_16)},
  {"support",".ReadTransformJob.Execute",0x8a44eb4,Native163_17,std::size(Native163_17)},
  {"support","BeyondDynamicBone.RenderSetupData..ctor",0x5b14370,Native163_18,std::size(Native163_18)},
  {"support",".Import_BoneVertexJob.Execute",0x59dfbe0,Native163_19,std::size(Native163_19)},
  {"support","ImportBoneVertex-Run",0x5a03e80,Native163_20,std::size(Native163_20)},
  {"support","TeleportList-Dispose",0xb254e6c,Native163_21,std::size(Native163_21)},
  {"support","TeleportList-IsCreated",0x61d53f0,Native163_22,std::size(Native163_22)},
};
inline const NativeMethodProfile *FindRequiredMethod163(const char *type,const char *name) {
  if(!type||!name)return nullptr;
  for(const auto &profile:RequiredMethods163)
    if(!std::strcmp(type,profile.type)&&!std::strcmp(name,profile.name))return &profile;
  return nullptr;
}
inline bool MappedMethodProfile(void *module,const unsigned char *code,const NativeMethodProfile &profile) {
  if(!module||!code||!profile.ranges||!profile.count||profile.rva>UINTPTR_MAX-uintptr_t(module)||
      uintptr_t(code)!=uintptr_t(module)+profile.rva)return false;
  for(size_t n=0;n<profile.count;++n)if(!MappedCodeRange(module,code,profile.ranges[n]))return false;
  return true;
}
}
