#pragma once

// poser_config.txt 读写。参照 {EIEM}/src/eiem_config.h 的 key=value 解析套路。
#include <windows.h>
#include <cstdio>
#include <cstring>

void Log(const char *fmt, ...);

static int g_guiToggleVK = VK_INSERT;   // 呼出/隐藏 GUI
static int g_screenshotVK = VK_F8;      // 截图
static char g_defaultPoseDir[MAX_PATH] = "";

// Default pose dir: prefer deriving from poser.dll location (...\plugin\poses)
// so presets work regardless of the game's working directory.
// poser_config.txt can override with default_pose_dir=<path>.
static void ResolveDefaultPoseDir() {
  HMODULE m = GetModuleHandleA("poser.dll");
  char p[MAX_PATH] = {};
  if (m && GetModuleFileNameA(m, p, MAX_PATH)) {
    char *slash = strrchr(p, '\\');
    if (slash)
      *slash = 0;
    snprintf(g_defaultPoseDir, sizeof(g_defaultPoseDir), "%s\\poses", p);
    return;
  }
  snprintf(g_defaultPoseDir, sizeof(g_defaultPoseDir), "plugin\\poses");
}

static int ParseVK(const char *s, int fallback) {
  if (!s || !*s) return fallback;
  const char *p = s;
  if (s[0] == 'V' && s[1] == 'K' && s[2] == '_')
    p = s + 3;
  // 符号名（大小写不敏感）：VK_F12 / F12 / VK_INSERT / INSERT ...
  static const struct {
    const char *name;
    int vk;
  } kNames[] = {
      {"F1", VK_F1},   {"F2", VK_F2},   {"F3", VK_F3},
      {"F4", VK_F4},   {"F5", VK_F5},   {"F6", VK_F6},
      {"F7", VK_F7},   {"F8", VK_F8},   {"F9", VK_F9},
      {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},
      {"INSERT", VK_INSERT}, {"DELETE", VK_DELETE},
      {"HOME", VK_HOME},     {"END", VK_END},
      {"PGUP", VK_PRIOR},    {"PGDN", VK_NEXT},
      {"TAB", VK_TAB},       {"ESC", VK_ESCAPE},
  };
  for (const auto &e : kNames) {
    if (_stricmp(p, e.name) == 0)
      return e.vk;
  }
  // 数字/十六进制：0x2D、45 均可（base 0 自动识别前缀）
  return (int)strtoul(p, nullptr, 0);
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
  ResolveDefaultPoseDir();
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
    else if (strcmp(key, "default_pose_dir") == 0) {
      if (val[0] == '\0') {
        ResolveDefaultPoseDir();
      } else if (val[1] == ':' ||
                 (val[0] == '\\' && val[1] == '\\')) {
        snprintf(g_defaultPoseDir, sizeof(g_defaultPoseDir), "%s", val);
      } else {
        // 相对路径按游戏根目录（poser.dll 的上一级）解析，不依赖工作目录
        HMODULE m = GetModuleHandleA("poser.dll");
        char base[MAX_PATH] = {};
        if (m && GetModuleFileNameA(m, base, MAX_PATH)) {
          char *s = strrchr(base, '\\');
          if (s)
            *s = 0; // ...\plugin
          s = strrchr(base, '\\');
          if (s)
            *s = 0; // game root
          snprintf(g_defaultPoseDir, sizeof(g_defaultPoseDir), "%s\\%s", base,
                   val);
        } else {
          snprintf(g_defaultPoseDir, sizeof(g_defaultPoseDir), "%s", val);
        }
      }
    }
  }
  fclose(f);
  Log("[CFG] gui_toggle_key=%d (0x%X) screenshot_key=%d",
      g_guiToggleVK, g_guiToggleVK, g_screenshotVK);
  return true;
}
