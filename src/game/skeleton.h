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

#include <vector>
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
// 把世界旋转增量 d 作用到骨的 localRotation（考虑父系），仅供 T-pose 内部使用
static void SetBoneWorldDelta(void *t, Quat d) {
  if (!t)
    return;
  Quat curLocal = GetBoneLocalRot(t);
  Quat localDelta = d;
  void *parent = g_transform_get_parent ? Invoke(g_transform_get_parent, t)
                                        : nullptr;
  if (parent) {
    Quat pw = GetBoneWorldRot(parent);
    localDelta = NormQ(Conj(pw) * d * pw);
  }
  SetBoneLocalRot(t, NormQ(localDelta * curLocal));
}

static Vec3 RotateVec(Quat q, Vec3 v) {
  Quat p{v.x, v.y, v.z, 0};
  Quat qc = Conj(q);
  Quat r = q * p * qc;
  return {r.x, r.y, r.z};
}

// 真正的 T-pose：所有骨 localRotation 归零 + 四肢沿世界轴摆直 + 脊柱朝上。
// （旧实现只把 localRotation 归零，但游戏 rig 绑定姿势非单位四元数，会鬼畜）
static void ApplyTPose() {
  __try {
    for (int i = 0; i < s_humanBoneCount; i++) {
      if (s_humanBones[i].locked || !s_humanBones[i].transform)
        continue;
      SetBoneLocalRot(s_humanBones[i].transform, Quat{0, 0, 0, 1});
    }
    // 四肢：根→中→末 的方向对齐到世界轴（左臂+X 右臂-X 双腿-Y）
    struct TLimbs { HumanBodyBones root, mid, end; Vec3 axis; };
    const TLimbs limbs[] = {
        {LeftUpperArm, LeftLowerArm, LeftHand, {1, 0, 0}},
        {RightUpperArm, RightLowerArm, RightHand, {-1, 0, 0}},
        {LeftUpperLeg, LeftLowerLeg, LeftFoot, {0, -1, 0}},
        {RightUpperLeg, RightLowerLeg, RightFoot, {0, -1, 0}},
    };
    for (const auto &lm : limbs) {
      void *rt = GetHumanoidBone(lm.root);
      void *mt = GetHumanoidBone(lm.mid);
      void *et = GetHumanoidBone(lm.end);
      if (!rt || !mt || !et)
        continue;
      Vec3 u0 = Norm(GetBoneWorldPos(mt) - GetBoneWorldPos(rt));
      Vec3 u1 = Norm(GetBoneWorldPos(et) - GetBoneWorldPos(mt));
      Vec3 ax = Norm(lm.axis);
      if (Len(u0) > 1e-4f)
        SetBoneWorldDelta(rt, Quat::FromTo(u0, ax));
      if (Len(u1) > 1e-4f)
        SetBoneWorldDelta(mt, Quat::FromTo(u1, ax));
    }
    // 脊柱朝上：把 Hips 的世界 up 对齐到 +Y
    void *hips = GetHumanoidBone(Hips);
    if (hips) {
      Vec3 up0 = Norm(RotateVec(GetBoneWorldRot(hips), Vec3{0, 1, 0}));
      if (Len(up0) > 1e-4f)
        SetBoneWorldDelta(hips, Quat::FromTo(up0, Vec3{0, 1, 0}));
    }
  } __except (1) {
    Log("[POSER] ApplyTPose exception");
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

// ---- 全骨骼采集（供 Blender 桥接/完整摆姿；不止 Humanoid 22 根）----
struct AllBone {
  void *transform = nullptr;
  void *parent = nullptr;
  char name[128] = {0};
  int parentIdx = -1; // -1 = 根
};
static std::vector<AllBone> s_allBones;
static int s_bonesRev = 0; // 骨骼列表版本号（角色切换重建时 +1，Blender 桥接据此自动刷新）

static void CollectAllBonesRecursive(void *t, void *parent, int depth) {
  if (!t || depth > 64 || s_allBones.size() > 512)
    return;
  AllBone b;
  b.transform = t;
  b.parent = parent;
  __try {
    if (g_component_get_gameObject && g_object_get_name) {
      void *go = Invoke(g_component_get_gameObject, t);
      if (go) {
        void *nb = Invoke(g_object_get_name, go);
        if (nb)
          ReadStr(nb, b.name, sizeof(b.name));
      }
    }
  } __except (1) {
  }
  s_allBones.push_back(b);
  __try {
    if (g_transform_get_childCount && g_transform_GetChild) {
      void *boxed = Invoke(g_transform_get_childCount, t);
      int n = boxed ? *(int *)((char *)boxed + 16) : 0;
      for (int i = 0; i < n && i < 64; i++) {
        int idx = i;
        void *params[] = {&idx};
        void *child = Invoke(g_transform_GetChild, t, params);
        if (child)
          CollectAllBonesRecursive(child, t, depth + 1);
      }
    }
  } __except (1) {
  }
}

static void RebuildAllBones() {
  s_allBones.clear();
  void *root = GetCharRootTransform();
  if (!root)
    return;
  CollectAllBonesRecursive(root, nullptr, 0);
  for (size_t i = 0; i < s_allBones.size(); i++) {
    if (s_allBones[i].parent) {
      for (size_t j = 0; j < s_allBones.size(); j++) {
        if (s_allBones[j].transform == s_allBones[i].parent) {
          s_allBones[i].parentIdx = (int)j;
          break;
        }
      }
    }
  }
  Log("[POSER] All bones rebuilt: %d", (int)s_allBones.size());
  s_bonesRev++;
}
