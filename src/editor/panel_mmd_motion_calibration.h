#pragma once
#include "editor/panel_scale.h"
#include "editor/panel_mmd.h"
#include "game/mmd_squad.h"

static bool MmdCalibrationPercent(const char *label, float &value, float low = 0, float high = 200) {
  float percent = value * 100;
  if (!ImGui::SliderFloat(label, &percent, low, high, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) return false;
  value = percent / 100; return true;
}
static const char *MmdCalibrationJointName(int role) {
  static const char *names[55] = {
    u8"骨盆", u8"左髋", u8"右髋", u8"左膝", u8"右膝", u8"左踝", u8"右踝",
    u8"腰椎", u8"胸部", u8"颈部", u8"头部", u8"左肩", u8"右肩",
    u8"左上臂", u8"右上臂", u8"左肘", u8"右肘", u8"左腕", u8"右腕", u8"左脚尖", u8"右脚尖",
    "", "", "",
    u8"左拇指根节", u8"左拇指中节", u8"左拇指末节", u8"左食指根节", u8"左食指中节", u8"左食指末节",
    u8"左中指根节", u8"左中指中节", u8"左中指末节", u8"左无名指根节", u8"左无名指中节", u8"左无名指末节",
    u8"左小指根节", u8"左小指中节", u8"左小指末节",
    u8"右拇指根节", u8"右拇指中节", u8"右拇指末节", u8"右食指根节", u8"右食指中节", u8"右食指末节",
    u8"右中指根节", u8"右中指中节", u8"右中指末节", u8"右无名指根节", u8"右无名指中节", u8"右无名指末节",
    u8"右小指根节", u8"右小指中节", u8"右小指末节", u8"上胸部"};
  return names[role];
}
static void DrawMmdCalibrationJoint(mmd::MotionCalibration &s, int role, bool linked) {
  ImGui::PushID(role);
  ImGui::TextUnformatted(MmdCalibrationJointName(role));
  ImGui::SameLine();
  bool edit = ImGui::Checkbox(u8"XYZ 偏移", &s.jointOffsetEnabled[role]);
  ImGui::SameLine();
  if (ImGui::SmallButton(u8"复位")) {
    s.joints[role] = 1; s.jointOffsets[role] = {}; edit = true;
  }
  const float labelWidth = (std::max)(ImGui::CalcTextSize(u8"动作比例").x, ImGui::CalcTextSize(u8"XYZ 偏移").x) + ImGui::GetStyle().ItemInnerSpacing.x;
  const float fieldWidth = (std::max)(1.f, ImGui::GetContentRegionAvail().x - labelWidth);
  ImGui::PushItemWidth(fieldWidth);
  edit |= MmdCalibrationPercent(u8"动作比例", s.joints[role]);
  if (s.jointOffsetEnabled[role]) {
    auto &v = s.jointOffsets[role];
    float degrees[3] = {v.x, v.y, v.z};
    if (ImGui::SliderFloat3(u8"XYZ 偏移##offset-values", degrees, -180, 180, u8"%.1f°", ImGuiSliderFlags_AlwaysClamp)) {
      v = {degrees[0], degrees[1], degrees[2]}; edit = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"从左到右为 X / Y / Z；双击输入角度。");
    const int mirror = mmd::MotionMirrorRole(role);
    if (mirror != role) {
      auto &axes = s.jointLinkMirror[(std::min)(role, mirror)];
      static const char *choices[3][2] = {
          {u8"X 同向", u8"X 镜像"}, {u8"Y 同向", u8"Y 镜像"}, {u8"Z 同向", u8"Z 镜像"}};
      const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
      for (int axis = 0; axis < 3; ++axis) {
        if (axis) ImGui::SameLine(0, gap);
        ImGui::PushID(axis);
        ImGui::SetNextItemWidth((std::max)(1.f, (fieldWidth - gap * 2) / 3));
        int mode = axes[axis] ? 1 : 0;
        if (ImGui::Combo("##offset-link", &mode, choices[axis], 2)) {
          axes[axis] = mode == 1; edit = true;
        }
        ImGui::PopID();
      }
      ImGui::SameLine(0, gap); ImGui::TextUnformatted(u8"联动方向");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"同向：另一侧角度相同；镜像：另一侧角度取反。\n左右关节共用规则，开启左右联动后生效。");
    }
    if (ImGui::SmallButton(u8"偏移归零")) {v = {}; edit = true;}
  }
  ImGui::PopItemWidth();
  if (edit && linked) mmd::MirrorJointCalibration(s, role);
  ImGui::Spacing();
  ImGui::PopID();
}
static void DrawMmdMotionCalibrationPanel() {
  auto &m = g_mmd;
  if (!m.showMotionCalibration) return;
  poser_ui::NextPanel(u8"动作校准", {550, 90}, {510, 620}, {380, 300}, g_resetPanelLayoutFrames > 0);
  if (!ImGui::Begin(u8"动作校准", &m.showMotionCalibration, g_pinPanels ? ImGuiWindowFlags_NoMove : 0)) {
    ImGui::End(); return;
  }
  const std::string selected = m.motionCalibrationTarget ?
      u8"第 " + std::to_string(m.motionCalibrationTarget) + u8" 位" : u8"共用参数";
  if (ImGui::BeginCombo(u8"校准对象", selected.c_str())) {
    for (int i = 0; i <= 4; ++i) {
      std::string label = i ? u8"第 " + std::to_string(i) + u8" 位：" +
          (g_squad.slots[i - 1].member.empty() ? u8"待读取" : g_squad.slots[i - 1].member) : u8"共用参数（单人 / 未独立的队员）";
      if (ImGui::Selectable(label.c_str(), m.motionCalibrationTarget == i)) m.motionCalibrationTarget = i;
    }
    ImGui::EndCombo();
  }
  const int target = m.motionCalibrationTarget;
  ImGui::PushID(target); // Text edits and controls never carry across members.
  ImGui::BeginDisabled(m.loading || g_squad.loading || m.preview);
  if (target) {
    auto &own = m.squadMotion[target - 1];
    bool independent = own.independent;
    if (ImGui::Checkbox(u8"此队员使用独立动作校准", &independent)) MmdSetIndependentMotion(target, independent);
    if (ImGui::SmallButton(u8"从共用参数重新复制")) MmdCopySharedMotion(target);
    ImGui::TextWrapped(u8"设置按小队位置保留；换队或调整顺序后，请检查对应参数。关闭独立校准会改用共用参数，保留独立值。");
  } else ImGui::TextWrapped(u8"共用参数影响单人及未开启独立校准的队员。");
  ImGui::EndDisabled();
  const bool inherited = target && !m.squadMotion[target - 1].independent;
  const auto settings = MmdMotionSettings(target);
  const bool squad = target || (g_mmdSquadBridge.hotkeyTarget && g_mmdSquadBridge.hotkeyTarget());
  const auto &timeline = squad ? g_squad.timeline : m.timeline;
  const auto &pending = squad ? g_squad.pending : s_mmdStartRequest;
  const double currentTime = pending.active && std::isfinite(pending.seconds) ?
      (std::max)(0., (std::min)(timeline.duration, pending.seconds)) : timeline.seconds;
  ImGui::TextWrapped(u8"暂停在问题帧调整，下一次游戏更新即预览。无需源 PMX。");
  ImGui::BeginDisabled(m.loading || g_squad.loading || m.preview || (squad && m.session.active));
  auto command = [&](int value) {
    if (squad) {
      g_squad.hotkeys = true;
      if (!MmdSquadCommand(value)) m.status = u8"请先在多人播放器为队员选择动作";
    } else MmdPlaybackCommand(value, false);
  };
  if (ImGui::Button(timeline.state == mmd::PlayState::Playing ? u8"暂停" : u8"播放 / 继续"))
    command(timeline.state == mmd::PlayState::Playing ? 1 : 0);
  ImGui::SameLine();
  if (ImGui::Button(u8"停止并恢复")) command(2);
  ImGui::SameLine();
  auto seek = [&](double seconds) {
    if (squad) MmdSquadSeek(seconds);
    else MmdSeekOrStart(seconds);
  };
  if (ImGui::SmallButton("<")) seek(currentTime - 1. / 30);
  ImGui::SameLine();
  if (ImGui::SmallButton(">")) seek(currentTime + 1. / 30);
  float seconds = float(currentTime);
  ImGui::BeginDisabled(timeline.duration <= 0);
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat("##motion-calibration-time", &seconds, 0, float(timeline.duration), "%.2f s", ImGuiSliderFlags_AlwaysClamp))
    seek(seconds);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"拖动选择进度并暂停；双击输入秒数。");
  ImGui::EndDisabled();
  ImGui::Text(u8"%s · 帧 %.1f / %.0f", squad ? u8"多人共用时间轴" : u8"单人", double(seconds) * 30, timeline.duration * 30);
  ImGui::EndDisabled();
  ImGui::BeginDisabled(m.loading || g_squad.loading || m.preview);
  ImGui::BeginDisabled(inherited);
  if (ImGui::Button(u8"保存动作校准")) MmdBeginLoad(8, {}, target);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(u8"载入动作校准")) MmdBeginLoad(7, {}, target);
  ImGui::EndDisabled();
  if (m.loading) ImGui::TextDisabled(u8"正在读取或保存…");
  else ImGui::TextWrapped("%s", m.status.c_str());
  DrawMmdFile(settings.file);
  if (inherited) ImGui::TextWrapped(u8"当前使用共用参数。开启独立校准或载入预设后，可单独调整此队员。");

  ImGui::BeginDisabled(m.loading || g_squad.loading || m.preview || inherited);
  auto &s = settings.motion;
  ImGui::Checkbox(u8"启用动作校准", &s.enabled);
  ImGui::SameLine();
  if (ImGui::Button(u8"全部复位")) {s = {}; settings.amplitude = {};}
  static bool linkedTargets[5] = {};
  bool &linked = linkedTargets[target];
  if (ImGui::Checkbox(u8"左右联动（开启时以左侧为准）", &linked) && linked) {
    for (int i : {0, 2}) mmd::MirrorLimbCalibration(s, i);
    for (int r = 0; r < 55; ++r) {
      int mirror = mmd::MotionMirrorRole(r);
      if (mirror > r) mmd::MirrorJointCalibration(s, r);
    }
  }
  ImGui::BeginChild("##motion-sizing-scroll", ImVec2(0, 0), false);
  ImGui::BeginDisabled(!s.enabled);
  if (ImGui::CollapsingHeader(u8"四肢伸展与手脚落点", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextWrapped(u8"100% 保留原伸展；降低可收近手脚。只调整动作，身体骨长不变，超出可达范围时自动限位。");
    static int limb = 0;
    ImGui::Combo(u8"部位", &limb, u8"左臂\0右臂\0左腿\0右腿\0");
    auto &l = s.limbs[limb];
    bool edit = MmdCalibrationPercent(limb < 2 ? u8"上臂伸展比例" : u8"大腿伸展比例", l.upper, 50, 150);
    edit |= MmdCalibrationPercent(limb < 2 ? u8"前臂伸展比例" : u8"小腿伸展比例", l.lower, 50, 150);
    float percent[3] = {l.offset.x * 100, l.offset.y * 100, l.offset.z * 100};
    if (ImGui::SliderFloat3(u8"落点 左 / 上 / 前", percent, -50, 50, "%.1f%%", ImGuiSliderFlags_AlwaysClamp)) {
      l.offset = {percent[0] / 100, percent[1] / 100, percent[2] / 100}; edit = true;
    }
    ImGui::TextDisabled(u8"偏移为该侧整条臂 / 腿长的百分比，随身体朝向；正值向左、上、前。");
    if (ImGui::SmallButton(u8"复位此侧伸展与落点")) {l = {}; edit = true;}
    if (edit && linked) mmd::MirrorLimbCalibration(s, limb);
    auto status = [&](const mmd::SampledPose &pose) {
      const int state = pose.sizingStatus[limb];
      ImGui::TextDisabled(u8"落点：%s", state < 0 ? u8"骨架或动作映射不适用" : state == 2 ? u8"超出可达范围，已限位" :
          state == 1 ? u8"修正已应用" : u8"使用原动作");
    };
    if (g_squad.active) {
      for (int i = 0; i < 4; ++i) if (g_squad.actors[i] && (!target || target == i + 1)) {
        ImGui::TextDisabled(u8"第 %d 位", i + 1); ImGui::SameLine(); status(g_squad.actors[i]->mapper.output);
      }
    } else if (!target && m.session.active && m.session.bodyOwned) status(m.mapper.output);
  }
  if (ImGui::CollapsingHeader(u8"逐关节比例与偏移")) {
    ImGui::TextWrapped(u8"100% 保留动作；0% 保持该关节的基准角度。与全身、部位幅度相乘；后续调节关节可能改变手脚落点。");
    ImGui::TextWrapped(u8"XYZ 偏移在动作上叠加固定角度，0° 不偏移，不受动作比例缩放。双击可输入度数。开启左右联动后，各关节的 X/Y/Z 可分别选择同向（角度相同）或镜像（角度取反）。");
    static int group = 0;
    ImGui::Combo(u8"关节分组", &group, u8"躯干与头颈\0肩臂与手腕\0髋膝与脚踝\0左手指\0右手指\0");
    for (int r = 0; r < 55; ++r) {
      int part = mmd::MotionPartForRole(r);
      bool selected = group == 0 ? (part == 0 || part == 1) :
          group == 1 ? (r >= 11 && r <= 18) :
          group == 2 ? ((r >= 1 && r <= 6) || r == 19 || r == 20) :
          group == 3 ? (r >= 24 && r <= 38) : (r >= 39 && r <= 53);
      if (!selected || part < 0) continue;
      DrawMmdCalibrationJoint(s, r, linked);
    }
  }
  if (ImGui::CollapsingHeader(u8"整体移动比例")) {
    MmdCalibrationPercent(u8"横向移动", s.travel.x);
    MmdCalibrationPercent(u8"上下起伏", s.travel.y);
    MmdCalibrationPercent(u8"前后移动", s.travel.z);
    if (ImGui::SmallButton(u8"复位移动比例")) s.travel = {1, 1, 1};
    ImGui::TextDisabled(u8"仅缩放动作自带位移；原地开关、高度修正与地形跟随后续生效。");
  }
  DrawMmdAmplitude(settings.amplitude, target);
  ImGui::EndDisabled();
  ImGui::EndChild();
  ImGui::EndDisabled();
  ImGui::PopID();
  ImGui::End();
}
