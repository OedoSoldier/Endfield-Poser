#pragma once
#include "game/secondary_body_settings.h"

// Called only from verified camera/SRP render callbacks, after normal update.
// MMD computes once in its pose update; rendering only replays cached targets.
static void SecondaryBodyRender(int) {
  if(!poser_agreement::Allowed()||CharacterSwitchInProgress())return;
  using namespace poser_secondary;
  LoadSettings();
  if(g_squad.active) {
    for(auto &a:g_squad.actors)if(a)Replay(a->saved.secondary);
    return;
  }
  if(g_mmd.session.active)Replay(g_mmd.session.secondary);
  // Only MMD pose updates acquire and advance a driver. Outside those
  // sessions, rendering never captures bones or writes to the game actor.
}
