/**
* @license MIT
*/
// Fork: event bus (docs/todo/event-graph/PLAN.md). Names and ids, and the bus core rules:
// order, scope isolation by instance, hop limit, drain budget, changes to listeners during a
// delivery, fixed-size limits, the interrupt mailbox and scene teardown.
#include "doctest/doctest.h"
#include <cstring>
#include <memory>
#include <vector>

#include "events/types.h"
#include "events/busCore.h"
#include "utils/hash.h"

using namespace P64::Events;
using Core::BusCore;
using Core::Handle;
using Core::Warning;

namespace
{
  ParsedName parse(const char* name) {
    return parseName(name, std::strlen(name));
  }

  struct Call
  {
    int listener;
    Event ev;
  };

  // shared by every listener of one test
  struct Log
  {
    std::vector<Call> calls{};
    std::vector<Warning> warnings{};
    BusCore* bus{};
  };

  // what a test listener does when called
  struct Listener
  {
    int id{};
    Log* log{};
    uint32_t repostHash{};     // post this hash again (same scope, instance 0)
    uint16_t repostSender{};
    Handle unsubscribeOther{}; // unsubscribe this handle when called
    bool unsubscribeAllOthers{};
    std::vector<Handle>* others{};
    bool subscribeOnce{};      // subscribe 'extra' to the same event once
    Listener* extra{};
    Handle self{};
    bool unsubscribeSelf{};
  };

  void onEvent(const Event& ev, void* user)
  {
    auto &l = *static_cast<Listener*>(user);
    l.log->calls.push_back({l.id, ev});
    auto &bus = *l.log->bus;

    if(l.repostHash) bus.post(l.repostHash, Scope::Global, 0, ev.value + 1, l.repostSender);
    if(l.unsubscribeOther.isValid()) bus.unsubscribe(l.unsubscribeOther);
    if(l.unsubscribeAllOthers && l.others) {
      for(auto h : *l.others) bus.unsubscribe(h);
    }
    if(l.subscribeOnce && l.extra) {
      l.subscribeOnce = false;
      l.extra->self = bus.subscribe(ev.id, 0, onEvent, l.extra);
    }
    if(l.unsubscribeSelf) bus.unsubscribe(l.self);
  }

  void onWarning(Warning warning, [[maybe_unused]] uint32_t hash, void* user)
  {
    static_cast<Log*>(user)->warnings.push_back(warning);
  }

  struct Fixture
  {
    std::unique_ptr<BusCore> bus = std::make_unique<BusCore>();
    Log log{};

    Fixture() {
      log.bus = bus.get();
      bus->setWarnHandler(onWarning, &log);
    }

    Listener make(int id) {
      Listener l{};
      l.id = id;
      l.log = &log;
      return l;
    }

    bool hasWarning(Warning warning) const {
      for(auto w : log.warnings) if(w == warning) return true;
      return false;
    }
  };

  constexpr uint32_t A = "global::Test.A"_event.hash;
  constexpr uint32_t B = "global::Test.B"_event.hash;
}

TEST_SUITE("event names")
{
  TEST_CASE("the hash is the CRC-32 the editor and Python compute") {
    CHECK(("scene::Loaded"_event).hash == Utils::Hash::crc32("scene::Loaded"));
    CHECK(("global::Engine.GameInit"_event).hash == Utils::Hash::crc32("global::Engine.GameInit"));
    CHECK(("mission::Started"_event).hash == Utils::Hash::crc32("mission::Started"));
    // values from Python's zlib.crc32, which a Python tool (such as an AI script compiler) would use
    CHECK(("scene::Loaded"_event).hash == 0x4a287479u);
    CHECK(("global::Engine.GameInit"_event).hash == 0x8ce7ae4eu);
    CHECK(("mission::Started"_event).hash == 0x896025b6u);
  }

  TEST_CASE("the namespace picks the scope") {
    CHECK(("global::Save.Requested"_event).scope == Scope::Global);
    CHECK(("scene::Loaded"_event).scope == Scope::Scene);
    CHECK(("prefab::Door.Open"_event).scope == Scope::Prefab);
    CHECK(("object::Hit"_event).scope == Scope::Object);
    CHECK(("ui::Hud.Show"_event).scope == Scope::UI);
    // namespaces a game declares behave like global
    CHECK(("mission::Started"_event).scope == Scope::Global);
    CHECK(("prefabs::X"_event).scope == Scope::Global);
  }

  TEST_CASE("numbered prefab namespaces are the same event, further up") {
    constexpr auto local = "prefab::Alarm"_event;
    constexpr auto zero = "prefab_0::Alarm"_event;
    constexpr auto parent = "prefab_1::Alarm"_event;
    constexpr auto far = "prefab_12::Alarm"_event;

    CHECK(local.hash == Utils::Hash::crc32("prefab::Alarm"));
    CHECK(local.hash == 0x83d31912u);
    CHECK(zero.hash == local.hash);
    CHECK(parent.hash == local.hash);
    CHECK(far.hash == local.hash);

    CHECK(local.up == 0);
    CHECK(zero.up == 0);
    CHECK(parent.up == 1);
    CHECK(far.up == 12);
    CHECK(parent.scope == Scope::Prefab);
    CHECK(("prefab_255::X"_event).up == 255);
  }

  TEST_CASE("valid names") {
    CHECK(parse("scene::Loaded").error == NameError::None);
    CHECK(parse("prefab::TriggerZone.Enter").error == NameError::None);
    CHECK(parse("scene::Step1").error == NameError::None);
    CHECK(parse("scene::a.b.c").error == NameError::None);
    CHECK(parse("my_game2::Thing_3").error == NameError::None);
    CHECK(parse("global::LevelDriver.TransitionStarted").error == NameError::None);
  }

  TEST_CASE("malformed names are rejected with the reason") {
    CHECK(parse("Loaded").error == NameError::MissingSeparator);
    CHECK(parse("scene:Loaded").error == NameError::MissingSeparator);
    CHECK(parse("::Loaded").error == NameError::BadNamespace);
    CHECK(parse("Scene::Loaded").error == NameError::BadNamespace);
    CHECK(parse("1scene::Loaded").error == NameError::BadNamespace);
    CHECK(parse("sce-ne::Loaded").error == NameError::BadNamespace);
    CHECK(parse("scene::").error == NameError::BadName);
    CHECK(parse("scene::.Loaded").error == NameError::BadName);
    CHECK(parse("scene::Loaded.").error == NameError::BadName);
    CHECK(parse("scene::Lo..aded").error == NameError::BadName);
    CHECK(parse("scene::Lo aded").error == NameError::BadName);
    CHECK(parse("scene::Loaded::Again").error == NameError::BadName);
    CHECK(parse("prefab_::X").error == NameError::BadPrefabNumber);
    CHECK(parse("prefab_01::X").error == NameError::BadPrefabNumber);
    CHECK(parse("prefab_256::X").error == NameError::BadPrefabNumber);
    CHECK(parse("prefab_1a::X").error == NameError::BadPrefabNumber);
  }

  TEST_CASE("global names need a system") {
    CHECK(parse("global::SaveRequested").error == NameError::GlobalNeedsSystem);
    CHECK(parse("global::Save.Requested.Now").error == NameError::GlobalNeedsSystem);
    CHECK(parse("global::1Save.Requested").error == NameError::GlobalNeedsSystem);
    CHECK(parse("global::Save.2nd").error == NameError::GlobalNeedsSystem);
    CHECK(parse("global::Save.Requested").error == NameError::None);
  }
}

TEST_SUITE("event bus core")
{
  TEST_CASE("events are delivered in posting order, listeners in subscribing order") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    f.bus->subscribe(A, 0, onEvent, &l1);
    f.bus->subscribe(A, 0, onEvent, &l2);

    f.bus->post(A, Scope::Global, 0, 10, 0);
    f.bus->post(A, Scope::Global, 0, 20, 0);
    CHECK(f.log.calls.empty()); // queued, nothing happens before a drain

    CHECK(f.bus->drain() == 2);
    REQUIRE(f.log.calls.size() == 4);
    CHECK(f.log.calls[0].listener == 1); CHECK(f.log.calls[0].ev.value == 10);
    CHECK(f.log.calls[1].listener == 2); CHECK(f.log.calls[1].ev.value == 10);
    CHECK(f.log.calls[2].listener == 1); CHECK(f.log.calls[2].ev.value == 20);
    CHECK(f.log.calls[3].listener == 2); CHECK(f.log.calls[3].ev.value == 20);
  }

  TEST_CASE("listeners only hear their own event") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    f.bus->subscribe(A, 0, onEvent, &l1);
    f.bus->subscribe(B, 0, onEvent, &l2);
    f.bus->post(B, Scope::Global, 0, 0, 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 1);
    CHECK(f.log.calls[0].listener == 2);
    CHECK(f.log.calls[0].ev.id == B);
  }

  TEST_CASE("scope instances are isolated") {
    Fixture f;
    const uint32_t open = "prefab::Door.Open"_event.hash;
    auto door5 = f.make(5), door6 = f.make(6);
    f.bus->subscribe(open, 5, onEvent, &door5);
    f.bus->subscribe(open, 6, onEvent, &door6);

    f.bus->post(open, Scope::Prefab, 5, 0, 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 1);
    CHECK(f.log.calls[0].listener == 5);

    f.bus->post(open, Scope::Prefab, 7, 0, 0); // an instance nobody listens in
    f.bus->drain();
    CHECK(f.log.calls.size() == 1);
  }

  TEST_CASE("events fired by listeners are delivered in the same drain") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    l1.repostHash = B;
    f.bus->subscribe(A, 0, onEvent, &l1);
    f.bus->subscribe(B, 0, onEvent, &l2);

    f.bus->post(A, Scope::Global, 0, 1, 0);
    CHECK(f.bus->drain() == 2);
    REQUIRE(f.log.calls.size() == 2);
    CHECK(f.log.calls[1].listener == 2);
    CHECK(f.log.calls[1].ev.value == 2);
    CHECK(f.bus->queuedCount() == 0);
  }

  TEST_CASE("a listener sending to itself stops at the hop limit") {
    Fixture f;
    auto loop = f.make(1);
    loop.repostHash = A;
    f.bus->subscribe(A, 0, onEvent, &loop);

    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->drain();

    // hops 0 to MAX_HOPS are delivered, the next post is dropped
    CHECK(f.log.calls.size() == BusCore::MAX_HOPS + 1);
    CHECK(f.log.calls.back().ev.value == BusCore::MAX_HOPS);
    CHECK(f.bus->getStats().droppedHopLimit == 1);
    CHECK(f.hasWarning(Warning::HopLimit));
    CHECK(f.bus->queuedCount() == 0);

    // a new chain starts at hop 0 again
    f.log.calls.clear();
    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->drain();
    CHECK(f.log.calls.size() == BusCore::MAX_HOPS + 1);
  }

  TEST_CASE("the instigator is carried along the chain") {
    Fixture f;
    auto first = f.make(1), second = f.make(2);
    first.repostHash = B;
    first.repostSender = 9;
    f.bus->subscribe(A, 0, onEvent, &first);
    f.bus->subscribe(B, 0, onEvent, &second);

    f.bus->post(A, Scope::Global, 0, 0, 7); // sender 7 starts the chain
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 2);
    CHECK(f.log.calls[0].ev.sender == 7);
    CHECK(f.log.calls[0].ev.instigator == 7);
    CHECK(f.log.calls[1].ev.sender == 9);
    CHECK(f.log.calls[1].ev.instigator == 7);
  }

  TEST_CASE("an explicit instigator wins") {
    Fixture f;
    auto l = f.make(1);
    f.bus->subscribe(A, 0, onEvent, &l);
    f.bus->post(A, Scope::Global, 0, 0, 3, 42); // a trigger (3) reports who walked in (42)
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 1);
    CHECK(f.log.calls[0].ev.sender == 3);
    CHECK(f.log.calls[0].ev.instigator == 42);
  }

  TEST_CASE("a drain stops at its budget and the rest waits for the next one") {
    Fixture f;
    auto l = f.make(1);
    f.bus->subscribe(A, 0, onEvent, &l);
    for(uint32_t i = 0; i < 100; ++i) f.bus->post(A, Scope::Global, 0, i, 0);

    CHECK(f.bus->drain(30) == 30);
    CHECK(f.bus->queuedCount() == 70);
    CHECK(f.bus->getStats().budgetHits == 1);
    CHECK(f.hasWarning(Warning::DrainBudget));

    CHECK(f.bus->drain(30) == 30);
    CHECK(f.bus->drain(100) == 40);
    CHECK(f.bus->queuedCount() == 0);
    REQUIRE(f.log.calls.size() == 100);
    for(uint32_t i = 0; i < 100; ++i) CHECK(f.log.calls[i].ev.value == i); // order kept across drains
    CHECK(f.bus->getStats().budgetHits == 2);
  }

  TEST_CASE("unsubscribing a later listener during a delivery skips it") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    f.bus->subscribe(A, 0, onEvent, &l1);
    Handle h2 = f.bus->subscribe(A, 0, onEvent, &l2);
    l1.unsubscribeOther = h2;

    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 2);
    CHECK(f.log.calls[0].listener == 1);
    CHECK(f.log.calls[1].listener == 1);
    CHECK(f.bus->listenerCount(A, 0) == 1);
    CHECK(f.bus->getStats().subscriptions == 1);
  }

  TEST_CASE("a listener can unsubscribe itself") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    l1.self = f.bus->subscribe(A, 0, onEvent, &l1);
    l1.unsubscribeSelf = true;
    f.bus->subscribe(A, 0, onEvent, &l2);

    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 3);
    CHECK(f.log.calls[0].listener == 1);
    CHECK(f.log.calls[1].listener == 2);
    CHECK(f.log.calls[2].listener == 2);
  }

  TEST_CASE("a listener added during a delivery hears the next event, not the current one") {
    Fixture f;
    auto l1 = f.make(1), l3 = f.make(3);
    l1.subscribeOnce = true;
    l1.extra = &l3;
    f.bus->subscribe(A, 0, onEvent, &l1);

    f.bus->post(A, Scope::Global, 0, 1, 0);
    f.bus->post(A, Scope::Global, 0, 2, 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 3);
    CHECK(f.log.calls[0].listener == 1); CHECK(f.log.calls[0].ev.value == 1);
    CHECK(f.log.calls[1].listener == 1); CHECK(f.log.calls[1].ev.value == 2);
    CHECK(f.log.calls[2].listener == 3); CHECK(f.log.calls[2].ev.value == 2);
  }

  TEST_CASE("many unsubscribes during one delivery are all cleaned up") {
    Fixture f;
    constexpr int COUNT = BusCore::PENDING_DEAD + 8; // more than the pending list holds
    auto first = f.make(0);
    std::vector<Listener> rest(COUNT);
    std::vector<Handle> handles;
    f.bus->subscribe(A, 0, onEvent, &first);
    for(int i = 0; i < COUNT; ++i) {
      rest[i] = f.make(i + 1);
      handles.push_back(f.bus->subscribe(A, 0, onEvent, &rest[i]));
    }
    first.unsubscribeAllOthers = true;
    first.others = &handles;

    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == 1);
    CHECK(f.bus->listenerCount(A, 0) == 1);
    CHECK(f.bus->getStats().subscriptions == 1);

    // every freed slot can be used again
    std::vector<Listener> again(BusCore::MAX_SUBS - 1);
    for(auto &l : again) {
      l = f.make(99);
      CHECK(f.bus->subscribe(B, 0, onEvent, &l).isValid());
    }
    CHECK(f.bus->getStats().subscriptions == BusCore::MAX_SUBS);
  }

  TEST_CASE("stale handles do nothing") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    Handle h1 = f.bus->subscribe(A, 0, onEvent, &l1);
    CHECK(f.bus->unsubscribe(h1));
    CHECK_FALSE(f.bus->unsubscribe(h1));

    Handle h2 = f.bus->subscribe(A, 0, onEvent, &l2); // reuses the freed slot
    CHECK(h2.slot == h1.slot);
    CHECK(h2.gen != h1.gen);
    CHECK_FALSE(f.bus->unsubscribe(h1));
    CHECK(f.bus->listenerCount(A, 0) == 1);
    CHECK_FALSE(f.bus->unsubscribe(Handle{}));
  }

  TEST_CASE("the queue drops events when full") {
    Fixture f;
    for(uint32_t i = 0; i < BusCore::QUEUE_SIZE; ++i) {
      CHECK(f.bus->post(A, Scope::Global, 0, i, 0));
    }
    CHECK_FALSE(f.bus->post(A, Scope::Global, 0, 0, 0));
    CHECK(f.bus->getStats().droppedQueueFull == 1);
    CHECK(f.bus->getStats().queuePeak == BusCore::QUEUE_SIZE);
    CHECK(f.hasWarning(Warning::QueueFull));
    CHECK(f.bus->drain(BusCore::QUEUE_SIZE) == BusCore::QUEUE_SIZE);
  }

  TEST_CASE("subscriptions are limited") {
    Fixture f;
    std::vector<Listener> ls(BusCore::MAX_SUBS + 1);
    std::vector<Handle> handles;
    for(uint32_t i = 0; i < BusCore::MAX_SUBS; ++i) {
      ls[i] = f.make((int)i);
      handles.push_back(f.bus->subscribe(A, 0, onEvent, &ls[i]));
      CHECK(handles.back().isValid());
    }
    ls.back() = f.make(-1);
    CHECK_FALSE(f.bus->subscribe(A, 0, onEvent, &ls.back()).isValid());
    CHECK(f.bus->getStats().subscribeFailed == 1);
    CHECK(f.hasWarning(Warning::PoolFull));

    f.bus->unsubscribe(handles[10]);
    CHECK(f.bus->subscribe(A, 0, onEvent, &ls.back()).isValid());
  }

  TEST_CASE("listeners keyed to objects are removed per object and all at once") {
    Fixture f;
    const uint32_t hit = "object::Hit"_event.hash;
    auto global = f.make(0), obj3 = f.make(3), obj4 = f.make(4);
    f.bus->subscribe(A, 0, onEvent, &global);
    f.bus->subscribe(hit, 3, onEvent, &obj3);
    f.bus->subscribe(hit, 4, onEvent, &obj4);

    CHECK(f.bus->unsubscribeInstance(0) == 0); // instance 0 is never an object
    CHECK(f.bus->unsubscribeInstance(3) == 1);
    CHECK(f.bus->listenerCount(hit, 3) == 0);
    CHECK(f.bus->listenerCount(hit, 4) == 1);

    CHECK(f.bus->unsubscribeAllInstances() == 1);
    CHECK(f.bus->listenerCount(hit, 4) == 0);
    CHECK(f.bus->listenerCount(A, 0) == 1); // code listeners stay
  }

  TEST_CASE("scene teardown drops scene, prefab and object events and keeps the rest in order") {
    Fixture f;
    const uint32_t sceneEv = "scene::Loaded"_event.hash;
    const uint32_t uiEv = "ui::Hud.Show"_event.hash;
    const uint32_t objEv = "object::Hit"_event.hash;
    auto l = f.make(1), lUi = f.make(2), lScene = f.make(3);
    f.bus->subscribe(A, 0, onEvent, &l);
    f.bus->subscribe(uiEv, 0, onEvent, &lUi);
    f.bus->subscribe(sceneEv, 0, onEvent, &lScene);

    f.bus->post(sceneEv, Scope::Scene, 0, 0, 0);
    f.bus->post(A, Scope::Global, 0, 1, 0);
    f.bus->post(objEv, Scope::Object, 3, 0, 0);
    f.bus->post(uiEv, Scope::UI, 0, 2, 0);
    f.bus->post(A, Scope::Global, 0, 3, 0);

    CHECK(f.bus->dropQueuedSceneEvents() == 2);
    CHECK(f.bus->drain() == 3);
    REQUIRE(f.log.calls.size() == 3);
    CHECK(f.log.calls[0].ev.value == 1);
    CHECK(f.log.calls[1].ev.value == 2);
    CHECK(f.log.calls[2].ev.value == 3);
  }

  TEST_CASE("the interrupt mailbox holds a fixed number of events") {
    Fixture f;
    auto l = f.make(1);
    f.bus->subscribe(A, 0, onEvent, &l);
    for(uint32_t i = 0; i < BusCore::MAILBOX_SIZE; ++i) CHECK(f.bus->mailboxPush(A, i));
    CHECK_FALSE(f.bus->mailboxPush(A, 99));
    CHECK(f.bus->getStats().droppedMailbox == 1);

    CHECK(f.log.calls.empty());
    CHECK(f.bus->mailboxFlush() == BusCore::MAILBOX_SIZE);
    CHECK(f.bus->mailboxFlush() == 0);
    f.bus->drain();
    REQUIRE(f.log.calls.size() == BusCore::MAILBOX_SIZE);
    CHECK(f.log.calls[0].ev.sender == 0);
    CHECK(f.log.calls.back().ev.value == BusCore::MAILBOX_SIZE - 1);
  }

  TEST_CASE("deliverNow calls listeners without queueing, and their posts are queued") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    l1.repostHash = B;
    f.bus->subscribe(A, 0, onEvent, &l1);
    f.bus->subscribe(B, 0, onEvent, &l2);

    CHECK(f.bus->deliverNow(A, Scope::Global, 0, 5, 0));
    REQUIRE(f.log.calls.size() == 1);
    CHECK(f.log.calls[0].listener == 1);
    CHECK(f.bus->queuedCount() == 1); // B waits for the next drain

    f.bus->drain();
    REQUIRE(f.log.calls.size() == 2);
    CHECK(f.log.calls[1].listener == 2);
  }

  TEST_CASE("a drain cannot run inside a delivery") {
    Fixture f;
    struct Nested { BusCore* bus; uint32_t result{99}; } nested{f.bus.get()};
    auto fn = [](const Event&, void* user) {
      auto &n = *static_cast<Nested*>(user);
      n.result = n.bus->drain();
    };
    f.bus->subscribe(A, 0, fn, &nested);
    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->post(A, Scope::Global, 0, 0, 0);
    CHECK(f.bus->drain() == 2);
    CHECK(nested.result == 0);
  }

  TEST_CASE("stats count what happened") {
    Fixture f;
    auto l1 = f.make(1), l2 = f.make(2);
    f.bus->subscribe(A, 0, onEvent, &l1);
    f.bus->subscribe(A, 0, onEvent, &l2);
    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->post(B, Scope::Global, 0, 0, 0); // nobody listens
    f.bus->drain();
    auto stats = f.bus->getStats();
    CHECK(stats.posted == 2);
    CHECK(stats.delivered == 2);
    CHECK(stats.listenerCalls == 2);
    CHECK(stats.queued == 0);
    CHECK(stats.subscriptions == 2);
    CHECK(stats.subscriptionsPeak == 2);
  }

  TEST_CASE("reset clears everything") {
    Fixture f;
    auto l = f.make(1);
    f.bus->subscribe(A, 0, onEvent, &l);
    f.bus->post(A, Scope::Global, 0, 0, 0);
    f.bus->reset();
    CHECK(f.bus->queuedCount() == 0);
    CHECK(f.bus->listenerCount(A, 0) == 0);
    CHECK(f.bus->drain() == 0);
    CHECK(f.log.calls.empty());
  }
}
