#pragma once

// Task 3.3：姿态预设库面板。
// 展示 plugin/poses/*.poser.json，点选加载/覆盖/删除；输入名字可另存为。
// 复用 math/pose_file.h 的 PoseDoc 序列化 + game/skeleton.h 的采集/应用。

#include "imgui.h"
#include "core/game_hooks.h"
#include "game/skeleton.h"
#include "math/pose_file.h"

#include <cstdio>
#include <string>
#include <vector>

static const char *kPoseDir = "plugin\\poses";
static std::vector<std::string> g_poseFiles;
static int g_selectedPose = -1;
static char g_poseName[128] = "";
static char g_poseStatus[256] = "";

static void RefreshPoseList() {
  g_poseFiles.clear();
  g_selectedPose = -1;
  CreateDirectoryA(kPoseDir, nullptr);
  std::string pat = std::string(kPoseDir) + "\\*.poser.json";
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
  return std::string(kPoseDir) + "\\" + g_poseFiles[idx];
}

static void SavePoseToFile(const char *name) {
  if (!name || !name[0] || s_humanBoneCount <= 0) {
    snprintf(g_poseStatus, sizeof(g_poseStatus),
             "\u5148\u51bb\u7ed3\u5e76\u6446\u597d\u59ff\u52bf"); // 先冻结并摆好姿势
    return;
  }
  PoseDoc doc = CapturePoseDoc(name);
  std::string json = PoseToJson(doc);
  std::string path = std::string(kPoseDir) + "\\" + name + ".poser.json";
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

static void DrawLibraryPanel() {
  ImGui::TextDisabled(u8"\u59ff\u6001\u9884\u8bbe (plugin/poses/*.poser.json)");
  ImGui::Separator();

  ImGui::Text(u8"\u59ff\u6001\u540d");
  ImGui::SameLine();
  ImGui::InputText(u8"##posename", g_poseName, sizeof(g_poseName));
  if (ImGui::SmallButton(u8"\u4fdd\u5b58\u5f53\u524d\u4e3a\u2026")) {
    if (g_poseName[0])
      SavePoseToFile(g_poseName);
    else
      snprintf(g_poseStatus, sizeof(g_poseStatus), "\u8bf7\u5148\u8f93\u5165\u59ff\u6001\u540d");
  }
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
}
