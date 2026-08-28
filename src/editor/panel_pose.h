#pragma once

// Task 3.1：FK 姿态编辑面板。
// 结构：顶部工具行（重置/T-Pose/镜像） + 面板骨骼小人（点选中、拖摆姿）
// + 左右分布的骨骼树/选中骨控制（Euler/位置滑条）。
// 依赖 skeleton.h 的 s_humanBones[] 与冻结态（freeze.h）。

#include "imgui.h"
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "game/ik_driver.h"
#include "math/quat_math.h"

// 当前选中骨在 s_humanBones 中的下标（-1 = 无）
static int g_selectedBone = -1;

// 按骨名选中骨骼（控制文件/外部测试用）
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

// ---- 骨骼分组（树形展示用）----
struct BoneGroupDef {
  const char *name;
  const HumanBodyBones *bones;
  int count;
};

static const HumanBodyBones kBonesTorso[] = {Hips, Spine, Chest, UpperChest,
                                             Neck};
static const HumanBodyBones kBonesHead[] = {Head, Jaw, LeftEye, RightEye};
static const HumanBodyBones kBonesArmL[] = {LeftShoulder, LeftUpperArm,
                                            LeftLowerArm, LeftHand};
static const HumanBodyBones kBonesArmR[] = {RightShoulder, RightUpperArm,
                                            RightLowerArm, RightHand};
static const HumanBodyBones kBonesLegL[] = {LeftUpperLeg, LeftLowerLeg,
                                            LeftFoot, LeftToes};
static const HumanBodyBones kBonesLegR[] = {RightUpperLeg, RightLowerLeg,
                                            RightFoot, RightToes};
static const HumanBodyBones kBonesFingerL[] = {
    LeftThumbProximal, LeftThumbIntermediate, LeftThumbDistal,
    LeftIndexProximal, LeftIndexIntermediate, LeftIndexDistal,
    LeftMiddleProximal, LeftMiddleIntermediate, LeftMiddleDistal,
    LeftRingProximal, LeftRingIntermediate, LeftRingDistal,
    LeftLittleProximal, LeftLittleIntermediate, LeftLittleDistal};
static const HumanBodyBones kBonesFingerR[] = {
    RightThumbProximal, RightThumbIntermediate, RightThumbDistal,
    RightIndexProximal, RightIndexIntermediate, RightIndexDistal,
    RightMiddleProximal, RightMiddleIntermediate, RightMiddleDistal,
    RightRingProximal, RightRingIntermediate, RightRingDistal,
    RightLittleProximal, RightLittleIntermediate, RightLittleDistal};

static const BoneGroupDef kBoneGroups[] = {
    {u8"\u8eaf\u5e72", kBonesTorso, 5},      // 躯干
    {u8"\u5934\u90e8", kBonesHead, 4},       // 头部
    {u8"\u5de6\u81c2", kBonesArmL, 4},       // 左臂
    {u8"\u53f3\u81c2", kBonesArmR, 4},       // 右臂
    {u8"\u5de6\u817f", kBonesLegL, 4},       // 左腿
    {u8"\u53f3\u817f", kBonesLegR, 4},       // 右腿
    {u8"\u5de6\u624b\u624b\u6307", kBonesFingerL, 15}, // 左手手指
    {u8"\u53f3\u624b\u624b\u6307", kBonesFingerR, 15}, // 右手手指
};
static const int kBoneGroupCount = (int)(sizeof(kBoneGroups) / sizeof(kBoneGroups[0]));

static const char *BoneGroupName(HumanBodyBones b) {
  for (int g = 0; g < kBoneGroupCount; g++)
    for (int i = 0; i < kBoneGroups[g].count; i++)
      if (kBoneGroups[g].bones[i] == b)
        return kBoneGroups[g].name;
  return u8"\u5176\u4ed6";
}

// 一次性 IK：以当前末端世界位置为目标，反向求解根/中骨，写完即停。
// 不做每帧驱动——实时求解是 EIEM 跳舞功能的做法，摆姿场景下会一直抢 FK。
static void IkSolveChainOnce(int ci) {
  if (ci < 0 || ci >= kIkChainCount)
    return;
  const IkChainDef &ch = kIkChains[ci];
  void *rootT = GetHumanoidBone(ch.root);
  void *midT = GetHumanoidBone(ch.mid);
  void *endT = GetHumanoidBone(ch.end);
  if (!rootT || !midT || !endT)
    return;
  if (IkBoneLocked(ch.root) || IkBoneLocked(ch.mid))
    return;
  SolveTwoBoneChain(rootT, midT, endT, GetBoneWorldPos(endT));
  Log("[POSER] IK one-shot solve: %s", kIkChains[ci].name);
}

// 返回包含该骨的 IK 链下标（-1 = 无）。四肢链互不重叠，最多命中一条。
static int IkChainForBone(HumanBodyBones b) {
  for (int ci = 0; ci < kIkChainCount; ci++) {
    const IkChainDef &ch = kIkChains[ci];
    if (ch.root == b || ch.mid == b || ch.end == b)
      return ci;
  }
  return -1;
}

// 编辑末端骨（手/脚）位置后，一次性反向求解该链的其它骨骼
static void IkSolveIfEndBone(HumanBodyBones b) {
  int ch = IkChainForBone(b);
  if (ch >= 0 && kIkChains[ch].end == b)
    IkSolveChainOnce(ch);
}

// ---- 骨骼树 + 选中骨控制 ----
static void DrawSkeletonSchematic(); // 面板骨骼小人（定义在文件后部）

static void DrawPosePanel() {
  if (s_humanBoneCount <= 0) {
    ImGui::TextDisabled(u8"\u672a\u6355\u83b7\u5230\u89d2\u8272\u9aa8\u9abc");
    return;
  }

  // 工具行：重置 / T-Pose / 镜像（简化：去掉 gizmo 开关与复制粘贴）
  if (ImGui::SmallButton(u8"\u91cd\u7f6e")) {
    ApplyPoseSnapshot();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("T-Pose"))
    ApplyTPose();
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u955c\u50cf L\u2192R"))
    MirrorPose(true);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u955c\u50cf R\u2192L"))
    MirrorPose(false);

  ImGui::Separator();

  // 面板骨骼小人：点关节选骨，拖关节摆姿势（正视图，绕世界 Z 旋转）
  DrawSkeletonSchematic();
  ImGui::Separator();

  ImGui::Separator();

  // 左侧：分组骨骼树
  float treeW = ImGui::GetContentRegionAvail().x * 0.5f;
  ImGui::BeginChild("##bonetree", ImVec2(treeW, 0), true);
  ImGui::TextDisabled(u8"\u9aa8\u9abc\u6811");
  ImGui::Separator();
  for (int g = 0; g < kBoneGroupCount; g++) {
    const BoneGroupDef &grp = kBoneGroups[g];
    if (ImGui::CollapsingHeader(grp.name)) {
      ImGui::Indent();
      for (int i = 0; i < grp.count; i++) {
        int idx = FindHumanBoneIndex(grp.bones[i]);
        if (idx < 0)
          continue;
        BoneHandle &bh = s_humanBones[idx];
        ImGui::PushID(idx);
        bool selected = (g_selectedBone == idx);
        if (ImGui::Selectable(bh.name, selected)) {
          g_selectedBone = (selected ? -1 : idx); // 再次点击取消选择
        }
        if (bh.locked) {
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), u8"\u9501");
        }
        ImGui::PopID();
      }
      ImGui::Unindent();
    }
  }
  ImGui::EndChild();

  // 右侧：选中骨控制
  ImGui::SameLine();
  ImGui::BeginChild("##boneedit", ImVec2(0, 0), true);
  if (g_selectedBone < 0 || g_selectedBone >= s_humanBoneCount) {
    ImGui::TextDisabled(u8"\u8bf7\u5728\u5de6\u4fa7\u9009\u62e9\u4e00\u6839\u9aa8\u9abc");
    ImGui::EndChild();
    return;
  }

  BoneHandle &bh = s_humanBones[g_selectedBone];
  ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", bh.name);
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", BoneGroupName(bh.humanBone));

  ImGui::Spacing();
  bool locked = bh.locked;
  if (ImGui::Checkbox(u8"\u9501\u5b9a\u8be5\u9aa8", &locked)) {
    bh.locked = locked;
    if (locked) {
      // 锁定瞬间固化当前位姿，供恢复时钉住
      bh.localPos = GetBoneLocalPos(bh.transform);
      bh.localRot = GetBoneLocalRot(bh.transform);
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u53c9\u8f74\u65cb\u8f6c (\u00b0)");

  // 实时回读当前 localRotation 的 Euler 角，保证滑条跟随 gizmo 拖拽
  Quat cur = GetBoneLocalRot(bh.transform);
  Vec3 euler = cur.ToEulerDeg();
  // 增量式局部轴旋转：拖 X 只绕骨局部 X 轴转，避免 Euler 分解非唯一导致的"拖一个其他跟着跳"
  const float kD2R = 3.14159265f / 180.0f;
  static Quat s_dragBase = {0, 0, 0, 1};
  static float s_dragEuler[3] = {0, 0, 0};
  float *eptr[3] = {&euler.x, &euler.y, &euler.z};
  const char *ids[3] = {u8"X##rx", u8"Y##ry", u8"Z##rz"};
  Vec3 axs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int i = 0; i < 3; i++) {
    if (ImGui::SliderFloat(ids[i], eptr[i], -180.0f, 180.0f, "%.1f")) {
      if (ImGui::IsItemActivated()) {
        s_dragBase = cur;
        s_dragEuler[0] = euler.x;
        s_dragEuler[1] = euler.y;
        s_dragEuler[2] = euler.z;
      }
      if (ImGui::IsItemActive() && !bh.locked) {
        float d = (*eptr[i] - s_dragEuler[i]) * kD2R;
        SetBoneLocalRot(bh.transform, NormQ(Quat::AxisAngle(axs[i], d) * s_dragBase));
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled(u8"\u4f4d\u7f6e");
  Vec3 pos = GetBoneLocalPos(bh.transform);
  if (ImGui::SliderFloat(u8"X##px", &pos.x, -3.0f, 3.0f, "%.3f")) {
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
    IkSolveIfEndBone(bh.humanBone);
  }
  if (ImGui::SliderFloat(u8"Y##py", &pos.y, -3.0f, 3.0f, "%.3f")) {
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
    IkSolveIfEndBone(bh.humanBone);
  }
  if (ImGui::SliderFloat(u8"Z##pz", &pos.z, -3.0f, 3.0f, "%.3f")) {
    if (!bh.locked)
      SetBoneLocalPos(bh.transform, pos);
    IkSolveIfEndBone(bh.humanBone);
  }

  ImGui::EndChild();
}

// 在面板内找某个 transform 对应的骨骼下标（画父-子连线用）
static int FindTransformIndex(void *t) {
  if (!t)
    return -1;
  for (int i = 0; i < s_humanBoneCount; i++)
    if (s_humanBones[i].transform == t)
      return i;
  return -1;
}

// ---- 面板骨骼小人（2D 正视图）----
// 仅用于选择：点关节选中骨骼（再点取消）；拖拽摆姿已移除（绕世界 Z 转父骨的
// 逻辑与四元数/欧拉换算不可靠），FK 用右侧滑条，IK 在编辑末端骨位置时一次性解算。
// 关节颜色：黄=选中，红=锁定，蓝=普通。
static void DrawSkeletonSchematic() {
  if (s_humanBoneCount <= 0) {
    ImGui::TextDisabled(u8"\u672a\u6355\u83b7\u89d2\u8272\u9aa8\u9abc");
    return;
  }
  const float kH = 240.0f;
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float w = avail.x < 60.0f ? 60.0f : avail.x;
  ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + w, origin.y + kH),
                    IM_COL32(25, 28, 38, 220));

  // 收集骨骼世界坐标 + 包围盒
  Vec3 wpos[64];
  float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
  for (int i = 0; i < s_humanBoneCount && i < 64; i++) {
    Vec3 p = GetBoneWorldPos(s_humanBones[i].transform);
    wpos[i] = p;
    if (p.x < minX) minX = p.x;
    if (p.x > maxX) maxX = p.x;
    if (p.y < minY) minY = p.y;
    if (p.y > maxY) maxY = p.y;
  }
  float spanX = (maxX - minX) > 1e-4f ? (maxX - minX) : 1.0f;
  float spanY = (maxY - minY) > 1e-4f ? (maxY - minY) : 1.0f;
  float sc = (w - 24.0f) / spanX;
  float sy = (kH - 24.0f) / spanY;
  if (sy < sc) sc = sy;
  if (sc <= 0.0f) sc = 1.0f;
  float midX = (minX + maxX) * 0.5f, midY = (minY + maxY) * 0.5f;
  auto To2D = [&](Vec3 p) -> ImVec2 {
    float px = origin.x + w * 0.5f + (p.x - midX) * sc;
    float py = origin.y + kH * 0.5f - (p.y - midY) * sc; // 屏幕 Y 向下，翻转为世界 Y 向上
    return ImVec2(px, py);
  };

  // 父-子连线
  for (int i = 0; i < s_humanBoneCount && i < 64; i++) {
    if (!s_humanBones[i].transform)
      continue;
    void *parent =
        g_transform_get_parent
            ? Invoke(g_transform_get_parent, s_humanBones[i].transform)
            : nullptr;
    int pi = FindTransformIndex(parent);
    if (pi >= 0) {
      ImVec2 a = To2D(wpos[pi]), b = To2D(wpos[i]);
      dl->AddLine(a, b, IM_COL32(190, 195, 210, 255), 1.6f);
    }
  }

  ImVec2 mouse = ImGui::GetIO().MousePos;

  // 关节点 + 点击选中（再点取消）
  for (int i = 0; i < s_humanBoneCount && i < 64; i++) {
    ImVec2 c = To2D(wpos[i]);
    bool sel = (i == g_selectedBone);
    ImU32 col = sel ? IM_COL32(255, 200, 60, 255)
                    : (s_humanBones[i].locked ? IM_COL32(255, 90, 90, 255)
                                              : IM_COL32(120, 200, 255, 255));
    dl->AddCircleFilled(c, sel ? 5.0f : 3.5f, col);
    float dx = mouse.x - c.x, dy = mouse.y - c.y;
    float d = sqrtf(dx * dx + dy * dy);
    if (ImGui::IsMouseClicked(0) && d < 10.0f)
      g_selectedBone = sel ? -1 : i;
  }

  ImGui::Dummy(ImVec2(w, kH));
}
