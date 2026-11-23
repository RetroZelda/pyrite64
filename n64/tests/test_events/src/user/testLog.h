#pragma once
// Shared state of the event bus runtime test: what was heard, in which frame.
#include "script/userScript.h"
#include "events/bus.h"

#include <vector>

namespace P64::User::EventTest
{
  enum class Rec : uint8_t
  {
    GameInit,
    SceneLoading,
    SceneLoaded,
    CurrentSceneLoaded,
    SceneUnloading,
    CurrentSceneUnloading,
    SceneUnloaded,
    Ping,       // the probe heard global::Test.Ping
    Pong,       // the runner heard global::Test.Pong
    Fixed,      // the probe posted FromFixed in fixedUpdate
    FromFixed,  // the runner heard global::Test.FromFixed
    Update,     // the probe posted FromUpdate in update
    FromUpdate, // the runner heard global::Test.FromUpdate
    Loop,       // the runner heard global::Test.Loop
    Carried,    // the runner heard global::Test.Carried
    Stale,      // the runner heard scene::Test.Stale (must not happen)
    Irq,        // the runner heard global::Test.Irq
  };

  constexpr const char* const REC_NAMES[] = {
    "GameInit", "SceneLoading", "SceneLoaded", "scene::Loaded", "SceneUnloading",
    "scene::Unloading", "SceneUnloaded", "Ping", "Pong", "Fixed", "FromFixed", "Update",
    "FromUpdate", "Loop", "Carried", "Stale", "Irq",
  };

  struct Record
  {
    Rec type{};
    uint32_t frame{};
    uint32_t value{};
    uint16_t sender{};
    uint16_t instigator{};
  };

  // the tests, in the order they run
  enum Test : uint32_t
  {
    T_BOOT,   // engine events at boot
    T_CHAIN,  // a listener posts, another listener hears it in the same frame
    T_DRAINS, // events fired in fixedUpdate and update arrive before update and after it
    T_HOPS,   // a listener sending to itself stops at the hop limit
    T_BUDGET, // a drain stops at its budget and the rest waits for the next drain
    T_CARRY,  // global events survive a scene change, scene events do not
    T_IRQ,    // postFromInterrupt from a real interrupt arrives at the next frame start
    T_COUNT
  };

  extern uint32_t currFrame;
  extern uint32_t activeTest;
  extern std::vector<Record> records;

  inline void add(Rec type, const Events::Event& ev) {
    records.push_back({type, currFrame, ev.value, ev.sender, ev.instigator});
  }

  inline void add(Rec type, uint32_t value, uint16_t sender) {
    records.push_back({type, currFrame, value, sender, sender});
  }

  inline constexpr auto PING        = "global::Test.Ping"_event;
  inline constexpr auto PONG        = "global::Test.Pong"_event;
  inline constexpr auto FROM_FIXED  = "global::Test.FromFixed"_event;
  inline constexpr auto FROM_UPDATE = "global::Test.FromUpdate"_event;
  inline constexpr auto LOOP        = "global::Test.Loop"_event;
  inline constexpr auto BURST       = "global::Test.Burst"_event;
  inline constexpr auto CARRIED     = "global::Test.Carried"_event;
  inline constexpr auto STALE       = "scene::Test.Stale"_event;
  inline constexpr auto IRQ         = "global::Test.Irq"_event;
}
