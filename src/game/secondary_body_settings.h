#pragma once
#include "game/mmd_secondary_motion.h"
#include "core/plugin_paths.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>

namespace poser_secondary {
static bool settingsLoaded=false,settingsReadFailed=false;
static std::string settingsStatus;
inline void LoadSettings() {
  if(settingsLoaded)return;settingsLoaded=true;
  try {
    auto path=std::filesystem::path(PoserFilePath(L"secondary_body.json"));
    if(!std::filesystem::exists(path))return;
    if(std::filesystem::file_size(path)>16384)throw std::runtime_error("settings too large");
    std::ifstream f(path);nlohmann::json j;f>>j;
    if(j.value("version",0)!=1)throw std::runtime_error("unsupported settings version");
    auto c=settings;
    auto number=[&](const char *key,float fallback,float lo,float hi){return secondary_body::Clamp(j.value(key,fallback),lo,hi,fallback);};
    c.vertical=number("vertical",1,0,2);c.lateral=number("lateral",1,0,2);c.depth=number("depth",1,0,2);
    c.frequency=number("frequency",2.5f,1,6);c.damping=number("damping",.65f,.25f,1.5f);
    c.angleLimit=number("angle_limit",.3141593f,0,.6f);c.offsetLimit=number("offset_limit",.012f,0,.025f);
    // Legacy gameplay settings are ignored; enhancement belongs to MMD only.
    bool on=j.value("enabled",false);float gain=number("strength",1,0,3);
    settings=c;enabled=on;strength=gain;
  }catch(const std::exception &e){settingsReadFailed=true;settingsStatus=u8"设置读取失败，原文件保留";Log("[BODY-PHYSICS] settings: %s",e.what());}
}
inline void SaveSettings() {
  try {
    if(settingsReadFailed)throw std::runtime_error("Existing settings need repair; not overwritten");
    nlohmann::json j={{"version",1},{"enabled",enabled},{"strength",strength},
      {"vertical",settings.vertical},{"lateral",settings.lateral},{"depth",settings.depth},
      {"frequency",settings.frequency},{"damping",settings.damping},{"angle_limit",settings.angleLimit},{"offset_limit",settings.offsetLimit}};
    auto path=std::filesystem::path(PoserFilePath(L"secondary_body.json")),temp=path;temp+=L".tmp";
    {std::ofstream f(temp,std::ios::binary);f<<j.dump(2);f.close();if(!f)throw std::runtime_error("write failed");}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("replace failed");
    settingsStatus=u8"已保存第二骨骼物理设置";
  }catch(const std::exception &e){settingsStatus=u8"保存失败，原文件保留";Log("[BODY-PHYSICS] settings: %s",e.what());}
}
}
