#pragma once

// poser_config.txt 读写。参照 {EIEM}/src/eiem_config.h 的 key=value 解析套路。
#include <windows.h>
#include <cstdio>
#include <cstring>

void Log(const char *fmt, ...);

static int g_guiToggleVK = VK_INSERT;   // 呼出/隐藏 GUI
static int g_screenshotVK = VK_F8;      // 截图
static float g_cameraSpeed = 5.0f;      // 自由相机移动速度
static bool g_cameraTakeover = false;   // 相机接管（默认关：用游戏自带相机，避免干扰）
static char g_defaultPoseDir[MAX_PATH] = "plugin\\poses";

static int ParseVK(const char *s, int fallback) {
  if (!s || !*s) return fallback;
  if (s[0] == 'V' && s[1] == 'K' && s[2] == '_')
    return (int)strtoul(s + 3, nullptr, 10); // VK_INSERT=0x2D → "0x2D"
  // 支持直接写 0x2D 或 45
  return (int)strtoul(s, nullptr, 0);
}

static void StripBom(char *line) {
  // 去掉 UTF-8 BOM（EF BB BF），兼容 PowerShell/记事本写出的配置文件
  unsigned char *p = (unsigned char *)line;
  if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
    char *dst = line;
    char *src = line + 3;
    while ((*dst++ = *src++))
      ;
  }
}

static bool LoadPoserConfig() {
  FILE *f = fopen("plugin\\poser_config.txt", "r");
  if (!f) return false;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    StripBom(line);
    char *e = line + strlen(line) - 1;
    while (e > line && (*e == '\n' || *e == '\r' || *e == ' ')) *e-- = 0;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char *key = line;
    const char *val = eq + 1;
    char *kend = eq - 1;
    while (kend > key && *kend == ' ') *kend-- = 0;
    while (*val == ' ') val++;

    if (strcmp(key, "gui_toggle_key") == 0)       g_guiToggleVK = ParseVK(val, VK_INSERT);
    else if (strcmp(key, "screenshot_key") == 0)  g_screenshotVK = ParseVK(val, VK_F8);
    else if (strcmp(key, "camera_speed") == 0)    g_cameraSpeed = (float)atof(val);
    else if (strcmp(key, "camera_takeover") == 0) g_cameraTakeover = (atoi(val) != 0);
    else if (strcmp(key, "default_pose_dir") == 0)
      snprintf(g_defaultPoseDir, sizeof(g_defaultPoseDir), "%s", val);
  }
  fclose(f);
  Log("[CFG] gui_toggle_key=%d (0x%X) screenshot_key=%d camera_speed=%.1f",
      g_guiToggleVK, g_guiToggleVK, g_screenshotVK, g_cameraSpeed);
  return true;
}
