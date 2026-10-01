#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include "nlohmann/json.hpp"

namespace mmd {
// Access only from the serialized MMD file worker. Save/open and all squad
// slots share a folder per preset type; no game objects or working-dir changes.
inline const char *PresetFolderKey(int kind) {
  return kind==7||kind==8?"motion_calibration":kind==3||kind==4?"rig_adaptation":nullptr;
}
inline nlohmann::json ReadPresetFolders(const std::filesystem::path &history) {
  try {
    if(std::filesystem::file_size(history)>512*1024)return nlohmann::json::object();
    std::ifstream stream(history,std::ios::binary);nlohmann::json data;stream>>data;
    if(data.value("version",0)==1&&data.at("folders").is_object()&&data.at("folders").size()<=32)
      return data.at("folders");
  }catch(const std::exception &){}
  return nlohmann::json::object();
}
inline std::filesystem::path PresetDialogDirectory(const std::filesystem::path &history,int kind,
                                                  const std::filesystem::path &fallback) {
  const char *key=PresetFolderKey(kind);if(!key)return fallback;
  try {
    auto folders=ReadPresetFolders(history);
    auto value=folders.value(key,std::string{});
    if(!value.empty()&&value.find('\0')==std::string::npos) {
      auto folder=std::filesystem::u8path(value);
      std::error_code error;
      if(folder.is_absolute()&&std::filesystem::is_directory(folder,error))return folder;
    }
  }catch(const std::exception &){}
  return fallback;
}
inline bool RememberPresetDirectory(const std::filesystem::path &history,int kind,
                                    const std::filesystem::path &selected) {
  const char *key=PresetFolderKey(kind);if(!key||selected.empty())return true;
  try {
    auto folder=std::filesystem::absolute(selected).parent_path().lexically_normal();
    if(!std::filesystem::is_directory(folder))return false;
    auto folders=ReadPresetFolders(history);
    const auto utf8=folder.u8string();
    auto found=folders.find(key);
    if(found!=folders.end()&&found->is_string()&&found->get<std::string>()==utf8)return true;
    folders[key]=utf8;
    std::filesystem::create_directories(history.parent_path());
    auto temporary=history;temporary+=L".tmp";
    {
      std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
      stream<<nlohmann::json{{"version",1},{"folders",folders}}.dump(2);stream.flush();
      if(!stream)return false;
    }
    return MoveFileExW(temporary.c_str(),history.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
  }catch(const std::exception &){return false;}
}
} // namespace mmd
