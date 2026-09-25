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

static void DrawMmdCollision() {
  if (!ImGui::CollapsingHeader(u8"地面与裙摆碰撞辅助")) return;
  auto &m=g_mmd; auto &c=m.contact;
  bool changed=ImGui::Checkbox(u8"脚底防穿地",&c.enabled);
  ImGui::BeginDisabled(!c.enabled);
  int mode=c.scene?0:1;
  if(ImGui::Combo(u8"地面来源",&mode,u8"探测游戏地面\0固定平面（起始脚底）\0")) {
    c.scene=mode==0;m.groundPlanes={};m.groundSampleTime=-1e30;changed=true;
  }
  changed |= ImGui::SliderFloat(u8"接触修正强度",&c.strength,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
  changed |= ImGui::SliderFloat(u8"最大抬脚修正",&c.maxLift,.01f,.5f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
  changed |= ImGui::SliderFloat(u8"鞋底厚度补偿",&c.sole,0,.2f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
  changed |= ImGui::SliderFloat(u8"地面高度微调",&c.groundOffset,-.3f,.3f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
  changed |= ImGui::Checkbox(u8"脚部跟随坡面",&c.slope);
  ImGui::EndDisabled();
  if(c.enabled && m.session.active) {
    ImGui::TextWrapped("%s",c.scene?(MmdNow()-m.groundSampleTime>.3?u8"等待游戏线程地面采样；当前保留原动作":m.groundProbe.status):u8"使用起始脚底估计的固定平面；可微调高度");
    ImGui::Text(u8"脚部修正：左 %s / 右 %s",m.contactResult.active[0]?u8"生效":u8"无",m.contactResult.active[1]?u8"生效":u8"无");
    if(m.contactResult.limited || m.contactResult.penetration>.005f)
      ImGui::TextWrapped(u8"修正受骨长或幅度上限限制，仍可能穿地；可调整整体高度。剩余估计 %.3f",m.contactResult.penetration);
  }
  ImGui::TextWrapped(u8"只修正接近地面的脚，不把抬脚和跳跃拉回地面；暂停、拖动和循环均重新采样。固定平面不识别台阶。不会处理身体、墙壁或衣物网格与地面的碰撞。");
  ImGui::Separator();
  ImGui::Checkbox(u8"BBC 原生同帧碰撞",&g_bbcSyncEnabled);
  ImGui::TextWrapped("%s",g_bbcStatus);
  ImGui::TextWrapped(u8"播放时让 BBC 读取当前动作骨骼，求解后由游戏写回衣物。暂停时继续模拟；停止恢复原调度。原生调度开关作用于全场景布料，可能增加 CPU 开销。");
  ImGui::Checkbox(u8"当前角色使用完整物理权重（含尾巴）",&g_bbcFullSimulation);
  ImGui::TextWrapped(u8"仅在 BBC 接管时生效：将当前角色已启用衣物、头发和尾巴的模拟/混合权重设为 1，避免保留待机动画的低权重。关闭或停止后恢复；不会启用隐藏组件。");
  if(ImGui::TreeNode(u8"当前角色 BBC 状态（含尾巴）")) {
    for(const auto &cloth:g_bbcPlaybackCloths)if(cloth.active) {
      ImGui::TextWrapped("%s",cloth.name);
      if(!cloth.compatible)ImGui::TextDisabled(u8"权重接口不兼容，保留原设置");
      else ImGui::TextDisabled(u8"模拟 %.2f / 混合 %.2f / 运行 %d / 跳过写回 %d / 裁剪 %d",
          cloth.weight,cloth.blend,cloth.running,cloth.skip,cloth.culled);
    }
    ImGui::TreePop();
  }
  bool skirt=ImGui::Checkbox(u8"裙摆碰撞增强",&g_skirtCollisionEnabled);
  ImGui::BeginDisabled(!g_skirtCollisionEnabled);
  skirt |= ImGui::Checkbox(u8"裙摆边碰撞（减少节点间穿透）",&g_skirtEdgeCollision);
  ImGui::Checkbox(u8"补齐腿部原生碰撞（大腿 / 膝盖 / 小腿）",&g_bbcLegCoverage);
  ImGui::BeginDisabled(!g_bbcLegCoverage);
  ImGui::SliderFloat(u8"腿部碰撞余量",&g_bbcLegPadding,0,.06f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::TextWrapped(u8"按当前角色骨长和原有大腿半径添加临时 BBC 胶囊，仅登记到裙摆。余量默认 0.015；过大会撑开裙子。关闭、停止或换人后撤销。需要开启 BBC 原生同帧碰撞。");
  for(const auto &cloth:g_bbcPlaybackCloths)if(cloth.active && cloth.garment.garment) {
    const auto &e=cloth.garment;int submitted=0,effective=0;
    for(const auto &leg:e.legs) {submitted+=leg.listed;effective+=leg.effective==1;}
    ImGui::TextWrapped("%s: %s",cloth.name,e.status);
    ImGui::Text(u8"新增胶囊：已提交 %d / 原生登记 %d；动画距离限位 %d / 背面限位 %d",submitted,effective,e.maxDistance,e.backstop);
  }
  skirt |= ImGui::Checkbox(u8"手动调整碰撞体尺寸",&g_skirtGeometryOverride);
  ImGui::BeginDisabled(!g_skirtGeometryOverride);
  skirt |= ImGui::SliderFloat(u8"大腿根碰撞扩张",&g_skirtHipRadiusDelta,0,.25f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
  skirt |= ImGui::SliderFloat(u8"碰撞半径倍率",&g_skirtRadiusA,.75f,1.5f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
  skirt |= ImGui::SliderFloat(u8"碰撞体长度倍率",&g_skirtLengthScale,.75f,1.3f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
  skirt |= ImGui::Checkbox(u8"锥形碰撞体",&g_skirtTaperOn);
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  ImGui::TextWrapped("%s",g_skirtStatus);
  ImGui::Text(u8"裙摆组件 %d / 检查碰撞体 %d / 实际调整 %d",int(g_skirtCloths.size()),g_skirtExamined,g_skirtMatched);
  if(g_skirtUnsupported) ImGui::TextWrapped(u8"%d 项接口或结构不支持，已跳过。",g_skirtUnsupported);
  for(auto &cloth:g_skirtCloths) {
    ImGui::TextWrapped("%s: %s",cloth.name,cloth.modeStatus);
    if(cloth.running>=0) ImGui::Text(u8"BBC 运行 %s / 写回 %s / 权重 %.2f / 求解模式 %d",
      cloth.running?u8"是":u8"否",cloth.skipWriting==0?u8"开启":u8"未开启",cloth.runtimeWeight,cloth.effectiveMode);
  }
  if(m.freezeCloth) ImGui::TextWrapped(u8"衣物已冻结：不会动态避让大腿。取消“冻结头发 / 衣物”后才能观察裙摆碰撞效果。");
  else ImGui::TextWrapped(u8"边碰撞检测布料节点之间的连线，开销高于原节点碰撞；关闭后恢复原模式。仅作用于已有碰撞的裙摆，不改变动作。碰撞体覆盖不到的部位、固定顶点和快速穿透仍可能穿模；扩张过大会撑起裙摆。");
  if(ImGui::SmallButton(u8"复位碰撞辅助")) {
    c={};g_skirtCollisionEnabled=true;g_skirtHipRadiusDelta=.124f;
    g_skirtRadiusA=1;g_skirtLengthScale=1;g_skirtTaperOn=true;g_skirtEdgeCollision=true;
    g_bbcSyncEnabled=true;g_skirtGeometryOverride=false;
    g_bbcFullSimulation=true;
    g_bbcLegCoverage=true;g_bbcLegPadding=.015f;
    changed=skirt=true;
  }
  if(skirt) SkirtMarkDirty();
  ImGui::TextWrapped(u8"停止播放后可随适配预设保存这些设置；不保存当前场景的绝对地面位置。");
  if(changed) MmdApplyFrame();
}
static void DrawMmdCamera() {
  if (!ImGui::CollapsingHeader(u8"MMD 镜头")) return;
  auto &m=g_mmd;auto &s=m.cameraSettings;const auto &keys=MmdCameraKeys();
  ImGui::BeginDisabled(m.loading || m.session.active);
  if (ImGui::Button(u8"选择镜头 VMD")) MmdBeginLoad(6);
  ImGui::SameLine();
  if (ImGui::Button(u8"移除镜头")) {
    m.cameraFile.clear();m.cameraTrack.clear();m.clip.cameras.clear();
    mmd::Recount(m.clip);MmdUpdateDuration();mmd_camera::Stop();
  }
  ImGui::EndDisabled();
  if(m.session.active)ImGui::TextDisabled(u8"停止并恢复后可更换镜头文件");
  if (keys.empty()) {
    ImGui::TextWrapped(u8"选择独立镜头 VMD，或打开包含镜头轨道的动作 VMD。也支持只播放镜头。");
    return;
  }
  ImGui::TextWrapped("%s",m.cameraFile.empty()?m.file.c_str():m.cameraFile.c_str());
  ImGui::Text(u8"镜头关键帧 %zu / %.2f 秒",keys.size(),keys.back().frame/30.0);
  bool changed=ImGui::Checkbox(u8"随动作播放镜头",&s.enabled);
  int origin=int(s.origin);
  if(ImGui::Combo(u8"镜头原点",&origin,u8"按文件轨迹／固定播放起点\0追踪当前角色位移\0")) {
    s.origin=static_cast<mmd::CameraOrigin>(origin);changed=true;
  }
  ImGui::TextWrapped(u8"VMD 没有角色跟随标志。按文件模式以开始播放时角色位置为零点，保留文件原有运镜；追踪模式额外叠加角色位移，已有跟拍的文件通常无需开启。");
  if(s.origin==mmd::CameraOrigin::Follow)changed|=ImGui::Checkbox(u8"跟随上下起伏",&s.followVertical);
  changed|=ImGui::SliderFloat3(u8"镜头偏移（左右／上下／前后）",&s.offset.x,-3,3,"%.3f");
  ImGui::TextWrapped(u8"偏移沿播放开始时的角色坐标轴，单位为游戏世界单位；调整中间的上下值可适配身高。");
  changed|=ImGui::Checkbox(u8"镜头比例跟随动作位移比例",&s.linkScale);
  if(!s.linkScale)changed|=ImGui::SliderFloat(u8"镜头单位比例",&s.scale,.001f,.3f,"%.4f");
  changed|=ImGui::SliderFloat(u8"镜头距离比例",&s.distanceScale,.1f,3.f,"%.2f");
  changed|=ImGui::SliderFloat(u8"镜头整体朝向",&s.yaw,-180,180,"%.1f deg");
  changed|=ImGui::SliderFloat(u8"视角偏移",&s.fovOffset,-60,60,"%.1f deg");
  changed|=ImGui::Checkbox(u8"相邻帧视为切镜",&s.cuts);
  if(ImGui::Button(u8"复位镜头调整")){s=mmd::CameraSettings{};changed=true;}
  if(changed)MmdPublishCamera();
  ImGui::TextWrapped("%s",mmd_camera::status.c_str());
  if(!mmd_camera::ready)ImGui::TextWrapped(u8"相机接口尚未就绪，身体动作仍可播放。");
  else if(mmd_camera::request.active && MmdNow()-mmd_camera::lastCallback>2)
    ImGui::TextWrapped(u8"等待游戏相机更新；尚未确认镜头实际生效。");
  ImGui::Text(u8"相机更新 %llu / 实际写入 %llu",(unsigned long long)mmd_camera::callbacks,(unsigned long long)mmd_camera::applied);
  ImGui::TextWrapped(u8"镜头与动作同步暂停、拖动、倍速和循环。隐藏面板后继续；停止、关闭镜头或换人后恢复原相机。镜头调整当前在本次运行中保留。");
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
    DrawMmdCamera();
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
    if (g_frameDiagnostics.source == 3) ImGui::TextDisabled(u8"帧来源：BBC 原生布料求解前");
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
                m.clip.morphs.size(), m.timeline.duration);
    ImGui::Separator();
    ImGui::BeginDisabled(!MmdHasContent() || m.loading || m.preview);
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
    ImGui::Text(u8"帧 %.1f / %.0f", m.timeline.seconds * 30, m.timeline.duration * 30);
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
        m.session.active && m.session.bodyOwned) {
      g_freezeAccessories = m.freezeCloth;
      if (m.freezeCloth)
        CaptureAccessorySnapshot();
      SetAllPhysicsEnabled(!m.freezeCloth, true);
    }
    DrawMmdCollision();
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
