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
    TriggerSpawned, // the runner heard scene::Trigger.Spawned, fired from the trigger's init()
    Open,       // a door's Open input
    Close,      // a door's Close input
    Knock,      // a door's Knock input, reached with send()
    Light,      // a lamp's Light input (object:: scope)
    SysDoorOpened, // the runner's global input pin EventTest.DoorOpened
    // phase 3
    ZoneEnter, ZoneExit, ZoneOccupied, ZoneEmptied, // the zone's collider pins, bound to scene::Zone.*
    ZoneEnterCb, ZoneExitCb, // the zone script's onCollisionEnter / onCollisionExit
    LampReady, LampDisabled, LampEnabled, // the lamp's object pins
    DoomedDestroyed,         // Destroy input, Destroyed output
    ChainRelayed, ChainDelayed, // Zone.Enter -> Relay -> Frame Delay
    BlipSpawned,             // a prefab spawned by the Spawner ran init()
    Logic,                   // a built-in object's output; 'obj' holds the LOGIC_* tag
  };

  constexpr const char* const REC_NAMES[] = {
    "GameInit", "SceneLoading", "SceneLoaded", "scene::Loaded", "SceneUnloading",
    "scene::Unloading", "SceneUnloaded", "Ping", "Pong", "Fixed", "FromFixed", "Update",
    "FromUpdate", "Loop", "Carried", "Stale", "Irq", "TriggerSpawned", "Open", "Close",
    "Knock", "Light", "SysDoorOpened", "ZoneEnter", "ZoneExit", "ZoneOccupied", "ZoneEmptied",
    "ZoneEnterCb", "ZoneExitCb", "LampReady", "LampDisabled", "LampEnabled", "DoomedDestroyed",
    "ChainRelayed", "ChainDelayed", "BlipSpawned", "Logic",
  };

  struct Record
  {
    Rec type{};
    uint32_t frame{};
    uint32_t value{};
    uint16_t sender{};
    uint16_t instigator{};
    uint16_t obj{}; // the object that received it, for pins
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
    T_DOOR,   // phase 2: pins, bindings, object scope, global input pins, send(), fire() in init()
    T_CONTACTS, // phase 3: collider enter/exit pins and callbacks, object pins, Relay and Frame Delay
    T_LOGIC,    // phase 3: every built-in event object, driven with send()
    T_COUNT
  };

  extern uint32_t currFrame;
  extern uint32_t activeTest;
  extern std::vector<Record> records;

  inline void add(Rec type, const Events::Event& ev, uint16_t obj = 0) {
    records.push_back({type, currFrame, ev.value, ev.sender, ev.instigator, obj});
  }

  inline void add(Rec type, uint32_t value, uint16_t sender) {
    records.push_back({type, currFrame, value, sender, sender, 0});
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
  inline constexpr auto TRIGGER_SPAWNED = "scene::Trigger.Spawned"_event;

  // outputs of the built-in objects in scene 5, recorded with their tag in Record::obj
  struct LogicName { const char* name; Events::EventId id; uint16_t tag; };
  inline constexpr LogicName LOGIC_NAMES[] = {
    {"Once.Fired",        "scene::L.Once.Fired"_event,        1},
    {"Counter.Changed",   "scene::L.Counter.Changed"_event,   2},
    {"Counter.Reached",   "scene::L.Counter.Reached"_event,   3},
    {"All.Complete",      "scene::L.All.Complete"_event,      4},
    {"Compare.Equal",     "scene::L.Compare.Equal"_event,     5},
    {"Compare.Less",      "scene::L.Compare.Less"_event,      6},
    {"Compare.Greater",   "scene::L.Compare.Greater"_event,   7},
    {"Gate.Passed",       "scene::L.Gate.Passed"_event,       8},
    {"Gate.Blocked",      "scene::L.Gate.Blocked"_event,      9},
    {"Toggle.A",          "scene::L.Toggle.A"_event,          10},
    {"Toggle.B",          "scene::L.Toggle.B"_event,          11},
    {"Sequence.Step1",    "scene::L.Sequence.Step1"_event,    12},
    {"Sequence.Step2",    "scene::L.Sequence.Step2"_event,    13},
    {"Sequence.Step3",    "scene::L.Sequence.Step3"_event,    14},
    {"Random.Out1",       "scene::L.Random.Out1"_event,       15},
    {"Delay.Elapsed",     "scene::L.Delay.Elapsed"_event,     16},
    {"Relay.Fired",       "scene::L.Relay.Fired"_event,       17},
    {"BridgeB.Received",  "scene::L.BridgeB.Received"_event,  18},
    {"Spawner.Spawned",   "scene::L.Spawner.Spawned"_event,   19},
    {"FrameDelay.Elapsed","scene::L.FrameDelay.Elapsed"_event,20},
  };
}
