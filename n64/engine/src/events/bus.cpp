/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: the event bus, N64 side: the one bus instance, drains, interrupt mailbox, hardware
// events and debug output. The rules live in busCore.cpp.
#include <cstdio>
#include <libdragon.h>

#include "events/bus.h"
#include "busCore.h"
#include "lib/logger.h"

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
  };

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

#if P64_EVENT_TRACE
  void onTrace(const Core::QueuedEvent& event, uint32_t listeners, [[maybe_unused]] void* user)
  {
    char buffer[16];
    debugf("[events] %s value=%lu sender=%u instigator=%u hops=%u listeners=%lu\n",
      describe(event.ev.id, buffer, sizeof(buffer)),
      (unsigned long)event.ev.value, (unsigned)event.ev.sender, (unsigned)event.ev.instigator,
      (unsigned)event.hops, (unsigned long)listeners);
  }
#endif

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
#if P64_EVENT_TRACE
  bus.setTraceHandler(onTrace, nullptr);
#endif

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

void P64::Events::Internal::onSceneDestroyed()
{
  // scene, prefab and object events belong to the scene that is gone; global and ui ones stay
  bus.dropQueuedSceneEvents();
  bus.unsubscribeAllInstances();
}
