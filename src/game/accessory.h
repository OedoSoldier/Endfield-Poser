#pragma once

// Task 2.4：从骨（头发/配饰/飘带/衣角等动态骨骼）采集 + 物理开关 + 锁定。
//
// 从骨 = 角色根下除 Humanoid 55 根外的其余骨骼链（通常是 DynamicBone/Cloth 类
// 程序化物理驱动的链）。本模块：
//   1. 递归遍历根 Transform 全部子骨，识别非 Humanoid 从骨并按父子连续链分组；
//   2. 对每条从骨的 GameObject 枚举组件，识别物理/布料组件（类名探针，见
//      kPhysicsClassSubstrings），提供逐链/逐骨禁用物理（Behaviour.set_enabled）；
//   3. 逐骨锁定：锁定的骨在快照恢复/FK/镜像等操作中被跳过，并钉在当前姿势。
//
// [in-game] 探针说明：实际物理组件类名（DynamicBone/CommonDynamicBone/Cloth/
// MagicaCloth/SpringBone…）需在游戏内核对；命中即按 Behaviour 禁用，未命中则
// 走"钉在快照值"的兜底（ApplyAccessorySnapshot 跳过被禁物理链）。

#include "game/skeleton.h"

#include <cstring>
#include <string>
#include <vector>

// 已知的动态物理/布料组件类名子串（[in-game] 探针确认后增补）
static const char *const kPhysicsClassSubstrings[] = {
    "DynamicBone", "CommonDynamicBone", "Cloth", "MagicaCloth",
    "SpringBone", "BoneWind", nullptr};

struct AccessoryBone {
  void *transform;
  char name[128];
  int parentIdx;  // 原始遍历列表中的父骨下标（-1 = 根）
  int chainId;    // 所属从骨链 id
  bool locked;    // 锁定：钉在当前姿势，恢复/FK/镜像跳过
  Vec3 localPos;
  Quat localRot;
  std::vector<void *> physicsComps; // 该骨 GameObject 上的物理/布料组件实例
};

struct AccessoryChain {
  int rootBoneIdx;
  char name[128];
  bool physicsEnabled;
  std::vector<int> bones; // 链上骨在 s_accessoryBones 中的下标
};

struct RawAccessoryBone {
  void *transform;
  char name[128];
  int parent; // 遍历列表父下标
  bool humanoid;
};

static std::vector<AccessoryBone> s_accessoryBones;
static std::vector<AccessoryChain> s_accessoryChains;
static std::vector<RawAccessoryBone> s_rawBones;

static bool IsHumanoidBoneTransform(void *t) {
  for (int i = 0; i < s_humanBoneCount; i++)
    if (s_humanBones[i].transform == t)
      return true;
  return false;
}

static bool IsPhysicsComponent(void *comp) {
  if (!comp)
    return false;
  __try {
    void *cls = il2cpp_object_get_class(comp);
    const char *cn = cls ? il2cpp_class_get_name(cls) : "";
    for (int i = 0; kPhysicsClassSubstrings[i]; i++)
      if (strstr(cn, kPhysicsClassSubstrings[i]))
        return true;
  } __except (1) {
  }
  return false;
}

// 枚举某骨 GameObject 上的物理组件到 AccessoryBone
static void CollectPhysicsComponents(AccessoryBone &b) {
  if (!g_component_get_gameObject || !g_gameObject_GetComponents ||
      !g_componentClass)
    return;
  __try {
    void *go = Invoke(g_component_get_gameObject, b.transform);
    if (!go)
      return;
    void *compType = il2cpp_class_get_type(g_componentClass);
    void *typeObj = compType ? il2cpp_type_get_object(compType) : nullptr;
    if (!typeObj)
      return;
    void *args[] = {typeObj};
    void *arr = Invoke(g_gameObject_GetComponents, go, args);
    if (!arr)
      return;
    int cnt = *(int *)((char *)arr + 24);
    void **data = (void **)((char *)arr + 32);
    for (int i = 0; i < cnt; i++)
      if (data[i] && IsPhysicsComponent(data[i]))
        b.physicsComps.push_back(data[i]);
    if (!b.physicsComps.empty())
      Log("[POSER] Bone '%s': %zu physics comp(s)", b.name,
          b.physicsComps.size());
  } __except (1) {
  }
}

static void WalkTransforms(void *t, int parentIdx, int depth) {
  if (!t || depth > 24)
    return;
  RawAccessoryBone rb;
  rb.transform = t;
  rb.parent = parentIdx;
  rb.humanoid = IsHumanoidBoneTransform(t);
  GetBoneName(t, rb.name, sizeof(rb.name));
  int idx = (int)s_rawBones.size();
  s_rawBones.push_back(rb);
  __try {
    void *cntBoxed = Invoke(g_transform_get_childCount, t);
    int cnt = cntBoxed ? *(int *)((char *)cntBoxed + 16) : 0;
    for (int i = 0; i < cnt; i++) {
      void *params[] = {&i};
      void *child = Invoke(g_transform_GetChild, t, params);
      if (child)
        WalkTransforms(child, idx, depth + 1);
    }
  } __except (1) {
  }
}

// 重建从骨列表 + 链分组（角色切换/冻结后调用）
static void RebuildAccessories() {
  s_accessoryBones.clear();
  s_accessoryChains.clear();
  s_rawBones.clear();

  RebuildHumanBones(); // 确保 humanoid 列表最新（IsHumanoidBoneTransform 依赖）

  void *root = GetCharRootTransform();
  if (!root || !g_transform_get_childCount || !g_transform_GetChild) {
    Log("[POSER] Accessory: root transform unavailable, skipped");
    return;
  }
  WalkTransforms(root, -1, 0);
  int n = (int)s_rawBones.size();

  std::vector<int> chainOf(n, -1); // raw idx -> chain id
  for (int i = 0; i < n; i++) {
    if (s_rawBones[i].humanoid)
      continue;
    // 向上找最近的非 humanoid 祖先，若它已入链则并入该链，否则新开链
    int p = s_rawBones[i].parent;
    while (p >= 0) {
      if (!s_rawBones[p].humanoid && chainOf[p] >= 0)
        break;
      if (s_rawBones[p].humanoid) {
        p = -1;
        break;
      }
      p = s_rawBones[p].parent;
    }
    int cid = (p >= 0) ? chainOf[p] : -1;
    if (cid < 0) {
      cid = (int)s_accessoryChains.size();
      AccessoryChain c;
      c.rootBoneIdx = -1;
      c.physicsEnabled = true;
      snprintf(c.name, sizeof(c.name), "%s",
               s_rawBones[i].name[0] ? s_rawBones[i].name : "chain");
      s_accessoryChains.push_back(c);
    }

    AccessoryBone b;
    b.transform = s_rawBones[i].transform;
    b.parentIdx = s_rawBones[i].parent;
    b.chainId = cid;
    b.locked = false;
    snprintf(b.name, sizeof(b.name), "%s",
             s_rawBones[i].name[0] ? s_rawBones[i].name : "bone");
    b.localPos = GetBoneLocalPos(b.transform);
    b.localRot = GetBoneLocalRot(b.transform);
    CollectPhysicsComponents(b);

    int bidx = (int)s_accessoryBones.size();
    s_accessoryBones.push_back(b);
    chainOf[i] = cid;
    if (s_accessoryChains[cid].rootBoneIdx < 0)
      s_accessoryChains[cid].rootBoneIdx = bidx;
    s_accessoryChains[cid].bones.push_back(bidx);
  }
  Log("[POSER] Accessories rebuilt: %zu chains, %zu bones",
      s_accessoryChains.size(), s_accessoryBones.size());
}

// ---- 物理开关 ----
static void SetPhysicsEnabled(int chainId, bool enabled) {
  if (chainId < 0 || chainId >= (int)s_accessoryChains.size())
    return;
  AccessoryChain &c = s_accessoryChains[chainId];
  c.physicsEnabled = enabled;
  for (int bidx : c.bones) {
    AccessoryBone &b = s_accessoryBones[bidx];
    for (void *comp : b.physicsComps) {
      if (!g_animator_set_enabled)
        continue;
      __try {
        int v = enabled ? 1 : 0;
        void *params[] = {&v};
        Invoke(g_animator_set_enabled, comp, params);
      } __except (1) {
      }
    }
  }
  Log("[POSER] Chain '%s' physics=%s", c.name, enabled ? "ON" : "OFF");
}

static void SetAllPhysicsEnabled(bool enabled) {
  for (int i = 0; i < (int)s_accessoryChains.size(); i++)
    SetPhysicsEnabled(i, enabled);
}

// ---- 锁定（钉在当前姿势；恢复/FK/镜像跳过）----
static void SetAccessoryBoneLocked(int boneIdx, bool locked) {
  if (boneIdx < 0 || boneIdx >= (int)s_accessoryBones.size())
    return;
  AccessoryBone &b = s_accessoryBones[boneIdx];
  if (locked) {
    b.localPos = GetBoneLocalPos(b.transform); // 钉在当前姿势
    b.localRot = GetBoneLocalRot(b.transform);
  }
  b.locked = locked;
  Log("[POSER] Bone '%s' locked=%d", b.name, locked ? 1 : 0);
}

static void SetChainLocked(int chainId, bool locked) {
  if (chainId < 0 || chainId >= (int)s_accessoryChains.size())
    return;
  for (int bidx : s_accessoryChains[chainId].bones)
    SetAccessoryBoneLocked(bidx, locked);
}

// ---- 快照（恢复"冻结帧姿态"，跳过锁定骨）----
static void CaptureAccessorySnapshot() {
  for (AccessoryBone &b : s_accessoryBones) {
    b.localPos = GetBoneLocalPos(b.transform);
    b.localRot = GetBoneLocalRot(b.transform);
  }
}

static void ApplyAccessorySnapshot() {
  for (AccessoryBone &b : s_accessoryBones) {
    if (b.locked)
      continue; // 锁定骨保持钉住姿势
    SetBoneLocalPos(b.transform, b.localPos);
    SetBoneLocalRot(b.transform, b.localRot);
  }
}

// 每帧维护：角色切换时重建从骨列表（供主循环调用）
static void AccessoryFrameTick() {
  if (g_charChanged) {
    g_charChanged = false;
    RebuildAccessories();
  }
}
