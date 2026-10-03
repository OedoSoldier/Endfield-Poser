#pragma once
// Bone names adapted from Sp1cHless/Arknights-Endfield-Plugin-Secondary-bodyphysics.
// Commit 8f20b05f52627e9c49c6dc0e3e90679deba1d9b5, GPL-3.0.
// Selection data only; the 3D inertial solver and Poser lifecycle are independent.
// See integrations/sbm/NOTICE.md and LICENSE.GPL-3.0.
namespace poser_secondary {
struct BonePair {const char *model,*right,*left;};
static constexpr BonePair profiles[]={
  {"chr_0014_aurora","R_breast_01_jnt","L_breast_01_jnt"},
  {"chr_0017_yvonne","breast_R_01_jnt","breast_L_01_jnt"},
  {"chr_0031_mifu","R_breast_01_jnt","L_breast_01_jnt"},
  {"chr_0030_zhuangfy","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0016_laevat","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0032_lizhiyan","R_breast_01_jnt","L_breast_01_jnt"},
  {"chr_0035_liino","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0013_aglina","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0026_lastrite","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0009_azrila","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0012_avywen","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0011_seraph","breast_base_R_a_01_jnt_ctrl","breast_base_L_a_01_jnt_ctrl"},
  {"chr_0007_ikut","xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},
  {"chr_0021_whiten","breast_R_01_jnt","breast_L_01_jnt"},
  {"chr_0019_karin","breast_R_01_jnt","breast_L_01_jnt"},
  {"chr_0022_bounda","breast_R_01_jnt","breast_L_01_jnt"},
  {"chr_0034_typhoea","breast_base_R_a_01_jnt","breast_base_L_a_01_jnt"},
};
static constexpr const char *fallbackPairs[][2]={
  {"breast_R_01_jnt","breast_L_01_jnt"},{"R_breast_01_jnt","L_breast_01_jnt"},
  {"xiong_R_0_skin_jnt","xiong_L_0_skin_jnt"},{"breast_R_01","breast_L_01"},
  {"R_breast_01","L_breast_01"},{"xiong_R_0_skin","xiong_L_0_skin"}
};
}
