#pragma once
#include "config.h"
#include "game/mmd_player.h"
#include "editor/panel_mmd_adaptation.h"
#include "imgui.h"

static void DrawMmdHotkeyHints() {
  char keys[4][48] = {};
  for (int i = 0; i < 4; ++i)
    HotkeyDisplay(g_mmdHotkeyVK[i], g_mmdHotkeyCtrl[i], keys[i], sizeof(keys[i]));
  ImGui::TextWrapped(u8"播放/继续：%s    暂停：%s", keys[0], keys[1]);
  ImGui::TextWrapped(u8"停止并恢复：%s    重置动作：%s", keys[2], keys[3]);
}

static void DrawMmdAmplitude() {
  if (!ImGui::CollapsingHeader(u8"动作幅度（全身 / 分部位）")) return;
  auto &a = g_mmd.amplitude;
  bool changed = false;
  auto slider = [&](const char *label, float &value) {
    float percent = value * 100.f;
    bool edit = ImGui::SliderFloat(label, &percent, 0, 200, "%.0f%%");
    if (edit) value = mmd::MotionAmplitude::safe(percent / 100.f);
    return edit;
  };
  changed |= slider(u8"全身幅度", a.master);
  if (ImGui::Button(u8"全部幅度复位 100%")) { a = {}; changed = true; }
  static bool linked = false;
  if (ImGui::Checkbox(u8"左右联动（开启时以左侧为准）", &linked) && linked) {
    for (int i = 2; i < int(mmd::MotionPart::Count); i += 2) a.parts[i + 1] = a.parts[i];
    changed = true;
  }
  const char *labels[] = {u8"躯干", u8"头颈", u8"左臂", u8"右臂", u8"左手 / 手指",
    u8"右手 / 手指", u8"左腿", u8"右腿", u8"左脚 / 脚尖", u8"右脚 / 脚尖"};
  for (int i = 0; i < int(mmd::MotionPart::Count); ++i) {
    ImGui::PushID(i);
    bool edited = slider(labels[i], a.parts[i]);
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"复位")) { a.parts[i] = 1; edited = true; }
    if (edited && linked && i >= 2) a.parts[i ^ 1] = a.parts[i];
    changed |= edited;
    ImGui::PopID();
  }
  ImGui::TextWrapped(u8"实际幅度 = 全身 × 部位（例如 80% × 50% = 40%）。100% 保留原动作；0% 回到该部位的基准姿态。手包含手腕和手指。");
  ImGui::TextWrapped(u8"只调关节旋转，不改变骨长、位移、表情和速度。可在暂停时调整；腿脚调整可能改变接地，不能保证消除穿模。停止后可随适配预设保存。");
  if (changed) MmdApplyFrame();
}

static void DrawMmdPanel() {
  auto &m = g_mmd;
  if (!m.show)
    return;
  const float panelX = ImGui::GetIO().DisplaySize.x >= 1630 ? 1130.f : 380.f;
  const float panelY = ImGui::GetIO().DisplaySize.x >= 1630 ? 10.f : 370.f;
  ImGui::SetNextWindowPos(ImVec2(panelX, panelY), PanelPositionCondition());
  ImGui::SetNextWindowSize(ImVec2(470, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(u8"MMD 播放器", &m.show,
                    g_pinPanels ? ImGuiWindowFlags_NoMove : 0)) {
    ImGui::End();
    return;
  }
  try {
    DrawMmdHotkeyHints();
    char panelKey[48] = {};
    HotkeyDisplay(g_guiToggleVK, g_guiToggleCtrl, panelKey, sizeof(panelKey));
    ImGui::TextWrapped(u8"显示/隐藏界面：%s（隐藏后快捷键仍有效）", panelKey);
    ImGui::Separator();
    if (ImGui::CollapsingHeader(u8"音乐同步")) {
      ImGui::BeginDisabled(m.loading || m.session.active);
      if (ImGui::Button(u8"选择音乐")) MmdBeginLoad(5);
      ImGui::SameLine();
      if (ImGui::Button(u8"移除音乐")) {
        m.audio.setClip({}); m.musicFile.clear(); m.musicError.clear();
      }
      ImGui::EndDisabled();
      if (m.session.active) ImGui::TextDisabled(u8"停止并恢复后可更换音乐");
      if (m.audio.clip()) {
        ImGui::TextWrapped("%s", m.musicFile.c_str());
        ImGui::Text(u8"音乐长度：%.2f 秒", m.audio.clip()->duration());
        if (ImGui::Checkbox(u8"随动作播放音乐", &m.musicEnabled)) {
          m.musicError.clear(); MmdSyncAudio();
        }
        if (ImGui::SliderFloat(u8"音乐音量", &m.musicVolume, 0, 1, "%.2f")) MmdSyncAudio();
        if (ImGui::InputFloat(u8"音乐偏移（秒）", &m.musicOffset, .01f, .1f, "%.3f")) {
          if (!std::isfinite(m.musicOffset)) m.musicOffset = 0;
          m.musicOffset = mmd::Clamp(m.musicOffset, -600.f, 600.f);
          m.audio.close(); MmdSyncAudio();
        }
        ImGui::TextWrapped(u8"正值：音乐晚开始；负值：从音乐中途开始。音乐随暂停、拖动、重置和循环同步。变速会同时改变音高。");
      } else ImGui::TextWrapped(u8"手动选择 WAV / MP3 / M4A 等音频；无需音乐也可播放动作。");
      if (!m.musicError.empty()) ImGui::TextWrapped("%s", m.musicError.c_str());
    }
    ImGui::TextDisabled(g_frameDiagnostics.gameDriven ? u8"动作更新：跟随游戏帧" : u8"动作更新：独立计时（游戏帧回调未触发）");
    if (g_frameDiagnostics.source == 2) ImGui::TextDisabled(u8"帧来源：游戏渲染管线 SRP");
    ImGui::TextDisabled(u8"实际 %.1f Hz / 最长间隔 %.1f ms / 最大耗时 %.1f ms",
                        g_frameDiagnostics.hz, g_frameDiagnostics.maxGapMs, g_frameDiagnostics.maxCostMs);
    if (g_frameDiagnostics.busy) ImGui::TextDisabled(u8"本秒因编辑占用跳过：%u", g_frameDiagnostics.busy);
    ImGui::Separator();
    ImGui::BeginDisabled(m.loading || m.session.active);
    if (ImGui::Button(u8"打开 VMD"))
      MmdBeginLoad(0);
    ImGui::SameLine();
    if (ImGui::Button(u8"追加口型 / 表情 / 眼神"))
      MmdBeginLoad(1);
    if (ImGui::Button(u8"选择 PMX 骨架参考"))
      MmdBeginLoad(2);
    ImGui::SameLine();
    if (m.reference && ImGui::SmallButton(u8"使用内置骨架")) {
      m.reference = false;
      if (MmdApplyAdaptation(m.adaptation, m.sourcePreset)) {
        m.referenceFile.clear(); m.scale = .08f;
      } else m.reference = true;
    }
    if (!m.reference) {
      int sourcePreset = m.sourcePreset;
      if (ImGui::Combo(u8"动作原始姿态", &sourcePreset,
                       u8"标准 MMD / A 姿\0提取动作 / T 姿\0")) {
        MmdApplyAdaptation(m.adaptation, sourcePreset);
      }
      if (m.rig.name == "Extracted T-pose")
        ImGui::TextWrapped(u8"T 姿基准：手臂和手指平行展开，中心骨在原点。");
    }
    ImGui::EndDisabled();
    if (m.loading)
      ImGui::TextDisabled(u8"正在读取文件...");
    if (!m.file.empty())
      ImGui::TextWrapped("%s", m.file.c_str());
    ImGui::TextWrapped(u8"骨架：%s",
                       m.reference ? m.referenceFile.c_str() : MmdSourceRigLabel());
    ImGui::Text(u8"骨骼轨道 %zu / 表情轨道 %zu / %.2f 秒", m.clip.bones.size(),
                m.clip.morphs.size(), m.clip.duration());
    ImGui::Separator();
    ImGui::BeginDisabled(m.clip.empty() || m.loading || m.preview);
    if (ImGui::Button(m.timeline.state == mmd::PlayState::Playing ? u8"暂停"
                                                                  : u8"播放")) {
      MmdPlaybackCommand(m.timeline.state == mmd::PlayState::Playing ? 1 : 0);
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"停止并恢复"))
      MmdPlaybackCommand(2);
    ImGui::SameLine();
    if (ImGui::Button(u8"重置动作"))
      MmdPlaybackCommand(3);
    ImGui::SameLine();
    if (ImGui::SmallButton("<")) {
      if (!m.session.active)
        MmdStart();
      if (m.session.active) {
        MmdSeek(m.timeline.seconds - 1. / 30);
        MmdApplyFrame();
      }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">")) {
      if (!m.session.active)
        MmdStart();
      if (m.session.active) {
        MmdSeek(m.timeline.seconds + 1. / 30);
        MmdApplyFrame();
      }
    }
    float seconds = float(m.timeline.seconds);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat(u8"##mmdtime", &seconds, 0,
                           float(m.timeline.duration), "%.2f s")) {
      if (!m.session.active)
        MmdStart();
      if (m.session.active) {
        MmdSeek(seconds);
        MmdApplyFrame();
      }
    }
    ImGui::Text(u8"帧 %.1f / %u", m.timeline.seconds * 30, m.clip.lastFrame);
    if (m.session.active && !m.preview)
      ImGui::TextDisabled(
          m.timeline.state == mmd::PlayState::Playing ? u8"正在播放"
          : m.timeline.seconds >= m.timeline.duration ? u8"已到末帧，保持姿态"
                                                      : u8"已暂停，保持姿态");
    float speed = float(m.timeline.speed);
    if (ImGui::SliderFloat(u8"播放速度", &speed, .25f, 2.f, "%.2fx")) {
      m.timeline.tick(MmdNow());
      m.timeline.speed = speed;
      MmdSyncAudio();
    }
    ImGui::Checkbox(u8"循环播放", &m.timeline.loop);
    ImGui::SameLine();
    ImGui::Checkbox(u8"原地播放", &m.inPlace);
    int ikMode = int(m.ikMode);
    if (ImGui::Combo(u8"动作 IK", &ikMode,
                     u8"跟随动作\0强制开启\0强制关闭\0")) {
      m.ikMode = static_cast<mmd::IkMode>(ikMode);
      MmdApplyFrame();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(u8"手动覆盖脚部、脚尖及 PMX 的 IK；暂停时也会立即更新姿态。\n关闭后按骨骼旋转播放；开启后使用所选骨架的 IK 目标。");
    ImGui::TextWrapped(u8"已有脚 IK 轨道但无开关时，跟随模式默认开启。若膝盖动作已写在骨骼旋转中，可手动强制关闭 IK。");
    if (m.session.active && !m.preview)
      ImGui::Text(u8"腿部 IK：左 %s / 右 %s",
                  m.mapper.output.legIkActive[0] ? u8"开启" : u8"关闭",
                  m.mapper.output.legIkActive[1] ? u8"开启" : u8"关闭");
    if (ImGui::SliderFloat(u8"位移比例", &m.scale, .001f, .3f, "%.4f"))
      m.autoScale = false;
    if (m.reference && ImGui::SmallButton(u8"按腿长恢复比例")) {
      m.autoScale = true;
      if (m.session.active)
        m.scale = m.mapper.suggestedScale;
    }
    ImGui::SliderFloat(u8"高度修正", &m.height, -1, 1, "%.3f");
    DrawMmdAmplitude();
    if (ImGui::Checkbox(u8"冻结头发 / 衣物", &m.freezeCloth) &&
        m.session.active) {
      g_freezeAccessories = m.freezeCloth;
      if (m.freezeCloth)
        CaptureAccessorySnapshot();
      SetAllPhysicsEnabled(!m.freezeCloth, true);
    }
    ImGui::EndDisabled();
    if (MmdOwnsPose()) {
      ImGui::TextDisabled(u8"动作控制中；暂停后可在姿态库保存当前身体姿态");
      if (!m.clip.morphs.empty())
        ImGui::TextDisabled(SMCSectionReady()
                                ? u8"表情系统已就绪"
                                : u8"表情系统初始化中，身体动作继续播放");
    }
    ImGui::TextWrapped("%s", m.status.c_str());
    DrawMmdAdaptationPanel();
    if (ImGui::CollapsingHeader(u8"角色校准")) {
      ImGui::TextWrapped("%s", m.calibrationStatus.c_str());
      ImGui::BeginDisabled(m.session.active || m.loading);
      if (ImGui::Button(u8"自动校准")) {
        m.profileRevision = -1;
        MmdPrepareProfile();
      }
      ImGui::SameLine();
      if (ImGui::Button(u8"手动 T 姿预览"))
        MmdBeginCalibration();
      ImGui::EndDisabled();
      if (m.preview) {
        ImGui::TextWrapped(
            u8"用骨骼面板修正 T 姿。确认后保存校准并恢复原姿态。");
        if (ImGui::Button(u8"确认保存校准"))
          MmdConfirmCalibration();
        ImGui::SameLine();
        if (ImGui::Button(u8"取消校准"))
          MmdStop();
      }
    }
    if (ImGui::CollapsingHeader(u8"表情映射")) {
      ImGui::BeginDisabled(MmdOwnsPose());
      bool changed = false;
      for (auto &kv : m.morphMap) {
        ImGui::PushID(kv.first.c_str());
        ImGui::TextUnformatted(kv.first.c_str());
        ImGui::SetNextItemWidth(185);
        const char *label = kv.second.slider < 0
                                ? u8"忽略 / 不支持"
                                : SMCSliderLabel(kv.second.slider);
        if (ImGui::BeginCombo("##target", label)) {
          if (ImGui::Selectable(u8"忽略 / 不支持", kv.second.slider < 0)) {
            kv.second.slider = -1;
            changed = true;
          }
          for (int i = 0; i < SMCSliderCount(); i++)
            if (ImGui::Selectable(SMCSliderLabel(i), kv.second.slider == i)) {
              kv.second.slider = i;
              changed = true;
            }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        changed |= ImGui::SliderFloat("##gain", &kv.second.gain, 0, 2, "%.2f");
        ImGui::PopID();
      }
      if (changed) {
        MmdSaveMappings();
        MmdReport();
      }
      ImGui::EndDisabled();
    }
    if (ImGui::CollapsingHeader(u8"导入报告")) {
      if (m.report.empty())
        ImGui::TextDisabled(u8"没有发现未映射轨道");
      ImGui::BeginChild("##mmdreport", ImVec2(0, 130), true);
      for (auto &line : m.report)
        ImGui::TextWrapped("%s", line.c_str());
      ImGui::EndChild();
    }
  } catch (const std::exception &e) {
    MmdStop();
    m.status = e.what();
  }
  ImGui::End();
}
