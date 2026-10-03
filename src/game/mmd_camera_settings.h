#pragma once
#include "math/mmd_camera_profile.h"
#include "game/mmd_io.h"

namespace mmd {
inline nlohmann::json WriteFixedCameraSettings(const FixedCameraSettings &s) {
  return {{"version",1},{"distance",s.distance},{"focal_length",s.focalLength},
    {"yaw",s.yaw},{"pitch",s.pitch},{"offset",{s.offset.x,s.offset.y,s.offset.z}},
    {"ignore_jump",s.ignoreJump},{"smooth_time",s.smoothTime}};
}
inline FixedCameraSettings ReadFixedCameraSettings(const nlohmann::json &j) {
  if(!j.is_object()||j.value("version",0)!=1)throw std::runtime_error("Invalid fixed camera settings");
  FixedCameraSettings s;s.distance=j.value("distance",4.f);s.focalLength=j.value("focal_length",35.f);
  s.yaw=j.value("yaw",0.f);s.pitch=j.value("pitch",0.f);
  s.ignoreJump=j.value("ignore_jump",false);
  s.smoothTime=j.value("smooth_time",.15f);
  auto xyz=j.value("offset",std::vector<float>{0,1,0});
  if(xyz.size()!=3)throw std::runtime_error("Invalid fixed camera offset");
  s.offset={xyz[0],xyz[1],xyz[2]};
  if(!ValidFixedCamera(s))throw std::runtime_error("Invalid fixed camera range");
  return s;
}
struct FixedCameraStore {
  bool loaded=false,readFailed=false;std::string status;
  void load(const std::filesystem::path &path,FixedCameraSettings &s) {
    if(loaded)return;loaded=true;
    try {
      if(!std::filesystem::exists(path))return;
      if(std::filesystem::file_size(path)>16384)throw std::runtime_error("Fixed camera settings too large");
      std::ifstream f(path);nlohmann::json j;f>>j;s=ReadFixedCameraSettings(j);
    }catch(const std::exception &e){readFailed=true;status=std::string(u8"跟踪设置读取失败，原文件保留：")+e.what();}
  }
  bool save(const std::filesystem::path &path,const FixedCameraSettings &s) {
    try {
      if(readFailed)throw std::runtime_error(u8"原设置文件损坏，请先备份并修复该文件");
      if(!ValidFixedCamera(s))throw std::runtime_error("Invalid fixed camera range");
      std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=L".tmp";
      {std::ofstream f(temp,std::ios::binary);f<<WriteFixedCameraSettings(s).dump(2);f.close();
       if(!f)throw std::runtime_error("Cannot write fixed camera settings");}
      if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace fixed camera settings");
      status=u8"跟踪参数已保存；下次需手动开启";return true;
    }catch(const std::exception &e){status=std::string(u8"保存失败：")+e.what();return false;}
  }
};
struct CameraPresetStore {
  nlohmann::json profiles=nlohmann::json::object();
  std::string selected,status;
  bool loaded=false,readFailed=false;
  void load(const std::filesystem::path &path) {
    if(loaded)return;
    loaded=true;
    try {
      if(!std::filesystem::exists(path))return;
      if(std::filesystem::file_size(path)>2*1024*1024)throw std::runtime_error("Camera settings file too large");
      std::ifstream f(path);nlohmann::json j;f>>j;
      if(j.value("version",0)!=1||!j.at("profiles").is_object()||j.at("profiles").size()>512)
        throw std::runtime_error("Invalid camera presets");
      profiles=j.at("profiles");
    }catch(const std::exception &e){readFailed=true;status=std::string(u8"镜头设置读取失败，原文件保留：")+e.what();}
  }
  void select(const std::string &model,const std::string &track,CameraSettings &s) {
    const auto key=model.empty()||track.empty()?std::string{}:model+":"+track;
    if(selected==key)return;
    selected=key;
    if(key.empty())return;
    try {
      if(profiles.contains(key)) {s=ReadCameraSettings(profiles.at(key));status=u8"已读取此角色与镜头的设置";}
      else {s={};if(!readFailed)status=u8"尚未保存此角色与镜头的设置";}
    }catch(const std::exception &e){s={};status=std::string(u8"此镜头预设无效，使用默认值：")+e.what();}
  }
  bool save(const std::filesystem::path &path,const CameraSettings &s) {
    try {
      if(readFailed)throw std::runtime_error(u8"原设置文件损坏，请先备份并修复该文件");
      if(selected.empty())throw std::runtime_error(u8"请先选择角色和镜头");
      auto next=profiles;next[selected]=WriteCameraSettings(s);
      if(next.size()>512)throw std::runtime_error(u8"镜头预设已达 512 份上限");
      auto text=nlohmann::json{{"version",1},{"profiles",next}}.dump(2);
      if(text.size()>2*1024*1024)throw std::runtime_error(u8"镜头预设文件已达大小上限");
      std::filesystem::create_directories(path.parent_path());auto temp=path;temp+=L".tmp";
      {std::ofstream f(temp,std::ios::binary);f<<text;f.close();if(!f)throw std::runtime_error("Cannot write camera settings");}
      if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace camera settings");
      profiles=std::move(next);status=u8"已保存；下次选择此角色与镜头时自动读取";return true;
    }catch(const std::exception &e){status=std::string(u8"保存失败：")+e.what();return false;}
  }
};
} // namespace mmd
