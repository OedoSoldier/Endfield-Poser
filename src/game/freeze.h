#pragma once

// Task 2.2：角色冻结系统。
// 目标（用户需求）："摆动作时不能突然没了"——冻结 = 抑制动画/IK/形态/布料对
// 骨骼的覆盖，让摆好的姿势稳定。核心手段：
//   1. 关闭 Animator（主写者消失，骨骼停在当前帧姿势）
//   2. PinCurrentPose() 固化当前帧为可编辑基线（供复位/FK 参考）
//   3. 其余写者逐项抑制：从骨物理组件（Task 2.4 accessory.h）、FinalIK/
//      SkeletalMorph/布料（随各 Task 探针补全，见下方注释）
//
// 不做每帧快照重写——否则 FK 拖骨会被立刻打回。编辑直接写骨骼，
// 因为 Animator 已关，姿势自然稳定。

#include "game/accessory.h"

#include <cstring>

static bool g_frozen = false;
static bool g_animatorWasEnabled = true;

static bool AnimatorIsEnabled() {
  if (!g_charAnimator || !g_animator_get_enabled)
    return true;
  __try {
    void *boxed = Invoke(g_animator_get_enabled, g_charAnimator);
    return boxed && *(bool *)((char *)boxed + 16);
  } __except (1) {
    return true;
  }
}

// 抑制除 Animator 外的骨骼写者。
// 说明（[in-game] 探针，随对应 Task 补全）：
//   - 从骨动态骨骼/布料物理 → SetAllPhysicsEnabled(false)（本模块）
//   - FinalIK（BipedIK/Grounder/LookAt）→ 按 {EIEM} s_bipedIK/s_grounderIK
//     收集逻辑，把 IKSolver weight 写 0 或禁用组件（Task 3.2 联动）
//   - SkeletalMorphCore.Update（写 m_allMorphBoneDirty=false 跳过）→ Task 4.2
//   - 角色 ParticleSystem.Pause → 可选
static void SuppressPoseWriters() {
  SetAllPhysicsEnabled(false); // 从骨物理关闭：物理不再每帧写骨
}

static void RestorePoseWriters() {
  SetAllPhysicsEnabled(true); // 解冻恢复从骨物理
}

static void FreezeCharacter() {
  if (!g_charAnimator || g_frozen)
    return;
  // 1. 记录并关闭 Animator
  g_animatorWasEnabled = AnimatorIsEnabled();
  if (g_animator_set_enabled) {
    __try {
      int v = 0;
      void *params[] = {&v};
      Invoke(g_animator_set_enabled, g_charAnimator, params);
    } __except (1) {
    }
  }
  // 2. 固化当前帧姿势为编辑基线（含从骨）
  PinCurrentPose();
  CaptureAccessorySnapshot();
  // 3. 抑制其余写者
  SuppressPoseWriters();
  g_frozen = true;
  Log("[POSER] Frozen (animator was enabled=%d)", g_animatorWasEnabled ? 1 : 0);
}

static void UnfreezeCharacter() {
  if (!g_frozen)
    return;
  RestorePoseWriters();
  if (g_animator_set_enabled && g_animatorWasEnabled) {
    __try {
      int v = 1;
      void *params[] = {&v};
      Invoke(g_animator_set_enabled, g_charAnimator, params);
    } __except (1) {
    }
  }
  g_frozen = false;
  Log("[POSER] Unfrozen");
}
