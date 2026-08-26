#pragma once

// Task 2.3：Humanoid 骨骼列表 + 姿势快照/恢复。
// 依赖 game_hooks.h 的骨骼句柄封装。冻结时由 freeze.h 调 PinCurrentPose()
// 固化当前帧姿势作为可编辑基线；FK/IK/形态编辑在此基础上进行，
// ApplyPoseSnapshot() 用于"回到冻结帧"或复位。
//
// 锁定位（locked）默认为 false；Task 2.4 从骨锁定与 FK 面板会按需置位。

#include "core/game_hooks.h"
#include "math/quat_math.h"

#include <cstring>

struct BoneHandle {
  HumanBodyBones humanBone;
  void *transform;
  char name[128];
  bool locked; // 锁定后：ApplyPoseSnapshot/FK/镜像等跳过此骨
  Vec3 localPos;
  Quat localRot;
};

static BoneHandle s_humanBones[kHumanBoneCount];
static int s_humanBoneCount = 0;

// 重建骨骼列表（角色切换后调用）
static void RebuildHumanBones() {
  s_humanBoneCount = 0;
  for (int b = 0; b < kHumanBoneCount; b++) {
    void *t = GetHumanoidBone((HumanBodyBones)b);
    if (!t)
      continue;
    BoneHandle &bh = s_humanBones[s_humanBoneCount];
    bh.humanBone = (HumanBodyBones)b;
    bh.transform = t;
    bh.locked = false;
    bh.localPos = GetBoneLocalPos(t);
    bh.localRot = GetBoneLocalRot(t);
    GetBoneName(t, bh.name, sizeof(bh.name));
    if (bh.name[0] == 0)
      snprintf(bh.name, sizeof(bh.name), "%s", HumanBoneName(b));
    s_humanBoneCount++;
  }
  Log("[POSER] Skeleton rebuilt: %d humanoid bones", s_humanBoneCount);
}

// 捕获当前帧全部骨骼 local pos/rot 到快照
static void CapturePoseSnapshot() {
  for (int i = 0; i < s_humanBoneCount; i++) {
    s_humanBones[i].localPos = GetBoneLocalPos(s_humanBones[i].transform);
    s_humanBones[i].localRot = GetBoneLocalRot(s_humanBones[i].transform);
  }
}

// 把快照写回骨骼（复位/回到冻结帧），跳过锁定骨
static void ApplyPoseSnapshot() {
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (s_humanBones[i].locked)
      continue;
    SetBoneLocalPos(s_humanBones[i].transform, s_humanBones[i].localPos);
    SetBoneLocalRot(s_humanBones[i].transform, s_humanBones[i].localRot);
  }
}

// 固化当前姿势为编辑基线：重建列表 + 采集快照
static void PinCurrentPose() {
  RebuildHumanBones();
  CapturePoseSnapshot();
  Log("[POSER] Pose pinned: %d bones", s_humanBoneCount);
}

// T-pose：所有骨 localRotation 置 0（保留位置），跳过锁定骨
static void ApplyTPose() {
  for (int i = 0; i < s_humanBoneCount; i++) {
    if (s_humanBones[i].locked)
      continue;
    SetBoneLocalRot(s_humanBones[i].transform, Quat{0, 0, 0, 1});
  }
}

// 按 HumanBodyBones 找骨骼在列表中的下标（找不到返回 -1）
static int FindHumanBoneIndex(HumanBodyBones bone) {
  for (int i = 0; i < s_humanBoneCount; i++)
    if (s_humanBones[i].humanBone == bone)
      return i;
  return -1;
}
