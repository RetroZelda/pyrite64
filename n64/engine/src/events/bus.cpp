/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: the event bus, N64 side: the one bus instance, drains, interrupt mailbox, hardware
// events and debug output. The rules live in busCore.cpp.
#include <algorithm>
#include <cstdio>
#include <vector>
#include <libdragon.h>

#include "events/bus.h"
#include "events/conditions.h"
#include "debug/debugMenu.h"
#include "debug/debugDraw.h"
#include "busCore.h"
#include "lib/logger.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "scene/componentTable.h"
#include "scene/objectFlags.h"
#include "scene/components/code.h"
#include "scene/components/eventLinks.h"
#include "events/pins.h"
#include "contactEvents.h"

namespace
{
  using namespace P64::Events;
  using Core::BusCore;

  BusCore bus{};

  constexpr NameEntry ENGINE_NAMES[] = {
    {Builtin::GameInit.hash,               "global::Engine.GameInit"},
    {Builtin::SceneLoading.hash,           "global::Engine.SceneLoading"},
    {Builtin::SceneLoaded.hash,            "global::Engine.SceneLoaded"},
    {Builtin::SceneUnloading.hash,         "global::Engine.SceneUnloading"},
    {Builtin::SceneUnloaded.hash,          "global::Engine.SceneUnloaded"},
    {Builtin::ResetPressed.hash,           "global::Engine.ResetPressed"},
    {Builtin::ControllerConnected.hash,    "global::Engine.ControllerConnected"},
    {Builtin::ControllerDisconnected.hash, "global::Engine.ControllerDisconnected"},
    {Builtin::PakInserted.hash,            "global::Engine.PakInserted"},
    {Builtin::PakRemoved.hash,             "global::Engine.PakRemoved"},
    {Builtin::CurrentSceneLoaded.hash,     "scene::Loaded"},
    {Builtin::CurrentSceneUnloading.hash,  "scene::Unloading"},
    {Builtin::PrefabSpawned.hash,          "prefab::Spawned"},
    {Builtin::PrefabDestroying.hash,       "prefab::Destroying"},
    {Builtin::PrefabEnabled.hash,          "prefab::Enabled"},
    {Builtin::PrefabDisabled.hash,         "prefab::Disabled"},
  };

  // prefab roots created since the last onFrameReady() (at scene load, or by the previous frame's
  // spawns): their onReady ran right before onFrameReady(), so they get prefab::Spawned then
  std::vector<uint16_t> newPrefabRoots{};

  // posts a prefab:: event in the instance of 'root' only if something there listens: a level
  // with hundreds of instances must not flood the queue
  void postToInstance(EventId id, P64::Object &root)
  {
    if(bus.listenerCount(id.hash, root.id) == 0) return;
    bus.post(id.hash, Scope::Prefab, root.id, root.id, root.id);
  }

  constinit const NameEntry* projectNames = nullptr;
  constinit uint32_t projectNameCount = 0;

  // warnings are rate-limited so a broken loop cannot flood the log
  constexpr uint32_t MAX_WARNINGS_PER_FRAME = 8;
  constinit uint32_t warningsThisFrame = 0;
  constinit uint32_t mailboxDropsReported = 0;

  struct PortState
  {
    bool connected;
    uint8_t accessory;
  };
  constinit PortState ports[JOYPAD_PORT_COUNT]{};

  const char* describe(uint32_t hash, char* buffer, size_t size)
  {
    const char* name = nameOf(hash);
    if(name) return name;
    snprintf(buffer, size, "0x%08lX", (unsigned long)hash);
    return buffer;
  }

  bool allowWarning()
  {
    if(warningsThisFrame >= MAX_WARNINGS_PER_FRAME) return false;
    ++warningsThisFrame;
    return true;
  }

  void onWarning(Core::Warning warning, uint32_t hash, [[maybe_unused]] void* user)
  {
    if(!allowWarning()) return;
    char buffer[16];
    const char* name = describe(hash, buffer, sizeof(buffer));
    switch(warning)
    {
      case Core::Warning::QueueFull:
        P64::Log::warn("[events] queue full (%d), dropped %s", (int)BusCore::QUEUE_SIZE, name);
        break;
      case Core::Warning::HopLimit:
        P64::Log::warn("[events] %s went past %d hops, dropped it (loop without a delay?)", name, (int)BusCore::MAX_HOPS);
        break;
      case Core::Warning::DrainBudget:
        P64::Log::warn("[events] drain delivered its budget of %d, %s and later events wait for the next drain", (int)BusCore::DRAIN_BUDGET, name);
        break;
      case Core::Warning::PoolFull:
        P64::Log::warn("[events] all %d subscription slots in use, listener for %s not added", (int)BusCore::MAX_SUBS, name);
        break;
    }
  }

  // the most recent deliveries, for the debug overlay's Events page (PLAN 10)
  struct RecentEvent
  {
    uint32_t hash{};
    uint32_t value{};
    uint16_t sender{};
    uint16_t listeners{};
  };
  constexpr uint32_t RECENT_COUNT = 12;
  constinit RecentEvent recent[RECENT_COUNT]{};
  constinit uint32_t recentNext = 0;
  constinit uint32_t recentTotal = 0;

  void onTrace(const Core::QueuedEvent& event, uint32_t listeners, [[maybe_unused]] void* user)
  {
    recent[recentNext] = {event.ev.id, event.ev.value, event.ev.sender, (uint16_t)std::min<uint32_t>(listeners, 0xFFFF)};
    recentNext = (recentNext + 1) % RECENT_COUNT;
    ++recentTotal;
#if P64_EVENT_TRACE
    char buffer[16];
    debugf("[events] %s value=%lu sender=%u instigator=%u hops=%u listeners=%lu\n",
      describe(event.ev.id, buffer, sizeof(buffer)),
      (unsigned long)event.ev.value, (unsigned)event.ev.sender, (unsigned)event.ev.instigator,
      (unsigned)event.hops, (unsigned long)listeners);
#endif
  }

  void drawDebugPage()
  {
    // the overlay is 320 pixels wide: in the monospace font (7 pixels a character, the columns line up)
    // every line from x = 15 keeps to 43 characters
    using P64::Debug::printf;
    P64::Debug::isMonospace = true;
    constexpr uint16_t x = 15;
    constexpr uint8_t step = 8;
    constexpr uint32_t SHOWN = 10; // of the RECENT_COUNT kept, so the list stays above the overlay's bars
    uint16_t y = 45;
    const auto stats = P64::Events::getStats();
    const auto limits = P64::Events::getLimits();
    printf(x, y, "queue %u/%lu  peak %u", (unsigned)stats.queued, (unsigned long)limits.queueSize,
      (unsigned)stats.queuePeak);
    y += step;
    printf(x, y, "listeners %u/%lu  peak %u", (unsigned)stats.subscriptions,
      (unsigned long)limits.maxSubscriptions, (unsigned)stats.subscriptionsPeak);
    y += step;
    printf(x, y, "posted %lu  delivered %lu", (unsigned long)stats.posted, (unsigned long)stats.delivered);
    y += step;
    printf(x, y, "listener calls %lu", (unsigned long)stats.listenerCalls);
    y += step;
    printf(x, y, "dropped: full %lu hops %lu irq %lu", (unsigned long)stats.droppedQueueFull,
      (unsigned long)stats.droppedHopLimit, (unsigned long)stats.droppedMailbox);
    y += step;
    printf(x, y, "  scope %lu budget %lu subs %lu", (unsigned long)stats.droppedWrongScope,
      (unsigned long)stats.budgetHits, (unsigned long)stats.subscribeFailed);
    y += step + 4;
    printf(x, y, "Latest first           from  value  n"); // n: listeners called
    y += step;
    const uint32_t count = std::min<uint32_t>(std::min<uint32_t>(recentTotal, RECENT_COUNT), SHOWN);
    for(uint32_t i = 0; i < count; ++i) {
      const auto &ev = recent[(recentNext + RECENT_COUNT - 1 - i) % RECENT_COUNT];
      char buffer[16];
      printf(x, y, "%-22.22s%5u%7lu%3u", describe(ev.hash, buffer, sizeof(buffer)), (unsigned)ev.sender,
        (unsigned long)ev.value, (unsigned)ev.listeners);
      y += step;
    }
    if(count == 0) printf(x, y, "(none yet)");
    P64::Debug::isMonospace = false;
  }

  void onResetPressed()
  {
    // runs in the reset interrupt; delivered at the start of the next frame
    postFromInterrupt(Builtin::ResetPressed);
  }

  PortState readPort(joypad_port_t port)
  {
    PortState state{};
    state.connected = joypad_is_connected(port);
    state.accessory = state.connected ? (uint8_t)joypad_get_accessory_type(port) : (uint8_t)JOYPAD_ACCESSORY_TYPE_NONE;
    return state;
  }

  void pollHardware()
  {
    // JOYPAD_PORT_FOREACH does not compile as C++ (it increments an enum)
    for(uint32_t i = 0; i < JOYPAD_PORT_COUNT; ++i)
    {
      const auto port = (joypad_port_t)i;
      PortState now = readPort(port);
      PortState &was = ports[port];
      const uint32_t portValue = (uint32_t)port;

      if(now.connected != was.connected) {
        bus.post((now.connected ? Builtin::ControllerConnected : Builtin::ControllerDisconnected).hash,
                 Scope::Global, 0, portValue, 0);
      }

      // UNKNOWN means libdragon is still identifying the accessory: wait for the real type
      if(now.accessory != was.accessory && now.accessory != (uint8_t)JOYPAD_ACCESSORY_TYPE_UNKNOWN) {
        if(was.accessory != (uint8_t)JOYPAD_ACCESSORY_TYPE_NONE) {
          bus.post(Builtin::PakRemoved.hash, Scope::Global, 0, portValue | ((uint32_t)was.accessory << 8), 0);
        }
        if(now.accessory != (uint8_t)JOYPAD_ACCESSORY_TYPE_NONE) {
          bus.post(Builtin::PakInserted.hash, Scope::Global, 0, portValue | ((uint32_t)now.accessory << 8), 0);
        }
        was.accessory = now.accessory;
      }
      was.connected = now.connected;
    }
  }

  void warnWrongScope(const char* call, EventId id)
  {
    if(!allowWarning()) return;
    char buffer[16];
    P64::Log::warn("[events] %s: %s needs an object to scope it, not supported here yet",
      call, describe(id.hash, buffer, sizeof(buffer)));
  }

  bool needsInstance(EventId id)
  {
    return id.scope == Scope::Prefab || id.scope == Scope::Object;
  }

  constinit uint32_t droppedWrongScope = 0;

  // send(): every script input on the target object with this pin name
  uint32_t onDirect(const Core::QueuedEvent& event, [[maybe_unused]] void* user)
  {
    P64::Object* obj = P64::SceneManager::getCurrent().getObjectById(event.instance);
    if(!obj) return 0;

    uint32_t calls = 0;
    auto refs = obj->getCompRefs();
    for(uint32_t c = 0; c < obj->compCount; ++c)
    {
      if(refs[c].type != P64::Comp::Code::ID) continue;
      auto *code = reinterpret_cast<P64::Comp::Code*>(reinterpret_cast<char*>(obj) + refs[c].offset);
      const auto *script = code->script;
      if(!script) continue;
      // Fork: conditions, their own pins (Check, Enable, ...) and the internal re-check
      if(script->evaluate) calls += P64::Conditions::Internal::onDirect(*obj, *code, event.ev);
      if(!script->onInput || !script->inputHashes) continue;
      for(uint32_t pin = 0; pin < script->inputCount; ++pin) {
        if(script->inputHashes[pin] != event.ev.id) continue;
        P64::Comp::Code::onInput(*obj, code, static_cast<uint8_t>(pin), event.ev);
        ++calls;
      }
    }
    return calls;
  }

  void markScoped(P64::Object& obj)
  {
    obj.setFlag(P64::ObjectFlags::EVENT_SCOPE, true);
  }
}

void P64::Events::post(EventId id, uint32_t value, uint16_t sender)
{
  if(needsInstance(id)) {
    ++droppedWrongScope;
    warnWrongScope("post", id);
    return;
  }
  bus.post(id.hash, id.scope, 0, value, sender);
}

bool P64::Events::postFromInterrupt(EventId id, uint32_t value)
{
  if(id.scope != Scope::Global) return false;
  // nestable: also safe when called from an interrupt handler
  disable_interrupts();
  bool ok = bus.mailboxPush(id.hash, value);
  enable_interrupts();
  return ok;
}

P64::Events::Listener P64::Events::listen(EventId id, ListenerFn fn, void* user)
{
  if(needsInstance(id)) {
    ++droppedWrongScope;
    warnWrongScope("listen", id);
    return {};
  }
  auto handle = bus.subscribe(id.hash, 0, fn, user);
  return {handle.slot, handle.gen};
}

void P64::Events::unlisten(Listener& listener)
{
  bus.unsubscribe({listener.slot, listener.gen});
  listener = {};
}

void P64::Events::postTo(Object& inScopeOf, EventId id, uint32_t value, uint16_t sender)
{
  if(!needsInstance(id)) {
    bus.post(id.hash, id.scope, 0, value, sender);
    return;
  }
  uint16_t instance = Internal::resolveInstance(inScopeOf, id.scope, id.up);
  if(instance == 0) {
    ++droppedWrongScope;
    warnWrongScope("postTo", id);
    return;
  }
  bus.post(id.hash, id.scope, instance, value, sender);
}

P64::Events::Listener P64::Events::listenIn(Object& inScopeOf, EventId id, ListenerFn fn, void* user)
{
  if(!needsInstance(id)) return listen(id, fn, user);
  uint16_t instance = Internal::resolveInstance(inScopeOf, id.scope, id.up);
  if(instance == 0) {
    ++droppedWrongScope;
    warnWrongScope("listenIn", id);
    return {};
  }
  return Internal::listenBound(id.hash, id.scope, instance, fn, user);
}

uint8_t P64::Events::Internal::componentIndexOf(const Object& obj, const void* componentData)
{
  auto refs = obj.getCompRefs();
  for(uint32_t c = 0; c < obj.compCount; ++c) {
    const char* data = reinterpret_cast<const char*>(&obj) + refs[c].offset;
    if(data == componentData) return static_cast<uint8_t>(c);
    // a script's data sits right behind its Code component
    if(refs[c].type == P64::Comp::Code::ID && data + sizeof(P64::Comp::Code) == componentData) {
      return static_cast<uint8_t>(c);
    }
  }
  return 0xFF;
}

bool P64::Events::Internal::sendChecked(uint16_t targetId, uint32_t inputHash, uint32_t value, uint16_t sender)
{
  if(targetId == 0) return false;
  return bus.post(inputHash, Scope::Direct, targetId, value, sender);
}

void P64::Events::fire(Object& obj, const void* componentData, uint8_t pin, uint32_t value, uint16_t instigator)
{
  auto *links = obj.getComponent<Comp::EventLinks>();
  if(!links) return; // nothing bound on this object
  const uint8_t comp = componentData ? Internal::componentIndexOf(obj, componentData) : Comp::EventLinks::COMP_OBJECT;

  for(uint32_t i = 0; i < links->count; ++i) {
    const auto &binding = links->bindings()[i];
    if(binding.isInput() || binding.rec.comp != comp || binding.rec.pin != pin) continue;
    bus.post(binding.rec.hash, binding.scope(), binding.instance, value, obj.id, instigator);
  }
}

void P64::Events::send(Object& target, PinName input, uint32_t value, uint16_t sender)
{
  bus.post(input.hash, Scope::Direct, target.id, value, sender);
}

void P64::Events::send(uint16_t targetId, PinName input, uint32_t value, uint16_t sender)
{
  if(targetId == 0) return;
  bus.post(input.hash, Scope::Direct, targetId, value, sender);
}

uint32_t P64::Events::listenerCount(EventId id)
{
  return bus.listenerCount(id.hash, 0);
}

P64::Events::Stats P64::Events::getStats()
{
  auto stats = bus.getStats();
  stats.droppedWrongScope = droppedWrongScope;
  return stats;
}

P64::Events::Limits P64::Events::getLimits()
{
  return {
    .queueSize = BusCore::QUEUE_SIZE,
    .maxSubscriptions = BusCore::MAX_SUBS,
    .mailboxSize = BusCore::MAILBOX_SIZE,
    .maxHops = BusCore::MAX_HOPS,
    .drainBudget = BusCore::DRAIN_BUDGET,
  };
}

void P64::Events::setNameTable(const NameEntry* entries, uint32_t count)
{
  projectNames = entries;
  projectNameCount = entries ? count : 0;
}

const char* P64::Events::nameOf(uint32_t hash)
{
  for(const auto &entry : ENGINE_NAMES) {
    if(entry.hash == hash) return entry.name;
  }
  for(uint32_t i = 0; i < projectNameCount; ++i) {
    if(projectNames[i].hash == hash) return projectNames[i].name;
  }
  return nullptr;
}

void P64::Events::Internal::init()
{
  bus.setWarnHandler(onWarning, nullptr);
  bus.setDirectHandler(onDirect, nullptr);
  bus.setTraceHandler(onTrace, nullptr); // feeds the debug page, and debugf with P64_EVENT_TRACE

  // the state at boot is the baseline, so controllers already plugged in send no event
  for(uint32_t i = 0; i < JOYPAD_PORT_COUNT; ++i) {
    const auto port = (joypad_port_t)i;
    ports[port] = readPort(port);
    if(ports[port].accessory == (uint8_t)JOYPAD_ACCESSORY_TYPE_UNKNOWN) {
      ports[port].accessory = (uint8_t)JOYPAD_ACCESSORY_TYPE_NONE;
    }
  }

  register_RESET_handler(onResetPressed);
}

void P64::Events::Internal::drain(DrainPoint point)
{
  if(point == DrainPoint::FrameStart)
  {
    warningsThisFrame = 0;
    pollHardware();

    disable_interrupts();
    bus.mailboxFlush();
    uint32_t mailboxDrops = bus.getStats().droppedMailbox;
    enable_interrupts();

    if(mailboxDrops != mailboxDropsReported) {
      if(allowWarning()) {
        P64::Log::warn("[events] %lu interrupt events dropped, mailbox holds %d per frame",
          (unsigned long)(mailboxDrops - mailboxDropsReported), (int)BusCore::MAILBOX_SIZE);
      }
      mailboxDropsReported = mailboxDrops;
    }
  }

  bus.drain(BusCore::DRAIN_BUDGET);
}

void P64::Events::Internal::postImmediate(EventId id, uint32_t value)
{
  bus.deliverNow(id.hash, id.scope, 0, value, 0);
}

uint16_t P64::Events::Internal::resolveInstance(Object& obj, Scope scope, uint8_t up)
{
  switch(scope)
  {
    case Scope::Object: return obj.id;
    case Scope::Prefab:
    {
      // the (up + 1)-th prefab instance root on the way from obj to the scene root (obj counts)
      auto &scene = SceneManager::getCurrent();
      uint32_t found = 0;
      for(Object* o = &obj; o; o = o->group ? scene.getObjectById(o->group) : nullptr) {
        if(!(o->flags & ObjectFlags::PREFAB_ROOT)) continue;
        if(found == up) return o->id;
        ++found;
      }
      return 0; // not inside that many prefabs: the binding stays silent
    }
    default: return 0;
  }
}

P64::Events::Listener P64::Events::Internal::listenBound(uint32_t hash, [[maybe_unused]] Scope scope, uint16_t instance, ListenerFn fn, void* user)
{
  if(instance != 0) {
    if(auto *scopeObj = SceneManager::getCurrent().getObjectById(instance)) markScoped(*scopeObj);
  }
  auto handle = bus.subscribe(hash, instance, fn, user);
  return {handle.slot, handle.gen};
}

void P64::Events::Internal::objectInput(Object& obj, uint8_t pin, [[maybe_unused]] const Event& ev)
{
  using In = Pins::ObjectIn;
  switch(static_cast<In>(pin))
  {
    case In::Enable:        obj.setEnabled(true); break;
    case In::Disable:       obj.setEnabled(false); break;
    case In::ToggleEnabled: obj.setEnabled(!obj.isSelfEnabled()); break;
    case In::Destroy:       obj.remove(); break;
    case In::Show:          obj.setVisible(true); break;
    case In::Hide:          obj.setVisible(false); break;
  }
}

void P64::Events::Internal::onObjectRemoving(Object& obj)
{
  // fires when removal is requested, so listeners still find the object this frame
  if(obj.getComponent<Comp::EventLinks>()) {
    fire(obj, nullptr, static_cast<uint8_t>(Pins::ObjectOut::Destroyed), obj.id);
  }
  if(obj.flags & ObjectFlags::PREFAB_ROOT) postToInstance(Builtin::PrefabDestroying, obj);
}

void P64::Events::Internal::onObjectStateChanged(Object& obj)
{
  if(obj.flags & ObjectFlags::PREFAB_ROOT) {
    postToInstance(obj.isEnabled() ? Builtin::PrefabEnabled : Builtin::PrefabDisabled, obj);
  }
}

void P64::Events::Internal::onPrefabRootLoaded(Object& obj)
{
  newPrefabRoots.push_back(obj.id);
}

void P64::Events::Internal::onFrameReady()
{
  if(newPrefabRoots.empty()) return;
  auto &scene = SceneManager::getCurrent();
  for(uint16_t id : newPrefabRoots) {
    if(auto *root = scene.getObjectById(id)) postToInstance(Builtin::PrefabSpawned, *root);
  }
  newPrefabRoots.clear();
}

void P64::Events::Internal::onObjectDestroyed(Object& obj)
{
  // listeners keyed to this object's scope must not outlive it: its id will be reused
  if(obj.flags & ObjectFlags::EVENT_SCOPE) {
    bus.unsubscribeInstance(obj.id);
  }
}

void P64::Events::Internal::onSceneDestroyed()
{
  // scene, prefab and object events belong to the scene that is gone; global and ui ones stay
  bus.dropQueuedSceneEvents();
  bus.unsubscribeAllInstances();
  Contacts::reset();
  Conditions::Internal::onSceneDestroyed();
  newPrefabRoots.clear();
}

void P64::Events::Internal::addDebugMenu()
{
  auto &menu = P64::Debug::Overlay::addCustomMenu("Events");
  menu.onDraw = drawDebugPage;
}
