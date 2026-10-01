#pragma once

// Panel dimensions use 1080p units. Scene projections remain in framebuffer pixels.
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace poser_ui {
struct DisplaySettings {
  bool automatic = true;
  float multiplier = 1.f;
  float applied = 1.f; // The scale of the window coordinates stored in the same ini.
  ImVec2 viewport{};
  ImGuiStyle baseStyle;
};
inline DisplaySettings display;

inline float ValidMultiplier(float value) {
  return std::isfinite(value) ? (std::clamp)(value, .5f, 2.f) : 1.f;
}
inline float EffectiveScale(float height, bool automatic, float multiplier) {
  // Keep smaller displays compact; above 1080p grow more gently (4K = 1.5x).
  const float resolution = std::isfinite(height) && height > 0 ? height / 1080.f : 1.f;
  const float base = automatic ? (resolution <= 1.f ? resolution : 1.f + (resolution - 1.f) * .5f) : 1.f;
  return (std::clamp)(base * ValidMultiplier(multiplier), .5f, 4.f);
}
inline float Scale(float value) { return value * display.applied; }
inline ImVec2 Size(float x, float y) { return ImVec2(Scale(x), Scale(y)); }
inline ImVec2 AvailableSize() {
  auto v = ImGui::GetIO().DisplaySize;
  return ImVec2((std::max)(1.f, v.x - Scale(16)), (std::max)(1.f, v.y - Scale(16)));
}
inline void Fit(ImVec2 &pos, ImVec2 &size, ImVec2 viewport, float margin) {
  const float mx = (std::min)(margin, viewport.x * .1f);
  const float my = (std::min)(margin, viewport.y * .1f);
  size.x = (std::clamp)(size.x, 1.f, (std::max)(1.f, viewport.x - 2 * mx));
  size.y = (std::clamp)(size.y, 1.f, (std::max)(1.f, viewport.y - 2 * my));
  pos.x = (std::clamp)(pos.x, mx, (std::max)(mx, viewport.x - mx - size.x));
  pos.y = (std::clamp)(pos.y, my, (std::max)(my, viewport.y - my - size.y));
}

inline void Initialize() {
  display = {};
  display.baseStyle = ImGui::GetStyle();
  ImGuiSettingsHandler handler;
  handler.TypeName = "PoserDisplay";
  handler.TypeHash = ImHashStr(handler.TypeName);
  handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char *name) -> void* {
    return std::strcmp(name, "Scale") == 0 ? &display : nullptr;
  };
  handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char *line) {
    int automatic;
    float value;
    if (std::sscanf(line, "Automatic=%d", &automatic) == 1) display.automatic = automatic != 0;
    else if (std::sscanf(line, "Multiplier=%f", &value) == 1) display.multiplier = ValidMultiplier(value);
    else if (std::sscanf(line, "Applied=%f", &value) == 1 && std::isfinite(value) && value >= .5f && value <= 4)
      display.applied = value;
  };
  handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler*, ImGuiTextBuffer *out) {
    out->appendf("[PoserDisplay][Scale]\nAutomatic=%d\nMultiplier=%.4f\nApplied=%.4f\n\n",
                 int(display.automatic), display.multiplier, display.applied);
  };
  ImGui::AddSettingsHandler(&handler);
  // Load before scaling the stored layout. Legacy layouts have a scale of one.
  if (ImGui::GetIO().IniFilename) ImGui::LoadIniSettingsFromDisk(ImGui::GetIO().IniFilename);
}

// Run after the platform backend supplies DisplaySize, before NewFrame(). Fonts
// use the bundled renderer's dynamic atlas; no texture/device recreation loop.
inline bool BeginFrame() {
  auto &g = *ImGui::GetCurrentContext();
  const ImVec2 viewport = g.IO.DisplaySize;
  if (viewport.x <= 0 || viewport.y <= 0) return false;
  const float scale = EffectiveScale(viewport.y, display.automatic, display.multiplier);
  const bool resized = viewport.x != display.viewport.x || viewport.y != display.viewport.y;
  if (!resized && std::fabs(scale - display.applied) < .0001f) return false;
  // Do not move a slider underneath a held mouse or an active text edit.
  if (!resized && g.ActiveId) return false;
  const float ratio = scale / display.applied;
  ImGui::GetStyle() = display.baseStyle;
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImGui::GetStyle().FontScaleMain = scale;
  for (auto *s = g.SettingsWindows.begin(); s; s = g.SettingsWindows.next_chunk(s)) {
    if (s->WantDelete || s->IsChild) continue;
    ImVec2 pos(float(s->Pos.x) * ratio, float(s->Pos.y) * ratio);
    ImVec2 size(float(s->Size.x) * ratio, float(s->Size.y) * ratio);
    Fit(pos, size, viewport, 8 * scale);
    s->Pos = ImVec2ih(pos); s->Size = ImVec2ih(size);
  }
  for (auto *window : g.Windows) {
    if (window->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_NoSavedSettings)) continue;
    ImVec2 pos(window->Pos.x * ratio, window->Pos.y * ratio);
    ImVec2 size(window->SizeFull.x * ratio, window->SizeFull.y * ratio);
    Fit(pos, size, viewport, 8 * scale);
    // Begin() measures last frame's cursor extents before auto-fitting. Scaling
    // only SizeFull leaves those extents at the old scale: full-width controls
    // then shrink the window by one pixel per frame instead of resizing once.
    auto scaleExtent = [&](ImVec2 &value) {
      value.x = window->DC.CursorStartPos.x + (value.x - window->DC.CursorStartPos.x) * ratio;
      value.y = window->DC.CursorStartPos.y + (value.y - window->DC.CursorStartPos.y) * ratio;
    };
    scaleExtent(window->DC.CursorMaxPos);
    scaleExtent(window->DC.IdealMaxPos);
    window->ContentSize.x *= ratio; window->ContentSize.y *= ratio;
    window->ContentSizeIdeal.x *= ratio; window->ContentSizeIdeal.y *= ratio;
    window->Scroll.x *= ratio; window->Scroll.y *= ratio;
    ImGui::SetWindowPos(window, pos, ImGuiCond_Always);
    ImGui::SetWindowSize(window, size, ImGuiCond_Always);
  }
  display.applied = scale;
  display.viewport = viewport;
  ImGui::MarkIniSettingsDirty();
  return true;
}

// Shared initial placement, scaled minimums and viewport bounds. Saved user
// positions survive reopen/restart; only out-of-view windows are moved back.
inline void NextPanel(const char *name, ImVec2 defaultPos, ImVec2 initialSize,
                      ImVec2 minimum, bool reset = false) {
  defaultPos = Size(defaultPos.x, defaultPos.y);
  initialSize = Size(initialSize.x, initialSize.y);
  minimum = Size(minimum.x, minimum.y);
  const ImVec2 maximum = AvailableSize();
  minimum.x = (std::min)(minimum.x, maximum.x);
  minimum.y = (std::min)(minimum.y, maximum.y);
  ImGui::SetNextWindowSizeConstraints(minimum, maximum);
  auto *w = ImGui::FindWindowByName(name);
  auto *s = ImGui::FindWindowSettingsByID(ImHashStr(name));
  ImVec2 pos = defaultPos, size((std::max)(initialSize.x, minimum.x), (std::max)(initialSize.y, minimum.y));
  if (!reset) {
    if (w) {pos = w->Pos; size = w->SizeFull;}
    else if (s) {pos = ImVec2(float(s->Pos.x), float(s->Pos.y)); size = ImVec2(float(s->Size.x), float(s->Size.y));}
  }
  const ImVec2 before = pos;
  Fit(pos, size, ImGui::GetIO().DisplaySize, Scale(8));
  ImGui::SetNextWindowPos(pos, reset || pos.x != before.x || pos.y != before.y ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
  if (initialSize.x > 0 && initialSize.y > 0)
    ImGui::SetNextWindowSize(size, reset ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
}

inline void DrawDisplaySettings() {
  if (!ImGui::CollapsingHeader(u8"界面缩放")) return;
  bool changed = ImGui::Checkbox(u8"随分辨率自动缩放（1080p 基准）", &display.automatic);
  ImGui::SetNextItemWidth(-1);
  changed |= ImGui::SliderFloat(u8"##ui-scale", &display.multiplier, .5f, 2.f, u8"手动倍率 %.2f×", ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(u8"1080p 为 1 倍，4K 为 1.5 倍，再乘手动倍率。双击可输入，结束调整后立即生效。");
  ImGui::Text(u8"当前 %.2f× · 画面 %.0f × %.0f", display.applied, display.viewport.x, display.viewport.y);
  if (ImGui::Button(u8"恢复默认缩放")) {display.automatic = true; display.multiplier = 1; changed = true;}
  if (changed) ImGui::MarkIniSettingsDirty();
}

// io.WantTextInput describes the preceding frame. Read the current editor state
// so the first typed digit is not sent to the game after a double-click.
inline bool TextInputActive() {
  const auto &g = *ImGui::GetCurrentContext();
  return g.ActiveId && g.InputTextState.ID == g.ActiveId && !(g.InputTextState.Flags & ImGuiInputTextFlags_ReadOnly);
}
} // namespace poser_ui
