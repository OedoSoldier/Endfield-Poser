#pragma once
#include <cctype>
#include <string>
#include <vector>

namespace mmd {
inline bool IsPlaybackPersistentEffect(std::string name) {
  for(char &c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
  return name.find("potentialeffect")!=std::string::npos||
      name.find("potential_effect")!=std::string::npos||name.find("_vfxpart_")!=std::string::npos;
}
// VFX meshes are not idle props. Their active state remains game-owned.
inline bool IsPlaybackPropNode(std::string name) {
  for (char &c : name)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  if(IsPlaybackPersistentEffect(name))return false;
  for (const char *prefix : {"propbone_", "prop_", "wep_", "weapon_", "wpn_"})
    if (name.rfind(prefix, 0) == 0)
      return true;
  return false;
}
struct PlaybackVisibilityNode {int parent=-1;bool prop=false,persistent=false;};
// Parent-before-child traversal. Preserve effect ancestors too: a live emitter
// below an inactive prop container is still invisible. Hide only its siblings.
inline std::vector<bool> PlaybackHiddenNodes(const std::vector<PlaybackVisibilityNode> &nodes) {
  const size_t count=nodes.size();std::vector<bool> keep(count),scope(count),effect(count),hidden(count);
  for(size_t n=0;n<count;++n){if(nodes[n].parent>=int(n)||nodes[n].parent < -1)return {};keep[n]=nodes[n].persistent;}
  for(size_t n=count;n-->0;)if(keep[n]&&nodes[n].parent>=0)keep[nodes[n].parent]=true;
  for(size_t n=0;n<count;++n){int p=nodes[n].parent;
    effect[n]=nodes[n].persistent||(p>=0&&effect[p]);scope[n]=nodes[n].prop||(p>=0&&scope[p]);
    hidden[n]=scope[n]&&!keep[n]&&!effect[n];
  }
  return hidden;
}
} // namespace mmd
