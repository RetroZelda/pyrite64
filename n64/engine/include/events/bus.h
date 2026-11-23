/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: the event bus, public API (docs/todo/event-graph/PLAN.md, section 4.3).
//
//   Listen:  auto l = P64::Events::listen("scene::Loaded"_event, onLoaded, this);
//   Post:    P64::Events::post("mission::Started"_event, missionId);
//   Stop:    P64::Events::unlisten(l);
//
// Events are queued and delivered in the same frame at three drain points: after the frame's
// spawns and addressed messages, after physics, and after update. See the README next to the
// plan for the full rules.
#pragma once
#include "types.h"

namespace P64 { class Object; }

namespace P64::Events
{
  /** Handle returned by listen(). Default-constructed handles are invalid. */
  struct Listener
  {
    uint16_t slot{0xFFFF};
    uint16_t gen{0};

    [[nodiscard]] constexpr bool isValid() const { return slot != 0xFFFF; }
  };

  /**
   * Queues an event for its scope's listeners. Use it for global::, scene::, ui:: and
   * game-declared namespaces. prefab:: and object:: events need an object to scope them and
   * are rejected here with a warning (postTo arrives with prefab scoping, phase 4).
   * Called from a listener, the event continues that chain: one more hop, same instigator.
   * Not interrupt-safe: use postFromInterrupt() in interrupt handlers.
   * @param value one number for the listeners
   * @param sender object id that fires it, 0 for code that is not an object
   */
  void post(EventId id, uint32_t value = 0, uint16_t sender = 0);

  /**
   * Queues a global event from an interrupt handler (or any code). Delivered at the start of
   * the next frame. Only global:: and game-declared namespaces.
   * @return false if the mailbox was full or the scope is not global
   */
  bool postFromInterrupt(EventId id, uint32_t value = 0);

  /**
   * Calls 'fn' for every 'id' event until unlisten(). Listeners of one event are called in the
   * order they were added. Code listeners survive scene changes.
   * prefab:: and object:: events are rejected here (listenIn arrives in phase 4).
   * @return invalid handle if rejected or if every subscription slot is in use
   */
  Listener listen(EventId id, ListenerFn fn, void* user = nullptr);

  /** Removes a listener and resets the handle. Safe during delivery and with stale handles. */
  void unlisten(Listener& listener);

  /**
   * Posts an object:: event in the scope of 'inScopeOf' (prefab:: events from phase 4).
   * Other namespaces work too and behave like post().
   */
  void postTo(Object& inScopeOf, EventId id, uint32_t value = 0, uint16_t sender = 0);

  /**
   * Listens to an object:: event in the scope of 'inScopeOf' (prefab:: events from phase 4).
   * The listener is removed when that object is destroyed; a later unlisten() is harmless.
   */
  Listener listenIn(Object& inScopeOf, EventId id, ListenerFn fn, void* user = nullptr);

  /**
   * Fires output pin 'pin' of the component whose data is 'componentData' on 'obj': posts every
   * event the pin is bound to. Scripts use the fire(obj, data, Out::X) their P64_EVENTS_OUT
   * declares instead of calling this.
   * @param instigator object that started the chain; 0 continues the current chain or uses obj
   */
  void fire(Object& obj, const void* componentData, uint8_t pin, uint32_t value = 0, uint16_t instigator = 0);

  /**
   * Sends to one object: every script on 'target' that declares an input named 'input' receives
   * it in onInput, through the same queue and drains as any event. Nothing has to be bound in
   * the editor. Replaces Scene::sendEvent.
   *   P64::Events::send(*enemy, "TakeDamage"_pin, amount, obj.id);
   */
  void send(Object& target, PinName input, uint32_t value = 0, uint16_t sender = 0);

  /**
   * The same by object id, for code that keeps ids (ObjectRef::id) rather than objects. The id is
   * looked up when the event is delivered, so the target may be created or initialised later in
   * the same frame; nothing happens if it is gone by then.
   */
  void send(uint16_t targetId, PinName input, uint32_t value = 0, uint16_t sender = 0);

  /** Number of listeners 'id' has right now. */
  uint32_t listenerCount(EventId id);

  /** Counters for debugging and tests. */
  Stats getStats();

  /** The limits this engine build uses (events/config.h, overridable with ENGINE_CXXFLAGS). */
  struct Limits
  {
    uint32_t queueSize;
    uint32_t maxSubscriptions;
    uint32_t mailboxSize;
    uint32_t maxHops;
    uint32_t drainBudget;
  };
  Limits getLimits();

  /** A name for debug output. The build generates the game's table (phase 2). */
  struct NameEntry
  {
    uint32_t hash;
    const char* name;
  };

  /** Registers names for debug output, in addition to the engine's own. */
  void setNameTable(const NameEntry* entries, uint32_t count);

  /** The name of an event hash for debug output, or nullptr if unknown. */
  const char* nameOf(uint32_t hash);

  /** Events the engine fires (PLAN 4.5). */
  namespace Builtin
  {
    // prefab::* : heard inside the prefab instance they concern
    inline constexpr EventId PrefabSpawned          = "prefab::Spawned"_event;                       // the instance and its objects are ready
    inline constexpr EventId PrefabDestroying       = "prefab::Destroying"_event;                    // removal of the instance root was requested
    inline constexpr EventId PrefabEnabled          = "prefab::Enabled"_event;                       // the instance root became active
    inline constexpr EventId PrefabDisabled         = "prefab::Disabled"_event;                      // the instance root became inactive

    // global::Engine.* : the engine is the system that declares these
    inline constexpr EventId GameInit               = "global::Engine.GameInit"_event;               // after the game-init hook, immediate
    inline constexpr EventId SceneLoading           = "global::Engine.SceneLoading"_event;           // before a scene is created, immediate, value: scene id
    inline constexpr EventId SceneLoaded            = "global::Engine.SceneLoaded"_event;            // first frame after every onReady, value: scene id
    inline constexpr EventId SceneUnloading         = "global::Engine.SceneUnloading"_event;         // before the scene is deleted, immediate, value: scene id
    inline constexpr EventId SceneUnloaded          = "global::Engine.SceneUnloaded"_event;          // after it was deleted, immediate, value: scene id
    inline constexpr EventId ResetPressed           = "global::Engine.ResetPressed"_event;           // N64 reset button
    inline constexpr EventId ControllerConnected    = "global::Engine.ControllerConnected"_event;    // value: port 0-3
    inline constexpr EventId ControllerDisconnected = "global::Engine.ControllerDisconnected"_event; // value: port 0-3
    inline constexpr EventId PakInserted            = "global::Engine.PakInserted"_event;            // value: port | (joypad_accessory_type_t << 8)
    inline constexpr EventId PakRemoved             = "global::Engine.PakRemoved"_event;             // value: port | (joypad_accessory_type_t << 8)

    // scene::* : heard by everything in the current scene
    inline constexpr EventId CurrentSceneLoaded     = "scene::Loaded"_event;                         // first frame after every onReady, value: scene id
    inline constexpr EventId CurrentSceneUnloading  = "scene::Unloading"_event;                      // before the scene is deleted, immediate, value: scene id
  }

  /** Engine-internal entry points, called by the scene manager and the scene. */
  namespace Internal
  {
    /** Adds the "Events" page to the debug overlay (recent events, counters). Called on scene load. */
    void addDebugMenu();

    enum class DrainPoint : uint8_t
    {
      FrameStart,   // after spawns, enable changes and addressed messages; also takes hardware and interrupt events
      AfterPhysics, // after fixedUpdate, the physics steps and collision callbacks
      AfterUpdate,  // after every component's update
    };

    void init();
    void drain(DrainPoint point);
    void postImmediate(EventId id, uint32_t value = 0);
    void onSceneDestroyed();
    void onObjectDestroyed(Object& obj);
    void onObjectRemoving(Object& obj); // remove() was called: fires Destroyed
    void onObjectStateChanged(Object& obj); // the object's combined enabled state changed
    void onPrefabRootLoaded(Object& obj);   // a PREFAB_ROOT object was created
    void onFrameReady();                    // after the frame's onReady calls

    // used by the EventLinks component
    uint16_t resolveInstance(Object& obj, Scope scope, uint8_t up);
    Listener listenBound(uint32_t hash, Scope scope, uint16_t instance, ListenerFn fn, void* user);
    void objectInput(Object& obj, uint8_t pin, const Event& ev);
  }

  /** Implemented by the build in the project's src/p64/eventNames.cpp. */
  namespace Generated
  {
    void registerNames();
  }
}
