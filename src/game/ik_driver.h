#pragma once

// Task 3.2：摆姿 IK 驱动。
// 对四肢链（左/右臂、左/右腿）做解析式 2-bone IK：每帧读取 根→中→末端 的世界
// 坐标，用 SolveTwoBone 解出目标位姿；把"当前方向→目标方向"的方向差
// （Quat::FromTo）折算成根/中骨的世界旋转增量，再变换回各自父系局部旋转写回：
//   localDelta = conj(parentWorld) * worldDelta * parentWorld
//   local'      = localDelta * local
// 目标位置 g_ikTarget 由 editor 层的 gizmo（ImGuizmo）在冻结态下拖拽修改。
//
// 可选驱动原生 BipedIK（RootMotion.FinalIK，[in-game] 探针收敛，默认关闭）：
// 收集角色根上的 BipedIK 组件，把当前链对应 LimbIK 的 IKPosition/权重写死为目标。
//
// 依赖：game_hooks.h（GetHumanoidBone / GetBoneWorld* / SetBoneLocal*）、
//       skeleton.h（锁定检查）。冻结态（g_frozen）由 freeze.h 维护。

#include "core/game_hooks.h"
#include "math/quat_math.h"
#include "math/ik_two_bone.h"
#include "game/skeleton.h"

#include <cstring>

// ---- 四肢链定义（2-bone：根→中→末端）----
enum IkChainId { IK_CHAIN_ARML = 0, IK_CHAIN_ARMR, IK_CHAIN_LEGL, IK_CHAIN_LEGR };
static const int kIkChainCount = 4;

struct IkChainDef {
  const char *name;
  HumanBodyBones root, mid, end;
};

static const IkChainDef kIkChains[kIkChainCount] = {
    {u8"左臂", LeftUpperArm, LeftLowerArm, LeftHand},
    {u8"右臂", RightUpperArm, RightLowerArm, RightHand},
    {u8"左腿", LeftUpperLeg, LeftLowerLeg, LeftFoot},
    {u8"右腿", RightUpperLeg, RightLowerLeg, RightFoot},
};

// ---- 状态（editor 面板读写）----
static IkChainId g_ikChain = IK_CHAIN_ARMR;
static bool g_ikActiveChain[kIkChainCount] = {}; // 每条链独立 IK 开关（可同时多链）
static bool g_ikActive = false;   // 聚合：任一链开启（面板/兼容）
static bool g_ikNative = false;   // true=驱动原生 BipedIK；false=自研 2-bone
static Vec3 g_ikTargets[kIkChainCount] = {};      // 每链独立目标（gizmo 拖拽更新）
static bool g_ikTargetValidChain[kIkChainCount] = {};

static bool IkBoneLocked(HumanBodyBones b) {
  int i = FindHumanBoneIndex(b);
  return i >= 0 && s_humanBones[i].locked;
}

// 切换链：目标失效，下一帧按当前末端位置重新初始化
static void IkSetChain(IkChainId c) {
  if (c >= 0 && c < kIkChainCount) {
    g_ikChain = c;
    g_ikTargetValidChain[c] = false;
  }
}

// 设置某条链的 IK 开关，并重算聚合 g_ikActive
static void SetChainIkActive(IkChainId c, bool on) {
  if (c < 0 || c >= kIkChainCount)
    return;
  g_ikActiveChain[c] = on;
  g_ikTargetValidChain[c] = false;
  g_ikActive = false;
  for (int i = 0; i < kIkChainCount; i++)
    if (g_ikActiveChain[i])
      g_ikActive = true;
}

static Vec3 IkTargetPos() {
  const IkChainDef &ch = kIkChains[(int)g_ikChain];
  void *endT = GetHumanoidBone(ch.end);
  if (!g_ikTargetValidChain[(int)g_ikChain] && endT) {
    g_ikTargets[(int)g_ikChain] = GetBoneWorldPos(endT);
    g_ikTargetValidChain[(int)g_ikChain] = true;
  }
  return g_ikTargets[(int)g_ikChain];
}

static void IkSetTarget(Vec3 p) {
  g_ikTargets[(int)g_ikChain] = p;
  g_ikTargetValidChain[(int)g_ikChain] = true;
}

// 把世界旋转增量 d 作用到 transform 的局部旋转（考虑父系链）
static void ApplyWorldRotDelta(void *t, Quat d) {
  if (!t)
    return;
  Quat curLocal = GetBoneLocalRot(t);
  Quat localDelta = d;
  if (g_transform_get_parent) {
    __try {
      void *parent = Invoke(g_transform_get_parent, t);
      if (parent) {
        Quat pw = GetBoneWorldRot(parent);
        localDelta = NormQ(Conj(pw) * d * pw);
      }
    } __except (1) {
    }
  }
  SetBoneLocalRot(t, NormQ(localDelta * curLocal));
}

// 解析式 2-bone 求解并写回根/中骨局部旋转。pole 取当前肘/膝位置，
// 保证弯折方向不因目标移动而翻转。
static void SolveTwoBoneChain(void *rootT, void *midT, void *endT, Vec3 target) {
  Vec3 a = GetBoneWorldPos(rootT);
  Vec3 b0 = GetBoneWorldPos(midT);
  Vec3 c0 = GetBoneWorldPos(endT);
  float len1 = Len(b0 - a), len2 = Len(c0 - b0);
  // 骨骼退化或目标太近（奇点）时跳过，避免手/脚乱飞
  if (len1 < 1e-4f || len2 < 1e-4f)
    return;
  if (Len(target - a) < len2 * 0.5f)
    return;
  Vec3 b = b0, c = c0;
  SolveTwoBone(a, b, c, target, b0 /*pole=当前肘/膝方向*/, true);
  if (!(b.x == b.x) || !(c.x == c.x)) // NaN 保护
    return;
  // 根骨：上臂/大腿方向对齐（b 由求解器给出）
  Vec3 curUp = Norm(b0 - a), newUp = Norm(b - a);
  if (Len(newUp) < 1e-4f)
    return;
  ApplyWorldRotDelta(rootT, Quat::FromTo(curUp, newUp));
  // 中骨：前臂/小腿方向对齐（c 已命中 target）
  Vec3 curFore = Norm(c0 - b0), newFore = Norm(c - b);
  if (Len(newFore) < 1e-4f)
    return;
  ApplyWorldRotDelta(midT, Quat::FromTo(curFore, newFore));
}

// ---- 原生 BipedIK（可选增强，[in-game] 探针收敛）----
// 复用参考（EIEM 已验证，AGPL-3.0，见 D:\PROJECTS\EIEM\src\globals.h / init.h）：
//   BipedIK → solvers 字段偏移        OFF_BIPEDIK_SOLVERS 0x40
//   solvers → 各链 LimbIK 偏移        LEFT_FOOT 0x10 / RIGHT_FOOT 0x18 /
//                                    LEFT_HAND 0x20 / RIGHT_HAND 0x28
//   IKSolver → IKPosition 偏移        IKPOS_X 0x14 / Y 0x18 / Z 0x1C
//   IKSolver → IKPositionWeight 偏移  IKPOS_WEIGHT 0x20
//   hook 点（均可用 FindMethod 动态解析，失败再按 RVA 回退）：
//     SolverManager.LateUpdate        RVA 0x035BD200
//     BipedIK.UpdateSolver            RVA 0x0326A380
//     IKSolverTrigonometric.OnUpdate  RVA 0x032759E0
static void *g_bipedIK = nullptr;     // RootMotion.FinalIK.BipedIK 组件
static void *g_bipedSolvers = nullptr; // solvers 字段（BipedIKSolvers）
static int OFF_limbSolver = -1;       // 当前链 LimbIK 在 solvers 上的偏移
static int OFF_ikPosition = -1;       // IKPosition(Vector3) 偏移
static int OFF_ikWeight = -1;         // IKPositionWeight(float) 偏移

// 收集角色根上的 BipedIK 组件（GetComponents(Component) 按类名匹配）
static void CollectBipedIK() {
  if (g_bipedIK || !g_gameObject_GetComponents || !g_component_get_gameObject)
    return;
  __try {
    void *rootT = GetCharRootTransform();
    void *go = rootT ? Invoke(g_component_get_gameObject, rootT) : nullptr;
    if (!go)
      return;
    void *params[] = {&g_componentClass};
    void *arr = Invoke(g_gameObject_GetComponents, go, params);
    if (!arr)
      return;
    int n = *(int32_t *)((char *)arr + IL2CPP_ARRAY_LEN);
    char *data = (char *)arr + IL2CPP_ARRAY_DATA;
    for (int i = 0; i < n; i++) {
      void *comp = *(void **)(data + i * sizeof(void *));
      if (!comp)
        continue;
      void *klass = il2cpp_object_get_class(comp);
      const char *cn = klass ? il2cpp_class_get_name(klass) : "";
      if (cn && strstr(cn, "BipedIK")) {
        g_bipedIK = comp;
        Log("[IK] BipedIK component found: %s", cn);
        // 解析 solvers 字段（BipedIKSolvers 对象）
        const char *solNames[] = {"solvers", "m_solvers"};
        const char *matched = nullptr;
        int so = FindFieldInHierarchy(klass, solNames, 2, &matched);
        if (so >= 0) {
          g_bipedSolvers = *(void **)((char *)comp + so);
          Log("[IK] BipedIK.solvers @0x%X (%s)", so, matched);
        } else {
          Log("[WARN] BipedIK.solvers unresolved (native IK disabled)");
          g_bipedIK = nullptr;
        }
        return;
      }
    }
  } __except (1) {
  }
}

// 解析当前链 LimbIK 的 IKPosition/IKPositionWeight 偏移（懒加载）
static bool ResolveNativeLimbSolver() {
  CollectBipedIK();
  if (!g_bipedSolvers)
    return false;
  const IkChainDef &ch = kIkChains[(int)g_ikChain];
  const char *limbNames[4] = {"leftArm", "rightArm", "leftLeg", "rightLeg"};
  const char *limbNames2[4] = {"m_leftArm", "m_rightArm", "m_leftLeg", "m_rightLeg"};
  int ci = (int)g_ikChain;
  __try {
    void *solversClass = il2cpp_object_get_class(g_bipedSolvers);
    const char *limbMatch = nullptr;
    const char *ln[2] = {limbNames[ci], limbNames2[ci]};
    OFF_limbSolver = FindFieldInHierarchy(solversClass, ln, 2, &limbMatch);
    if (OFF_limbSolver < 0) {
      Log("[WARN] Limb solver %s unresolved (native IK disabled)", limbNames[ci]);
      return false;
    }
    void *limb = *(void **)((char *)g_bipedSolvers + OFF_limbSolver);
    if (!limb)
      return false;
    void *limbClass = il2cpp_object_get_class(limb);
    const char *ikNames[] = {"IKPosition", "m_IKPosition"};
    const char *ikMatch = nullptr;
    OFF_ikPosition = FindFieldInHierarchy(limbClass, ikNames, 2, &ikMatch);
    const char *wNames[] = {"IKPositionWeight", "m_IKPositionWeight"};
    const char *wMatch = nullptr;
    OFF_ikWeight = FindFieldInHierarchy(limbClass, wNames, 2, &wMatch);
    OFF_ikPosition = SafeOff(OFF_ikPosition, 0x24, "ikPosition");
    OFF_ikWeight = SafeOff(OFF_ikWeight, 0x34, "ikWeight");
    Log("[IK] Limb %s: IKPosition@0x%X weight@0x%X", limbNames[ci],
        OFF_ikPosition, OFF_ikWeight);
    return true;
  } __except (1) {
    return false;
  }
}

// 原生驱动：把当前链 LimbIK 的 IKPosition 拖到 g_ikTarget、权重置 1
static void NativeIkFrameTick(const IkChainDef &ch) {
  if (!ResolveNativeLimbSolver())
    return;
  __try {
    void *limb = *(void **)((char *)g_bipedSolvers + OFF_limbSolver);
    if (!limb)
      return;
    if (!g_ikTargetValidChain[(int)g_ikChain]) {
      void *endT = GetHumanoidBone(ch.end);
      if (endT)
        g_ikTargets[(int)g_ikChain] = GetBoneWorldPos(endT);
      g_ikTargetValidChain[(int)g_ikChain] = true;
    }
    *(Vec3 *)((char *)limb + OFF_ikPosition) = g_ikTargets[(int)g_ikChain];
    *(float *)((char *)limb + OFF_ikWeight) = 1.0f;
  } __except (1) {
  }
}

// 每帧驱动（冻结态 + IK 开启时调用；editor 的 gizmo 先改 g_ikTarget）
static void IkFrameTick() {
  if (!g_frozen)
    return;
  // 原生模式：仍只驱动当前选中单链（实验功能）
  if (g_ikNative) {
    if (g_ikActiveChain[(int)g_ikChain]) {
      const IkChainDef &ch = kIkChains[(int)g_ikChain];
      void *rootT = GetHumanoidBone(ch.root);
      void *midT = GetHumanoidBone(ch.mid);
      void *endT = GetHumanoidBone(ch.end);
      if (rootT && midT && endT &&
          !IkBoneLocked(ch.root) && !IkBoneLocked(ch.mid))
        NativeIkFrameTick(ch);
    }
    return;
  }
  // 自研 2-bone：遍历所有开启 IK 的链（可多链同时，FK/IK 混合）
  for (int ci = 0; ci < kIkChainCount; ci++) {
    if (!g_ikActiveChain[ci])
      continue;
    const IkChainDef &ch = kIkChains[ci];
    void *rootT = GetHumanoidBone(ch.root);
    void *midT = GetHumanoidBone(ch.mid);
    void *endT = GetHumanoidBone(ch.end);
    if (!rootT || !midT || !endT)
      continue;
    if (IkBoneLocked(ch.root) || IkBoneLocked(ch.mid))
      continue; // 链上根/中骨被锁定则不做 IK
    if (!g_ikTargetValidChain[ci]) {
      g_ikTargets[ci] = GetBoneWorldPos(endT);
      g_ikTargetValidChain[ci] = true;
    }
    SolveTwoBoneChain(rootT, midT, endT, g_ikTargets[ci]);
  }
}
