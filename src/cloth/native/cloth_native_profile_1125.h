#pragma once
#include "cloth_native_profile.h"

namespace poser_cloth_boundary {
// Installed November build: direct ValidPosition boundary after completed input jobs.
// Required lifecycle, synchronous setup and elastic conversion audited separately
// from optional native solver adapters. Keep legacy profiles unchanged.
inline constexpr CodeRange Update1125[]{
  {0x0,3582,0xe60584f8b4ac041aULL},
};
inline constexpr CodeRange Read1125[]{
  {0x0,331,0xb086573146c036dULL},
  {0x14b,187,0xc58849500ece304fULL},
  {0x206,69,0x3d1cef1a8bb14ce0ULL},
  {0x24b,13,0xf416a3b3877cca45ULL},
  {0x258,10,0x938695c9e582b6c9ULL},
  {0x1e975d8,52,0xf31787f7f16af56aULL},
};
inline constexpr CodeRange Write1125[]{
  {0x0,262,0x6a53c188c8215811ULL},
  {0x106,172,0xb58707af01302fd7ULL},
  {0x1b2,57,0xad09bd3413063860ULL},
  {0x1eb,13,0xb069590adebda63cULL},
  {0x1f8,10,0xde27e87f28a19c32ULL},
  {0x15d61d2,52,0x13ae58e4531ce072ULL},
};
inline constexpr CodeRange Valid1125[]{
  {0x0,693,0x17a85bd09638f4d4ULL},
  {0x1587410,223,0xcf821b78ff845041ULL},
};
inline constexpr CodeRange TeamUpdate1125[]{
  {0x0,12212,0x88dfcd84c5148218ULL},
};
inline constexpr CodeRange GetTeam1125[]{
  {0x0,100,0xbee4fc3497435857ULL},
  {0x64,173,0x23354f40ce77bbebULL},
  {0x111,4,0xe02f80043c75c399ULL},
};
inline constexpr CodeRange GetBone1125[]{
  {0x0,74,0x1767036c6d16822aULL},
  {0x4a,173,0x66bf0d2e744593cULL},
  {0xf7,40,0xfd877ef7955f56e0ULL},
  {0x1f9092e,7,0xc125f0660715c582ULL},
};
inline constexpr size_t BeforeTeam1125=0x1df, CompletedValid1125=0x4f1;
inline constexpr bool RequiredChain1125Certified=true;
inline constexpr CodeRange Native1125_0[]{
  {0x0,231,0x2ae905c7fe30fee0ULL},
};
inline constexpr CodeRange Native1125_1[]{
  {0x0,84,0x3833585de26e00cULL},
  {0x14f6bfa,25,0xc1aa17bdde70c804ULL},
};
inline constexpr CodeRange Native1125_2[]{
  {0x0,62,0x859a5734445eeb7fULL},
};
inline constexpr CodeRange Native1125_3[]{
  {0x0,990,0xcab9af4bf2a83303ULL},
};
inline constexpr CodeRange Native1125_4[]{
  {0x0,1564,0x416ee62276620e0bULL},
  {0x17ca39e,1727,0xdd39834319b5d61bULL},
};
inline constexpr CodeRange Native1125_5[]{
  {0x0,9268,0x2b6267b6b13dc0abULL},
};
inline constexpr CodeRange Native1125_6[]{
  {0x0,10788,0x71161362706a2362ULL},
};
inline constexpr CodeRange Native1125_7[]{
  {0x0,248,0x2c07c63f2370351dULL},
};
inline constexpr CodeRange Native1125_8[]{
  {0x0,334,0xb26d520ffd65fd97ULL},
};
inline constexpr CodeRange Native1125_9[]{
  {0x0,57,0x6488b701473ea1ddULL},
  {0x39,167,0x5fee972c90709e92ULL},
  {0xe0,16,0xac6d20a5ece762d8ULL},
  {0xf0,6,0x31897b1651a72779ULL},
  {0xf6,64,0xb3fc750906cc6286ULL},
};
inline constexpr CodeRange Native1125_10[]{
  {0x0,146,0x7d77351d73dee7cfULL},
};
inline constexpr CodeRange Native1125_11[]{
  {0x0,7026,0x18d760d6285b8aeaULL},
};
inline constexpr CodeRange Native1125_12[]{
  {0x0,382,0x23e5e5394ff41694ULL},
  {0x1b3c53e,58,0x33ccd69f14fb3b5fULL},
};
inline constexpr CodeRange Native1125_13[]{
  {0x0,11313,0x76e8d208e3c3aac1ULL},
};
inline constexpr CodeRange Native1125_14[]{
  {0x0,112,0xf7e05db87c014ad7ULL},
  {0x102ba0a,34,0xf801bbc34e30b8eULL},
};
inline constexpr CodeRange Native1125_15[]{
  {0x0,95,0xa857132802ff6295ULL},
  {0x5f,56,0x7fe0d9452db85dfdULL},
  {0x97,35,0x7ca615b7170bca41ULL},
  {0xba,24,0x5229428ef36e7a04ULL},
  {0xd2,229,0x13533fdf729559f1ULL},
  {0x1b7,20,0x25ebce500d013428ULL},
  {0x1cb,40,0x7558a9a34e4aa684ULL},
  {0x1f3,24,0x3a45b2c4cf24a479ULL},
  {0x1e9b722,17,0x6ef3620fa431ce60ULL},
};
inline constexpr CodeRange Native1125_16[]{
  {0x0,57,0x399e7ae5e5db512aULL},
  {0x39,112,0x6a92f3a089a18904ULL},
  {0xa9,80,0x863c0d29661caa69ULL},
  {0xf9,67,0x8d10f1a144a66eacULL},
};
inline constexpr CodeRange Native1125_17[]{
  {0x0,139,0xcec9fdccff36443ULL},
};
inline constexpr CodeRange Native1125_18[]{
  {0x0,38,0xfb246525cc7b1a2aULL},
  {0x26,205,0xc2048a2cc442d362ULL},
  {0xf3,24,0x14f218ae0fe14f15ULL},
  {0x1b3af98,10,0xe0190fae681f24dbULL},
};
inline constexpr CodeRange Native1125_19[]{
  {0x0,22,0x6193766c808f3f29ULL},
  {0x16,116,0xef21f8adad4d35e2ULL},
  {0x8a,72,0x76c09182fdfe5360ULL},
  {0xd2,6,0xec734bb85bc2e41aULL},
  {0x148a72e,493,0xf22c85600b66492bULL},
};
inline constexpr CodeRange Native1125_20[]{
  {0x0,385,0x79f659391fef05beULL},
};
inline constexpr CodeRange Native1125_21[]{
  {0x0,775,0x2511a371684bcf24ULL},
};
inline constexpr CodeRange Native1125_22[]{
  {0x0,2844,0xa75a8314ebd63671ULL},
};
inline constexpr CodeRange Native1125_23[]{
  {0x0,837,0x69b927b83f1419c1ULL},
};
inline constexpr CodeRange Native1125_24[]{
  {0x0,224,0x71d68b1527768dadULL},
  {0x173c406,23,0xe4b020c6dbd3f7beULL},
};
inline constexpr CodeRange Native1125_25[]{
  {0x0,65,0x48f96708d09f78a6ULL},
};
inline constexpr CodeRange Native1125_26[]{
  {0x0,12,0x934a3fc2f8136c18ULL},
};
inline constexpr NativeMethodProfile RequiredMethods1125[]{
  {"BeyondBoneCloth","ResetCloth",0x76e7d58,Native1125_0,std::size(Native1125_0)},
  {"BeyondBoneCloth","DisposeTeleportResources",0x508fdc0,Native1125_1,std::size(Native1125_1)},
  {"BeyondBoneCloth","Initialize",0x5f97bb0,Native1125_2,std::size(Native1125_2)},
  {"BeyondBoneCloth","BuildAndRun",0x76e5b2c,Native1125_3,std::size(Native1125_3)},
  {"ClothProcess","Init",0x4d58290,Native1125_4,std::size(Native1125_4)},
  {"ClothProcess","PreBuildDataConstruction",0x5106e70,Native1125_5,std::size(Native1125_5)},
  {"ClothProcess","DisposeInternal",0x5091370,Native1125_6,std::size(Native1125_6)},
  {"ClothProcess","GenerateInitialization",0x76e8bcc,Native1125_7,std::size(Native1125_7)},
  {"ClothManager","RemoveCloth",0x508ffb0,Native1125_8,std::size(Native1125_8)},
  {"PreBuildManager","RegisterPreBuildData",0x5106d30,Native1125_9,std::size(Native1125_9)},
  {"PreBuildManager","UnregisterPreBuildData",0x51065e0,Native1125_10,std::size(Native1125_10)},
  {"RenderSetupData","ReadTransformInformation",0x41e8cc0,Native1125_11,std::size(Native1125_11)},
  {"TransformData","ShareDeserialize",0x493a1e0,Native1125_12,std::size(Native1125_12)},
  {"VirtualMesh","ImportBoneType",0x4478740,Native1125_13,std::size(Native1125_13)},
};
inline constexpr NativeMethodProfile RequiredSupport1125[]{
  {"support","BeyondDynamicBone.BeyondBoneCloth.IsValid",0x55f1f80,Native1125_14,std::size(Native1125_14)},
  {"support","BeyondDynamicBone.TeamManager.GetTeamDataRef",0x4527460,Native1125_15,std::size(Native1125_15)},
  {"support","BeyondDynamicBone.ClothProcess.UpdateRendererUse",0x5bcff00,Native1125_16,std::size(Native1125_16)},
  {"support","BeyondDynamicBone.BeyondBoneCloth.get_Process",0x4528f80,Native1125_17,std::size(Native1125_17)},
  {"support","BeyondDynamicBone.ClothProcess.CreateBoneRenderSetupData",0x493b7c0,Native1125_18,std::size(Native1125_18)},
  {"support","BeyondDynamicBone.ClothProcess.StartRuntimeBuild",0x5106850,Native1125_19,std::size(Native1125_19)},
  {"support","BeyondDynamicBone.ClothProcess.Dispose",0x508fe20,Native1125_20,std::size(Native1125_20)},
  {"support",".ReadTransformJob.Execute",0x7767dd0,Native1125_21,std::size(Native1125_21)},
  {"support","BeyondDynamicBone.RenderSetupData..ctor",0x493a8e0,Native1125_22,std::size(Native1125_22)},
  {"support",".Import_BoneVertexJob.Execute",0x41e7480,Native1125_23,std::size(Native1125_23)},
  {"support","ImportBoneVertex-Run",0x4ad3590,Native1125_24,std::size(Native1125_24)},
  {"support","TeleportList-Dispose",0xa114284,Native1125_25,std::size(Native1125_25)},
  {"support","TeleportList-IsCreated",0x52b9e60,Native1125_26,std::size(Native1125_26)},
};
inline constexpr CodeRange ElasticProducerRanges1125[]{
  {0x0,617,0xe89c6e3e3ddac378ULL},
  {0x17cb446,8,0x31980453a7f86e2cULL},
};
inline constexpr NativeMethodProfile ElasticProducer1125{"ClothSerializeData","GetClothParameters",0x4d56ad0,ElasticProducerRanges1125,std::size(ElasticProducerRanges1125)};
inline constexpr CodeRange ElasticConvertRanges1125[]{
  {0x0,42,0xfb3838b3a0c9ff12ULL},
  {0x17cadd6,21,0xcfd7b21f2f86c4d5ULL},
};
inline constexpr NativeMethodProfile ElasticConvert1125{"TetherConstraintParams","Convert",0x4d57150,ElasticConvertRanges1125,std::size(ElasticConvertRanges1125)};
inline constexpr size_t ElasticCall1125=0x17b;
inline const NativeMethodProfile *FindRequiredMethod1125(const char *type,const char *name) {
  if(!type||!name)return nullptr;
  for(const auto &profile:RequiredMethods1125)
    if(!std::strcmp(type,profile.type)&&!std::strcmp(name,profile.name))return &profile;
  return nullptr;
}
}
