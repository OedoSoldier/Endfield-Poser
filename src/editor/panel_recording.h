#pragma once
#include "game/mmd_recording.h"
#include <shellapi.h>

static void DrawMmdRecordingControls() {
  using namespace poser_recording;
  static mmd_recording::Options settings;
  static int target=0,fps=1,warmup=2,aa=0;
  ImGui::TextWrapped(u8"回到动作首帧，等待稳定后，按固定帧率逐帧输出图像序列。分层模式同时保存完整画面、背景和透明人物，后台保存与录制并行。");
  ImGui::BeginDisabled(busy||poser_capture::busy);
  ImGui::Combo(u8"录制对象",&target,u8"自动（当前播放器）\0单人播放器\0多人播放器\0");
  ImGui::Combo(u8"输出帧率",&fps,"30 FPS\0" "60 FPS\0");
  ImGui::Combo(u8"首帧稳定时间",&warmup,u8"0 秒（就绪后立即开始）\0" u8"1 秒\0" u8"2 秒\0");
  ImGui::Checkbox(u8"快速无损 PNG",&settings.fastPng);
  if(settings.fastPng)ImGui::TextWrapped(u8"人物 PNG 快速无损压缩。完整画面或选用 PNG 的背景保存为 RGB PNG，4K 每帧约 24 MB。");
  ImGui::Checkbox(u8"人物分层保存（含背景与完整画面）",&settings.layered);
  if(settings.layered) {
    ImGui::TextWrapped(u8"完整成片保存到 full/，与背景和人物层使用相同帧号，始终为 PNG；会增加渲染时间和磁盘占用。");
    ImGui::Checkbox(u8"录制时遮住分层闪烁（显示进度）",&settings.hideIntermediate);
    if(settings.hideIntermediate)ImGui::TextWrapped(u8"录制时显示静态进度背景，Poser 面板仍可操作；完成或停止后恢复游戏画面。仅影响屏幕预览，不进入导出图片。");
    ImGui::Checkbox(u8"背景保存为 JPEG（更小，有损）",&settings.jpegBackground);
    if(settings.jpegBackground)ImGui::SliderInt(u8"背景 JPEG 质量",&settings.jpegQuality,60,100,"%d",ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox(u8"多角色分别保存到不同层",&settings.separate);
    ImGui::Combo(u8"分层抗锯齿",&aa,u8"跟随游戏（DLSS / TAA 等）\0FXAA 兼容模式\0");
    ImGui::TextWrapped(u8"单人或合并人物层会在条件允许时复用完整画面的颜色，减少重复采集。分层仍需多次渲染；独立人物层保留各自完整轮廓，合成时需自行处理角色遮挡顺序。");
  }
  if(ImGui::Button(u8"开始录制序列")) {
    settings.squad=target==2||(target==0&&!g_mmd.session.active&&(g_squad.active||g_squad.hotkeys));
    settings.fps=fps?60:30;settings.warmup=warmup;settings.followAA=aa==0;
    Request(settings);
  }
  ImGui::EndDisabled();
  if(busy) {
    ImGui::ProgressBar(total?float(completed)/total:0,ImVec2(-1,0));
    if(ImGui::Button(u8"停止录制（保留已完成帧）"))Cancel();
  }
  if(ImGui::Button(u8"打开录制目录")) {
    auto directory=lastDirectory.empty()?std::filesystem::path(Directory()):std::filesystem::u8path(lastDirectory);
    std::error_code error;std::filesystem::create_directories(directory,error);
    if(!error)ShellExecuteW(nullptr,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
  }
  ImGui::TextWrapped("%s",status.c_str());
  ImGui::TextDisabled(u8"输出：plugin/recordings/每次录制独立目录");
  ImGui::TextWrapped(u8"输出尺寸跟随游戏相机。按原速录制一次，忽略循环／倍速；录完暂停在末帧。图像序列不含声音，录制信息保存各层格式、帧率与配乐路径、偏移，便于后期同步。");
}
static void DrawMmdRecording() {
  if(ImGui::CollapsingHeader(u8"MMD 序列录制"))DrawMmdRecordingControls();
}
static void DrawMmdRecordingProgress() {
  ImGui::Begin(u8"MMD 序列录制",nullptr,ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::TextWrapped(u8"正在录制，编辑面板暂时锁定。停止后自动恢复游戏时间和画面。");
  DrawMmdRecordingControls();ImGui::End();
}
