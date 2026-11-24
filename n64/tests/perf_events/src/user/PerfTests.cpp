#include "script/globalScript.h"
#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "debug/debugDraw.h"
#include "./perf.h"

// Benchmark of the event bus and the conditions. Each test runs in one frame, straight from this
// hook: it produces events (post, fire, send) and then calls the bus's drain itself, timing both
// with the CPU's cycle counter. Results go to the debug log ("[perf] ...") and to the screen.
// See README.md in this folder for how to run it and how to read the numbers.

constinit uint32_t P64::User::Perf::sinkCalls = 0;
constinit uint32_t P64::User::Perf::codeCalls = 0;
constinit uint32_t P64::User::Perf::evaluations = 0;
constinit bool P64::User::Perf::flag = false;
constinit P64::User::Perf::ScriptRef P64::User::Perf::source{};
constinit P64::User::Perf::ScriptRef P64::User::Perf::sourceBusy{};
constinit uint16_t P64::User::Perf::targetId = 0;
constinit uint16_t P64::User::Perf::targetFarId = 0;
constinit uint16_t P64::User::Perf::evalId = 0;

namespace P64::GlobalScript::C5D2B8E4F1A07C94
{
  using namespace P64::User::Perf;
  using Events::Internal::DrainPoint;

  P64_GLOBAL_EVENTS_OUT(Perf, Nobody, One, Four, Sixteen);

  // the same numbers as gen_scene.py
  constexpr uint32_t FAN = 64, CONDS_A = 64, CONDS_B = 32, CONDS_C = 32, ANDS = 16, CONDS_L = 8, ANDS_L = 4;

  constexpr uint32_t BATCH = 100;           // events per drain in the micro tests (the queue holds 128)
  constexpr uint32_t FRAME_60_US = 16667;   // one frame at 60 fps
  constexpr uint32_t FRAME_30_US = 33333;   // one frame at 30 fps

  struct Result
  {
    const char* name;
    const char* unit;  // what 'units' counts
    uint32_t units;
    uint64_t produce;  // ticks spent posting, firing or sending
    uint64_t deliver;  // ticks spent in the drain
    bool ok;           // the calls that had to happen did
  };

  constexpr uint32_t MAX_RESULTS = 40;
  constinit Result results[MAX_RESULTS]{};
  constinit uint32_t resultCount = 0;
  constinit uint32_t frame = 0;
  constinit uint32_t nextTest = 0;
  constinit bool done = false;

  void drain() { Events::Internal::drain(DrainPoint::AfterUpdate); }

  // 46.875 MHz ticks: 64/3 ns each
  uint64_t toNs(uint64_t ticks) { return ticks * 64 / 3; }
  uint64_t toUs(uint64_t ticks) { return toNs(ticks) / 1000; }

  void onCode([[maybe_unused]] const Events::Event& ev, [[maybe_unused]] void* user) { ++codeCalls; }

  // 'reps' times: produce, then drain, timed apart. '*counter' must grow by callsPerRep each time.
  template<typename Produce>
  Result run(const char* name, const char* unit, uint32_t unitsPerRep, uint32_t reps,
             const uint32_t* counter, uint32_t callsPerRep, Produce produce)
  {
    Result r{name, unit, unitsPerRep * reps, 0, 0, true};
    const uint32_t before = counter ? *counter : 0;
    for(uint32_t rep = 0; rep < reps; ++rep) {
      const uint64_t t0 = get_ticks();
      produce(rep);
      const uint64_t t1 = get_ticks();
      drain();
      const uint64_t t2 = get_ticks();
      r.produce += t1 - t0;
      r.deliver += t2 - t1;
    }
    if(counter) r.ok = (*counter - before) == callsPerRep * reps;
    return r;
  }

  // the same work repeated until 'budgetUs' is used up: how much fits into that time
  template<typename Produce>
  Result fill(const char* name, const char* unit, uint32_t unitsPerRep, uint32_t budgetUs, Produce produce)
  {
    Result r{name, unit, 0, 0, 0, true};
    const uint64_t budget = (uint64_t)budgetUs * TICKS_PER_SECOND / 1000000;
    const uint64_t start = get_ticks();
    uint32_t rep = 0;
    while(get_ticks() - start < budget) {
      const uint64_t t0 = get_ticks();
      produce(rep++);
      const uint64_t t1 = get_ticks();
      drain();
      const uint64_t t2 = get_ticks();
      r.produce += t1 - t0;
      r.deliver += t2 - t1;
      r.units += unitsPerRep;
    }
    return r;
  }

  void postMany(Events::EventId id, uint32_t count)
  {
    for(uint32_t i = 0; i < count; ++i) Events::post(id, i);
  }

  // ---- what one unit of work is, shared by the micro tests and the fill tests

  void oneListener(uint32_t) { postMany("global::Perf.One"_event, BATCH); }
  void boundInput(uint32_t)  { postMany("scene::P.One"_event, BATCH); }
  void checkA(uint32_t)      { flag = !flag; Events::post("scene::P.CheckA"_event); }
  void checkB(uint32_t)      { flag = !flag; Events::post("scene::P.CheckB"_event); }
  void checkC(uint32_t)      { flag = !flag; Events::post("scene::P.CheckC"_event); }
  void checkL(uint32_t)      { flag = !flag; Events::post("scene::P.CheckL"_event); }

  // ---- the tests, one a frame

  Result testEmptyDrain()
  {
    return run("drain with nothing queued", "drains", 1, 1000, nullptr, 0, [](uint32_t) {});
  }

  Result testNobody()
  {
    return run("event nobody listens to", "events", BATCH, 10, nullptr, 0,
      [](uint32_t) { postMany("global::Perf.Nobody"_event, BATCH); });
  }

  Result testOne()     { return run("event to 1 code listener", "events", BATCH, 10, &codeCalls, BATCH, oneListener); }

  Result testFour()
  {
    return run("event to 4 code listeners", "events", BATCH, 10, &codeCalls, BATCH * 4,
      [](uint32_t) { postMany("global::Perf.Four"_event, BATCH); });
  }

  Result testSixteen()
  {
    return run("event to 16 code listeners", "events", BATCH, 10, &codeCalls, BATCH * 16,
      [](uint32_t) { postMany("global::Perf.Sixteen"_event, BATCH); });
  }

  Result testBound()   { return run("event to 1 script input (bound pin)", "events", BATCH, 10, &sinkCalls, BATCH, boundInput); }

  Result testFan()
  {
    return run("event to 64 script inputs", "input calls", 2 * FAN, 10, &sinkCalls, 2 * FAN,
      [](uint32_t) { postMany("scene::P.Fan"_event, 2); });
  }

  Result testFire()
  {
    if(!source.obj) return {"fire(): script output, 1 binding", "fires", 0, 0, 0, false};
    return run("fire(): script output, 1 binding", "fires", BATCH, 10, &sinkCalls, BATCH,
      [](uint32_t) { for(uint32_t i = 0; i < BATCH; ++i) Events::fire(*source.obj, source.data, 0, i); });
  }

  Result testFireBusy()
  {
    if(!sourceBusy.obj) return {"fire(): object with 16 bindings", "fires", 0, 0, 0, false};
    return run("fire(): object with 16 bindings", "fires", BATCH, 10, &sinkCalls, BATCH,
      [](uint32_t) { for(uint32_t i = 0; i < BATCH; ++i) Events::fire(*sourceBusy.obj, sourceBusy.data, 0, i); });
  }

  Result testSend()
  {
    return run("send() by id, object id below 128", "sends", BATCH, 10, &sinkCalls, BATCH,
      [](uint32_t) { for(uint32_t i = 0; i < BATCH; ++i) Events::send(targetId, "Tick"_pin, i, 0); });
  }

  Result testSendFar()
  {
    return run("send() by id, object id above 128", "sends", BATCH, 10, &sinkCalls, BATCH,
      [](uint32_t) { for(uint32_t i = 0; i < BATCH; ++i) Events::send(targetFarId, "Tick"_pin, i, 0); });
  }

  // what finding an object by its id costs: everything that holds an id pays this
  constinit uint32_t found = 0;
  Result testLookupNear()
  {
    return run("getObjectById(), id below 128", "lookups", 1000, 1, &found, 1000,
      [](uint32_t) { auto &scene = SceneManager::getCurrent(); for(uint32_t i = 0; i < 1000; ++i) found += scene.getObjectById(targetId) != nullptr; });
  }

  Result testLookupFar()
  {
    return run("getObjectById(), id above 128", "lookups", 1000, 1, &found, 1000,
      [](uint32_t) { auto &scene = SceneManager::getCurrent(); for(uint32_t i = 0; i < 1000; ++i) found += scene.getObjectById(targetFarId) != nullptr; });
  }

  Result testChain()
  {
    return run("chain through 8 Relay objects", "chains", 10, 10, &codeCalls, 10,
      [](uint32_t) { postMany("scene::P.Chain0"_event, 10); });
  }

  Result testCondA()
  {
    return run("condition check, nothing bound", "checks", CONDS_A, 20, &evaluations, CONDS_A, checkA);
  }

  Result testCondB()
  {
    // every check changes the answer: True or False and NewlyTrue or NewlyFalse reach a listener
    const uint32_t before = codeCalls;
    Result r = run("condition check, firing 2 bound outputs", "checks", CONDS_B, 20, &evaluations, CONDS_B, checkB);
    r.ok = r.ok && (codeCalls - before) == CONDS_B * 20 * 2;
    return r;
  }

  Result testCondC()
  {
    // each And is checked once a wave and reads its two conditions: 32 + 16 * 2 evaluations
    const uint32_t before = codeCalls;
    Result r = run("32 conditions feeding 16 And nodes, ids above 128", "checks", CONDS_C + ANDS, 20,
                   &evaluations, CONDS_C + ANDS * 2, checkC);
    r.ok = r.ok && (codeCalls - before) == ANDS * 20;
    return r;
  }

  Result testCondL()
  {
    // the same wiring as above with object ids below 128
    const uint32_t before = codeCalls;
    Result r = run("8 conditions feeding 4 And nodes, ids below 128", "checks", CONDS_L + ANDS_L, 20,
                   &evaluations, CONDS_L + ANDS_L * 2, checkL);
    r.ok = r.ok && (codeCalls - before) == ANDS_L * 20;
    return r;
  }

  Result testEvaluate()
  {
    Object* obj = SceneManager::getCurrent().getObjectById(evalId);
    if(!obj) return {"Conditions::evaluate() from code", "calls", 0, 0, 0, false};
    static Object* target;
    target = obj;
    return run("Conditions::evaluate() from code", "calls", 1000, 1, &evaluations, 1000,
      [](uint32_t) { for(uint32_t i = 0; i < 1000; ++i) Conditions::evaluate(*target); });
  }

  Result testCheck()
  {
    Object* obj = SceneManager::getCurrent().getObjectById(evalId);
    if(!obj) return {"Conditions::check() from code", "calls", 0, 0, 0, false};
    static Object* target;
    target = obj;
    return run("Conditions::check() from code", "calls", 1000, 1, &evaluations, 1000,
      [](uint32_t) { for(uint32_t i = 0; i < 1000; ++i) { flag = !flag; Conditions::check(*target); } });
  }

  // ---- limits: what the bus refuses with its default sizes

  Result testQueueCap()
  {
    // more events at once than the queue holds: the rest is dropped (and the bus warns)
    const auto limits = Events::getLimits();
    const auto before = Events::getStats();
    const uint32_t callsBefore = codeCalls;
    const uint32_t count = limits.queueSize + 172;
    Result r = run("posting more at once than the queue holds", "events accepted", 0, 1, nullptr, 0,
      [count](uint32_t) { postMany("global::Perf.One"_event, count); });
    const auto after = Events::getStats();
    r.units = codeCalls - callsBefore;
    r.ok = r.units == limits.queueSize && (after.droppedQueueFull - before.droppedQueueFull) == 172;
    return r;
  }

  // ---- how much fits into a frame

  Result fillOne60()   { return fill("60 fps frame of: event to 1 code listener", "events", BATCH, FRAME_60_US, oneListener); }
  Result fillOne30()   { return fill("30 fps frame of: event to 1 code listener", "events", BATCH, FRAME_30_US, oneListener); }
  Result fillBound60() { return fill("60 fps frame of: event to 1 script input", "events", BATCH, FRAME_60_US, boundInput); }
  Result fillBound30() { return fill("30 fps frame of: event to 1 script input", "events", BATCH, FRAME_30_US, boundInput); }
  Result fillCondA60() { return fill("60 fps frame of: condition check, nothing bound", "checks", CONDS_A, FRAME_60_US, checkA); }
  Result fillCondA30() { return fill("30 fps frame of: condition check, nothing bound", "checks", CONDS_A, FRAME_30_US, checkA); }
  Result fillCondB60() { return fill("60 fps frame of: condition check, 2 outputs fire", "checks", CONDS_B, FRAME_60_US, checkB); }
  Result fillCondB30() { return fill("30 fps frame of: condition check, 2 outputs fire", "checks", CONDS_B, FRAME_30_US, checkB); }
  Result fillCondC60() { return fill("60 fps frame of: conditions and And nodes", "checks", CONDS_C + ANDS, FRAME_60_US, checkC); }
  Result fillCondC30() { return fill("30 fps frame of: conditions and And nodes", "checks", CONDS_C + ANDS, FRAME_30_US, checkC); }

  // With raised limits (see README.md): one drain that takes a whole frame, to show that the
  // per-event cost holds when the events are not spread over many drains.
  Result testOneBigDrain()
  {
    const auto limits = Events::getLimits();
    if(limits.queueSize < 8192) return {"one drain of a 60 fps frame (needs raised limits)", "events", 0, 0, 0, true};

    // the count that filled a 60 fps frame in fillOne60()
    uint32_t count = 0;
    for(uint32_t i = 0; i < resultCount; ++i) {
      if(results[i].name[0] == '6' && results[i].unit[0] == 'e' && count == 0) count = results[i].units;
    }
    if(count == 0 || count > limits.queueSize) count = limits.queueSize;

    const auto before = Events::getStats();
    static uint32_t posted;
    posted = count;
    Result r = run("one drain of a 60 fps frame of events", "events", count, 1, &codeCalls, count,
      [](uint32_t) { postMany("global::Perf.One"_event, posted); });
    const auto after = Events::getStats();
    r.ok = r.ok && after.droppedQueueFull == before.droppedQueueFull && after.budgetHits == before.budgetHits;
    return r;
  }

  using TestFn = Result(*)();
  // The micro tests run PASSES times, each in a frame of its own, and the fastest run counts: an
  // interrupt (audio, video) that lands inside a timed stretch would otherwise be measured too.
  constexpr TestFn MICRO[] = {
    testEmptyDrain, testNobody, testOne, testFour, testSixteen, testBound, testFan,
    testFire, testFireBusy, testSend, testSendFar, testLookupNear, testLookupFar, testChain,
    testCondA, testCondB, testCondL, testCondC, testEvaluate, testCheck,
  };
  constexpr TestFn ONCE[] = {
    testQueueCap,
    fillOne60, fillOne30, fillBound60, fillBound30,
    fillCondA60, fillCondA30, fillCondB60, fillCondB30, fillCondC60, fillCondC30,
    testOneBigDrain,
  };
  constexpr uint32_t PASSES = 3;
  constexpr uint32_t MICRO_COUNT = sizeof(MICRO) / sizeof(MICRO[0]);
  constexpr uint32_t ONCE_COUNT = sizeof(ONCE) / sizeof(ONCE[0]);
  constexpr uint32_t TEST_COUNT = MICRO_COUNT + ONCE_COUNT;
  static_assert(TEST_COUNT <= MAX_RESULTS);
  constinit uint32_t pass = 0;

  void printResult(const Result &r)
  {
    const uint64_t total = r.produce + r.deliver;
    const uint64_t each = r.units ? toNs(total) / r.units : 0;
    debugf("[perf] %-46s | %6lu %-15s | produce %6llu us | deliver %6llu us | %6llu ns each | %s\n",
      r.name, (unsigned long)r.units, r.unit, (unsigned long long)toUs(r.produce),
      (unsigned long long)toUs(r.deliver), (unsigned long long)each, r.ok ? "ok" : "CHECK FAILED");
  }

  // ---- hooks

  void onGameInit()
  {
    Events::listen("global::Perf.One"_event, onCode);
    for(uint32_t i = 0; i < 4; ++i)  Events::listen("global::Perf.Four"_event, onCode);
    for(uint32_t i = 0; i < 16; ++i) Events::listen("global::Perf.Sixteen"_event, onCode);

    Events::listen("scene::P.B.True"_event, onCode);
    Events::listen("scene::P.B.False"_event, onCode);
    Events::listen("scene::P.B.NewlyTrue"_event, onCode);
    Events::listen("scene::P.B.NewlyFalse"_event, onCode);
    Events::listen("scene::P.And.NewlyTrue"_event, onCode);
    Events::listen("scene::P.And.NewlyFalse"_event, onCode);
    Events::listen("scene::P.AndL.NewlyTrue"_event, onCode);
    Events::listen("scene::P.AndL.NewlyFalse"_event, onCode);
    Events::listen("scene::P.Chain8"_event, onCode);
  }

  void onSceneUpdate()
  {
    if(done) return;
    ++frame;
    if(frame < 10) return; // let the scene settle

    if(frame == 10) {
      const auto limits = Events::getLimits();
      const auto stats = Events::getStats();
      const auto conditions = Conditions::getStats();
      debugf("[perf] limits: queue %lu, drain budget %lu, subscriptions %lu (in use %u), condition providers %lu (in use %lu)\n",
        (unsigned long)limits.queueSize, (unsigned long)limits.drainBudget, (unsigned long)limits.maxSubscriptions,
        (unsigned)stats.subscriptions, (unsigned long)conditions.maxProviders, (unsigned long)conditions.providers);
      debugf("[perf] objects in the scene: %lu\n", (unsigned long)SceneManager::getCurrent().getObjectCount());
      return;
    }

    if(nextTest < MICRO_COUNT) {
      const Result r = MICRO[nextTest]();
      Result &best = results[nextTest];
      const bool faster = (r.produce + r.deliver) < (best.produce + best.deliver);
      if(pass == 0 || !r.ok || (best.ok && faster)) best = r;
      if(++pass < PASSES) return;
      pass = 0;
      printResult(best);
      resultCount = ++nextTest;
      return;
    }
    if(nextTest < TEST_COUNT) {
      results[resultCount] = ONCE[nextTest++ - MICRO_COUNT]();
      printResult(results[resultCount++]);
      return;
    }

    const auto stats = Events::getStats();
    debugf("[perf] totals: %lu events delivered, %lu listener calls, queue peak %u, dropped (queue full) %lu, budget hits %lu\n",
      (unsigned long)stats.delivered, (unsigned long)stats.listenerCalls, (unsigned)stats.queuePeak,
      (unsigned long)stats.droppedQueueFull, (unsigned long)stats.budgetHits);
    debugf("[perf] DONE\n");
    done = true;
  }

  void onSceneDraw2D()
  {
    // Two pages, because not every result fits on one: the micro tests (ns each), then what fits
    // into a frame (a count). While the tests run the page follows them; afterwards it alternates.
    static uint32_t drawn = 0;
    ++drawn;
    const bool second = done ? ((drawn / 240) % 2) == 1 : resultCount > MICRO_COUNT;
    const uint32_t from = second ? MICRO_COUNT : 0;
    const uint32_t to = second ? resultCount : (resultCount < MICRO_COUNT ? resultCount : MICRO_COUNT);

    Debug::printStart();
    Debug::isMonospace = true; // 7 pixels a character, so the columns line up
    int16_t posY = 12;
    Debug::printf(12, posY, "%s  %s", second ? "How much fits into a frame" : "Events, conditions: ns each",
      done ? (second ? "DONE 2/2" : "DONE 1/2") : "running");
    posY += 11;
    for(uint32_t i = from; i < to; ++i)
    {
      const auto &r = results[i];
      const uint64_t each = r.units ? toNs(r.produce + r.deliver) / r.units : 0;
      if(!r.ok) Debug::setColor({0xFF, 0x44, 0x44, 0xFF});
      Debug::printf(12, posY, "%.31s", r.name);
      Debug::printf(236, posY, "%7lu", (unsigned long)(second ? r.units : each));
      if(!r.ok) Debug::setColor();
      posY += 9;
    }
    Debug::isMonospace = false;
  }
}
