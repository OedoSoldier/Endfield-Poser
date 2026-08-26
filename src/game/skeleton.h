#pragma once

// Task 2.3：Humanoid 骨骼列表 + 姿势快照/恢复。
// 依赖 game_hooks.h 的骨骼句柄封装。冻结时由 freeze.h 调 PinCurrentPose()
// 固化当前帧姿势作为可编辑基线；FK/IK/形态编辑在此基础上进行，
// ApplyPoseSnapshot() 用于"回到冻结帧"或复位。
//
// 锁定位（locked）默认为 false；Task 2.4 从骨锁定与 FK 面板会按需置位。

#include "core/game_hooks.h"
#include "math/quat_math.h"
#include "math/pose_file.h"

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

// ---- 姿态操作（Task 3.3）：镜像 / 存取 ----
// 镜像四元数（跨左右平面 X=0）：q' = (-qx, qy, qz, -qw)。
// Unity Humanoid 左右骨局部轴互为镜像，故直接用该共轭公式。
static Quat MirrorQuat(Quat q) { return Quat{-q.x, q.y, q.z, -q.w}; }
static Vec3 MirrorPos(Vec3 p) { return Vec3{-p.x, p.y, p.z}; }

// 左右对称对（镜像/复制用）
struct SymPair { HumanBodyBones l, r; };
static const SymPair kSymPairs[] = {
    {LeftShoulder, RightShoulder}, {LeftUpperArm, RightUpperArm},
    {LeftLowerArm, RightLowerArm}, {LeftHand, RightHand},
    {LeftUpperLeg, RightUpperLeg}, {LeftLowerLeg, RightLowerLeg},
    {LeftFoot, RightFoot},         {LeftToes, RightToes},
    {LeftThumbProximal, RightThumbProximal},
    {LeftThumbIntermediate, RightThumbIntermediate},
    {LeftThumbDistal, RightThumbDistal},
    {LeftIndexProximal, RightIndexProximal},
    {LeftIndexIntermediate, RightIndexIntermediate},
    {LeftIndexDistal, RightIndexDistal},
    {LeftMiddleProximal, RightMiddleProximal},
    {LeftMiddleIntermediate, RightMiddleIntermediate},
    {LeftMiddleDistal, RightMiddleDistal},
    {LeftRingProximal, RightRingProximal},
    {LeftRingIntermediate, RightRingIntermediate},
    {LeftRingDistal, RightRingDistal},
    {LeftLittleProximal, RightLittleProximal},
    {LeftLittleIntermediate, RightLittleIntermediate},
    {LeftLittleDistal, RightLittleDistal},
};
static const int kSymPairCount = (int)(sizeof(kSymPairs) / sizeof(kSymPairs[0]));

// 把一侧位姿镜像复制到另一侧（leftToRight=false 表示 R→L）
static void MirrorPose(bool leftToRight) {
  int done = 0;
  for (int i = 0; i < kSymPairCount; i++) {
    int si = FindHumanBoneIndex(leftToRight ? kSymPairs[i].l : kSymPairs[i].r);
    int di = FindHumanBoneIndex(leftToRight ? kSymPairs[i].r : kSymPairs[i].l);
    if (si < 0 || di < 0)
      continue;
    if (s_humanBones[si].locked || s_humanBones[di].locked)
      continue;
    Quat q = GetBoneLocalRot(s_humanBones[si].transform);
    Vec3 p = GetBoneLocalPos(s_humanBones[si].transform);
    SetBoneLocalRot(s_humanBones[di].transform, MirrorQuat(q));
    SetBoneLocalPos(s_humanBones[di].transform, MirrorPos(p));
    done++;
  }
  Log("[POSER] Mirrored %s: %d bones", leftToRight ? "L->R" : "R->L", done);
}

// 采集当前全部 Humanoid 骨到位姿文档（存盘用）
static PoseDoc CapturePoseDoc(const char *name) {
  PoseDoc doc;
  doc.name = name ? name : "";
  for (int i = 0; i < s_humanBoneCount; i++) {
    PoseBone pb;
    pb.name = s_humanBones[i].name;
    pb.pos = GetBoneLocalPos(s_humanBones[i].transform);
    pb.rot = GetBoneLocalRot(s_humanBones[i].transform);
    doc.bones.push_back(pb);
  }
  return doc;
}

// 应用位姿文档（按名称匹配；跳过锁定骨与未知骨）
static void ApplyPoseDoc(const PoseDoc &doc) {
  int applied = 0;
  for (size_t bi = 0; bi < doc.bones.size(); bi++) {
    const PoseBone &pb = doc.bones[bi];
    int idx = -1;
    for (int i = 0; i < s_humanBoneCount; i++)
      if (strcmp(s_humanBones[i].name, pb.name.c_str()) == 0) {
        idx = i;
        break;
      }
    if (idx < 0 || s_humanBones[idx].locked)
      continue;
    SetBoneLocalPos(s_humanBones[idx].transform, pb.pos);
    SetBoneLocalRot(s_humanBones[idx].transform, pb.rot);
    applied++;
  }
  Log("[POSER] Applied pose '%s': %d/%d bones", doc.name.c_str(), applied,
      (int)doc.bones.size());
}
