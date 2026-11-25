#include "script/globalScript.h"
#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "debug/debugDraw.h"
#include "ui/UI.h"
#include "vi/swapChain.h"
#include "./testLog.h"

#include <vector>

// Runtime test of the event bus (phase 1 of docs/todo/event-graph/PLAN.md).
// Each test runs in a freshly loaded scene for a number of frames, then checks what the
// listeners recorded. Results are printed through debugf and drawn on screen.

namespace P64::UI::Generated { void uiInitializeBindings(); }

constinit uint32_t P64::User::EventTest::currFrame = 0;
constinit uint32_t P64::User::EventTest::activeTest = 0;
constinit std::vector<P64::User::EventTest::Record> P64::User::EventTest::records{};
constinit bool P64::User::EventTest::condSlots[8]{};
constinit uint32_t P64::User::EventTest::kitValue = 0;

namespace P64::GlobalScript::C5D2B8E4F1A07C93
{
  using namespace P64::User::EventTest;
  namespace Builtin = P64::Events::Builtin;

  constexpr uint32_t ANY = 0xFFFFFFFF;
  constexpr uint16_t PROBE_ID = 2; // runtime id of the Probe object in scenes 1 and 2
  constexpr uint16_t TRIGGER_ID = 2, DOOR_A = 3, DOOR_B = 4; // scene 3

  // Phase 2: this global script is a system with an input pin and declares the test events
  P64_GLOBAL_EVENTS_IN(EventTest, DoorOpened);
  P64_GLOBAL_EVENTS_OUT(Test, Ping, Pong, FromFixed, FromUpdate, Loop, Burst, Carried, Irq, CondTick, KitTick);
  static_assert(TestOutIds[0] == "global::Test.Ping"_event.hash, "P64_GLOBAL_EVENTS_OUT hashes like _event");
  static_assert(TestOutIds[7] == "global::Test.Irq"_event.hash);

  void onInput(EventTestIn input, const Events::Event& ev)
  {
    if(input == EventTestIn::DoorOpened) add(Rec::SysDoorOpened, ev);
  }

  struct Expect
  {
    Rec type;
    uint32_t frame{ANY};
    uint32_t value{ANY};
    uint32_t sender{ANY};
    uint32_t instigator{ANY};
    uint32_t obj{ANY};
  };

  struct TestDef
  {
    const char* name;
    uint16_t sceneId;
    uint32_t frames; // frames to run before checking
    bool (*check)();
  };

  constinit int8_t results[T_COUNT]{};
  constinit bool done = false;
  constinit Events::Stats statsAtStart{};

  // T_BUDGET
  constexpr uint32_t BURST_EVENTS = 100;
  constinit uint32_t burstPerFrame[16]{};
  constinit uint32_t burstTotal = 0;

  // T_IRQ
  constinit volatile bool irqArmed = false;
  constinit volatile bool irqFired = false;
  constinit bool irqRegistered = false;

  void* tag(Rec rec) { return reinterpret_cast<void*>(static_cast<uintptr_t>(rec)); }

  void printRecords()
  {
    for(uint32_t i = 0; i < records.size(); ++i) {
      const auto &r = records[i];
      debugf("  [%02lu] frame %lu %s value=%lu sender=%u instigator=%u obj=%u\n",
        (unsigned long)i, (unsigned long)r.frame, REC_NAMES[(uint32_t)r.type],
        (unsigned long)r.value, (unsigned)r.sender, (unsigned)r.instigator, (unsigned)r.obj);
    }
  }

  bool matches(const Record &r, const Expect &e)
  {
    return r.type == e.type
      && (e.frame == ANY || r.frame == e.frame)
      && (e.value == ANY || r.value == e.value)
      && (e.sender == ANY || r.sender == e.sender)
      && (e.instigator == ANY || r.instigator == e.instigator)
      && (e.obj == ANY || r.obj == e.obj);
  }

  bool expectExactly(const std::vector<Expect> &expected)
  {
    bool ok = expected.size() == records.size();
    for(uint32_t i = 0; ok && i < expected.size(); ++i) {
      if(!matches(records[i], expected[i])) {
        debugf("  record %lu: expected %s\n", (unsigned long)i, REC_NAMES[(uint32_t)expected[i].type]);
        ok = false;
      }
    }
    if(!ok) {
      debugf("  expected %lu records, got %lu:\n", (unsigned long)expected.size(), (unsigned long)records.size());
      printRecords();
    }
    return ok;
  }

  bool expectTrue(bool condition, const char* what)
  {
    if(!condition) debugf("  failed: %s\n", what);
    return condition;
  }

  // the three unload events of the previous scene, then the load events of this one
  void addSceneChange(std::vector<Expect> &e, uint32_t oldScene, uint32_t newScene)
  {
    e.push_back({Rec::SceneUnloading, ANY, oldScene, 0});
    e.push_back({Rec::CurrentSceneUnloading, ANY, oldScene, 0});
    e.push_back({Rec::SceneUnloaded, ANY, oldScene, 0});
    e.push_back({Rec::SceneLoading, 0, newScene, 0});
  }

  // ---- listeners ----

  void onRecord(const Events::Event& ev, void* user)
  {
    add(static_cast<Rec>(reinterpret_cast<uintptr_t>(user)), ev);
  }

  void onLogic(const Events::Event& ev, void* user)
  {
    const auto tag = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(user));
    add(Rec::Logic, ev, tag);
    // Spawner.Spawned (tag 19) carries the id of a prefab that only exists next frame: sends to it
    // must wait for it and arrive in order, like a projectile's init message after addObject()
    if(tag == 19 && activeTest == T_LOGIC) {
      Events::send(static_cast<uint16_t>(ev.value), "Poke"_pin, 7, 2);
      Events::send(static_cast<uint16_t>(ev.value), "Nudge"_pin, 8, 2);
    }
  }

  void onCond(const Events::Event& ev, void* user)
  {
    add(Rec::Cond, ev, static_cast<uint16_t>(reinterpret_cast<uintptr_t>(user)));
  }

  void onCurrentSceneLoaded(const Events::Event& ev, [[maybe_unused]] void* user)
  {
    add(Rec::CurrentSceneLoaded, ev);
    if(activeTest == T_CHAIN) Events::post(PING, 7); // the probe answers with Pong
  }

  void onCurrentSceneUnloading(const Events::Event& ev, [[maybe_unused]] void* user)
  {
    add(Rec::CurrentSceneUnloading, ev);
    if(activeTest == T_CARRY) {
      Events::post(STALE, 1);   // a scene event: dropped together with the scene
      Events::post(CARRIED, 2); // a global event: delivered in the next scene
    }
  }

  void onLoop(const Events::Event& ev, [[maybe_unused]] void* user)
  {
    add(Rec::Loop, ev);
    Events::post(LOOP, ev.value + 1); // sends to itself; the hop limit stops it
  }

  void onBurst(const Events::Event& ev, [[maybe_unused]] void* user)
  {
    if(currFrame < 16) ++burstPerFrame[currFrame];
    ++burstTotal;
    Events::post(BURST, ev.value); // each burst event is a chain of hop-limit + 1 deliveries
  }

  void onVblank()
  {
    // runs in the VI interrupt
    if(irqArmed && !irqFired) {
      irqFired = true;
      Events::postFromInterrupt(IRQ, 42);
    }
  }

  // ---- checks ----

  bool checkBoot()
  {
    return expectExactly({
      {Rec::GameInit,           0, 0, 0},
      {Rec::SceneLoading,       0, 1, 0},
      {Rec::SceneLoaded,        1, 1, 0},
      {Rec::CurrentSceneLoaded, 1, 1, 0},
    });
  }

  bool checkChain()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 1, 1);
    e.push_back({Rec::SceneLoaded,        1, 1, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 1, 0});
    e.push_back({Rec::Ping, 1, 7, 0, 0});               // posted by the runner while handling scene::Loaded
    e.push_back({Rec::Pong, 1, 8, PROBE_ID, PROBE_ID}); // posted by the probe while handling Ping
    return expectExactly(e);
  }

  bool checkDrains()
  {
    uint32_t fixedIdx = ANY;
    for(uint32_t i = 0; i < records.size(); ++i) {
      if(records[i].type == Rec::Fixed) { fixedIdx = i; break; }
    }
    if(!expectTrue(fixedIdx != ANY, "fixedUpdate ran and posted")) { printRecords(); return false; }
    const uint32_t frame = records[fixedIdx].frame;

    std::vector<Expect> e{};
    addSceneChange(e, 1, 1);
    e.push_back({Rec::SceneLoaded,        1, 1, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 1, 0});
    e.push_back({Rec::Fixed,      frame, 0, PROBE_ID});
    e.push_back({Rec::FromFixed,  frame, 0, PROBE_ID}); // drain 2: after physics, before update
    e.push_back({Rec::Update,     frame, 0, PROBE_ID});
    e.push_back({Rec::FromUpdate, frame, 0, PROBE_ID}); // drain 3: after update, same frame
    return expectExactly(e);
  }

  bool checkHops()
  {
    const auto stats = Events::getStats();
    bool ok = true;
    uint32_t loops = 0;
    for(const auto &r : records) {
      if(r.type != Rec::Loop) continue;
      ok &= expectTrue(r.frame == 2, "every loop delivery in frame 2");
      ok &= expectTrue(r.value == loops, "loop values count up from 0");
      ++loops;
    }
    ok &= expectTrue(loops == Events::getLimits().maxHops + 1, "hop limit + 1 deliveries");
    ok &= expectTrue(stats.droppedHopLimit - statsAtStart.droppedHopLimit == 1, "one post dropped at the hop limit");
    if(!ok) printRecords();
    return ok;
  }

  bool checkBudget()
  {
    const auto stats = Events::getStats();
    const auto limits = Events::getLimits();
    const uint32_t PER_CHAIN = limits.maxHops + 1;
    const uint32_t TOTAL = BURST_EVENTS * PER_CHAIN;
    const uint32_t PER_FRAME = 3 * limits.drainBudget;
    bool ok = true;
    ok &= expectTrue(burstTotal == TOTAL, "every burst chain delivered in full");
    ok &= expectTrue(burstPerFrame[2] == PER_FRAME, "frame 2: three drains at their budget");
    ok &= expectTrue(burstPerFrame[3] == PER_FRAME, "frame 3: three drains at their budget");
    ok &= expectTrue(burstPerFrame[4] == TOTAL - 2 * PER_FRAME, "frame 4: the rest");
    ok &= expectTrue(stats.budgetHits - statsAtStart.budgetHits == 6, "six drains hit their budget");
    ok &= expectTrue(stats.droppedQueueFull == statsAtStart.droppedQueueFull, "nothing lost to a full queue");
    ok &= expectTrue(stats.droppedHopLimit - statsAtStart.droppedHopLimit == BURST_EVENTS, "each chain ended at the hop limit");
    if(!ok) {
      debugf("  total %lu, frames 2-5: %lu %lu %lu %lu, budget hits %lu\n", (unsigned long)burstTotal,
        (unsigned long)burstPerFrame[2], (unsigned long)burstPerFrame[3], (unsigned long)burstPerFrame[4],
        (unsigned long)burstPerFrame[5], (unsigned long)(stats.budgetHits - statsAtStart.budgetHits));
    }
    return ok;
  }

  bool checkCarry()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 1, 2);
    e.push_back({Rec::Carried, 1, 2, 0});               // queued during the unload, kept, delivered first
    e.push_back({Rec::SceneLoaded,        1, 2, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 2, 0});
    return expectExactly(e);                            // no Stale record
  }

  bool checkIrq()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 2, 2);
    e.push_back({Rec::SceneLoaded,        1, 2, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 2, 0});
    bool ok = true;
    uint32_t irqs = 0;
    for(const auto &r : records) {
      if(r.type != Rec::Irq) continue;
      ++irqs;
      ok &= expectTrue(r.value == 42 && r.sender == 0, "interrupt event value and sender");
      ok &= expectTrue(r.frame >= 3, "interrupt event arrives at a later frame start");
      e.push_back({Rec::Irq, r.frame, 42, 0});
    }
    ok &= expectTrue(irqs == 1, "exactly one interrupt event");
    ok &= expectExactly(e);
    // the probes of earlier scenes removed their listeners in destroy()
    ok &= expectTrue(Events::listenerCount(PING) == 1, "only the current probe listens to Ping");
    return ok;
  }

  bool checkDoor()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 2, 3);
    e.push_back({Rec::TriggerSpawned, 1, 1, TRIGGER_ID, TRIGGER_ID});  // fired in init(), queued before the load events
    e.push_back({Rec::SceneLoaded,        1, 3, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 3, 0});
    // frame 2: Enter reaches both doors through scene::Trigger.Enter, in the order they subscribed
    e.push_back({Rec::Open,  2, 0, TRIGGER_ID, 77, DOOR_A});
    e.push_back({Rec::Open,  2, 0, TRIGGER_ID, 77, DOOR_B});
    // each door's Opened reaches its own lamp only (object::), door A's also the global input pin
    e.push_back({Rec::Light, 2, DOOR_A, DOOR_A, 77, DOOR_A});
    e.push_back({Rec::SysDoorOpened, 2, DOOR_A, DOOR_A, 77});
    e.push_back({Rec::Light, 2, DOOR_B, DOOR_B, 77, DOOR_B});
    // frame 3: send() by input name, to an object and to an id (an id of 0 sends nothing), frame 4: Exit reaches door A only
    e.push_back({Rec::Knock, 3, 5, TRIGGER_ID, TRIGGER_ID, DOOR_A});
    e.push_back({Rec::Knock, 3, 6, TRIGGER_ID, TRIGGER_ID, DOOR_B});
    e.push_back({Rec::Close, 4, 0, TRIGGER_ID, TRIGGER_ID, DOOR_A});
    return expectExactly(e);
  }

  uint32_t frameOf(Rec type)
  {
    for(const auto &r : records) if(r.type == type) return r.frame;
    return ANY;
  }

  uint32_t countOf(Rec type)
  {
    uint32_t n = 0;
    for(const auto &r : records) n += r.type == type ? 1 : 0;
    return n;
  }

  const Record* findRec(Rec type)
  {
    for(const auto &r : records) if(r.type == type) return &r;
    return nullptr;
  }

  bool checkContacts()
  {
    constexpr uint16_t ZONE = 2, MOVER = 3, LAMP = 4, DOOMED = 5;
    bool ok = true;
    for(auto rec : {Rec::ZoneEnter, Rec::ZoneExit, Rec::ZoneOccupied, Rec::ZoneEmptied, Rec::ZoneEnterCb,
                    Rec::ZoneExitCb, Rec::LampReady, Rec::LampDisabled, Rec::LampEnabled, Rec::DoomedDestroyed,
                    Rec::ChainRelayed, Rec::ChainDelayed}) {
      if(countOf(rec) != 1) {
        debugf("  %s recorded %lu times, expected once\n", REC_NAMES[(uint32_t)rec], (unsigned long)countOf(rec));
        ok = false;
      }
    }
    if(!ok) { printRecords(); return false; }

    const uint32_t enter = frameOf(Rec::ZoneEnter);
    const uint32_t exit = frameOf(Rec::ZoneExit);
    ok &= expectTrue(enter > 2 && exit > enter, "the mover enters, then leaves");
    ok &= expectTrue(findRec(Rec::ZoneEnter)->value == MOVER && findRec(Rec::ZoneEnter)->sender == ZONE, "Enter: value is the mover, sender the zone");
    ok &= expectTrue(findRec(Rec::ZoneEnter)->instigator == MOVER, "Enter: the mover is the instigator");
    ok &= expectTrue(findRec(Rec::ZoneExit)->value == MOVER, "Exit: value is the mover");
    ok &= expectTrue(frameOf(Rec::ZoneOccupied) == enter && frameOf(Rec::ZoneEmptied) == exit, "Occupied with the first enter, Emptied with the last exit");
    ok &= expectTrue(frameOf(Rec::ZoneEnterCb) == enter && findRec(Rec::ZoneEnterCb)->value == MOVER, "onCollisionEnter with the mover's id");
    ok &= expectTrue(frameOf(Rec::ZoneExitCb) == exit && findRec(Rec::ZoneExitCb)->value == MOVER, "onCollisionExit with the mover's id");
    ok &= expectTrue(frameOf(Rec::LampReady) == 1 && findRec(Rec::LampReady)->value == LAMP, "Ready in the first frame");
    ok &= expectTrue(frameOf(Rec::LampDisabled) == enter + 1, "Disable input applies at the next frame's state change");
    ok &= expectTrue(frameOf(Rec::LampEnabled) == exit + 1, "a disabled object still receives Enable");
    ok &= expectTrue(frameOf(Rec::DoomedDestroyed) == exit && findRec(Rec::DoomedDestroyed)->value == DOOMED, "Destroyed fires when removal is requested");
    ok &= expectTrue(frameOf(Rec::ChainRelayed) == enter, "Relay passes Enter on in the same drain");
    ok &= expectTrue(frameOf(Rec::ChainDelayed) == enter + 3, "Frame Delay of 3 frames");
    ok &= expectTrue(findRec(Rec::ChainDelayed)->instigator == MOVER, "the delay keeps the instigator");
    ok &= expectTrue(SceneManager::getCurrent().getObjectById(DOOMED) == nullptr, "the destroyed object is gone");
    if(!ok) printRecords();
    return ok;
  }

  // the (frame, value) pairs recorded for one built-in object output
  bool expectLogic(uint16_t tag, std::initializer_list<std::pair<uint32_t, uint32_t>> expected)
  {
    std::vector<std::pair<uint32_t, uint32_t>> got{};
    for(const auto &r : records) {
      if(r.type == Rec::Logic && r.obj == tag) got.push_back({r.frame, r.value});
    }
    bool ok = got.size() == expected.size();
    size_t i = 0;
    for(const auto &e : expected) {
      if(!ok)break;
      ok = (e.first == ANY || got[i].first == e.first) && (e.second == ANY || got[i].second == e.second);
      ++i;
    }
    if(!ok) {
      const char* name = "?";
      for(const auto &n : LOGIC_NAMES) if(n.tag == tag) name = n.name;
      debugf("  %s: expected %lu outputs, got %lu:", name, (unsigned long)expected.size(), (unsigned long)got.size());
      for(const auto &g : got) debugf(" (frame %lu, value %lu)", (unsigned long)g.first, (unsigned long)g.second);
      debugf("\n");
    }
    return ok;
  }

  bool checkLogic()
  {
    bool ok = true;
    ok &= expectLogic(1,  {{2, 1}, {5, 3}});           // Once: Reset re-arms it
    ok &= expectLogic(2,  {{2, 1}, {3, 2}, {4, 1}});   // Counter.Changed
    ok &= expectLogic(3,  {{3, 2}});                   // Counter.Reached at the target
    ok &= expectLogic(4,  {{3, 0}, {5, 0}});           // All: A+B, then B+A after the auto reset
    ok &= expectLogic(5,  {{3, 10}});                  // Compare.Equal
    ok &= expectLogic(6,  {{2, 5}});                   // Compare.Less
    ok &= expectLogic(7,  {{4, 20}});                  // Compare.Greater
    ok &= expectLogic(8,  {{2, 1}, {6, 3}});           // Gate.Passed before Close and after Reset
    ok &= expectLogic(9,  {{4, 2}});                   // Gate.Blocked while closed
    ok &= expectLogic(10, {{2, 0}, {4, 0}, {6, 0}});   // Toggle.A
    ok &= expectLogic(11, {{3, 0}});                   // Toggle.B
    ok &= expectLogic(12, {{2, 0}, {5, 0}});           // Sequence.Step1, looping after 3 steps
    ok &= expectLogic(13, {{3, 0}});
    ok &= expectLogic(14, {{4, 0}});
    ok &= expectLogic(15, {{2, 0}});                   // Random with one output
    ok &= expectLogic(16, {{ANY, 9}});                 // Delay: once, later, with Start's value
    ok &= expectLogic(17, {{2, 4}, {6, 6}});           // Relay: nothing while disabled
    ok &= expectLogic(18, {{3, 77}});                  // Message Bridge: message out, message in
    ok &= expectLogic(19, {{2, ANY}});                 // Spawner.Spawned
    ok &= expectLogic(20, {{5, 55}});                  // Frame Delay of 3 frames
    for(const auto &r : records) {
      if(r.type == Rec::Logic && r.obj == 16) ok &= expectTrue(r.frame >= 3 && r.frame <= 6, "Delay of 0.05 s elapses after a few frames");
    }

    const Record* blip = findRec(Rec::BlipSpawned);
    uint32_t spawnedId = 0;
    for(const auto &r : records) if(r.type == Rec::Logic && r.obj == 19) spawnedId = r.value;
    ok &= expectTrue(blip && blip->frame == 3 && blip->value == spawnedId && spawnedId >= 0x100, "the spawned prefab exists next frame with the reported id");

    // two sends made in frame 2, before the prefab existed: delivered in frame 3, after its init(),
    // in the order they were made, each once
    const Record* poked = findRec(Rec::BlipPoked);
    const Record* nudged = findRec(Rec::BlipNudged);
    uint32_t pokes = 0, nudges = 0;
    for(const auto &r : records) { pokes += r.type == Rec::BlipPoked; nudges += r.type == Rec::BlipNudged; }
    ok &= expectTrue(pokes == 1 && poked && poked->frame == 3 && poked->value == 7 && poked->sender == 2 && poked->obj == spawnedId,
      "a send to a prefab queued this frame arrives next frame");
    ok &= expectTrue(nudges == 1 && nudged && nudged->frame == 3 && nudged->value == 8, "and so does a second one");
    ok &= expectTrue(blip && poked && nudged && blip < poked && poked < nudged, "after init(), in the order sent");

    // the root's init() sent 40 greetings to its child, an object of the same spawn that exists
    // already: all arrive in the frame of the spawn, none waits or is dropped
    const Record* greeted = findRec(Rec::BlipGreeted);
    ok &= expectTrue(greeted && greeted->frame == 3 && greeted->value == BLIP_GREETINGS,
      "sends between the objects of one spawn all arrive in the spawn's frame");
    if(!ok) printRecords();
    return ok;
  }

  bool checkPrefabs()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 5, 6);
    e.push_back({Rec::SceneLoaded,        1, 6, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 6, 0});
    // prefab::Spawned in each kit placed in the scene, the nested ones included
    e.push_back({Rec::KitSpawned, 1, 10});
    e.push_back({Rec::KitSpawned, 1, 20});
    e.push_back({Rec::KitSpawned, 1, 31});
    e.push_back({Rec::KitSpawned, 1, 41});
    e.push_back({Rec::KitSpawned, 1, 63}); // W1 (60) / Enc (+2) / Kit (+1)
    e.push_back({Rec::KitDoorOpened, 2, 10}); // each plate opens only its own kit's door
    e.push_back({Rec::KitDoorOpened, 3, 20});
    e.push_back({Rec::KitDoorOpened, 4, 31}); // the kit nested in E1, through an override on E1
    e.push_back({Rec::TallyHeard,    4, 30}); // prefab_1:: fires into the encounter around the kit
    e.push_back({Rec::SirenHeard,    5, 31}); // E1's alarm reaches only the kit inside E1
    e.push_back({Rec::SirenHeard,    6, 20}); // K2's repointed link: prefab_1::Alarm means scene::GlobalAlarm
    e.push_back({Rec::KitSpawned,    9, 50}); // a kit spawned from code
    e.push_back({Rec::KitDoorOpened,10, 50}); // reached with postTo()
    e.push_back({Rec::SirenFar,     11, 63}); // prefab_2:: reaches the wing around the encounter; the
                                              // other kits have no grandparent and stay silent
    return expectExactly(e);
  }

  // scene 7 must give the same records as it is, wrapped into a prefab and unpacked again
  bool checkNesting()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 6, 7);
    e.push_back({Rec::SceneLoaded,        1, 7, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 7, 0});
    e.push_back({Rec::KitSpawned,    1, 71});
    e.push_back({Rec::KitDoorOpened, 2, 71}); // scene::Press7, posted by code
    e.push_back({Rec::TallyHeard,    2, 0});  // scene::Kit7Opened, heard outside the group
    e.push_back({Rec::SirenHeard,    3, 71}); // the horn's alarm, through the kit's repointed link
    return expectExactly(e);
  }

  // scene 8, wired only by graph operations: Driver.Go -> Relay -> Door.Open, the door's Opened
  // output into a global system input, Driver.Shut -> Door.Close, Closed -> the lamp on the same
  // object (object::), and scene::Loaded from the scope node -> Door.Knock
  bool checkGraph()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 7, 8);
    e.push_back({Rec::SceneLoaded,        1, 8, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 8, 0});
    e.push_back({Rec::Knock,              1, 8});
    e.push_back({Rec::Open,               2});
    e.push_back({Rec::SysDoorOpened,      2});
    e.push_back({Rec::Close,              4});
    e.push_back({Rec::Light,              4});
    return expectExactly(e);
  }

  // scene 9: the driver shows TestHud (Show pin) on frame 2 and plays its Flash clip (PlayClip
  // pin) on frame 4. The canvas's first focusable element gets focus once it shows, the clip's
  // Fire output marker fires halfway and its end fires ClipFinished: all three reach Watch.
  bool checkCanvas()
  {
    std::vector<Expect> e{};
    addSceneChange(e, 8, 9);
    e.push_back({Rec::SceneLoaded,        1, 9, 0});
    e.push_back({Rec::CurrentSceneLoaded, 1, 9, 0});
    e.push_back({Rec::HudFocused, ANY, 0}); // element 0
    e.push_back({Rec::HudMid,     ANY, 0});
    e.push_back({Rec::HudDone,    ANY, 0}); // clip 0
    if(!expectExactly(e)) return false;
    // shown on frame 2: focus follows within a frame or two; the clip starts on frame 4
    // records 0-3: the scene change, 4-5: loaded; then focus, marker, clip end
    const bool ok = expectTrue(records[6].frame >= 2 && records[6].frame <= 4, "focus soon after Show")
        && expectTrue(records[7].frame > 4 && records[8].frame > records[7].frame, "marker, then clip end, after Flash");
    if(!ok) printRecords();
    return ok;
  }

  // the (frame, value) pairs recorded for one output of a condition in scene 10
  bool expectCond(uint16_t tag, std::initializer_list<std::pair<uint32_t, uint32_t>> expected)
  {
    std::vector<std::pair<uint32_t, uint32_t>> got{};
    for(const auto &r : records) {
      if(r.type == Rec::Cond && r.obj == tag) got.push_back({r.frame, r.value});
    }
    bool ok = got.size() == expected.size();
    size_t i = 0;
    for(const auto &e : expected) {
      if(!ok)break;
      ok = got[i].first == e.first && (e.second == ANY || got[i].second == e.second);
      ++i;
    }
    if(!ok) {
      const char* name = "?";
      for(const auto &n : COND_NAMES) if(n.tag == tag) name = n.name;
      debugf("  %s: expected %lu outputs, got %lu:", name, (unsigned long)expected.size(), (unsigned long)got.size());
      for(const auto &g : got) debugf(" (frame %lu, value %lu)", (unsigned long)g.first, (unsigned long)g.second);
      debugf("\n");
    }
    return ok;
  }

  // the records of one type, in order, as (frame, value)
  bool expectRecs(Rec type, std::initializer_list<std::pair<uint32_t, uint32_t>> expected)
  {
    std::vector<std::pair<uint32_t, uint32_t>> got{};
    for(const auto &r : records) {
      if(r.type == type) got.push_back({r.frame, r.value});
    }
    bool ok = got.size() == expected.size();
    size_t i = 0;
    for(const auto &e : expected) {
      if(!ok)break;
      ok = got[i].first == e.first && got[i].second == e.second;
      ++i;
    }
    if(!ok) {
      debugf("  %s: expected %lu records, got %lu:", REC_NAMES[(uint32_t)type], (unsigned long)expected.size(), (unsigned long)got.size());
      for(const auto &g : got) debugf(" (frame %lu, value %lu)", (unsigned long)g.first, (unsigned long)g.second);
      debugf("\n");
    }
    return ok;
  }

  // Scene 10, driven by TestDriver10. A and B check themselves on global::Test.CondTick (the tick's
  // value is passed on); And, Or and Xor read both; Nand is a Not behind And; M has no triggers.
  bool checkConditions()
  {
    bool ok = true;
    // frame 2: both false. 3: A true. 4: B true. 5: A disabled (and false). 6: A enabled, reset, true.
    ok &= expectCond(COND_A_FALSE,       {{2, 5}});                  // every check that is false...
    ok &= expectCond(COND_A_TRUE,        {{3, 6}, {4, 7}, {6, 9}});  // ...or true; nothing while disabled
    ok &= expectCond(COND_A_NEWLY_TRUE,  {{3, 6}, {6, 9}});          // false to true, and again after Reset
    ok &= expectCond(COND_A_NEWLY_FALSE, {});                        // it starts false; disabled at frame 5
    ok &= expectCond(COND_A_RESULT,      {{2, 0}, {3, 1}, {4, 1}, {6, 1}});

    // one check per tick, though both inputs were checked; it reads A while A is disabled.
    // A logic node following its inputs fires with its own answer as the value: 1 or 0
    ok &= expectCond(COND_AND_FALSE,       {{2, 0}, {3, 0}, {5, 0}});
    ok &= expectCond(COND_AND_TRUE,        {{4, 1}, {6, 1}});
    ok &= expectCond(COND_AND_NEWLY_TRUE,  {{4, 1}, {6, 1}});
    ok &= expectCond(COND_AND_NEWLY_FALSE, {{5, 0}});
    ok &= expectCond(COND_OR_NEWLY_TRUE,   {{3, 1}});
    ok &= expectCond(COND_OR_NEWLY_FALSE,  {});
    ok &= expectCond(COND_XOR_NEWLY_TRUE,  {{3, 1}, {5, 1}});
    ok &= expectCond(COND_XOR_NEWLY_FALSE, {{4, 0}, {6, 0}});
    // two logic nodes deep: Nand follows And, which follows A and B
    ok &= expectCond(COND_NAND_NEWLY_TRUE,  {{2, 1}, {5, 1}});
    ok &= expectCond(COND_NAND_NEWLY_FALSE, {{4, 0}, {6, 0}});

    // frame 7: Check by name with a value. 9: Conditions::check() from code
    ok &= expectCond(COND_M_TRUE,   {{7, 77}, {9, 5}});
    ok &= expectCond(COND_M_FALSE,  {});
    ok &= expectCond(COND_M_RESULT, {{7, 1}, {9, 1}});
    // Not follows M's Result (7, 9: its answer, 0); at 8 it is checked itself (the Check's value, 3)
    // and reads M, which is false by then
    ok &= expectCond(COND_NOT_FALSE, {{7, 0}, {9, 0}});
    ok &= expectCond(COND_NOT_TRUE,  {{8, 3}});
    // an input nothing provides counts as false
    ok &= expectCond(COND_ORPHAN_FALSE, {{7, 1}});
    ok &= expectCond(COND_ORPHAN_TRUE,  {});
    // starts disabled, so the ticks do nothing; one event on Enable and Check: Enable first
    ok &= expectCond(COND_GATED_TRUE, {{7, 9}});

    // "Auto Check" off: the script's trigger is not bound, so only the Check at frame 7 asks it
    ok &= expectCond(COND_QUIET_TRUE,  {{7, 4}});
    ok &= expectCond(COND_QUIET_FALSE, {});

    // a node graph's Fire Event node (Start -> Fire Event, value 42) reaches a Check input
    ok &= expectCond(COND_G_TRUE, {{1, 42}});

    // evaluate(): false, true, and false for an object that is no condition
    ok &= expectRecs(Rec::CondEval, {{8, 0}, {8, 1}, {8, 0}});

    // prefab instances at x 10 and 20, and one spawned at x 30 in frame 11: each Not reads the
    // condition of its own instance (true while kitValue is the instance's x)
    ok &= expectRecs(Rec::KitNotFalse, {{10, 10}, {11, 20}, {12, 30}});
    ok &= expectRecs(Rec::KitNotTrue,  {{10, 20}, {11, 10}, {11, 30}, {12, 10}, {12, 20}});

    const auto stats = Conditions::getStats();
    ok &= expectTrue(stats.providers == 7, "A, B, M, And and the three kit conditions have a bound Result");
    if(!ok) {
      debugf("  providers %lu (peak %lu of %lu), checks %lu\n", (unsigned long)stats.providers,
        (unsigned long)stats.providersPeak, (unsigned long)stats.maxProviders, (unsigned long)stats.checks);
      printRecords();
    }
    return ok;
  }

  // Scene 11, driven by TestDriver11 (docs/todo/anim-events/PLAN.md). AnimA, AnimB and AnimC show
  // the model events/anim_test.json lists (Walk.STEP, Idle.BLINK, Any.STEP). A binds Walk.STEP,
  // Any.STEP and MainFinished, B only Walk.STEP, C nothing.
  bool checkAnimEvents()
  {
    constexpr uint16_t A = 3, B = 4;
    constexpr uint16_t INSTIGATOR = 9;
    std::vector<Record> got{};
    for(const auto &r : records) {
      if(r.type >= Rec::AnimFired && r.type <= Rec::AnimStepB) got.push_back(r);
    }
    const std::vector<Expect> expected = {
      // frame 2, the driver's calls: listed on A, unlisted on A, listed on B, nothing bound on C, Any on A
      {Rec::AnimFired, 2, 1, 1}, {Rec::AnimFired, 2, 0, 2}, {Rec::AnimFired, 2, 1, 3},
      {Rec::AnimFired, 2, 0, 4}, {Rec::AnimFired, 2, 1, 5},
      // each object's own pins only, with the value and instigator the driver passed, the object as sender
      {Rec::AnimStepA, 2, 11, A, INSTIGATOR}, {Rec::AnimStepB, 2, 21, B, INSTIGATOR},
      {Rec::AnimAnyA, 2, 13, A, INSTIGATOR},
      // A plays Walk (clip 1) once from frame 3: MainFinished, pin 0, is still there
      {Rec::AnimFinishedA, ANY, 1, A},
    };
    bool ok = got.size() == expected.size();
    for(size_t i = 0; ok && i < expected.size(); ++i) ok = matches(got[i], expected[i]);
    if(!ok) {
      debugf("  expected %lu animation records, got %lu:\n", (unsigned long)expected.size(), (unsigned long)got.size());
      printRecords();
    }
    return ok;
  }

  constexpr TestDef TESTS[T_COUNT] = {
    {"Engine events at boot",         1, 3,  checkBoot},
    {"Chain in the same frame",       1, 3,  checkChain},
    {"Drain after physics and update", 1, 12, checkDrains},
    {"Hop limit",                     1, 4,  checkHops},
    {"Drain budget",                  1, 8,  checkBudget},
    {"Scene change keeps global only", 2, 3,  checkCarry},
    {"Post from an interrupt",        2, 8,  checkIrq},
    {"Pins, bindings and send()",     3, 6,  checkDoor},
    {"Collider enter/exit, object pins", 4, 26, checkContacts},
    {"Built-in event objects",        5, 9,  checkLogic},
    {"Prefab scope and nesting",      6, 13, checkPrefabs},
    {"Same events after To Prefab and Unpack", 7, 5, checkNesting},
    {"Wired by the graph",            8, 6, checkGraph},
    {"UI canvas pins",                9, 30, checkCanvas},
    {"Conditions and logic nodes",    10, 14, checkConditions},
    {"Animation event pins",          11, 60, checkAnimEvents},
  };

  void runStep()
  {
    if(currFrame == 1) {
      statsAtStart = Events::getStats();
      burstTotal = 0;
      for(auto &count : burstPerFrame) count = 0;
    }

    switch(activeTest)
    {
      case T_HOPS:
        if(currFrame == 2) Events::post(LOOP, 0);
        break;
      case T_BUDGET:
        if(currFrame == 2) {
          for(uint32_t i = 0; i < BURST_EVENTS; ++i) Events::post(BURST, i);
        }
        break;
      case T_IRQ:
        if(currFrame == 2 && !irqRegistered) {
          irqArmed = true;
          irqRegistered = true;
          register_VI_handler(onVblank);
        }
        if(irqRegistered && irqFired) {
          unregister_VI_handler(onVblank);
          irqRegistered = false;
        }
        break;
      default: break;
    }
  }

  // ---- hooks ----

  void onGameInit()
  {
    for(auto &r : results) r = -1;

    // code listeners live until unlisten, across scene changes
    Events::listen(Builtin::GameInit,       onRecord, tag(Rec::GameInit));
    Events::listen(Builtin::SceneLoading,   onRecord, tag(Rec::SceneLoading));
    Events::listen(Builtin::SceneLoaded,    onRecord, tag(Rec::SceneLoaded));
    Events::listen(Builtin::SceneUnloading, onRecord, tag(Rec::SceneUnloading));
    Events::listen(Builtin::SceneUnloaded,  onRecord, tag(Rec::SceneUnloaded));
    Events::listen(Builtin::CurrentSceneLoaded,    onCurrentSceneLoaded);
    Events::listen(Builtin::CurrentSceneUnloading, onCurrentSceneUnloading);
    Events::listen(PONG,        onRecord, tag(Rec::Pong));
    Events::listen(FROM_FIXED,  onRecord, tag(Rec::FromFixed));
    Events::listen(FROM_UPDATE, onRecord, tag(Rec::FromUpdate));
    Events::listen(LOOP,        onLoop);
    Events::listen(BURST,       onBurst);
    Events::listen(CARRIED,     onRecord, tag(Rec::Carried));
    Events::listen(STALE,       onRecord, tag(Rec::Stale));
    Events::listen(IRQ,         onRecord, tag(Rec::Irq));
    Events::listen(TRIGGER_SPAWNED, onRecord, tag(Rec::TriggerSpawned));

    // phase 3
    Events::listen("scene::Zone.Enter"_event,      onRecord, tag(Rec::ZoneEnter));
    Events::listen("scene::Zone.Exit"_event,       onRecord, tag(Rec::ZoneExit));
    Events::listen("scene::Zone.Occupied"_event,   onRecord, tag(Rec::ZoneOccupied));
    Events::listen("scene::Zone.Emptied"_event,    onRecord, tag(Rec::ZoneEmptied));
    Events::listen("scene::Lamp.Ready"_event,      onRecord, tag(Rec::LampReady));
    Events::listen("scene::Lamp.Disabled"_event,   onRecord, tag(Rec::LampDisabled));
    Events::listen("scene::Lamp.Enabled"_event,    onRecord, tag(Rec::LampEnabled));
    Events::listen("scene::Doomed.Destroyed"_event,onRecord, tag(Rec::DoomedDestroyed));
    Events::listen("scene::Chain.Relayed"_event,   onRecord, tag(Rec::ChainRelayed));
    Events::listen("scene::Chain.Delayed"_event,   onRecord, tag(Rec::ChainDelayed));
    for(const auto &logic : LOGIC_NAMES) {
      Events::listen(logic.id, onLogic, reinterpret_cast<void*>(static_cast<uintptr_t>(logic.tag)));
    }
    for(const auto &cond : COND_NAMES) {
      Events::listen(cond.id, onCond, reinterpret_cast<void*>(static_cast<uintptr_t>(cond.tag)));
    }

    // animation events, scene 11
    Events::listen("scene::AnimA.Step"_event,     onRecord, tag(Rec::AnimStepA));
    Events::listen("scene::AnimA.AnyStep"_event,  onRecord, tag(Rec::AnimAnyA));
    Events::listen("scene::AnimA.Finished"_event, onRecord, tag(Rec::AnimFinishedA));
    Events::listen("scene::AnimB.Step"_event,     onRecord, tag(Rec::AnimStepB));
  }

  void onScenePreLoad()
  {
    currFrame = 0;
    P64::UI::init(); // phase 6: the UI canvases, driven like a game does
  }

  void onScenePostLoad()
  {
    P64::UI::Generated::uiInitializeBindings();
  }

  void onScenePostUnload()
  {
    P64::UI::destroy();
  }

  void onSceneUpdate()
  {
    P64::UI::update(VI::SwapChain::getDeltaTime());
    if(done) return;
    ++currFrame;
    runStep();

    const auto &test = TESTS[activeTest];
    if(currFrame < test.frames) return;

    const bool ok = test.check();
    results[activeTest] = ok ? 1 : 0;
    debugf("[event test] %lu %s: %s\n", (unsigned long)activeTest, test.name, ok ? "OK" : "FAIL");

    ++activeTest;
    records.clear();
    if(activeTest >= T_COUNT) {
      done = true;
      uint32_t passed = 0;
      for(auto r : results) passed += (r == 1) ? 1 : 0;
      debugf("[event test] DONE: %lu of %lu passed\n", (unsigned long)passed, (unsigned long)T_COUNT);
      return;
    }

    const uint16_t nextScene = TESTS[activeTest].sceneId;
    if(nextScene != SceneManager::getCurrent().getId()) {
      SceneManager::load(nextScene);
    } else {
      SceneManager::reload();
    }
  }

  void onSceneDraw2D()
  {
    P64::UI::draw_2D();
    Debug::printStart();

    int16_t posY = 16;
    Debug::print(22, posY, "Tests - (Event bus)");
    posY += 16;

    for(uint32_t t = 0; t < T_COUNT; ++t)
    {
      const auto res = results[t];
      Debug::printf(22, posY, "%02lu | %s : ", (unsigned long)t, TESTS[t].name);
      const uint16_t posX = 268;

      if(res < 0) {
        Debug::printf(posX, posY, "Pending...");
      } else if(res == 0) {
        Debug::setColor({0xFF, 0x22, 0x22, 0xFF});
        Debug::printf(posX, posY, "FAIL!");
        Debug::setColor();
      } else {
        Debug::setColor({0x22, 0xFF, 0x22, 0xFF});
        Debug::printf(posX, posY, "OK");
        Debug::setColor();
      }
      posY += 9;
    }

    Debug::isMonospace = false;
  }
}
