#pragma once
#include "math/quat_math.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct PoseBone { std::string name; Vec3 pos; Quat rot; };
struct PoseMorph { std::string name; float value; };

struct PoseDoc {
    std::string name;
    std::vector<PoseBone> bones;
    std::vector<PoseMorph> morphs;
};

inline std::string PoseToJson(const PoseDoc& d){
    nlohmann::json j;
    j["name"] = d.name;
    for (auto& b : d.bones)
        j["bones"].push_back({{"n",b.name},{"p",{b.pos.x,b.pos.y,b.pos.z}},
                              {"r",{b.rot.x,b.rot.y,b.rot.z,b.rot.w}}});
    for (auto& m : d.morphs)
        j["morphs"].push_back({{"n",m.name},{"v",m.value}});
    return j.dump(2);
}

inline PoseDoc PoseFromJson(const std::string& s){
    PoseDoc d;
    auto j = nlohmann::json::parse(s);
    d.name = j.value("name", "");
    if (j.contains("bones"))
        for (auto& e : j["bones"]) {
            PoseBone b; b.name = e["n"].get<std::string>();
            b.pos = {e["p"][0].get<float>(), e["p"][1].get<float>(), e["p"][2].get<float>()};
            b.rot = {e["r"][0].get<float>(), e["r"][1].get<float>(), e["r"][2].get<float>(), e["r"][3].get<float>()};
            d.bones.push_back(b);
        }
    if (j.contains("morphs"))
        for (auto& e : j["morphs"])
            d.morphs.push_back({e["n"].get<std::string>(), e["v"].get<float>()});
    return d;
}
