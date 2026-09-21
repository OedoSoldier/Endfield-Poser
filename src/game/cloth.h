#pragma once

// 裙子碰撞调整（移植自 {EIEM}/src/cloth.h，AGPL-3.0）。
// 游戏的裙摆布料（BeyondBoneCloth）带一组大腿根碰撞胶囊；把胶囊加粗后
// 摆腿姿势时大腿不容易穿裙。冻结时应用调整，解冻/换角色时恢复原始参数。
//
// 与 EIEM 的差异：EIEM 在 MMD 播放开始时应用，这里改成冻结（进入摆姿）时
// 应用；组件采集复用 poser 自己的 Transform 递归遍历（freeze.h 同款思路）。

#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "math/quat_math.h"

#include <cstring>

#define POSER_MAX_BBC 16

// 用户可调参数（面板滑条直接改这些）
static float g_skirtHipRadiusDelta = 0.124f; // 大腿根半径补充
static float g_skirtRadiusA = 1.0f;          // 裙摆球半径倍率
static float g_skirtRadiusB = 3.0f;          // hipR 回退倍率
static float g_skirtLengthScale = 1.0f;      // 裙长缩放
static bool g_skirtTaperOn = true;           // 锥形调整开关

// 运行时状态
static void *g_bbcInstances[POSER_MAX_BBC];
static int g_bbcCount = 0;
static int g_skirtBBCIndex = -1;
static float g_skirtOrigSize[8][3] = {};
static float g_skirtOrigCenter[8][3] = {};
static int g_skirtColliderCount = 0;
static bool g_skirtScaleResolved = false;
static bool g_skirtDirty = false;
static int g_skirtRetryFrames = 0;
static float g_skirtHeightScale = 1.0f;
static float g_skirtCenterOfs[3] = {0, 0, 0};
static void *g_coll_SetSize = nullptr;
static void *g_coll_UpdateParams = nullptr;
static void *g_cloth_SetParamChange = nullptr;
static int OFF_bbc_process = -1;
static int OFF_cp_colliderList = -1;
static int OFF_radiusSep = -1;

// 换角色时清空采集数据（保留用户滑条参数）
static void ResetSkirtState() {
  g_bbcCount = 0;
  g_skirtBBCIndex = -1;
  g_skirtScaleResolved = false;
  g_skirtDirty = false;
  g_skirtRetryFrames = 0;
  g_skirtColliderCount = 0;
  g_coll_SetSize = nullptr;
  g_coll_UpdateParams = nullptr;
  g_cloth_SetParamChange = nullptr;
  OFF_bbc_process = OFF_cp_colliderList = OFF_radiusSep = -1;
  memset(g_bbcInstances, 0, sizeof(g_bbcInstances));
  memset(g_skirtOrigSize, 0, sizeof(g_skirtOrigSize));
  memset(g_skirtOrigCenter, 0, sizeof(g_skirtOrigCenter));
}

// 递归遍历角色 Transform 层级，收集 BeyondBoneCloth 组件，识别 MC_Skirt
static void CollectSkirtOnTransform(void *t, int depth) {
  if (!t || depth > 8 || g_bbcCount >= POSER_MAX_BBC)
    return;
  __try {
    if (g_component_get_gameObject && g_gameObject_GetComponents &&
        g_componentClass) {
      void *go = Invoke(g_component_get_gameObject, t);
      if (go) {
        void *type = il2cpp_class_get_type(g_componentClass);
        void *typeObj = type ? il2cpp_type_get_object(type) : nullptr;
        if (typeObj) {
          void *args[] = {typeObj};
          void *arr = Invoke(g_gameObject_GetComponents, go, args);
          if (arr) {
            int cnt = *(int *)((char *)arr + IL2CPP_ARRAY_LEN);
            void **data = (void **)((char *)arr + IL2CPP_ARRAY_DATA);
            for (int i = 0; i < cnt && g_bbcCount < POSER_MAX_BBC; i++) {
              if (!data[i])
                continue;
              void *cls = il2cpp_object_get_class(data[i]);
              const char *cn = cls ? il2cpp_class_get_name(cls) : "";
              if (!cn || strcmp(cn, "BeyondBoneCloth") != 0)
                continue;
              int idx = g_bbcCount;
              g_bbcInstances[g_bbcCount++] = data[i];
              if (g_skirtBBCIndex < 0 && g_object_get_name) {
                void *go2 = Invoke(g_component_get_gameObject, data[i]);
                void *ns = go2 ? Invoke(g_object_get_name, go2) : nullptr;
                char buf[128] = {};
                if (ns)
                  ReadStr(ns, buf, sizeof(buf));
                // 游戏里裙子的命名是带角色前缀的（如 "MC_Chen_Skirt"），
                // 早期只匹配 "MC_Skirt" 导致永远找不到 → 改成不区分大小写找 "skirt"。
                char low[128];
                int k = 0;
                for (; buf[k] && k < (int)sizeof(low) - 1; k++) {
                  char c = buf[k];
                  low[k] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                }
                low[k] = 0;
                bool isSkirt = strstr(low, "skirt") != nullptr;
                Log("[SKIRT] BBC[%d] GO '%s'%s", idx, buf,
                    isSkirt ? "  <- skirt" : "");
                if (isSkirt) {
                  g_skirtBBCIndex = idx;
                  Log("[SKIRT] skirt identified at index %d", idx);
                }
              }
            }
          }
        }
      }
    }
    if (g_transform_get_childCount && g_transform_GetChild) {
      void *cntBoxed = Invoke(g_transform_get_childCount, t);
      int cnt = cntBoxed ? *(int *)((char *)cntBoxed + 16) : 0;
      for (int i = 0; i < cnt && g_bbcCount < POSER_MAX_BBC; i++) {
        void *params[] = {&i};
        void *ch = Invoke(g_transform_GetChild, t, params);
        if (ch)
          CollectSkirtOnTransform(ch, depth + 1);
      }
    }
  } __except (1) {
  }
}

static void CollectSkirtCloth() {
  g_bbcCount = 0;
  g_skirtBBCIndex = -1;
  void *rootT = GetCharRootTransform();
  if (!rootT)
    return;
  CollectSkirtOnTransform(rootT, 0);
  Log("[SKIRT] BeyondBoneCloth instances=%d skirtIdx=%d", g_bbcCount,
      g_skirtBBCIndex);
}

// 应用裙摆碰撞调整（core logic 参照 EIEM cloth.h ApplySkirtColliderScale）
static void ApplySkirtColliderScale() {
  if (g_skirtBBCIndex < 0 || g_skirtBBCIndex >= g_bbcCount)
    return;
  if (!g_bbcInstances[g_skirtBBCIndex])
    return;
  if (!g_skirtDirty)
    return;
  g_skirtDirty = false;

  void *skirt = g_bbcInstances[g_skirtBBCIndex];
  int offProc = SafeOff(OFF_bbc_process, 0xA8, "bbc.process");
  int offCL = SafeOff(OFF_cp_colliderList, 0x3A0, "cp.colliderList");
  int offRS = SafeOff(OFF_radiusSep, 0x45, "coll.radiusSeparation");
  __try {
    void *process = *(void **)((char *)skirt + offProc);
    if (!process)
      return;
    void *colliderList = *(void **)((char *)process + offCL);
    if (!colliderList || (uintptr_t)colliderList < 0x10000)
      return;
    void *items = *(void **)((char *)colliderList + 0x10);
    int count = *(int *)((char *)colliderList + 0x18);
    if (!items || count <= 0 || count > 8)
      return;

    if (!g_skirtScaleResolved) {
      void *coll0 = *(void **)((char *)items + 0x20);
      void *collCls = coll0 ? il2cpp_object_get_class(coll0) : nullptr;
      if (collCls) {
        g_coll_SetSize = FindMethodInHierarchy(collCls, "SetSize", 1);
        g_coll_UpdateParams =
            FindMethodInHierarchy(collCls, "UpdateParameters", 0);
        if (OFF_radiusSep < 0) {
          const char *rsNames[] = {"radiusSeparation", "m_radiusSeparation"};
          OFF_radiusSep = FindFieldInHierarchy(collCls, rsNames, 2, nullptr);
          offRS = SafeOff(OFF_radiusSep, 0x45, "coll.radiusSeparation");
        }
      }
      void *clothCls = il2cpp_object_get_class(skirt);
      if (clothCls) {
        g_cloth_SetParamChange =
            FindMethodInHierarchy(clothCls, "SetParameterChange", 0);
        if (OFF_bbc_process < 0) {
          const char *procNames[] = {"process", "m_process", "clothProcess"};
          OFF_bbc_process =
              FindFieldInHierarchy(clothCls, procNames, 3, nullptr);
          offProc = SafeOff(OFF_bbc_process, 0xA8, "bbc.process");
        }
        if (OFF_cp_colliderList < 0 && process) {
          void *procCls = il2cpp_object_get_class(process);
          if (procCls) {
            const char *clNames[] = {"colliderList", "m_colliderList",
                                     "colliders"};
            OFF_cp_colliderList =
                FindFieldInHierarchy(procCls, clNames, 3, nullptr);
            offCL = SafeOff(OFF_cp_colliderList, 0x3A0, "cp.colliderList");
          }
        }
      }
      // 高度缩放：按 Hips→Head 距离粗估角色比例（EIEM 用 g_charHeight/1.245）
      void *hipsT = GetHumanoidBone(Hips);
      void *headT = GetHumanoidBone(Head);
      if (hipsT && headT) {
        Vec3 h0 = GetBoneWorldPos(hipsT);
        Vec3 h1 = GetBoneWorldPos(headT);
        Vec3 d = h1 - h0;
        float l = Len(d);
        if (l > 0.1f)
          g_skirtHeightScale = l;
      }
      g_skirtColliderCount = count;
      for (int i = 0; i < count; i++) {
        void *c = *(void **)((char *)items + 0x20 + i * 8);
        if (!c)
          continue;
        float *sz = (float *)((char *)c + 0x2C);
        float *ct = (float *)((char *)c + 0x20);
        for (int k = 0; k < 3; k++) {
          g_skirtOrigSize[i][k] = sz[k];
          g_skirtOrigCenter[i][k] = ct[k];
        }
      }
      g_skirtScaleResolved = true;
      Log("[SKIRT] resolved: SetSize=%p UpdateParams=%p SetParamChange=%p, %d colliders, hs=%.2f",
          g_coll_SetSize, g_coll_UpdateParams, g_cloth_SetParamChange, count,
          g_skirtHeightScale);
    }

    if (!g_coll_SetSize || !g_coll_UpdateParams)
      return;

    for (int i = 0; i < g_skirtColliderCount; i++) {
      void *c = *(void **)((char *)items + 0x20 + i * 8);
      if (!c)
        continue;
      bool isThighCapsule = fabsf(g_skirtOrigCenter[i][0]) > 0.1f;
      if (!isThighCapsule) {
        *(unsigned char *)((char *)c + offRS) = 0;
        float *ctR = (float *)((char *)c + 0x20);
        ctR[0] = g_skirtOrigCenter[i][0];
        ctR[1] = g_skirtOrigCenter[i][1];
        ctR[2] = g_skirtOrigCenter[i][2];
        Vec3 origSz = {g_skirtOrigSize[i][0], g_skirtOrigSize[i][1],
                       g_skirtOrigSize[i][2]};
        void *exc = nullptr;
        void *args[] = {&origSz};
        il2cpp_runtime_invoke(g_coll_SetSize, c, args, &exc);
        il2cpp_runtime_invoke(g_coll_UpdateParams, c, nullptr, &exc);
        continue;
      }
      *(unsigned char *)((char *)c + offRS) = g_skirtTaperOn ? 1 : 0;
      float *ct = (float *)((char *)c + 0x20);
      ct[0] = g_skirtOrigCenter[i][0] + g_skirtCenterOfs[0];
      ct[1] = g_skirtOrigCenter[i][1] + g_skirtCenterOfs[1];
      ct[2] = g_skirtOrigCenter[i][2] + g_skirtCenterOfs[2];
      float baseR = g_skirtOrigSize[i][0];
      float hipR;
      if (g_skirtHipRadiusDelta >= 0.0f) {
        hipR = baseR + g_skirtHipRadiusDelta * g_skirtHeightScale;
        float maxR = baseR * 3.0f;
        if (hipR > maxR)
          hipR = maxR;
      } else {
        hipR = baseR * g_skirtRadiusB;
      }
      Vec3 newSize = {baseR * g_skirtRadiusA, hipR,
                      g_skirtOrigSize[i][2] * g_skirtLengthScale};
      void *exc = nullptr;
      void *args[] = {&newSize};
      il2cpp_runtime_invoke(g_coll_SetSize, c, args, &exc);
      il2cpp_runtime_invoke(g_coll_UpdateParams, c, nullptr, &exc);
    }
    if (g_cloth_SetParamChange) {
      void *exc = nullptr;
      il2cpp_runtime_invoke(g_cloth_SetParamChange, skirt, nullptr, &exc);
    }
    Log("[SKIRT] applied taper=%d hipDelta=%.3f lenScale=%.2f",
        g_skirtTaperOn ? 1 : 0, g_skirtHipRadiusDelta, g_skirtLengthScale);
  } __except (1) {
    Log("[SKIRT] exception in ApplySkirtColliderScale");
  }
}

// 恢复原始碰撞参数（解冻/换角色前调用）
static void RestoreSkirtColliders() {
  if (!g_skirtScaleResolved)
    return;
  if (g_skirtBBCIndex < 0 || g_skirtBBCIndex >= g_bbcCount)
    return;
  if (!g_bbcInstances[g_skirtBBCIndex])
    return;
  void *skirt = g_bbcInstances[g_skirtBBCIndex];
  int offProc = SafeOff(OFF_bbc_process, 0xA8, "bbc.process");
  int offCL = SafeOff(OFF_cp_colliderList, 0x3A0, "cp.colliderList");
  int offRS = SafeOff(OFF_radiusSep, 0x45, "coll.radiusSeparation");
  __try {
    void *process = *(void **)((char *)skirt + offProc);
    if (!process)
      return;
    void *colliderList = *(void **)((char *)process + offCL);
    if (!colliderList || (uintptr_t)colliderList < 0x10000)
      return;
    void *items = *(void **)((char *)colliderList + 0x10);
    int count = *(int *)((char *)colliderList + 0x18);
    if (!items || count <= 0 || count > 8)
      return;
    for (int i = 0; i < count && i < g_skirtColliderCount; i++) {
      void *c = *(void **)((char *)items + 0x20 + i * 8);
      if (!c)
        continue;
      *(unsigned char *)((char *)c + offRS) = 0;
      float *ct = (float *)((char *)c + 0x20);
      ct[0] = g_skirtOrigCenter[i][0];
      ct[1] = g_skirtOrigCenter[i][1];
      ct[2] = g_skirtOrigCenter[i][2];
      if (g_coll_SetSize) {
        Vec3 origSize = {g_skirtOrigSize[i][0], g_skirtOrigSize[i][1],
                         g_skirtOrigSize[i][2]};
        void *exc = nullptr;
        void *args[] = {&origSize};
        il2cpp_runtime_invoke(g_coll_SetSize, c, args, &exc);
        if (g_coll_UpdateParams)
          il2cpp_runtime_invoke(g_coll_UpdateParams, c, nullptr, &exc);
      }
    }
    if (g_cloth_SetParamChange) {
      void *exc = nullptr;
      il2cpp_runtime_invoke(g_cloth_SetParamChange, skirt, nullptr, &exc);
    }
    Log("[SKIRT] restored %d colliders", g_skirtColliderCount);
  } __except (1) {
    Log("[SKIRT] exception in RestoreSkirtColliders");
  }
  g_skirtScaleResolved = false;
  g_skirtDirty = true;
  g_skirtRadiusA = 1.0f;
  g_skirtRadiusB = 3.0f;
  g_skirtLengthScale = 1.0f;
  g_skirtCenterOfs[0] = g_skirtCenterOfs[1] = g_skirtCenterOfs[2] = 0;
}

// 冻结时开始：采集 + 标记应用（布料 process 可能没就绪，重试若干帧）
static void SkirtBegin() {
  if (g_bbcCount == 0)
    CollectSkirtCloth();
  g_skirtDirty = true;
  g_skirtRetryFrames = 30;
}

// 每帧维持：需要应用时（含重试）执行
static void SkirtTick() {
  if (!g_skirtDirty)
    return;
  if (g_bbcCount == 0) {
    g_skirtDirty = false;
    return;
  }
  if (g_skirtRetryFrames > 0) {
    g_skirtRetryFrames--;
    g_skirtScaleResolved = false;
  }
  ApplySkirtColliderScale();
}

// 面板滑块改动后调用
static void SkirtMarkDirty() {
  g_skirtDirty = true;
}
