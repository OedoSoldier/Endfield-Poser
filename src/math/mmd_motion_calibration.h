#pragma once
#include "math/mmd_amplitude.h"
#include "nlohmann/json.hpp"
#include <stdexcept>

namespace mmd {
// Manual motion sizing, independent of the Avatar's bind calibration.
// Ratios change a virtual endpoint; the solver retains actual target lengths.
struct LimbCalibration {
  float upper = 1, lower = 1;
  Vec3 offset; // Fractions of target limb length, anatomical left/up/forward.
  bool identity() const {
    return upper == 1 && lower == 1 && offset.x == 0 && offset.y == 0 && offset.z == 0;
  }
};
struct MotionCalibration {
  bool enabled = true;
  Vec3 travel{1, 1, 1};
  std::array<LimbCalibration, 4> limbs; // Left arm, right arm, left leg, right leg.
  std::array<float, 55> joints;
  std::array<bool, 55> jointOffsetEnabled{};
  std::array<Vec3, 55> jointOffsets{}; // Degrees in the animated joint's local axes.
  // One bidirectional rule per pair, stored at the lower role index.
  // false copies the signed angle, true negates it. Unpaired entries stay zero.
  std::array<std::array<bool, 3>, 55> jointLinkMirror{};
  MotionCalibration() { joints.fill(1); }
  float factor(int role) const {
    return enabled && role >= 0 && role < 55 && MotionPartForRole(role) >= 0
        ? MotionAmplitude::safe(joints[role]) : 1.f;
  }
  Vec3 offset(int role) const {
    if (!enabled || role < 0 || role >= 55 || MotionPartForRole(role) < 0 || !jointOffsetEnabled[role])
      return {};
    auto safe = [](float v) {return std::isfinite(v) ? (std::max)(-180.f, (std::min)(180.f, v)) : 0.f;};
    const auto v = jointOffsets[role];
    return {safe(v.x), safe(v.y), safe(v.z)};
  }
};
inline int MotionMirrorRole(int role) {
  if (role >= 24 && role <= 53) return role < 39 ? role + 15 : role - 15;
  for (int left : {1, 3, 5, 11, 13, 15, 17, 19}) {
    if (role == left) return left + 1;
    if (role == left + 1) return left;
  }
  return role;
}
inline void MirrorLimbCalibration(MotionCalibration &s, int index) {
  s.limbs[index ^ 1] = s.limbs[index];
  s.limbs[index ^ 1].offset.x *= -1;
}
inline void MirrorJointCalibration(MotionCalibration &s, int role) {
  if (role < 0 || role >= 55) return;
  const int mirror = MotionMirrorRole(role);
  if (mirror == role) return;
  const auto &axes = s.jointLinkMirror[(std::min)(role, mirror)];
  const auto v = s.jointOffsets[role];
  auto angle = [](float value, bool reverse) {return reverse && value != 0 ? -value : value;};
  s.joints[mirror] = s.joints[role];
  s.jointOffsetEnabled[mirror] = s.jointOffsetEnabled[role];
  s.jointOffsets[mirror] = {angle(v.x, axes[0]), angle(v.y, axes[1]), angle(v.z, axes[2])};
}
inline float MotionValue(float x, float low, float high, float fallback) {
  return std::isfinite(x) ? (std::max)(low, (std::min)(high, x)) : fallback;
}
inline Vec3 CalibratedTravel(Vec3 value, const MotionCalibration &s) {
  if (!s.enabled) return value;
  return {value.x * MotionValue(s.travel.x, 0, 2, 1),
          value.y * MotionValue(s.travel.y, 0, 2, 1),
          value.z * MotionValue(s.travel.z, 0, 2, 1)};
}
inline Vec3 CalibratedLimbGoal(Vec3 a, Vec3 b, Vec3 c, Quat body,
                              const LimbCalibration &s) {
  Vec3 offset{MotionValue(s.offset.x, -.5f, .5f, 0),
              MotionValue(s.offset.y, -.5f, .5f, 0),
              MotionValue(s.offset.z, -.5f, .5f, 0)};
  return a + (b - a) * MotionValue(s.upper, .5f, 1.5f, 1) +
      (c - b) * MotionValue(s.lower, .5f, 1.5f, 1) +
      body * offset * (Len(b - a) + Len(c - b));
}
inline nlohmann::json MotionCalibrationJson(const MotionCalibration &s) {
  nlohmann::json j = {{"version", 3}, {"enabled", s.enabled},
      {"travel", {s.travel.x, s.travel.y, s.travel.z}}, {"joints", s.joints},
      {"joint_link_mirror", s.jointLinkMirror}};
  j["limbs"] = nlohmann::json::array();
  for (const auto &l : s.limbs)
    j["limbs"].push_back({{"upper", l.upper}, {"lower", l.lower},
        {"offset", {l.offset.x, l.offset.y, l.offset.z}}});
  j["joint_offsets"] = nlohmann::json::array();
  for (int i = 0; i < 55; ++i) {
    const auto v = s.jointOffsets[i];
    j["joint_offsets"].push_back({{"enabled", s.jointOffsetEnabled[i]}, {"degrees", {v.x, v.y, v.z}}});
  }
  return j;
}
inline MotionCalibration ReadMotionCalibration(const nlohmann::json &preset) {
  MotionCalibration s;
  if (!preset.contains("motion_calibration")) return s; // Older rig presets.
  const auto &j = preset.at("motion_calibration");
  if (!j.is_object() || !j.contains("version") || !j.at("version").is_number_integer() ||
      (j.at("version") != 1 && j.at("version") != 2 && j.at("version") != 3))
    throw std::runtime_error(u8"不支持的动作校准版本");
  auto number = [](const nlohmann::json &v, float lo, float hi) {
    if (!v.is_number()) throw std::runtime_error(u8"动作校准数值必须为数字");
    float f = v.get<float>();
    if (!std::isfinite(f) || f < lo || f > hi)
      throw std::runtime_error(u8"动作校准数值超出范围");
    return f;
  };
  auto vector = [&](const nlohmann::json &v, float lo, float hi) {
    if (!v.is_array() || v.size() != 3)
      throw std::runtime_error(u8"动作校准向量无效");
    return Vec3{number(v[0], lo, hi), number(v[1], lo, hi), number(v[2], lo, hi)};
  };
  s.enabled = j.at("enabled").get<bool>();
  s.travel = vector(j.at("travel"), 0, 2);
  const auto &limbs = j.at("limbs"), &joints = j.at("joints");
  if (!limbs.is_array() || limbs.size() != 4 || !joints.is_array() || joints.size() != 55)
    throw std::runtime_error(u8"动作校准部位数量无效");
  for (size_t i = 0; i < 4; ++i) {
    s.limbs[i].upper = number(limbs[i].at("upper"), .5f, 1.5f);
    s.limbs[i].lower = number(limbs[i].at("lower"), .5f, 1.5f);
    s.limbs[i].offset = vector(limbs[i].at("offset"), -.5f, .5f);
  }
  for (int i = 0; i < 55; ++i) {
    s.joints[i] = number(joints[i], 0, 2);
    if (MotionPartForRole(i) < 0 && s.joints[i] != 1)
      throw std::runtime_error(u8"眼睛与下颌请使用表情面板调整");
  }
  if (j.at("version") >= 2) {
    const auto &axes = j.at("joint_offsets");
    if (!axes.is_array() || axes.size() != 55)
      throw std::runtime_error(u8"动作校准三轴关节数量无效");
    for (int i = 0; i < 55; ++i) {
      s.jointOffsetEnabled[i] = axes[i].at("enabled").get<bool>();
      s.jointOffsets[i] = vector(axes[i].at("degrees"), -180, 180);
      const auto v = s.jointOffsets[i];
      if (MotionPartForRole(i) < 0 && (s.jointOffsetEnabled[i] || v.x != 0 || v.y != 0 || v.z != 0))
        throw std::runtime_error(u8"眼睛与下颌请使用表情面板调整");
    }
  }
  if (j.at("version") == 3) {
    const auto &links = j.at("joint_link_mirror");
    if (!links.is_array() || links.size() != 55)
      throw std::runtime_error(u8"动作校准联动规则数量无效");
    for (int role = 0; role < 55; ++role) {
      const auto &axes = links[role];
      if (!axes.is_array() || axes.size() != 3)
        throw std::runtime_error(u8"动作校准联动轴数量无效");
      for (int axis = 0; axis < 3; ++axis) {
        const bool reverse = axes[axis].get<bool>();
        if (reverse && MotionMirrorRole(role) <= role)
          throw std::runtime_error(u8"联动规则应保存在对应的左侧关节");
        s.jointLinkMirror[role][axis] = reverse;
      }
    }
  }
  return s;
}
} // namespace mmd
