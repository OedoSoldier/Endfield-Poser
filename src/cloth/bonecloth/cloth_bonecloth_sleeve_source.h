#pragma once
#include <cstring>
#include "cloth_bonecloth_profile.h"
namespace eiem_cloth_asset {
inline constexpr const char *JsspsiClothPrefab="e35214f71bf8886bf2d1ffe31d65e38893d5de2f866f068443b0de0685377f96";
inline bool JsspsiSleeveName(const char *name){return name&&(!strcmp(name,"MC_jsspsi_sleeve_L")||!strcmp(name,"MC_jsspsi_sleeve_R"));}
inline bool JsspsiSleeveSource(const ClothBoneProfile &p){return p.runtimeGenerated&&p.runtimeSleeve&&p.prefabSha&&
    !strcmp(p.prefabSha,JsspsiClothPrefab)&&JsspsiSleeveName(p.component)&&!p.loop&&p.rootCount==1&&p.boneCount==7&&p.depth==7&&
    p.colliderCount==0&&!p.candidateIgnoredCount&&!p.ownershipCount&&!p.inputAnchorCount&&!p.sourceBranchCount;}
}
