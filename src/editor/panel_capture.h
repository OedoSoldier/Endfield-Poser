#pragma once
#include "game/transparent_capture.h"
#include <shellapi.h>
#pragma comment(lib,"shell32.lib")

static void DrawTransparentCapture() {
  if(!ImGui::CollapsingHeader(u8"人物透明截图"))return;
  ImGui::TextWrapped(u8"保留完整场景中的人物颜色、光照与反光，再提取人物透明遮罩，并清理边缘底色。截图时画面可能短暂闪烁。");
  static int aaMode=0;
  ImGui::BeginDisabled(poser_capture::busy||!g_charAnimator);
  ImGui::Combo(u8"透明边缘抗锯齿",&aaMode,u8"跟随游戏（DLSS / TAA 等）\0FXAA 兼容模式\0");
  if(ImGui::Button(u8"保存人物透明 PNG"))poser_capture::Request(FrameNow(),aaMode==0);
  ImGui::EndDisabled();
  if(poser_capture::busy) {
    ImGui::SameLine();if(ImGui::Button(u8"取消截图"))poser_capture::Cancel();
  }
  if(ImGui::Button(u8"打开截图目录")) {
    auto directory=poser_capture::Directory();
    std::error_code error;std::filesystem::create_directories(directory,error);
    if(!error)ShellExecuteW(nullptr,L"open",directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    else poser_capture::Status(u8"无法打开截图目录，请检查目录权限");
  }
  ImGui::TextWrapped("%s",poser_capture::Status().c_str());
  ImGui::TextWrapped(u8"请先冻结角色、暂停动作，并保持镜头不动，确保人物没有被前景挡住。跟随游戏会等待多帧；若透明通道不兼容，可切换 FXAA，但边缘较粗。");
  ImGui::TextDisabled(u8"输出：plugin/screenshots");
}
