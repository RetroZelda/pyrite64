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
      if(!script || !script->onInput || !script->inputHashes) continue;
      for(uint32_t pin = 0; pin < script->inputCount; ++pin) {
        if(script->inputHashes[pin] != event.ev.id) continue;
        P64::Comp::Code::onInput(*obj, code, static_cast<uint8_t>(pin), event.ev);
        ++calls;
      }
    }
    return calls;
  }

  // the component index of 'componentData' on 'obj', or 0xFF
  uint8_t componentIndexOf(const P64::Object& obj, const void* componentData)
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

void P64::Events::fire(Object& obj, const void* componentData, uint8_t pin, uint32_t value, uint16_t instigator)
{
  auto *links = obj.getComponent<Comp::EventLinks>();
  if(!links) return; // nothing bound on this object
  const uint8_t comp = componentData ? componentIndexOf(obj, componentData) : Comp::EventLinks::COMP_OBJECT;

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

uint16_t P64::Events::Internal::resolveInstance(Object& obj, Scope scope, [[maybe_unused]] uint8_t up)
{
  switch(scope)
  {
    case Scope::Object: return obj.id;
    case Scope::Prefab: return 0; // phase 4: the prefab instance 'up' levels above obj
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
}
