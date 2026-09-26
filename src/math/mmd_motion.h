#pragma once
// VMD reader and time-domain sampling. No game or Windows dependencies.
#include "math/quat_math.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace mmd {
using Decoder = std::function<std::string(const std::string &, unsigned)>;
inline float Clamp(float v, float a, float b) {
  return (std::max)(a, (std::min)(b, v));
}
struct Reader {
  const std::vector<uint8_t> &bytes;
  size_t offset = 0;
  explicit Reader(const std::vector<uint8_t> &b) : bytes(b) {}
  size_t remaining() const { return bytes.size() - offset; }
  void require(size_t n) const {
    if (n > remaining())
      throw std::runtime_error("Truncated MMD file");
  }
  void skip(size_t n) {
    require(n);
    offset += n;
  }
  template <class T> T read() {
    require(sizeof(T));
    T v;
    std::memcpy(&v, bytes.data() + offset, sizeof(T));
    offset += sizeof(T);
    return v;
  }
  uint32_t count(size_t minSize, uint32_t limit = 2000000) {
    uint32_t n = read<uint32_t>();
    if (n > limit || (minSize && n > remaining() / minSize))
      throw std::runtime_error("Invalid MMD record count");
    return n;
  }
  float number() {
    float f = read<float>();
    if (!std::isfinite(f))
      throw std::runtime_error("Non-finite MMD value");
    return f;
  }
  Vec3 vec() {
    float x = number(), y = number(), z = number();
    if (std::fabs(x) > 1e6f || std::fabs(y) > 1e6f || std::fabs(z) > 1e6f)
      throw std::runtime_error("MMD vector exceeds supported range");
    return {x, y, z};
  }
  Quat quat() {
    float x = number(), y = number(), z = number(), w = number();
    double length = std::sqrt(double(x) * x + double(y) * y + double(z) * z +
                              double(w) * w);
    if (length < 1e-12)
      return {};
    return {float(x / length), float(y / length), float(z / length),
            float(w / length)};
  }
  std::string raw(size_t n) {
    require(n);
    std::string s(reinterpret_cast<const char *>(bytes.data() + offset), n);
    offset += n;
    return s;
  }
  std::string fixed(size_t n, const Decoder &decode) {
    std::string s = raw(n);
    auto z = s.find('\0');
    if (z != s.npos)
      s.resize(z);
    return decode(s, 932);
  }
};
// Normalize common ASCII/full-width variants without changing Japanese names.
inline std::string Name(std::string s) {
  const char *from[] = {u8"０", u8"１", u8"２", u8"３", u8"４", u8"５",
                        u8"６", u8"７", u8"８", u8"９", u8"Ｉ", u8"Ｋ"};
  const char *to[] = {"0", "1", "2", "3", "4", "5",
                      "6", "7", "8", "9", "I", "K"};
  for (size_t i = 0; i < 12; i++) {
    size_t p = 0;
    while ((p = s.find(from[i], p)) != s.npos) {
      s.replace(p, std::strlen(from[i]), to[i]);
      p++;
    }
  }
  return s;
}
struct Curve {
  float x1 = 0, y1 = 0, x2 = 1, y2 = 1;
};
inline float Bezier(const Curve &c, float x) {
  x = Clamp(x, 0, 1);
  if (x == 0 || x == 1)
    return x;
  auto b = [](float t, float a, float z) {
    float u = 1 - t;
    return 3 * u * u * t * a + 3 * u * t * t * z + t * t * t;
  };
  float lo = 0, hi = 1, t = x;
  for (int i = 0; i < 22; i++) {
    float bx = b(t, c.x1, c.x2);
    if (std::fabs(bx - x) < 1e-6f)
      break;
    if (bx < x)
      lo = t;
    else
      hi = t;
    t = (lo + hi) * 0.5f;
  }
  return b(t, c.y1, c.y2);
}
struct BoneKey {
  uint32_t frame = 0;
  Vec3 position;
  Quat rotation;
  std::array<Curve, 4> curves;
};
struct MorphKey {
  uint32_t frame = 0;
  float weight = 0;
};
struct IkKey {
  uint32_t frame = 0;
  bool enabled = true;
};
struct CameraKey {
  uint32_t frame = 0;
  float distance = -45, fov = 30;
  Vec3 target, rotation; // VMD Euler radians, not a bone quaternion
  std::array<Curve, 6> curves; // target XYZ, rotation, distance, FOV
  bool perspective = true;
};
struct LocalPose {
  Vec3 position;
  Quat rotation;
};
struct MotionClip {
  std::string model;
  std::map<std::string, std::vector<BoneKey>> bones;
  std::map<std::string, std::vector<MorphKey>> morphs;
  std::map<std::string, std::vector<IkKey>> ik;
  std::vector<CameraKey> cameras;
  uint32_t lastFrame = 0;
  size_t boneKeys = 0, morphKeys = 0;
  std::vector<std::string> warnings;
  double duration() const { return lastFrame / 30.0; }
  bool empty() const { return bones.empty() && morphs.empty() && cameras.empty(); }
};
template <class T> inline void SortKeys(std::vector<T> &v) {
  std::stable_sort(v.begin(), v.end(),
                   [](const T &a, const T &b) { return a.frame < b.frame; });
  size_t n = 0;
  for (const auto &k : v) {
    if (n && v[n - 1].frame == k.frame)
      v[n - 1] = k;
    else
      v[n++] = k;
  }
  v.resize(n);
}
inline void Recount(MotionClip &c) {
  c.lastFrame = 0;
  c.boneKeys = 0;
  c.morphKeys = 0;
  for (auto &kv : c.bones) {
    SortKeys(kv.second);
    if (!kv.second.empty())
      c.lastFrame = (std::max)(c.lastFrame, kv.second.back().frame);
    c.boneKeys += kv.second.size();
  }
  for (auto &kv : c.morphs) {
    SortKeys(kv.second);
    if (!kv.second.empty())
      c.lastFrame = (std::max)(c.lastFrame, kv.second.back().frame);
    c.morphKeys += kv.second.size();
  }
  for (auto &kv : c.ik)
    SortKeys(kv.second);
  SortKeys(c.cameras);
  if (!c.cameras.empty())
    c.lastFrame = (std::max)(c.lastFrame, c.cameras.back().frame);
}
inline MotionClip ReadVmd(const std::vector<uint8_t> &bytes,
                          const Decoder &decode) {
  Reader r(bytes);
  MotionClip c;
  auto signature = r.raw(30);
  bool old = signature.rfind("Vocaloid Motion Data file", 0) == 0;
  if (signature.compare(0, 25, "Vocaloid Motion Data 0002") != 0 && !old)
    throw std::runtime_error("Not a VMD 0002/file motion");
  c.model = r.fixed(old ? 10 : 20, decode);
  auto n = r.count(111);
  for (uint32_t i = 0; i < n; i++) {
    auto name = Name(r.fixed(15, decode));
    BoneKey k;
    k.frame = r.read<uint32_t>();
    k.position = r.vec();
    k.rotation = r.quat();
    auto curve = r.raw(64);
    for (int j = 0; j < 4; j++)
      k.curves[j] = {Clamp(uint8_t(curve[j]) / 127.f, 0, 1),
                     Clamp(uint8_t(curve[j + 4]) / 127.f, 0, 1),
                     Clamp(uint8_t(curve[j + 8]) / 127.f, 0, 1),
                     Clamp(uint8_t(curve[j + 12]) / 127.f, 0, 1)};
    if (!name.empty())
      c.bones[name].push_back(k);
  }
  if (r.remaining()) {
    n = r.count(23);
    for (uint32_t i = 0; i < n; i++) {
      auto name = Name(r.fixed(15, decode));
      MorphKey k;
      k.frame = r.read<uint32_t>();
      k.weight = Clamp(r.number(), 0, 1);
      if (!name.empty())
        c.morphs[name].push_back(k);
    }
  }
  if (r.remaining()) {
    n = r.count(61);
    c.cameras.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
      CameraKey k;
      k.frame = r.read<uint32_t>();
      k.distance = r.number();
      if (std::fabs(k.distance) > 1e6f)
        throw std::runtime_error("Camera distance exceeds supported range");
      k.target = r.vec();
      k.rotation = r.vec();
      auto curve = r.raw(24);
      for (int j = 0; j < 6; ++j) {
        // Camera bytes are x1,x2,y1,y2, unlike bone interpolation.
        auto unit = [&](int at) { return Clamp(uint8_t(curve[j * 4 + at]) / 127.f, 0, 1); };
        k.curves[j] = {unit(0), unit(2), unit(1), unit(3)};
      }
      uint32_t angle = r.read<uint32_t>();
      auto projection = r.read<uint8_t>();
      if (angle < 1 || angle >= 180 || projection > 1)
        throw std::runtime_error("Invalid VMD camera projection/FOV");
      k.fov = float(angle);
      k.perspective = projection == 0;
      c.cameras.push_back(k);
    }
  }
  for (auto section :
       {std::pair<size_t, const char *>(28, "Light tracks ignored"),
        {9, "Shadow tracks ignored"}}) {
    if (!r.remaining())
      break;
    n = r.count(section.first);
    if (n)
      c.warnings.push_back(section.second);
    r.skip(n * section.first);
  }
  if (r.remaining()) {
    n = r.count(9, 1000000);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t f = r.read<uint32_t>();
      r.read<uint8_t>();
      auto m = r.count(21, 10000);
      for (uint32_t j = 0; j < m; j++) {
        auto name = Name(r.fixed(20, decode));
        bool on = r.read<uint8_t>() != 0;
        c.ik[name].push_back({f, on});
      }
    }
  }
  if (r.remaining())
    c.warnings.push_back("Trailing VMD extension ignored");
  Recount(c);
  return c;
}
template <class K>
inline size_t Upper(const std::vector<K> &keys, double frame) {
  return size_t(
      std::upper_bound(keys.begin(), keys.end(), frame,
                       [](double f, const K &k) { return f < k.frame; }) -
      keys.begin());
}
inline LocalPose SampleBone(const std::vector<BoneKey> &keys, double frame) {
  if (keys.empty())
    return {};
  auto n = Upper(keys, frame);
  if (n == 0)
    return {keys.front().position, keys.front().rotation};
  if (n == keys.size())
    return {keys.back().position, keys.back().rotation};
  const auto &a = keys[n - 1];
  const auto &b = keys[n];
  float t = float((frame - a.frame) / double(b.frame - a.frame));
  return {
      {a.position.x + (b.position.x - a.position.x) * Bezier(b.curves[0], t),
       a.position.y + (b.position.y - a.position.y) * Bezier(b.curves[1], t),
       a.position.z + (b.position.z - a.position.z) * Bezier(b.curves[2], t)},
      Quat::Slerp(a.rotation, b.rotation, Bezier(b.curves[3], t))};
}
inline float SampleMorph(const std::vector<MorphKey> &keys, double frame) {
  if (keys.empty())
    return 0;
  size_t n = Upper(keys, frame);
  if (!n)
    return keys.front().weight;
  if (n == keys.size())
    return keys.back().weight;
  const auto &a = keys[n - 1];
  const auto &b = keys[n];
  return a.weight + (b.weight - a.weight) *
                        float((frame - a.frame) / double(b.frame - a.frame));
}
enum class IkMode { FollowMotion, ForceOn, ForceOff };
inline bool SampleIk(const MotionClip &clip, const std::string &name,
                     double frame, IkMode mode = IkMode::FollowMotion) {
  if (mode != IkMode::FollowMotion)
    return mode == IkMode::ForceOn;
  auto it = clip.ik.find(name);
  if (it == clip.ik.end())
    return true;
  size_t n = Upper(it->second, frame);
  return n ? it->second[n - 1].enabled : true;
}
inline bool EyeBone(const std::string &n) {
  return n == u8"両目" || n == u8"左目" || n == u8"右目";
}
inline void AppendFace(MotionClip &to, const MotionClip &from) {
  for (const auto &kv : from.morphs)
    to.morphs[kv.first] = kv.second;
  for (const auto &kv : from.bones)
    if (EyeBone(kv.first))
      to.bones[kv.first] = kv.second;
  Recount(to);
}
enum class PlayState { Stopped, Playing, Paused };
struct Timeline {
  PlayState state = PlayState::Stopped;
  double seconds = 0, duration = 0, speed = 1, lastNow = 0;
  bool loop = false;
  void play(double now) {
    if (seconds >= duration && duration > 0)
      seconds = 0;
    lastNow = now;
    state = PlayState::Playing;
  }
  void tick(double now) {
    double dt = (std::max)(0.0, now - lastNow);
    lastNow = now;
    if (state != PlayState::Playing)
      return;
    seconds += dt * speed;
    if (seconds >= duration) {
      if (loop && duration > 0)
        seconds = std::fmod(seconds, duration);
      else {
        seconds = duration;
        state = PlayState::Paused;
      }
    }
  }
  void pause(double now) {
    tick(now);
    state = PlayState::Paused;
  }
  void seek(double t, double now) {
    seconds = (std::max)(0.0, (std::min)(duration, t));
    lastNow = now;
    if (state != PlayState::Stopped)
      state = PlayState::Paused;
  }
  void stop() {
    state = PlayState::Stopped;
    seconds = 0;
  }
};
// UI transport requests are consumed on the game thread before cloth capture.
// Preserve a seek/pause that arrives while playback is waiting for that thread.
struct DeferredStart {
  bool active=false, paused=false;
  double seconds=NAN;
  void play() {active=true;paused=false;seconds=NAN;}
  void seek(double value) {active=true;paused=true;seconds=value;}
  void pause() {if(active) paused=true;}
  void cancel() {*this={};}
  void apply(Timeline &timeline, double now) const {
    if (!active) return;
    if (std::isfinite(seconds)) timeline.seek(seconds,now);
    if (paused) timeline.pause(now);
    else timeline.play(now);
  }
};
} // namespace mmd
