#include "script/globalScript.h"
#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "debug/debugDraw.h"
#include "./testLog.h"

#include <vector>

// Runtime test of the event bus (phase 1 of docs/todo/event-graph/PLAN.md).
// Each test runs in a freshly loaded scene for a number of frames, then checks what the
// listeners recorded. Results are printed through debugf and drawn on screen.

constinit uint32_t P64::User::EventTest::currFrame = 0;
constinit uint32_t P64::User::EventTest::activeTest = 0;
constinit std::vector<P64::User::EventTest::Record> P64::User::EventTest::records{};

namespace P64::GlobalScript::C5D2B8E4F1A07C93
{
  using namespace P64::User::EventTest;
  namespace Builtin = P64::Events::Builtin;

  constexpr uint32_t ANY = 0xFFFFFFFF;
  constexpr uint16_t PROBE_ID = 2; // runtime id of the Probe object in scenes 1 and 2
  constexpr uint16_t TRIGGER_ID = 2, DOOR_A = 3, DOOR_B = 4; // scene 3

  // Phase 2: this global script is a system with an input pin and declares the test events
  P64_GLOBAL_EVENTS_IN(EventTest, DoorOpened);
  P64_GLOBAL_EVENTS_OUT(Test, Ping, Pong, FromFixed, FromUpdate, Loop, Burst, Carried, Irq);
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
    // frame 3: send() by input name, frame 4: Exit reaches door A only
    e.push_back({Rec::Knock, 3, 5, TRIGGER_ID, TRIGGER_ID, DOOR_A});
    e.push_back({Rec::Close, 4, 0, TRIGGER_ID, TRIGGER_ID, DOOR_A});
    return expectExactly(e);
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
  }

  void onScenePreLoad()
  {
    currFrame = 0;
  }

  void onSceneUpdate()
  {
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
