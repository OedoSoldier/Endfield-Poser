#pragma once

// SkeletalMorph（SMC）游戏原生表情驱动。
//
// 参照 {EIEM}/src/smc_face.h + globals.h + init.h（Sasye/EIEM, AGPL-3.0）
// 精简移植：hook 游戏的 SkeletalMorphCore.Update / DoEvaluateMorphToBoneJob，
// 动态解析 SMC 字段偏移，从 NativeHashMap + AvatarData.morphMappingNames 把
// morph 名映射到大列表（morph→骨骼增量）区间，再把面板滑条权重换算成
// 骨骼局部位姿增量，在 SMC.Update 之后覆盖写回。经典 BlendShape 路径见 morph.h。
//
// 单一 TU（poser.cpp）设计：全部 static 全局在该 TU 内共享。
// 版本敏感：偏移优先动态解析，失败走 SafeOff 回退（见下方各 0x?? 常量）。

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "core/base.h"
#include "core/il2cpp_api.h"
#include "core/game_hooks.h"

// ---- 常量 ----
#define SMC_MAX_BIGLIST 8192
#define SMC_MAX_FACE_BONES 256
#define SMC_BONE_MAP_SIZE 512
#define SMC_NUM_MOUTH 5
#define SMC_MAX_EXTRA_TARGETS 4

// morph→骨骼增量条目（游戏 SkeletalMorphCore 大列表元素，packed 44 字节）
#pragma pack(push, 1)
struct SMCMorphBoneEntry {
  int32_t boneNameHash;
  int32_t boneID;
  float deltaPosX, deltaPosY, deltaPosZ;
  float deltaRotX, deltaRotY, deltaRotZ;
  float pad[3];
};
#pragma pack(pop)
static_assert(sizeof(SMCMorphBoneEntry) == 44,
              "SMCMorphBoneEntry must be 44 bytes");

// SMC 维护的面部骨骼快照（局部位姿 + transform 引用）
struct SMCFaceBone {
  float px, py, pz;
  float rx, ry, rz, rw;
  void *transform;
};

struct SMCMouthShape {
  const char *name;
  int nameHash;
  int morphId;
  int startIdx;
  int count;
  int jobStartIdx;
  int jobCount;
  bool resolved;
};

struct SMCExtraTarget {
  const char *endfieldName;
  int nameHash;
  int startIdx;
  int count;
  int jobStartIdx;
  int jobCount;
  bool resolved;
};

struct SMCExtraMorph {
  const char *vmdNameUtf8;
  const char *label;
  SMCExtraTarget targets[SMC_MAX_EXTRA_TARGETS];
  int targetCount;
  float weight;
  float prevWeight;
};

// ---- SMC 字段偏移（-1=未解析，读时走 SafeOff 回退）----
static int s_offAllMorphs = -1;
static int s_offBigList = -1; // m_morphNameHashToMorphDataBoneBigList
static int s_offNativeHashMap = -1;
static int s_offMorphBSDirty = -1;
static int s_offAllMorphBoneDirty = -1;
static int s_offAvatarData = -1;
static int s_offAllBonesTransforms = -1;
static int s_offBoneIDToIdx = -1;
static int s_offMorphMappingNames = -1; // AvatarData 上
static int s_offSmcEyeLookAt = -1;

// ---- 运行时状态 ----
static void *s_smcClass = nullptr;
static void *s_smcCore = nullptr;
static void *s_confirmedSMC = nullptr;
static int s_frame = 0;
static volatile bool s_driving = false; // 面板启用 SMC 表情驱动
static bool s_eyeIKDisabled = false;

static SMCMorphBoneEntry s_capturedExpression[SMC_MAX_BIGLIST];
static int s_capturedLen = 0;
static bool s_bigListCaptured = false;

static SMCFaceBone s_faceBones[SMC_MAX_FACE_BONES];
static SMCFaceBone s_faceRestPose[SMC_MAX_FACE_BONES];
static int s_faceBoneCount = 0;
static bool s_faceBonesCaptured = false;
static bool s_faceBoneTouched[SMC_MAX_FACE_BONES] = {};
static void **s_faceBoneRefs = nullptr;

static int s_boneIDToIdx[SMC_BONE_MAP_SIZE];
static int s_boneIDMapCount = 0;
static bool s_boneMapReady = false;

// 口型（A/I/U/E/O）+ 表情表（名称/哈希来自 EIEM 逆向，版本敏感）
static SMCMouthShape s_mouthShapes[SMC_NUM_MOUTH] = {
    {"A", 299073642, -1, -1, -1, -1, -1, false},
    {"I", 1271943943, -1, -1, -1, -1, -1, false},
    {"U", 1701661734, -1, -1, -1, -1, -1, false},
    {"E", -781522180, -1, -1, -1, -1, -1, false},
    {"O", -348812070, -1, -1, -1, -1, -1, false},
};
static float s_mouthWeights[SMC_NUM_MOUTH] = {};
static bool s_mouthResolved = false;

static SMCExtraMorph s_extraMorphs[] = {
    {"\xe3\x81\xbe\xe3\x81\xb0\xe3\x81\x9f\xe3\x81\x8d", "blink",
     {{"eye_thinkcloseeyes_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_thinkcloseeyes_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe7\xac\x91\xe3\x81\x84", "smile_eye",
     {{"eye_relax_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_relax_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe3\x82\xa6\xe3\x82\xa3\xe3\x83\xb3\xe3\x82\xaf", "wink_L",
     {{"eye_thinkcloseeyes_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     1, 0, 0},
    {"\xe3\x82\xa6\xe3\x82\xa3\xe3\x83\xb3\xe3\x82\xaf\xe5\x8f\xb3", "wink_R",
     {{"eye_thinkcloseeyes_a_R_ctrl", 0, -1, -1, -1, -1, false}},
     1, 0, 0},
    {"\xe3\x81\xaa\xe3\x81\x94\xe3\x81\xbf", "nagomi",
     {{"eye_relax_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_relax_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe3\x81\xb3\xe3\x81\xa3\xe3\x81\x8f\xe3\x82\x8a", "surprise_eye",
     {{"eye_relax_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_relax_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe4\xb8\x8a", "brow_up",
     {{"brow_offset_u_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_offset_u_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe4\xb8\x8b", "brow_down",
     {{"brow_offset_d_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_offset_d_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe6\x80\x92\xe3\x82\x8a", "brow_angry",
     {{"brow_attack_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_attack_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe5\x9b\xb0\xe3\x82\x8b", "brow_sad",
     {{"brow_relax_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_relax_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe3\x81\xab\xe3\x81\x93\xe3\x82\x8a", "brow_smile",
     {{"brow_relax_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_relax_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe3\x82\xa6\xe3\x82\xa3\xe3\x83\xb3\xe3\x82\xaf\xef\xbc\x92", "wink2_L",
     {{"eye_thinkcloseeyes_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     1, 0, 0},
    {"\xe3\x82\xa6\xe3\x82\xa3\xe3\x83\xb3\xe3\x82\xaf\xef\xbc\x92\xe5\x8f\xb3",
     "wink2_R",
     {{"eye_thinkcloseeyes_a_R_ctrl", 0, -1, -1, -1, -1, false}},
     1, 0, 0},
    {"\xef\xbd\xb3\xef\xbd\xa8\xef\xbe\x9d\xef\xbd\xb8\xef\xbc\x92\xe5\x8f\xb3",
     "wink2_R_half",
     {{"eye_thinkcloseeyes_a_R_ctrl", 0, -1, -1, -1, -1, false}},
     1, 0, 0},
    {"\xe6\x82\xb2\xe3\x81\x97\xe3\x81\x84", "sad_eye",
     {{"eye_relax_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_relax_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe7\x9c\x9f\xe9\x9d\xa2\xe7\x9b\xae", "serious",
     {{"brow_attack_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_attack_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe5\x89\x8d", "forward",
     {{"brow_offset_d_R_ctrl", 0, -1, -1, -1, -1, false},
      {"brow_offset_d_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe3\x81\x98\xe3\x83\xbc\xe3\x81\xa3", "stare",
     {{"eye_attack_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_attack_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
    {"\xe3\x81\xaf\xe3\x81\x85", "hau",
     {{"eye_thinkcloseeyes_a_R_ctrl", 0, -1, -1, -1, -1, false},
      {"eye_thinkcloseeyes_a_L_ctrl", 0, -1, -1, -1, -1, false}},
     2, 0, 0},
};
static const int s_extraMorphCount =
    (int)(sizeof(s_extraMorphs) / sizeof(s_extraMorphs[0]));
static bool s_extraMorphsResolved = false;

// ---- hook 类型 ----
typedef void(__fastcall *SMCSMCUpdate_t)(void *, float, void *);
typedef void(__fastcall *SMCMorphJob_t)(void *, void *, void *, void *);
static SMCSMCUpdate_t s_origSMCUpdate = nullptr;
static SMCMorphJob_t s_origMorphJob = nullptr;
static SMCMorphJob_t s_origSpecialMorphJob = nullptr;

// ---- 前向声明 ----
static void ResolveSMCOffsets(void *cls);
static void ResolveSMCMouthShapes(void *smcBase);
static void SMCCaptureBigList(void *smcBase);
static void SMCRestoreBigList();
static void SMCWriteTouchedBones();

// 动态解析 SMC 类字段偏移（优先字段名，失败回退 SafeOff 常量）
static void ResolveSMCOffsets(void *cls) {
  if (!cls)
    return;
  auto getOff = [&](const char *name) -> int {
    void *fiter = nullptr;
    void *field;
    while ((field = il2cpp_class_get_fields(cls, &fiter))) {
      const char *fn = il2cpp_field_get_name(field);
      if (fn && strcmp(fn, name) == 0)
        return (int)il2cpp_field_get_offset(field);
    }
    return -1;
  };
  __try {
    s_offAllMorphs = getOff("m_allMorphs");
    s_offBigList = getOff("m_morphNameHashToMorphDataBoneBigList");
    s_offNativeHashMap = getOff("m_morphNameHashToMorphData");
    s_offMorphBSDirty = getOff("m_morphBSDirty");
    s_offAllMorphBoneDirty = getOff("m_allMorphBoneDirty");
    s_offAvatarData = getOff("m_avatarData");
    if (s_offAvatarData < 0)
      s_offAvatarData = getOff("morphData");
    s_offAllBonesTransforms = getOff("m_allBonesTransforms");
    s_offBoneIDToIdx = getOff("m_boneIDToIdx");
    s_offSmcEyeLookAt = getOff("m_isEyeLookAtIKEnable");
    if (s_offSmcEyeLookAt < 0)
      s_offSmcEyeLookAt = getOff("enableEyeLookAtIK");
    if (s_offSmcEyeLookAt < 0)
      s_offSmcEyeLookAt = getOff("m_enableEyeLookAtIK");
    if (s_offSmcEyeLookAt < 0)
      s_offSmcEyeLookAt = getOff("_enableEyeLookAtIK");
    Log("[SMC] offsets: allMorphs=%d bigList=%d hashMap=%d bsDirty=%d "
        "boneDirty=%d avatarData=%d bones=%d boneIDToIdx=%d eyeLookAt=%d",
        s_offAllMorphs, s_offBigList, s_offNativeHashMap, s_offMorphBSDirty,
        s_offAllMorphBoneDirty, s_offAvatarData, s_offAllBonesTransforms,
        s_offBoneIDToIdx, s_offSmcEyeLookAt);
  } __except (1) {
    Log("[SMC] ResolveSMCOffsets exception");
  }
}

// 从 SMC 大列表字段复制当前评估结果（job hook 或 Update 首帧调用）
static void SMCCaptureBigList(void *smcBase) {
  if (!smcBase || s_bigListCaptured)
    return;
  __try {
    int blOff = SafeOff(s_offBigList, 0x120, "smc.bigList");
    void *bigBuf = *(void **)((char *)smcBase + blOff);
    int bigLen = *(int *)((char *)smcBase + blOff + 8);
    if (bigBuf && bigLen > 0 && bigLen < SMC_MAX_BIGLIST) {
      if (bigLen > SMC_MAX_BIGLIST)
        bigLen = SMC_MAX_BIGLIST;
      memcpy(s_capturedExpression, bigBuf,
             bigLen * sizeof(SMCMorphBoneEntry));
      s_capturedLen = bigLen;
      s_bigListCaptured = true;
      int nonZero = 0;
      for (int i = 0; i < bigLen; i++) {
        const SMCMorphBoneEntry &e = s_capturedExpression[i];
        if (e.deltaPosX || e.deltaPosY || e.deltaPosZ || e.deltaRotX ||
            e.deltaRotY || e.deltaRotZ)
          nonZero++;
      }
      Log("[SMC] BigList captured: %d entries (%d nonzero)", bigLen, nonZero);
      for (int i = 0; i < 3 && i < bigLen; i++) {
        const SMCMorphBoneEntry &e = s_capturedExpression[i];
        Log("[SMC]   [%d] hash=%d boneID=%d dP=(%.3f,%.3f,%.3f) "
            "dR=(%.3f,%.3f,%.3f)",
            i, e.boneNameHash, e.boneID, e.deltaPosX, e.deltaPosY,
            e.deltaPosZ, e.deltaRotX, e.deltaRotY, e.deltaRotZ);
      }
    } else {
      Log("[SMC] BigList invalid: buf=%p len=%d", bigBuf, bigLen);
    }
  } __except (1) {
    Log("[SMC] SMCCaptureBigList exception");
  }
}

// 把大列表数据还回游戏（停止驱动/换角色时避免留脏数据）
static void SMCRestoreBigList() {
  if (!s_confirmedSMC || s_capturedLen <= 0)
    return;
  __try {
    int blOff = SafeOff(s_offBigList, 0x120, "smc.bigList");
    void *bigBuf = *(void **)((char *)s_confirmedSMC + blOff);
    int bigLen = *(int *)((char *)s_confirmedSMC + blOff + 8);
    if (bigBuf && bigLen == s_capturedLen) {
      memcpy(bigBuf, s_capturedExpression,
             bigLen * sizeof(SMCMorphBoneEntry));
      Log("[SMC] BigList restored (%d entries)", bigLen);
    }
  } __except (1) {
    Log("[SMC] BigList restore failed");
  }
}

// NativeHashMap 探测 + morph 名 → 大列表区间映射（A/I/U/E/O 用硬编码哈希，
// 表情目标按名字在 morphMappingNames 里反查哈希）
static void ResolveSMCMouthShapes(void *smcBase) {
  if (s_mouthResolved)
    return;
  __try {
    int hmOff = SafeOff(s_offNativeHashMap, 0xF8, "smc.nativeHashMap");
    void *hmBuffer = *(void **)((char *)smcBase + hmOff);
    if (!hmBuffer) {
      Log("[SMC] No hashmap at SMC+0x%X", hmOff);
      return;
    }
    int *hmData = (int *)hmBuffer;
    int keyCapacity = hmData[8];
    int bucketCap = hmData[9];
    int allocLen = hmData[10];
    if (keyCapacity <= 0 || keyCapacity > 1000 || bucketCap <= 0 ||
        allocLen <= 0 || allocLen > keyCapacity) {
      Log("[SMC] Invalid hashmap: keyCap=%d bucketCap=%d allocLen=%d",
          keyCapacity, bucketCap, allocLen);
      return;
    }
    if ((bucketCap & (bucketCap + 1)) != 0)
      Log("[SMC] WARN: bucketCap=%d not power-of-2 bitmask, layout may differ",
          bucketCap);
    char *values = *(char **)(&hmData[0]);
    int *keys = *(int **)(&hmData[2]);
    int *nextArr = *(int **)(&hmData[4]);
    int *buckets = *(int **)(&hmData[6]);
    if (!values || !keys || !nextArr || !buckets) {
      Log("[SMC] Null pointers in hashmap");
      return;
    }
    int valStride = (int)(((uintptr_t)keys - (uintptr_t)values) /
                          keyCapacity);
    if (valStride < 8 || valStride > 200) {
      Log("[SMC] Invalid valStride=%d", valStride);
      return;
    }
    if (valStride != 40)
      Log("[SMC] WARN: valStride=%d (expected 40), layout may differ",
          valStride);

    for (int m = 0; m < SMC_NUM_MOUTH; m++) {
      int targetHash = s_mouthShapes[m].nameHash;
      int bucket = ((unsigned)targetHash) & ((unsigned)bucketCap);
      int entryIdx = buckets[bucket];
      int depth = 0;
      while (entryIdx >= 0 && entryIdx < keyCapacity && depth < 100) {
        if (keys[entryIdx] == targetHash) {
          int *vi = (int *)(values + entryIdx * valStride);
          s_mouthShapes[m].morphId = vi[0];
          s_mouthShapes[m].startIdx = vi[4];
          s_mouthShapes[m].count = vi[5];
          s_mouthShapes[m].jobStartIdx = vi[4];
          s_mouthShapes[m].jobCount = vi[5];
          s_mouthShapes[m].resolved = true;
          Log("[SMC] Mouth '%s': morphId=%d start=%d count=%d partType=%d",
              s_mouthShapes[m].name, vi[0], vi[4], vi[5], vi[3]);
          if (vi[4] < 0 || vi[4] > 10000 || vi[5] <= 0 || vi[5] > 500) {
            Log("[SMC] WARN: '%s' suspicious start/count, layout may differ",
                s_mouthShapes[m].name);
            s_mouthShapes[m].resolved = false;
          }
          break;
        }
        entryIdx = nextArr[entryIdx];
        depth++;
      }
      if (!s_mouthShapes[m].resolved)
        Log("[SMC] Mouth '%s' not found in hashmap", s_mouthShapes[m].name);
    }

    // AvatarData.morphMappingNames：名字数组，供表情目标反查
    int adOff = SafeOff(s_offAvatarData, 0x58, "smc.avatarData");
    void *avatarData = *(void **)((char *)smcBase + adOff);
    if (avatarData && s_offMorphMappingNames < 0) {
      void *adClass = il2cpp_object_get_class(avatarData);
      if (adClass) {
        void *fiter = nullptr;
        void *field;
        while ((field = il2cpp_class_get_fields(adClass, &fiter))) {
          const char *fn = il2cpp_field_get_name(field);
          if (fn && strcmp(fn, "morphMappingNames") == 0) {
            s_offMorphMappingNames = (int)il2cpp_field_get_offset(field);
            Log("[SMC] AvatarData.morphMappingNames=0x%X",
                s_offMorphMappingNames);
            break;
          }
        }
      }
    }
    int mnOff = SafeOff(s_offMorphMappingNames, 0x38, "smc.morphMappingNames");
    void *nameArr =
        avatarData ? *(void **)((char *)avatarData + mnOff) : nullptr;
    int nameArrLen = nameArr ? *(int *)((char *)nameArr + IL2CPP_ARRAY_LEN) : 0;
    if (nameArr && nameArrLen > 0 && nameArrLen <= 500) {
      int resolvedTargets = 0;
      for (int e = 0; e < allocLen && e < keyCapacity; e++) {
        int *vi = (int *)(values + e * valStride);
        int morphId = vi[0];
        int smcStart = vi[4];
        int smcCount = vi[5];
        if (morphId < 0 || morphId >= nameArrLen || smcCount <= 0)
          continue;
        char nn[128] = {};
        void *strObj = *(void **)((char *)nameArr + IL2CPP_ARRAY_DATA +
                                  morphId * 8);
        if (!strObj)
          continue;
        ReadStr(strObj, nn, sizeof(nn));
        for (int em = 0; em < s_extraMorphCount; em++) {
          for (int t = 0; t < s_extraMorphs[em].targetCount; t++) {
            SMCExtraTarget &tgt = s_extraMorphs[em].targets[t];
            if (!tgt.resolved && strcmp(nn, tgt.endfieldName) == 0) {
              tgt.nameHash = keys[e];
              tgt.startIdx = smcStart;
              tgt.count = smcCount;
              tgt.jobStartIdx = smcStart;
              tgt.jobCount = smcCount;
              tgt.resolved = (smcStart >= 0 && smcStart <= 10000 &&
                              smcCount > 0 && smcCount <= 500);
              if (tgt.resolved) {
                resolvedTargets++;
                Log("[SMC] %s -> %s: start=%d count=%d", nn,
                    s_extraMorphs[em].label, smcStart, smcCount);
              } else {
                Log("[SMC] WARN: %s suspicious start=%d count=%d", nn,
                    smcStart, smcCount);
              }
            }
          }
        }
      }
      s_extraMorphsResolved = (resolvedTargets > 0);
      Log("[SMC] Resolved %d/%d extra morph targets", resolvedTargets,
          s_extraMorphCount > 0
              ? s_extraMorphs[0].targetCount * s_extraMorphCount
              : 0);
    } else {
      Log("[SMC] morphMappingNames empty: arr=%p len=%d", nameArr,
          nameArrLen);
    }
    s_mouthResolved = true;
  } __except (1) {
    Log("[SMC] ResolveSMCMouthShapes exception");
    s_mouthResolved = true; // 防止每帧重试打日志
  }
}

// DoEvaluateMorphToBoneJob：确认 SMC 实例、抓大列表、驱动时清零游戏自身增量
static void __fastcall HookedSMCMorphJob(void *__this, void *param1,
                                         void *param2, void *methodInfo) {
  if (!s_confirmedSMC && param1) {
    s_confirmedSMC = param1;
    Log("[SMC] Confirmed SMC from MorphToBoneJob: %p", param1);
    if (!s_eyeIKDisabled) {
      s_eyeIKDisabled = true;
      __try {
        int eyeOff = SafeOff(s_offSmcEyeLookAt, 0x1DD,
                             "smc.enableEyeLookAtIK");
        *(bool *)((char *)param1 + eyeOff) = false;
        Log("[SMC] EyeLookAtIK disabled at SMC+0x%X", eyeOff);
      } __except (1) {
        Log("[SMC] Failed to disable EyeLookAtIK");
      }
    }
  }
  if (!s_bigListCaptured && param1 && param1 == s_confirmedSMC)
    SMCCaptureBigList(param1);

  // 面板驱动中：清零大列表增量，防止游戏按原始 morph 权重改写骨骼
  if (s_driving && s_faceBonesCaptured && s_boneMapReady && param1 &&
      param1 == s_confirmedSMC) {
    __try {
      int blOff = SafeOff(s_offBigList, 0x120, "smc.bigList");
      void *bigBuf = *(void **)((char *)param1 + blOff);
      int bigLen = *(int *)((char *)param1 + blOff + 8);
      if (bigBuf && bigLen > 0 && bigLen == s_capturedLen) {
        SMCMorphBoneEntry *live = (SMCMorphBoneEntry *)bigBuf;
        for (int i = 0; i < bigLen; i++) {
          int boneID = live[i].boneID;
          int arrIdx =
              (boneID >= 0 && boneID < SMC_BONE_MAP_SIZE)
                  ? s_boneIDToIdx[boneID]
                  : -1;
          if (arrIdx < 0 || arrIdx >= s_faceBoneCount)
            continue;
          live[i].deltaPosX = live[i].deltaPosY = live[i].deltaPosZ = 0;
          live[i].deltaRotX = live[i].deltaRotY = live[i].deltaRotZ = 0;
        }
      }
    } __except (1) {
    }
  }
  if (s_origMorphJob)
    s_origMorphJob(__this, param1, param2, methodInfo);
}

static void __fastcall HookedSMCSpecialMorphJob(void *__this, void *param1,
                                                void *param2,
                                                void *methodInfo) {
  if (s_origSpecialMorphJob)
    s_origSpecialMorphJob(__this, param1, param2, methodInfo);
}

// 写回被触碰的面部骨骼（局部位姿）
static void SMCWriteTouchedBones() {
  __try {
    for (int i = 0; i < s_faceBoneCount; i++) {
      if (!s_faceBoneTouched[i] || !s_faceBones[i].transform)
        continue;
      SetBoneLocalPos(s_faceBones[i].transform,
                      Vec3(s_faceBones[i].px, s_faceBones[i].py,
                           s_faceBones[i].pz));
      SetBoneLocalRot(s_faceBones[i].transform,
                      Quat(s_faceBones[i].rx, s_faceBones[i].ry,
                           s_faceBones[i].rz, s_faceBones[i].rw));
    }
  } __except (1) {
  }
}

// SkeletalMorphCore.Update：初始化（偏移/口型/大列表/骨骼静息位姿/骨映射），
// 之后每帧按面板权重累加增量，在原始 Update 之后覆盖写回
static void __fastcall HookedSMCUpdate(void *__this, float deltaTime,
                                       void *methodInfo) {
  if (!s_smcCore) {
    if (s_confirmedSMC && __this == s_confirmedSMC) {
      s_smcCore = __this;
      s_frame = 0;
      s_faceBoneRefs = nullptr;
      s_faceBonesCaptured = false;
      Log("[SMC] Locked SMC core: %p", __this);
    } else {
      if (s_origSMCUpdate)
        s_origSMCUpdate(__this, deltaTime, methodInfo);
      return;
    }
  }
  if (__this != s_smcCore) {
    if (s_origSMCUpdate)
      s_origSMCUpdate(__this, deltaTime, methodInfo);
    return;
  }
  s_frame++;

  if (s_frame == 1) {
    ResolveSMCOffsets(s_smcClass);
    ResolveSMCMouthShapes((char *)__this);
    SMCCaptureBigList(__this); // job hook 可能已抓过，这里兜底
  }

  // 驱动中：先写上一帧结果，抵消游戏本帧改写。
  // 必须限定在冻结态：hook 是启动时就装上的，解冻后若继续写，会永久盖住游戏的面部动画。
  if (s_driving && g_frozen && s_faceBonesCaptured && s_frame > 5)
    SMCWriteTouchedBones();

  // 面部骨骼引用（m_allBonesTransforms，一次性）
  if (!s_faceBoneRefs && s_frame >= 1) {
    __try {
      int btOff =
          SafeOff(s_offAllBonesTransforms, 0x60, "smc.allBonesTransforms");
      void *bonesArr = *(void **)((char *)__this + btOff);
      if (bonesArr) {
        int boneLen = *(int *)((char *)bonesArr + IL2CPP_ARRAY_LEN);
        if (boneLen > 0 && boneLen <= SMC_MAX_FACE_BONES) {
          s_faceBoneRefs = (void **)((char *)bonesArr + IL2CPP_ARRAY_DATA);
          s_faceBoneCount = boneLen;
          Log("[SMC] Bone refs resolved: %d", boneLen);
        }
      }
    } __except (1) {
    }
  }

  // 静息位姿（一次性，首帧捕获）
  if (s_faceBoneRefs && !s_faceBonesCaptured && s_frame >= 1) {
    int captured = 0;
    for (int i = 0; i < s_faceBoneCount; i++) {
      if (!s_faceBoneRefs[i])
        continue;
      Vec3 p = GetBoneLocalPos(s_faceBoneRefs[i]);
      Quat r = GetBoneLocalRot(s_faceBoneRefs[i]);
      s_faceBones[i] = {p.x, p.y, p.z, r.x, r.y, r.z, r.w, s_faceBoneRefs[i]};
      captured++;
    }
    memcpy(s_faceRestPose, s_faceBones, sizeof(s_faceBones));
    s_faceBonesCaptured = true;
    Log("[SMC] Rest pose captured: %d bones", captured);
  }

  // 骨 ID → 骨骼数组下标映射（第 20 帧后游戏数据就绪）
  if (s_bigListCaptured && !s_boneMapReady && s_frame >= 20) {
    memset(s_boneIDToIdx, -1, sizeof(s_boneIDToIdx));
    __try {
      int biOff = SafeOff(s_offBoneIDToIdx, 0xE0, "smc.boneIDToIdx");
      void *buf = *(void **)((char *)__this + biOff);
      int len = *(int *)((char *)__this + biOff + 8);
      if (buf && len > 0 && len < 500) {
        int *data = (int *)buf;
        bool allNeg = true;
        for (int i = 0; i < 8 && i < len * 2; i++) {
          if (data[i] != -1) {
            allNeg = false;
            break;
          }
        }
        int mapped = 0;
        if (!allNeg) { // 成对数组：boneID, arrIdx
          for (int i = 0; i < len; i++) {
            int boneID = data[i * 2];
            int arrIdx = data[i * 2 + 1];
            if (boneID >= 0 && boneID < SMC_BONE_MAP_SIZE && arrIdx >= 0 &&
                arrIdx < SMC_MAX_FACE_BONES) {
              s_boneIDToIdx[boneID] = arrIdx;
              mapped++;
            }
          }
        } else { // 扁平查找数组：idx → arrIdx
          for (int i = 0; i < len; i++) {
            if (data[i] >= 0 && data[i] < SMC_MAX_FACE_BONES) {
              s_boneIDToIdx[i] = data[i];
              mapped++;
            }
          }
        }
        s_boneIDMapCount = mapped;
        s_boneMapReady = (mapped > 0);
        Log("[SMC] Bone map: mapped %d bones (%s)", mapped,
            allNeg ? "flat" : "pair");
        if (!s_boneMapReady) {
          if (s_offAllMorphBoneDirty > 0)
            *(bool *)((char *)__this + s_offAllMorphBoneDirty) = true;
        }
      }
    } __except (1) {
    }
  }

  // 按面板权重累加 morph 增量到静息位姿
  if (s_driving && s_boneMapReady && s_boneIDMapCount > 0 &&
      s_capturedLen > 0 && s_mouthResolved) {
    __try {
      float totalMouth = 0;
      for (int s = 0; s < SMC_NUM_MOUTH; s++)
        totalMouth += s_mouthWeights[s];
      if (totalMouth > 1.0f) {
        float scale = 1.0f / totalMouth;
        for (int s = 0; s < SMC_NUM_MOUTH; s++)
          s_mouthWeights[s] *= scale;
      }

      memcpy(s_faceBones, s_faceRestPose, sizeof(s_faceBones));
      float deltaPosAccum[SMC_MAX_FACE_BONES][3] = {};
      float deltaRotAccum[SMC_MAX_FACE_BONES][3] = {};
      memset(s_faceBoneTouched, 0, sizeof(s_faceBoneTouched));
      int applied = 0;

      for (int s = 0; s < SMC_NUM_MOUTH; s++) {
        float w = s_mouthWeights[s];
        if (w < 0.001f)
          continue;
        int start = s_mouthShapes[s].jobStartIdx;
        int cnt = s_mouthShapes[s].jobCount;
        if (start < 0 || start + cnt > s_capturedLen)
          continue;
        for (int i = start; i < start + cnt; i++) {
          const SMCMorphBoneEntry &e = s_capturedExpression[i];
          int arrIdx =
              (e.boneID >= 0 && e.boneID < SMC_BONE_MAP_SIZE)
                  ? s_boneIDToIdx[e.boneID]
                  : -1;
          if (arrIdx < 0 || arrIdx >= s_faceBoneCount)
            continue;
          if (fabsf(e.deltaPosX) > 1.0f || fabsf(e.deltaPosY) > 1.0f ||
              fabsf(e.deltaPosZ) > 1.0f)
            continue;
          deltaPosAccum[arrIdx][0] += e.deltaPosX * w;
          deltaPosAccum[arrIdx][1] += e.deltaPosY * w;
          deltaPosAccum[arrIdx][2] += e.deltaPosZ * w;
          if (fabsf(e.deltaRotX) < 30.0f && fabsf(e.deltaRotY) < 30.0f &&
              fabsf(e.deltaRotZ) < 30.0f) {
            deltaRotAccum[arrIdx][0] += e.deltaRotX * w;
            deltaRotAccum[arrIdx][1] += e.deltaRotY * w;
            deltaRotAccum[arrIdx][2] += e.deltaRotZ * w;
          }
          s_faceBoneTouched[arrIdx] = true;
          applied++;
        }
      }

      if (s_extraMorphsResolved) {
        for (int em = 0; em < s_extraMorphCount; em++) {
          float w = s_extraMorphs[em].weight;
          if (w < 0.001f)
            continue;
          for (int t = 0; t < s_extraMorphs[em].targetCount; t++) {
            const SMCExtraTarget &tgt = s_extraMorphs[em].targets[t];
            if (!tgt.resolved || tgt.startIdx < 0 ||
                tgt.startIdx + tgt.count > s_capturedLen)
              continue;
            bool isEyeMorph = (strncmp(tgt.endfieldName, "eye_", 4) == 0);
            for (int i = tgt.startIdx; i < tgt.startIdx + tgt.count; i++) {
              const SMCMorphBoneEntry &e = s_capturedExpression[i];
              int arrIdx =
                  (e.boneID >= 0 && e.boneID < SMC_BONE_MAP_SIZE)
                      ? s_boneIDToIdx[e.boneID]
                      : -1;
              if (arrIdx < 0 || arrIdx >= s_faceBoneCount)
                continue;
              if (fabsf(e.deltaPosX) > 1.0f || fabsf(e.deltaPosY) > 1.0f ||
                  fabsf(e.deltaPosZ) > 1.0f)
                continue;
              deltaPosAccum[arrIdx][0] += e.deltaPosX * w;
              deltaPosAccum[arrIdx][1] += e.deltaPosY * w;
              deltaPosAccum[arrIdx][2] += e.deltaPosZ * w;
              if (!isEyeMorph) { // 眼部 morph 只做位移，避免眼珠旋转打架
                deltaRotAccum[arrIdx][0] += e.deltaRotX * w;
                deltaRotAccum[arrIdx][1] += e.deltaRotY * w;
                deltaRotAccum[arrIdx][2] += e.deltaRotZ * w;
              }
              s_faceBoneTouched[arrIdx] = true;
              applied++;
            }
          }
        }
      }

      for (int b = 0; b < s_faceBoneCount; b++) {
        if (!s_faceBoneTouched[b])
          continue;
        s_faceBones[b].px = s_faceRestPose[b].px + deltaPosAccum[b][0];
        s_faceBones[b].py = s_faceRestPose[b].py + deltaPosAccum[b][1];
        s_faceBones[b].pz = s_faceRestPose[b].pz + deltaPosAccum[b][2];
        Quat dq = Quat::FromEulerDeg(
            Vec3(deltaRotAccum[b][0], deltaRotAccum[b][1],
                 deltaRotAccum[b][2]));
        Quat rq(s_faceRestPose[b].rx, s_faceRestPose[b].ry,
                s_faceRestPose[b].rz, s_faceRestPose[b].rw);
        Quat res = dq * rq;
        float len = sqrtf(res.x * res.x + res.y * res.y + res.z * res.z +
                          res.w * res.w);
        if (len > 1e-6f) {
          res.x /= len;
          res.y /= len;
          res.z /= len;
          res.w /= len;
        }
        s_faceBones[b].rx = res.x;
        s_faceBones[b].ry = res.y;
        s_faceBones[b].rz = res.z;
        s_faceBones[b].rw = res.w;
      }
      if (applied > 0 && s_frame % 600 == 0)
        Log("[SMC] Applied %d bone deltas (frame %d)", applied, s_frame);
    } __except (1) {
      Log("[SMC] delta accumulation exception");
    }
  }

  if (s_origSMCUpdate)
    s_origSMCUpdate(__this, deltaTime, methodInfo);

  // 覆盖写回（原始 Update 之后）
  if (s_driving && s_faceBonesCaptured && s_frame > 5)
    SMCWriteTouchedBones();
}

// 安装 SMC hook（IL2CPP Resolve 成功后调用一次）
static void InstallSMCFaceHooks() {
  __try {
    size_t ac = 0;
    void **asms = nullptr;
    void *domain = il2cpp_domain_get();
    if (domain)
      asms = il2cpp_domain_get_assemblies(domain, &ac);

    void *smcClass =
        FindClass("Beyond.Gameplay.View.SkeletalMorph", "SkeletalMorphCore",
                  asms, ac);
    if (!smcClass)
      smcClass = FindClass("Beyond.Gameplay.View", "SkeletalMorphCore", asms,
                           ac);
    if (!smcClass)
      smcClass = FindClass("Beyond.Gameplay.Core", "SkeletalMorphCore", asms,
                           ac);
    if (!smcClass) {
      Log("[SMC] SkeletalMorphCore class not found (SMC face morph disabled)");
      return;
    }
    s_smcClass = smcClass;
    Log("[SMC] SkeletalMorphCore class found");

    void *updateMethod = FindMethod(smcClass, "Update", 1);
    if (updateMethod)
      Hook(updateMethod, "SkeletalMorphCore.Update", (void *)HookedSMCUpdate,
           (void **)&s_origSMCUpdate);
    else
      Log("[SMC] Update method not found");

    void *jobMethod = FindMethod(smcClass, "DoEvaluateMorphToBoneJob", 2);
    if (jobMethod)
      Hook(jobMethod, "DoEvaluateMorphToBoneJob", (void *)HookedSMCMorphJob,
           (void **)&s_origMorphJob);
    else
      Log("[SMC] DoEvaluateMorphToBoneJob not found");

    void *specialJob =
        FindMethod(smcClass, "DoEvaluateSpecialMorphToBoneJob", 2);
    if (specialJob)
      Hook(specialJob, "DoEvaluateSpecialMorphToBoneJob",
           (void *)HookedSMCSpecialMorphJob, (void **)&s_origSpecialMorphJob);
  } __except (1) {
    Log("[SMC] InstallSMCFaceHooks exception");
  }
}

// 角色切换 / 停止驱动时重置（把大列表还回游戏）
static void ResetSMCState() {
  SMCRestoreBigList();
  s_smcCore = nullptr;
  s_confirmedSMC = nullptr;
  s_frame = 0;
  s_bigListCaptured = false;
  s_capturedLen = 0;
  s_faceBoneRefs = nullptr;
  s_faceBoneCount = 0;
  s_faceBonesCaptured = false;
  s_boneMapReady = false;
  s_boneIDMapCount = 0;
  s_eyeIKDisabled = false;
  memset(s_boneIDToIdx, -1, sizeof(s_boneIDToIdx));
  memset(s_faceBoneTouched, 0, sizeof(s_faceBoneTouched));
  for (int i = 0; i < SMC_NUM_MOUTH; i++) {
    s_mouthShapes[i].resolved = false;
    s_mouthWeights[i] = 0.0f;
  }
  for (int i = 0; i < s_extraMorphCount; i++) {
    s_extraMorphs[i].weight = 0.0f;
    s_extraMorphs[i].prevWeight = 0.0f;
    for (int t = 0; t < s_extraMorphs[i].targetCount; t++)
      s_extraMorphs[i].targets[t].resolved = false;
  }
  s_mouthResolved = false;
  s_extraMorphsResolved = false;
  s_driving = false;
  Log("[SMC] state reset");
}

// ---- 面板 API ----
static bool SMCSectionReady() {
  return s_smcCore && s_bigListCaptured && s_mouthResolved;
}

static bool SMCFaceDriving() { return s_driving; }
static void SMCFaceSetDriving(bool on) {
  if (s_driving == on)
    return;
  if (!on) {
    s_driving = false;
    for (int i = 0; i < SMC_NUM_MOUTH; i++)
      s_mouthWeights[i] = 0.0f;
    for (int i = 0; i < s_extraMorphCount; i++)
      s_extraMorphs[i].weight = 0.0f;
    SMCRestoreBigList();
    Log("[SMC] driving disabled");
  } else {
    s_driving = true;
    Log("[SMC] driving enabled");
  }
}

static int SMCSliderCount() {
  return SMC_NUM_MOUTH + s_extraMorphCount;
}

static const char *SMCSliderLabel(int i) {
  if (i < SMC_NUM_MOUTH)
    return s_mouthShapes[i].name;
  return s_extraMorphs[i - SMC_NUM_MOUTH].label;
}

static float SMCSliderValue(int i) {
  if (i < SMC_NUM_MOUTH)
    return s_mouthWeights[i];
  return s_extraMorphs[i - SMC_NUM_MOUTH].weight;
}

static void SMCSliderSet(int i, float v) {
  if (v < 0.0f)
    v = 0.0f;
  if (v > 1.0f)
    v = 1.0f;
  if (i < SMC_NUM_MOUTH) {
    s_mouthWeights[i] = v;
  } else {
    s_extraMorphs[i - SMC_NUM_MOUTH].weight = v;
    s_extraMorphs[i - SMC_NUM_MOUTH].prevWeight = v;
  }
  s_driving = true;
}

static void SMCRestoreWeights() {
  for (int i = 0; i < SMC_NUM_MOUTH; i++)
    s_mouthWeights[i] = 0.0f;
  for (int i = 0; i < s_extraMorphCount; i++)
    s_extraMorphs[i].weight = 0.0f;
}
