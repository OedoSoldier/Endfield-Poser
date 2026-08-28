#pragma once

// 选中骨骼状态：由 WebUI /api/select 与外部控制通道共享。
// 摆姿编辑 UI 已迁往 Blender，游戏侧只保留选中标记（供 WebUI 高亮/拖拽）。

#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "math/quat_math.h"

#include <cstring>

static int g_selectedBone = -1;

// 在 s_humanBones 中查找 transform 对应的骨骼下标（WebUI 父-子连线用）
static int FindTransformIndex(void *t) {
  if (!t)
    return -1;
  for (int i = 0; i < s_humanBoneCount; i++)
    if (s_humanBones[i].transform == t)
      return i;
  return -1;
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

static bool SelectBoneByName(const char *name) {
  if (!name || !*name)
    return false;
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (strcmp(s_humanBones[i].name, name) == 0) {
      g_selectedBone = i;
      return true;
    }
  }
  for (int i = 0; i < s_humanBoneCount; i++) {
    const char *hn = HumanBoneName(s_humanBones[i].humanBone);
    if (hn && strcmp(hn, name) == 0) {
      g_selectedBone = i;
      return true;
    }
  }
  return false;
}
