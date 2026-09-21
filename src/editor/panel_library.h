#pragma once

// Task 3.3：姿态预设库面板。
// 展示 plugin/poses/*.poser.json，点选加载/覆盖/删除；输入名字可另存为。
// 复用 math/pose_file.h 的 PoseDoc 序列化 + game/skeleton.h 的采集/应用。

#include "imgui.h"
#include "core/game_hooks.h"
#include "config.h"
#include "game/skeleton.h"
#include "math/pose_file.h"

#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> g_poseFiles;
static int g_selectedPose = -1;
static char g_poseName[128] = "";
static char g_poseStatus[256] = "";

static void RefreshPoseList() {
  g_poseFiles.clear();
  g_selectedPose = -1;
  CreateDirectoryA(g_defaultPoseDir, nullptr);
  std::string pat = std::string(g_defaultPoseDir) + "\\*.poser.json";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      g_poseFiles.push_back(fd.cFileName);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}

static std::string PoseFilePath(int idx) {
  return std::string(g_defaultPoseDir) + "\\" + g_poseFiles[idx];
}

// 自动编号：Pose_001, Pose_002, ...（跳过已存在的最大号）
static void MakeNextPoseName(char *out, size_t sz) {
  int maxN = 0;
  for (const auto &f : g_poseFiles) {
    int n = 0;
    if (sscanf(f.c_str(), "Pose_%d.poser.json", &n) == 1 && n > maxN)
      maxN = n;
  }
  snprintf(out, sz, "Pose_%03d", maxN + 1);
}

static void SavePoseToFile(const char *name) {
  if (s_humanBoneCount <= 0) {
    snprintf(g_poseStatus, sizeof(g_poseStatus),
             "\u5148\u51bb\u7ed3\u5e76\u6446\u597d\u59ff\u52bf"); // 先冻结并摆好姿势
    return;
  }
  char autoName[64];
  if (!name || !name[0]) {
    RefreshPoseList(); // 确保编号基于最新文件列表
    MakeNextPoseName(autoName, sizeof(autoName));
    name = autoName;
  }
  PoseDoc doc = CapturePoseDoc(name);
  std::string json = PoseToJson(doc);
  std::string path = std::string(g_defaultPoseDir) + "\\" + name + ".poser.json";
  FILE *f = nullptr;
  if (fopen_s(&f, path.c_str(), "wb") == 0 && f) {
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    snprintf(g_poseStatus, sizeof(g_poseStatus), "\u5df2\u4fdd\u5b58 %s", name);
  } else {
    snprintf(g_poseStatus, sizeof(g_poseStatus),
             "\u4fdd\u5b58\u5931\u8d25 %s", path.c_str());
  }
  RefreshPoseList();
  g_selectedPose = (int)g_poseFiles.size() - 1;
}

static void LoadPoseFromFile(int idx) {
  if (idx < 0 || idx >= (int)g_poseFiles.size())
    return;
  FILE *f = nullptr;
  if (fopen_s(&f, PoseFilePath(idx).c_str(), "rb") != 0 || !f)
    return;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::string text(sz > 0 ? sz : 0, '\0');
  if (sz > 0)
    fread(&text[0], 1, (size_t)sz, f);
  fclose(f);
  try {
    PoseDoc doc = PoseFromJson(text);
    ApplyPoseDoc(doc);
    snprintf(g_poseStatus, sizeof(g_poseStatus), "\u5df2\u52a0\u8f7d %s",
             g_poseFiles[idx].c_str());
  } catch (...) {
    snprintf(g_poseStatus, sizeof(g_poseStatus), "\u89e3\u6790\u5931\u8d25 %s",
             g_poseFiles[idx].c_str());
  }
}

static void DeletePoseFile(int idx) {
  if (idx < 0 || idx >= (int)g_poseFiles.size())
    return;
  DeleteFileA(PoseFilePath(idx).c_str());
  snprintf(g_poseStatus, sizeof(g_poseStatus), "\u5df2\u5220\u9664 %s",
           g_poseFiles[idx].c_str());
  RefreshPoseList();
}

// ---- 外置导出/导入：写到游戏目录之外（默认 我的文档\EndfieldPoser\poses）----
static bool PoseNameValid(const char *name) {
  if (!name || !name[0] || strlen(name) > 64)
    return false;
  for (const char *p = name; *p; p++)
    if (strchr("\\/:*?\"<>|", *p))
      return false;
  return true;
}

static void ExportPoseToDir(const char *dir, const char *name) {
  if (s_humanBoneCount <= 0) {
    snprintf(g_poseStatus, sizeof(g_poseStatus), u8"先冻结并摆好姿势");
    return;
  }
  if (!PoseNameValid(name)) {
    snprintf(g_poseStatus, sizeof(g_poseStatus),
             u8"名称不合法（不能为空/含 \\/:*?<>|）");
    return;
  }
  CreateDirectoryA(dir, nullptr);
  PoseDoc doc = CapturePoseDoc(name);
  std::string json = PoseToJson(doc);
  std::string path = std::string(dir) + "\\" + name + ".poser.json";
  FILE *f = nullptr;
  if (fopen_s(&f, path.c_str(), "wb") == 0 && f) {
    fwrite(json.data(), 1, json.size(), f);
    fclose(f);
    snprintf(g_poseStatus, sizeof(g_poseStatus), u8"已导出 %s", path.c_str());
    Log("[POSE] exported to %s", path.c_str());
  } else {
    snprintf(g_poseStatus, sizeof(g_poseStatus),
             u8"导出失败 %s（目录存在吗？）", path.c_str());
  }
}

static void ImportPoseFromDir(const char *dir, const char *name) {
  if (!PoseNameValid(name)) {
    snprintf(g_poseStatus, sizeof(g_poseStatus), u8"名称不合法");
    return;
  }
  std::string path = std::string(dir) + "\\" + name + ".poser.json";
  FILE *f = nullptr;
  if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) {
    snprintf(g_poseStatus, sizeof(g_poseStatus), u8"读不到 %s", path.c_str());
    return;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::string text(sz > 0 ? sz : 0, '\0');
  if (sz > 0)
    fread(&text[0], 1, (size_t)sz, f);
  fclose(f);
  try {
    PoseDoc doc = PoseFromJson(text);
    ApplyPoseDoc(doc);
    snprintf(g_poseStatus, sizeof(g_poseStatus), u8"已从外部载入 %s", name);
    Log("[POSE] imported from %s", path.c_str());
  } catch (...) {
    snprintf(g_poseStatus, sizeof(g_poseStatus), u8"解析失败 %s", path.c_str());
  }
}

static void DrawLibraryPanel() {
  ImGui::TextDisabled(u8"\u59ff\u6001\u9884\u8bbe (plugin/poses/*.poser.json)");
  ImGui::Separator();

  if (ImGui::SmallButton(u8"\u4fdd\u5b58\u9884\u8bbe")) // 一键保存（自动编号）
    SavePoseToFile(g_poseName[0] ? g_poseName : nullptr);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"\u5237\u65b0\u5217\u8868"))
    RefreshPoseList();
  ImGui::Separator();

  if (g_poseFiles.empty()) {
    ImGui::TextDisabled(u8"\uff08\u6682\u65e0\u9884\u8bbe\uff09");
  } else {
    ImGui::BeginChild("##poselist", ImVec2(0, 160), true);
    for (size_t i = 0; i < g_poseFiles.size(); i++) {
      bool sel = ((int)i == g_selectedPose);
      if (ImGui::Selectable(g_poseFiles[i].c_str(), sel))
        g_selectedPose = (int)i;
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::SmallButton(u8"\u52a0\u8f7d") && g_selectedPose >= 0)
      LoadPoseFromFile(g_selectedPose);
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"\u8986\u76d6\u4fdd\u5b58") && g_selectedPose >= 0) {
      std::string fn = g_poseFiles[g_selectedPose];
      const char *ext = ".poser.json";
      size_t p = fn.rfind(ext);
      if (p != std::string::npos)
        fn = fn.substr(0, p);
      SavePoseToFile(fn.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"\u5220\u9664") && g_selectedPose >= 0)
      DeletePoseFile(g_selectedPose);
  }

  if (g_poseStatus[0]) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", g_poseStatus);
  }

  // ---- 命名 + 外置保存（写在游戏目录之外）----
  ImGui::Separator();
  ImGui::TextDisabled(u8"命名 / 导出到游戏外");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##posename", u8"名称（留空则自动编号 Pose_001）",
                           g_poseName, sizeof(g_poseName));
  ImGui::SetNextItemWidth(-1);
  ImGui::InputText("##posedir", g_exportPoseDir, sizeof(g_exportPoseDir));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        u8"外置姿态目录（可在 poser_config.txt 用 export_pose_dir= 改）");
  if (ImGui::SmallButton(u8"另存到外部"))
    ExportPoseToDir(g_exportPoseDir, g_poseName);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"从外部载入"))
    ImportPoseFromDir(g_exportPoseDir, g_poseName);
}
