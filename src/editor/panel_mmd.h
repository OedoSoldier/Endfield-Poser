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

static void DrawMmdCloth() {
  if (!ImGui::CollapsingHeader(u8"衣物物理")) return;
  float hip=s_skirtHipRadiusDelta.load();
  bool changed=ImGui::SliderFloat(u8"腿根半径补偿", &hip, 0, .25f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
  if(ImGui::SmallButton(u8"恢复默认补偿")) {hip=.124f;changed=true;}
  if(changed) {s_skirtHipRadiusDelta.store(hip);s_skirtDirty.store(true);}
  ImGui::TextWrapped(u8"播放和暂停时使用角色原有衣物、头发和尾巴物理。裙摆被撑开时减小补偿；停止后恢复原设置。可随适配预设保存。");
  if(g_mmd.freezeCloth) ImGui::TextWrapped(u8"衣物已冻结：取消“冻结头发 / 衣物”后恢复动态模拟。");
  else if(s_cloth.releasing) ImGui::TextWrapped(u8"正在恢复原有物理设置；未完成前不会接管新角色。");
  else if(s_cloth.failed) ImGui::TextWrapped(u8"原生物理初始化或校验未通过，已停止调整。详细原因见日志。");
  else if(s_cloth.active) {
    int ready=0,suspended=0;
    for(int n=0;n<s_cloth.count;++n) {
      ready+=s_cloth.instances[n].startup.phase==poser_cloth::Phase::Ready;
      suspended+=s_cloth.instances[n].startup.phase==poser_cloth::Phase::Suspended;
    }
    ImGui::Text(u8"组件确认 %d / %d，游戏暂挂 %d",ready,s_cloth.count,suspended);
  } else ImGui::TextDisabled(u8"开始播放后检查原生物理组件");
  ImGui::TextWrapped(u8"实际避让范围由角色原有布料和碰撞体决定。缺少可动衣物骨骼的部位仍可能穿模。脚底位置可用播放器的“高度修正”调整。");
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
    if (g_frameDiagnostics.source == 3) ImGui::TextDisabled(u8"帧来源：角色更新");
    if (g_frameDiagnostics.source == 4) ImGui::TextDisabled(u8"帧来源：游戏镜头更新");
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
      MmdSeekOrStart(m.timeline.seconds - 1. / 30);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(">")) {
      MmdSeekOrStart(m.timeline.seconds + 1. / 30);
    }
    float seconds = float(m.timeline.seconds);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat(u8"##mmdtime", &seconds, 0,
                           float(m.timeline.duration), "%.2f s")) {
      MmdSeekOrStart(seconds);
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
    DrawMmdCloth();
    ImGui::EndDisabled();
    if (MmdOwnsPose()) {
      ImGui::TextDisabled(u8"动作控制中；暂停后可在姿态库保存当前身体姿态");
      if (!m.clip.morphs.empty())
        ImGui::TextDisabled(((m.faceSettings.uniform()||s_faceHierarchy.ready)&&
                            (!m.faceSettings.uses(face_mixing::Driver::Character)||s_characterBinding.ready||m.faceSettings.fallback)&&
                            SMCSectionReady())
                                ? u8"表情系统已就绪"
                                : u8"表情尚未就绪或骨骼不匹配，身体动作继续播放");
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
    if (ImGui::CollapsingHeader(u8"角色表情与强度",ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &settings=m.faceSettings;
      if(m.characterFace) {
        ImGui::Text(u8"当前角色校准：%s",m.characterFace->label.c_str());
        if(s_characterBinding.ready)ImGui::Text(u8"可用表情 %d / %d",s_characterBinding.usableCount,int(m.characterFace->morphs.size()));
        else ImGui::TextWrapped("%s",s_characterBinding.status.empty()?u8"等待当前角色中性脸":s_characterBinding.status.c_str());
      } else ImGui::TextWrapped(u8"当前角色没有 MMD 表情校准。");
      if(m.faceLibraryLoading)ImGui::TextDisabled(u8"正在读取角色校准…");
      if(!m.faceLibraryError.empty())ImGui::TextWrapped(u8"校准读取失败，已保留原数据：%s",m.faceLibraryError.c_str());
      ImGui::BeginDisabled(MmdOwnsPose()||m.faceLibraryLoading);
      if(ImGui::SmallButton(u8"重新读取校准"))MmdReloadCharacterFaces();
      ImGui::EndDisabled();
      if(ImGui::Checkbox(u8"专属校准缺失时使用固定映射",&settings.fallback)){MmdSaveFaceSettings();MmdReport();}
      ImGui::TextWrapped(u8"优先使用当前角色的 MMD 表情；缺失或无法适配的轨道按此开关处理。部分形状只能近似还原。");
      float percent=settings.strength*100;
      if(ImGui::SliderFloat(u8"整体表情强度",&percent,0,200,"%.0f%%"))settings.strength=percent*.01f;
      if(ImGui::IsItemDeactivatedAfterEdit())MmdSaveFaceSettings();
      ImGui::SameLine();
      if(ImGui::SmallButton(u8"复位全部强度")){settings.strength=1;settings.gain.fill(1);MmdSaveFaceSettings();}
      if(ImGui::BeginTable("##faceregions",3,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(u8"部位",0,.65f);ImGui::TableSetupColumn(u8"映射方式",0,1.7f);
        ImGui::TableSetupColumn(u8"独立强度",0,1.f);ImGui::TableHeadersRow();
        for(int r=0;r<face_mixing::RegionCount;++r) {
          ImGui::PushID(r);ImGui::TableNextRow();ImGui::TableNextColumn();
          ImGui::TextUnformatted(face_mixing::Label(r));ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);
          int mode=int(settings.driver[r]);
          if(ImGui::Combo("##source",&mode,u8"角色专属映射\0固定表情映射\0关闭\0")) {
            settings.driver[r]=static_cast<face_mixing::Driver>(mode);MmdSaveFaceSettings();MmdReport();
          }
          ImGui::TableNextColumn();ImGui::SetNextItemWidth(-1);float regionPercent=settings.gain[r]*100;
          if(ImGui::SliderFloat("##strength",&regionPercent,0,200,"%.0f%%"))settings.gain[r]=regionPercent*.01f;
          if(ImGui::IsItemDeactivatedAfterEdit())MmdSaveFaceSettings();ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if(ImGui::SmallButton(u8"全部使用角色专属映射")) {
        settings.driver.fill(face_mixing::Driver::Character);MmdSaveFaceSettings();MmdReport();
      }
      ImGui::TextWrapped(u8"整体和部位强度可以在播放或暂停时调整；眼神方向仍由动作控制。");
    }
    if(ImGui::CollapsingHeader(u8"表情映射")) {
      static int editSource=0;ImGui::Combo(u8"编辑映射表",&editSource,u8"角色专属映射\0固定表情映射\0");
      bool native=editSource==1;
      ImGui::TextWrapped(u8"角色专属映射按角色分别保存。可将动作中的自定义名称绑定到该角色已有表情。");
      ImGui::BeginDisabled(MmdOwnsPose()||(!native&&!m.characterFace));bool changed=false;
      for(auto &kv:m.morphMap) {
        int &slider=native?kv.second.nativeSlider:kv.second.slider;float &gain=native?kv.second.nativeGain:kv.second.gain;
        ImGui::PushID(kv.first.c_str());ImGui::TextUnformatted(kv.first.c_str());ImGui::SetNextItemWidth(185);
        bool valid=slider>=0&&(native?slider<SMCSliderCount():m.characterFace&&slider<int(m.characterFace->morphs.size()));
        const char *label=!valid?u8"未指定 / 不支持":native?SMCSliderLabel(slider):m.characterFace->morphs[slider].name.c_str();
        if(ImGui::BeginCombo("##target",label)) {
          if(ImGui::Selectable(u8"未指定 / 不支持",slider<0)){slider=-1;changed=true;}
          int count=native?SMCSliderCount():m.characterFace?int(m.characterFace->morphs.size()):0;
          for(int i=0;i<count;++i) {
            if(!native&&!m.characterFace->morphs[i].supported)continue;
            const char *name=native?SMCSliderLabel(i):m.characterFace->morphs[i].name.c_str();
            if(ImGui::Selectable(name,slider==i)){slider=i;changed=true;}
          }
          ImGui::EndCombo();
        }
        ImGui::SameLine();ImGui::SetNextItemWidth(130);changed|=ImGui::SliderFloat("##gain",&gain,0,2,"%.2f");
        if(!native&&valid&&m.characterFace->morphs[slider].residual>.1f)
          ImGui::TextDisabled(u8"此表情部分形状为近似");
        ImGui::PopID();
      }
      if(changed){MmdSaveMappings(native);MmdReport();}ImGui::EndDisabled();
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
