#pragma once
#include "imgui.h"

// Optional font files are user input. Let ImGui reject malformed font data
// and roll back its atlas entry so the caller can try the next candidate.
// Keep normal UI assertions enabled outside this single loading operation.
inline ImFont *TryLoadOverlayFont(ImGuiIO &io, const char *path,
                                  ImFontConfig config, const ImWchar *ranges) {
  struct RestoreErrors {
    ImGuiIO &io;
    bool assertion, tooltip;
    ~RestoreErrors() {
      io.ConfigErrorRecoveryEnableAssert = assertion;
      io.ConfigErrorRecoveryEnableTooltip = tooltip;
    }
  } restore{io, io.ConfigErrorRecoveryEnableAssert,
            io.ConfigErrorRecoveryEnableTooltip};
  io.ConfigErrorRecoveryEnableAssert = false;
  io.ConfigErrorRecoveryEnableTooltip = false;
  config.Flags |= ImFontFlags_NoLoadError;
  return io.Fonts->AddFontFromFileTTF(path, 18.0f, &config, ranges);
}
