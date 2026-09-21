#pragma once

// 撤销 / 重做。
// 单元 = 整副骨架的 local 变换快照（几百根骨、单份十几 KB，深度 64 也就 ~1MB），
// 好处是所有编辑路径（旋转盘 / 滑条 / 数值输入 / 复位 / T-pose / 姿态载入）都自动覆盖。
// 触发：写骨钩子（game_hooks 的 g_boneWriteHook2）通知；连续编辑按 400ms 空闲合并成一步，
// 冻结维持那类"每帧自动回写"由 accessory.h 挂起钩子排除在外。

#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "math/quat_math.h"

#include <vector>
#include <string>

struct UndoSnap {
  int bonesRev = -1;
  std::vector<Vec3> pos;
  std::vector<Quat> rot;
  std::string label; // 操作名，用于面板里显示操作序列
};

static std::vector<UndoSnap> g_undoStack;
static std::vector<UndoSnap> g_redoStack;
static UndoSnap g_undoIdle;    // 最近一次"空闲"（编辑前）的状态
static UndoSnap g_undoPending; // 当前这段连续编辑开始前的状态
static bool g_undoDirty = false;
static bool g_undoApplying = false;
static DWORD g_undoLastEdit = 0;
static int g_undoRev = -1;
static char g_undoStagedLabel[48] = "";
static const size_t kUndoDepth = 64;
static const DWORD kUndoMergeMs = 400;

// 在会修改骨骼的动作前调用，给这次编辑起个名字（显示在操作序列里）。
// 手柄拖拽/滑条这类没有显式名字的编辑会记作"编辑"。
static void UndoStageLabel(const char *s) {
  snprintf(g_undoStagedLabel, sizeof(g_undoStagedLabel), "%s", s ? s : "");
}

static UndoSnap UndoMakeSnap() {
  UndoSnap s;
  s.bonesRev = s_bonesRev;
  s.pos.reserve(s_allBones.size());
  s.rot.reserve(s_allBones.size());
  for (const AllBone &b : s_allBones) {
    s.pos.push_back(GetBoneLocalPos(b.transform));
    s.rot.push_back(GetBoneLocalRot(b.transform));
  }
  return s;
}

static bool UndoApplySnap(const UndoSnap &s) {
  if (s.bonesRev != s_bonesRev || s.pos.size() != s_allBones.size())
    return false; // 骨架重建过：下标已失效，丢弃
  g_undoApplying = true;
  for (size_t i = 0; i < s_allBones.size(); i++) {
    SetBoneLocalPos(s_allBones[i].transform, s.pos[i]);
    SetBoneLocalRot(s_allBones[i].transform, s.rot[i]);
  }
  g_undoApplying = false;
  return true;
}

// 写骨钩子：标记"正在编辑"，并把编辑开始前的状态准备好
static void UndoNoteEdit(void * /*transform*/) {
  if (g_undoApplying || s_allBones.empty())
    return;
  if (!g_undoDirty) {
    g_undoPending =
        (g_undoIdle.bonesRev == s_bonesRev) ? g_undoIdle : UndoMakeSnap();
    g_undoPending.label =
        g_undoStagedLabel[0] ? g_undoStagedLabel : "\u7f16\u8f91";
    g_undoStagedLabel[0] = 0;
    g_undoDirty = true;
  }
  g_undoLastEdit = GetTickCount();
}

static bool UndoAvailable() { return !g_undoStack.empty(); }
static bool RedoAvailable() { return !g_redoStack.empty(); }
static int UndoDepth() { return (int)g_undoStack.size(); }
static int RedoDepth() { return (int)g_redoStack.size(); }
static const char *UndoLabelAt(int i) {
  return (i >= 0 && i < (int)g_undoStack.size()) ? g_undoStack[i].label.c_str()
                                                 : "";
}
static const char *RedoLabelAt(int i) {
  return (i >= 0 && i < (int)g_redoStack.size()) ? g_redoStack[i].label.c_str()
                                                 : "";
}

// 每帧调用：合并连续编辑 + 骨架重建后清栈
static void UndoTick() {
  if (g_undoRev != s_bonesRev) {
    g_undoStack.clear();
    g_redoStack.clear();
    g_undoDirty = false;
    g_undoIdle = UndoMakeSnap();
    g_undoRev = s_bonesRev;
    if (g_undoRev > 0)
      Log("[UNDO] reset (bones rev %d)", s_bonesRev);
    return;
  }
  if (g_undoDirty && GetTickCount() - g_undoLastEdit > kUndoMergeMs) {
    g_undoStack.push_back(g_undoPending); // 编辑前的状态入栈
    if (g_undoStack.size() > kUndoDepth)
      g_undoStack.erase(g_undoStack.begin());
    g_redoStack.clear();
    g_undoDirty = false;
    g_undoIdle = UndoMakeSnap();
    Log("[UNDO] push (depth=%d)", (int)g_undoStack.size());
  }
}

static void UndoPerform() {
  if (g_undoStack.empty() || s_allBones.empty())
    return;
  UndoSnap target = g_undoStack.back();
  UndoSnap before = UndoMakeSnap();
  before.label = target.label; // 重做时沿用同一个操作名
  g_undoStack.pop_back();
  if (!UndoApplySnap(target)) {
    g_undoStack.clear();
    g_redoStack.clear();
    return;
  }
  g_redoStack.push_back(before);
  if (g_redoStack.size() > kUndoDepth)
    g_redoStack.erase(g_redoStack.begin());
  g_undoIdle = UndoMakeSnap();
  g_undoDirty = false;
  Log("[UNDO] undo -> %d left", (int)g_undoStack.size());
}

static void RedoPerform() {
  if (g_redoStack.empty() || s_allBones.empty())
    return;
  UndoSnap target = g_redoStack.back();
  UndoSnap before = UndoMakeSnap();
  before.label = target.label;
  g_redoStack.pop_back();
  if (!UndoApplySnap(target)) {
    g_undoStack.clear();
    g_redoStack.clear();
    return;
  }
  g_undoStack.push_back(before);
  if (g_undoStack.size() > kUndoDepth)
    g_undoStack.erase(g_undoStack.begin());
  g_undoIdle = UndoMakeSnap();
  g_undoDirty = false;
  Log("[UNDO] redo -> %d left", (int)g_redoStack.size());
}

static void InstallUndoHook() { g_boneWriteHook2 = UndoNoteEdit; }
