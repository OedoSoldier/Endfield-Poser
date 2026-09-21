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
#include "game/cloth.h"

#include <cstring>

static bool g_animatorWasEnabled = true;
// 冻结选项：勾选后连飘带/裙子/头发等从骨一起冻结；默认关 = 从骨保持实时演算。
// 默认冻结飘带/裙子/头发等从骨（钉在冻结瞬间姿态），取消勾选才让它们继续实时演算。
static bool g_freezeAccessories = true;

// ---- FinalIK / 游戏 IK 组件抑制（参照 {EIEM} trojan.h 采集逻辑，AGPL-3.0）----
// 冻结时把角色根上会写骨骼的 IK/动画组件一并禁用，解冻恢复。
#define POSER_MAX_IK_COMPS 8
static void *s_ikBiped[POSER_MAX_IK_COMPS];
static int s_ikBipedCount = 0;
static void *s_ikGrounder[POSER_MAX_IK_COMPS];
static int s_ikGrounderCount = 0;
static void *s_ikLookAt[POSER_MAX_IK_COMPS];
static int s_ikLookAtCount = 0;
static void *s_ikDamper[4];
static int s_ikDamperCount = 0;
static void *s_animatorMono = nullptr;

// 递归采集：遍历角色整个 Transform 层级，按类名收集会写骨骼的组件
static void CollectIKOnTransform(void *t, int depth) {
  if (!t || depth > 6)
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
            for (int i = 0; i < cnt; i++) {
              if (!data[i])
                continue;
              void *cls = il2cpp_object_get_class(data[i]);
              const char *cn = cls ? il2cpp_class_get_name(cls) : "";
              if (!cn)
                continue;
              if (strcmp(cn, "BipedIK") == 0 &&
                  s_ikBipedCount < POSER_MAX_IK_COMPS)
                s_ikBiped[s_ikBipedCount++] = data[i];
              else if (strcmp(cn, "GrounderBipedIK") == 0 &&
                       s_ikGrounderCount < POSER_MAX_IK_COMPS)
                s_ikGrounder[s_ikGrounderCount++] = data[i];
              else if (strcmp(cn, "LookAtComponent") == 0 &&
                       s_ikLookAtCount < POSER_MAX_IK_COMPS)
                s_ikLookAt[s_ikLookAtCount++] = data[i];
              else if (strcmp(cn, "TransformFollowDamper") == 0 &&
                       s_ikDamperCount < 4)
                s_ikDamper[s_ikDamperCount++] = data[i];
              else if (strcmp(cn, "AnimatorMono") == 0)
                s_animatorMono = data[i];
            }
          }
        }
      }
    }
    // 子节点
    if (g_transform_get_childCount && g_transform_GetChild) {
      void *cntBoxed = Invoke(g_transform_get_childCount, t);
      int cnt = cntBoxed ? *(int *)((char *)cntBoxed + 16) : 0;
      for (int i = 0; i < cnt; i++) {
        void *params[] = {&i};
        void *ch = Invoke(g_transform_GetChild, t, params);
        if (ch)
          CollectIKOnTransform(ch, depth + 1);
      }
    }
  } __except (1) {
  }
}

static void DumpNativeIkOnce(); // 定义在下方（诊断用）

static void CollectIKComponents() {
  s_ikBipedCount = s_ikGrounderCount = s_ikLookAtCount = s_ikDamperCount = 0;
  s_animatorMono = nullptr;
  void *rootT = GetCharRootTransform();
  if (!rootT)
    return;
  CollectIKOnTransform(rootT, 0);
  Log("[POSER] IK comps: biped=%d grounder=%d lookAt=%d damper=%d mono=%p",
      s_ikBipedCount, s_ikGrounderCount, s_ikLookAtCount, s_ikDamperCount,
      s_animatorMono);
  DumpNativeIkOnce();
}

// [诊断] 一次性把游戏原生 BipedIK 的字段结构 + 当前字段值打进日志，
// 用来判断能否直接驱动游戏自带的 IK（FinalIK）：找 solvers / 目标 / 权重字段。
static void DumpNativeIkOnce() {
  static bool done = false;
  if (done || s_ikBipedCount == 0)
    return;
  done = true;
  void *biped = s_ikBiped[0];
  DumpFieldsHierarchy(il2cpp_object_get_class(biped));
  __try {
    void *cur = il2cpp_object_get_class(biped);
    int depth = 0;
    while (cur && depth < 6) {
      void *it = nullptr, *f;
      while ((f = il2cpp_class_get_fields(cur, &it))) {
        const char *fn = il2cpp_field_get_name(f);
        size_t fo = il2cpp_field_get_offset(f);
        if (fo >= 0x800)
          continue;
        void *v = *(void **)((char *)biped + fo);
        Log("[IKDUMP]   [0x%X] %s = %p", (int)fo, fn ? fn : "?", v);
      }
      cur = il2cpp_class_get_parent(cur);
      depth++;
    }
  } __except (1) {
    Log("[IKDUMP] fields dump exception");
  }
}

static void SetIKComponentsEnabled(bool on) {
  if (!g_animator_set_enabled)
    return;
  int v = on ? 1 : 0;
  void *params[] = {&v};
  auto Apply = [&](void *c) {
    if (!c)
      return;
    __try {
      Invoke(g_animator_set_enabled, c, params);
    } __except (1) {
    }
  };
  for (int i = 0; i < s_ikBipedCount; i++)
    Apply(s_ikBiped[i]);
  for (int i = 0; i < s_ikGrounderCount; i++)
    Apply(s_ikGrounder[i]);
  for (int i = 0; i < s_ikLookAtCount; i++)
    Apply(s_ikLookAt[i]);
  for (int i = 0; i < s_ikDamperCount; i++)
    Apply(s_ikDamper[i]);
  Apply(s_animatorMono);
  Log("[POSER] IK components %s", on ? "restored" : "disabled");
}

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
  // 只禁用会写 Humanoid 肢体的 IK/动画组件（回弹根源）；
  // 从骨/布料物理保持开启（头发裙摆继续结算），它们不写人体骨骼。
  CollectIKComponents();         // 先采集角色身上的 FinalIK/动画组件
  SetIKComponentsEnabled(false); // 再禁用：BipedIK/Grounder/LookAt/Damper 等
}

static void RestorePoseWriters() {
  SetIKComponentsEnabled(true);
}

// 每帧维持冻结：游戏常会重新启用 Animator/动画组件/IK 组件，
// 只在冻结瞬间关一次不够（"四肢一改就回弹"的根源）。逐帧强制关闭全部写者。
static void MaintainFreeze() {
  if (!g_frozen)
    return;
  SkirtTick();
  if (g_animator_set_enabled) {
    int v = 0;
    void *params[] = {&v};
    auto Disable = [&](void *c) {
      if (!c)
        return;
      __try {
        Invoke(g_animator_set_enabled, c, params);
      } __except (1) {
      }
    };
    Disable(g_charAnimator);
    Disable(g_charAnimComp);
    for (int i = 0; i < s_ikBipedCount; i++)
      Disable(s_ikBiped[i]);
    for (int i = 0; i < s_ikGrounderCount; i++)
      Disable(s_ikGrounder[i]);
    for (int i = 0; i < s_ikLookAtCount; i++)
      Disable(s_ikLookAt[i]);
    for (int i = 0; i < s_ikDamperCount; i++)
      Disable(s_ikDamper[i]);
    Disable(s_animatorMono);
  }
  if (g_freezeAccessories) {
    MaintainAccessoryPhysicsFreeze();
    ApplyAccessorySnapshot(); // 每帧再钉一次从骨，防止物理/动画把它拉回默认
  }
}

static void FreezeCharacter() {
  Log("[POSER] FreezeCharacter called: animator=%p frozen=%d",
      g_charAnimator, (int)g_frozen);
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
  // 1b. 连游戏自己的动画组件一起禁（ECS/自定义动画会绕过 Animator.enabled 直接写骨）
  if (g_charAnimComp && g_animator_set_enabled) {
    __try {
      int v = 0;
      void *params[] = {&v};
      Invoke(g_animator_set_enabled, g_charAnimComp, params);
    } __except (1) {
    }
  }
  // 2. 固化当前帧姿势为编辑基线（含从骨）
  PinCurrentPose();
  if (g_freezeAccessories && s_accessoryChains.empty())
    RebuildAccessories(); // 先保证从骨表存在，快照才反映冻结瞬间姿势
  CaptureAccessorySnapshot();
  Log("[POSER] Freeze: pose pinned");
  // 3. 抑制其余骨骼写者：FinalIK/Grounder/LookAt/Damper + 从骨物理。
  //    （之前这个调用缺失，导致四肢一直被游戏 IK 写回、一改就弹回去）
  SuppressPoseWriters();
  Log("[POSER] Freeze: writers suppressed");
  SkirtBegin();
  Log("[POSER] Freeze: skirt begin");
  if (g_freezeAccessories) {
    SetAllPhysicsEnabled(false);
    ApplyAccessorySnapshot(); // 物理禁用后立刻把从骨钉到冻结瞬间姿势，避免回落默认
    Log("[POSER] Freeze: accessory physics disabled (option ON)");
  }
  g_frozen = true;
  Log("[POSER] Frozen (animator was enabled=%d)", g_animatorWasEnabled ? 1 : 0);
}

static void UnfreezeCharacter() {
  if (!g_frozen)
    return;
  // 先恢复动画驱动（Animator + 游戏动画组件），再恢复从骨物理/布料，
  // 否则布料/物理在动画未驱动时重启会卡在冻结姿态。
  if (g_animator_set_enabled && g_animatorWasEnabled) {
    __try {
      int v = 1;
      void *params[] = {&v};
      Invoke(g_animator_set_enabled, g_charAnimator, params);
    } __except (1) {
    }
  }
  if (g_charAnimComp && g_animator_set_enabled) {
    __try {
      int v = 1;
      void *params[] = {&v};
      Invoke(g_animator_set_enabled, g_charAnimComp, params);
    } __except (1) {
    }
  }
  // 恢复 IK/物理写者（在动画重新驱动之后，避免布料卡在冻结姿态）
  RestorePoseWriters();
  if (g_freezeAccessories) {
    SetAllPhysicsEnabled(true);
    Log("[POSER] Freeze: accessory physics restored (option ON)");
  }
  RestoreSkirtColliders();
  g_frozen = false;
  Log("[POSER] Unfrozen");
}
