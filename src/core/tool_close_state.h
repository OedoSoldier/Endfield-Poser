#pragma once
#include <atomic>

// Per-process only: a new game starts with a fresh Running state.
namespace poser_close {
enum class Phase { Running, Requested, Restoring, Draining, Closed };
struct State {
  std::atomic<Phase> phase{Phase::Running};
  bool request() {auto from=Phase::Running;return phase.compare_exchange_strong(from,Phase::Requested);}
  bool beginRestore() {auto from=Phase::Requested;return phase.compare_exchange_strong(from,Phase::Restoring);}
  bool beginDrain(bool restored) {
    if(!restored)return false;
    auto from=Phase::Restoring;return phase.compare_exchange_strong(from,Phase::Draining);
  }
  bool closing() const {return phase.load()!=Phase::Running;}
};
static State state;
static bool Closing(){return state.closing();}
}
