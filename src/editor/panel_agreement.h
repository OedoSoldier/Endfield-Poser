#pragma once
// Agreement structure and presentation reference: Sasye/EIEM src/gui.h
// https://github.com/Sasye/EIEM (AGPL-3.0). Text adapted for Endfield Poser.
#include "imgui.h"
#include "core/user_agreement.h"

static bool g_showUserAgreement = false; // GUI thread only; reopen from main panel.

static bool DrawUserAgreement() {
  const bool required = !poser_agreement::Allowed();
  if (!required && !g_showUserAgreement) return false;
  const char *title = u8"用户协议与免责声明###PoserUserAgreement";
  if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(display.x * .5f, display.y * .5f),
                          ImGuiCond_Always, ImVec2(.5f, .5f));
  ImGui::SetNextWindowSize(ImVec2((std::min)(680.f, (std::max)(280.f, display.x - 24.f)),
                                 (std::min)(680.f, (std::max)(300.f, display.y - 24.f))));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(.075f, .085f, .11f, .98f));
  static DWORD saveError = ERROR_SUCCESS;
  if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
    const ImVec4 gold(1.f, .85f, .25f, 1.f);
    ImGui::TextColored(gold, "Endfield Poser");
    ImGui::SameLine();
    ImGui::TextDisabled(u8"协议版本 %d", poser_agreement::kRevision);
    ImGui::TextWrapped(required ? u8"首次使用前，请阅读以下说明。点击同意后启用插件，并在本机记住本版协议的确认。"
                                : u8"你已确认本版协议。这里可以随时重新查阅。 ");
    ImGui::Separator();
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.f +
                         ImGui::GetTextLineHeightWithSpacing() * (saveError ? 4.f : 2.f);
    ImGui::BeginChild("##agreement_text", ImVec2(0, (std::max)(40.f,
                      ImGui::GetContentRegionAvail().y - footer)), true);
    ImGui::TextColored(gold, u8"1. 开源许可与用户权利");
    ImGui::TextWrapped(u8"本插件按 AGPL-3.0 许可开放源码，可在遵守许可的前提下使用、修改和分发。完整条款见随附 LICENSE；本提示不限制许可证授予的权利。插件与鹰角网络无隶属、合作或授权关系。 ");
    ImGui::Spacing();
    ImGui::TextColored(gold, u8"2. 免费获取与反欺诈");
    ImGui::TextWrapped(u8"本插件可从 GitHub 免费获取。请勿冒充作者或官方、隐瞒免费来源或作出虚假收费宣传；转发时请保留来源及许可信息。付费获取并不代表获得游戏官方授权或安全保证。 ");
    ImGui::TextWrapped("https://github.com/OedoSoldier/Endfield-Poser");
    ImGui::Spacing();
    ImGui::TextColored(gold, u8"3. 内容与使用约定");
    ImGui::TextWrapped(u8"插件不包含游戏美术资产。游戏中的动画、场景、模型等资产属于各自权利人，不因本工具开源而适用 AGPL。导入的 MMD 动作、镜头、模型参考和音乐也须遵守原作者的授权及使用条件。 ");
    ImGui::TextWrapped(u8"请将本工具用于学习、技术研究和摄影交流，尊重游戏及素材作者的规则；不要用于影响其他玩家、绕过付费内容、侵权或违法内容。请勿利用游戏资产制作或传播涉及色情、暴力、政治敏感等不合适的动作与动画。 ");
    ImGui::Spacing();
    ImGui::TextColored(gold, u8"4. 风险与免责声明");
    ImGui::TextWrapped(u8"本工具会注入并修改游戏客户端进程，可能违反游戏服务条款，存在账号受限或封禁、崩溃及数据损坏等风险。游戏更新或与其他模组共存也可能使功能失效。 ");
    ImGui::TextWrapped(u8"本工具按现状提供，不保证兼容性、稳定性或账号安全；在适用法律允许的范围内，作者及贡献者不对使用造成的损失承担责任。请自行评估并承担使用风险，备份自己的配置和姿态。 ");
    ImGui::Spacing();
    ImGui::TextWrapped(u8"不接受时可选择“暂不使用”。插件功能保持未启用，游戏继续运行；之后可重新打开面板阅读，或退出游戏后使用安装向导卸载。 ");
    ImGui::TextDisabled(u8"参考：Sasye/EIEM 用户协议（AGPL-3.0）");
    ImGui::EndChild();
    ImGui::TextWrapped(u8"若无法点击，请按住 Alt 呼出游戏光标。确认记录仅保存在本机。 ");
    if (saveError)
      ImGui::TextWrapped(u8"确认记录保存失败（Windows 错误 %lu），请检查 plugin 目录写入权限后重试。", saveError);
    if (required) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.78f, .61f, .08f, 1));
      if (ImGui::Button(u8"我已阅读并同意", ImVec2(0, 32))) {
        if (poser_agreement::state.accept(PoserFilePath(poser_agreement::kFileName), saveError)) {
          // Do not replay controls pressed while the agreement was on screen.
          InterlockedExchange(&g_hotkeyFreezeReq, 0);
          InterlockedExchange(&g_mmdHotkeyRequests, 0);
          InterlockedExchange(&g_leftClickReq, 0);
          g_showUserAgreement = false;
          ImGui::CloseCurrentPopup();
          Log("[AGREEMENT] revision %d accepted and saved", poser_agreement::kRevision);
        }
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button(u8"暂不使用", ImVec2(0, 32))) {
        g_guiVisible = false;
        saveError = ERROR_SUCCESS;
        ImGui::CloseCurrentPopup();
        Log("[AGREEMENT] deferred; plugin controls remain unavailable");
      }
    } else if (ImGui::Button(u8"关闭", ImVec2(140, 32))) {
      g_showUserAgreement = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  return true;
}
