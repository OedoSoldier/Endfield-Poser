#pragma once
#include "game/frozen_grip.h"
#include "math/first_person.h"
#include <array>
#include <atomic>
#include <memory>

// Adapted from upstream first_person.h: immutable settings and the game's own
// mouse-look driver. Target snapshots retain the exact playback actors; Unity
// reads and head hiding remain in the camera callback, never in the panel.
namespace first_person {
static Settings settings;
static std::shared_ptr<const Settings> published=std::make_shared<const Settings>();
static std::atomic<bool> active{false},headHolding{false};
static std::atomic<const char*> status{u8"第一人称未开启"};
struct Target {
  void *actor=nullptr,*head=nullptr;
  Quat headToView;
  bool oriented=false;
  std::shared_ptr<GripReferences> references;
};
struct Context {
  bool playback=false,squad=false,blocked=false;
  uint64_t session=0;
  void *owner=nullptr;
  std::array<Target,4> targets;
};
static std::shared_ptr<const Context> context;
static void Publish() {std::atomic_store(&published,std::make_shared<const Settings>(settings));}
static Settings Snapshot() {auto p=std::atomic_load(&published);return p?*p:Settings{};}
static void ClearPlayback() {std::atomic_store(&context,std::shared_ptr<const Context>{});}
static void Disable() {settings.enabled=settings.mmdEnabled=false;Publish();ClearPlayback();}
static void PublishContext(const Context &next) {
  auto old=std::atomic_load(&context);
  bool same=old&&old->session==next.session&&old->owner==next.owner&&old->blocked==next.blocked&&
    old->playback==next.playback&&old->squad==next.squad;
  if(same)for(int i=0;i<4;++i) {
    const auto &a=old->targets[i],&b=next.targets[i];
    if(a.actor!=b.actor||a.head!=b.head||a.oriented!=b.oriented||a.references!=b.references||
        Quat::Angle(a.headToView,b.headToView)>.001f){same=false;break;}
  }
  if(!same)std::atomic_store(&context,std::make_shared<const Context>(next));
}
// An explicitly chosen empty slot must never fall back to another character.
static const Target *Select(const Context &c,const Settings &s,void *controlled) {
  if(c.blocked||!c.playback||!s.mmdEnabled||!Valid(s))return nullptr;
  if(!c.squad)return c.targets[0].actor?&c.targets[0]:nullptr;
  if(s.member>=0)return c.targets[s.member].actor?&c.targets[s.member]:nullptr;
  for(const auto &target:c.targets)if(target.actor&&target.actor==controlled)return &target;
  return nullptr;
}
} // namespace first_person
