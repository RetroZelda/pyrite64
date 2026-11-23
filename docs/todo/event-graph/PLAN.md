# Event graph: plan

- **Status:** approved by the owner on 2026-11-23 (draft 7). Phase 1 is implemented and tested;
  the how-to guide is in the kentucky-castle repo (`docs/EventGraph/README.md`), and
  [PLANNING-STATE.md](PLANNING-STATE.md) tracks progress.
- **Date:** 2026-11-23
- **Branches:** `feature/event-graph` in `pyrite64` (from `stream/full-frontal`) and in
  `kentucky-castle` (from `main`)
- **Background:** [FINDINGS.md](FINDINGS.md) holds the facts this plan is built on, with file and
  line references. [PLANNING-STATE.md](PLANNING-STATE.md) tracks the planning work and records the
  owner's answers word for word.

Paths are relative to the `pyrite64` repo unless they start with `KC/` (the `kentucky-castle`
repo).

## 1. Summary

The engine today only has addressed messages: one sender, one target object id, delivered the next
frame, with no enter/exit detection. This plan adds a named and scoped event system next to it,
moves Kentucky Castle onto it, and then marks the old messages as deprecated in the fork.

What gets built:

1. **A runtime event bus** in the engine: events named `namespace::Name`, compiled to 32-bit ids,
   delivered only inside their scope (game, scene, prefab instance, object, UI), with fixed
   memory and loop protection.
2. **Pins.** C++ object scripts, global scripts and engine components declare input events and
   output events.
3. **Bindings stored as plain text** with each object in the scene and prefab files, and baked
   into a small hidden component at build time. Typing a name is all it takes to declare an event,
   except in `global`, where every event belongs to a named system.
4. **Direct sends:** code can send to one object's input by pin name. This is what replaces the
   old addressed messages.
5. **Automatic events** fired by the engine for the game, scene, prefab and object life cycles,
   plus the reset button, controllers and paks.
6. **Enter and exit detection for colliders** inside the engine, as pins and as script callbacks.
7. **Built-in event objects** (relay, time delay, frame delay, counter, gate and more) shipped as
   engine scripts.
8. **Editor support in two steps:** first an "Events" section in the object inspector, then the
   Event Graph window where each object is one node and wires connect outputs to inputs.
9. **UI canvas pins**, so events can show a canvas, play a clip or reach element scripts, and the
   UI can report activation, clip ends and timeline markers.
10. **Adoption in Kentucky Castle**, including event opcodes for the AI scripts, followed by the
    retirement of the old messages.

Node graphs are not touched by this work.

### The collider-and-door example, end to end

1. The Collider component on `TriggerZone` offers the outputs `Enter` and `Exit`.
2. The `Door` script declares the inputs `Open` and `Close`.
3. In the prefab's event graph you drag a wire from `Enter` to `Open`. The editor creates the event
   `prefab::TriggerZone.Enter` and writes that name on both pins in the prefab file.
4. The build turns the name into an id and stores the two bindings with the two objects.
5. At runtime the collision step notices that an object entered, the collider fires its `Enter`
   pin, and the bus delivers the event to `Door.Open` in the same frame, before `update` runs.
6. Five instances of the prefab each have their own `prefab::TriggerZone.Enter`, because the bus
   keys every prefab event by the instance it belongs to.

### Order of work

| Phase | At the end you can | Size |
|---|---|---|
| 0. Approval | Start, with this plan as the spec | S |
| 1. Runtime core | Post and receive named events from C++ code | M |
| 2. Script pins, bindings, direct sends | Wire a door to a trigger script by typing bindings in the inspector | L |
| 3. Engine pins and built-in objects | Do the same with no custom trigger code, plus logic objects | L |
| 4. Prefab scope | Place five door prefabs that do not affect each other | L |
| 5. Event Graph window | Author all of the above by dragging wires | XL |
| 6. UI canvases | Drive the HUD from events and get menu activation back | M |
| 7. Tools, manual and example | Browse, find and rename events; read the manual; build the example | M |
| 8. Kentucky Castle adoption | Remove hand-written triggers and relays in the game | L |
| 9. Retire the old messages | Build the game with no use of `sendEvent` or `onEvent` | M |

Sizes are relative to each other, not time estimates.

## 2. What you asked for and what this plan adds

### 2.1 Your requirements

| You asked for | Where the plan covers it |
|---|---|
| A graph of events, similar to the node-graph view | 8.2 |
| Scene, prefab, object and UI scopes (the level scope was dropped later: a level is a prefab) | 3.3, 3.4, 9 |
| Objects and components declare input and output events | 4.7, 5.1 |
| Output of one object wired to the input of another | 3.7, 8.2 |
| Scenes fire events automatically; engine default plus game additions | 4.5, 6.4 |
| Prefabs fire events automatically (created, destroyed) | 4.5 |
| Each graph offers what is relative to its scope | 8.2, 8.3 |
| Prefab-local events through namespacing | 3.4 |
| Namespaces global, scene, prefab, object, ui, and game-defined ones | 3.3 |
| `namespace::event` in plain text, compiled to an integer | 3.2, 6.1, 7 |
| A wire automatically creates an event in the graph's scope; manual names allowed | 8.2 |
| Built-in event objects such as the event repeater | 5.4 |
| C++ scripts declare inputs and outputs | 5.1 |
| Global scripts have pins that always use the `global` namespace; each global event carries the name of the system that declares it; a Global node that can be added to a graph shows one collapsible region per system (added later) | 5.5, 8.2 |
| One node per object, one collapsible category per script, unconnected pins hidden when collapsed | 8.2 |
| A node may send to itself; loose recursion detection | 3.5, 7 |

### 2.2 Additions you did not ask for

Each line says when it lands. "Later" means it is designed for but not part of these phases.

| Addition | Why | When |
|---|---|---|
| Engine enter/exit detection for colliders, as pins and script callbacks | Your example needs it and the engine has none | Phase 3 |
| Each event carries a sender, an instigator and one value | A door may need to know who opened it | Phase 1 |
| Direct sends to one object's input by pin name | Needed to retire the old addressed messages | Phase 2 |
| Defined delivery timing, a hop limit and a per-frame budget | Makes self-sends and loops safe | Phase 1 |
| Exposed pins on prefabs and per-instance wiring | Lets a scene wire into one prefab instance | Phase 4 |
| A way for code-spawned prefabs to talk to the outside | They have no outer graph | Phase 4 |
| Build-time checks: typos, dangling pins, id collisions, loops | Plain-text names are easy to mistype | Phase 2 onward |
| An "Events" section in the object inspector | Works before the graph exists and stays as a fallback | Phase 2 |
| Built-in pins on every object (Enable, Disable, Destroy, Show, Hide; Ready, Enabled, Disabled, Destroyed) | Common hookups need no script | Phase 3 |
| Pins on engine components (audio, particles, animation, camera, light) | Same | Phase 3 |
| Occupancy outputs on triggers (first object in, last object out) | Doors usually want these, not per-object events | Phase 3 |
| More logic objects than the repeater | Delays, counter, gate, once, sequence and others | Phase 3 |
| A bridge to the existing addressed messages, both ways | Old scripts keep working during migration | Phase 3 |
| Listeners for code that is neither an object script nor a global script (systems) | A system may need a listener only for a while | Phase 1 |
| Interrupt-safe posting | Needed for hardware events | Phase 1 |
| An optional catalog for descriptions and game-defined namespaces | Tooltips, pickers, and a place to declare `mission::` | Phase 2 |
| Debug tools: on-screen event log, trace output, counters | Names are stripped from release builds | Phase 7 |
| Event browser, find usages, rename, downstream highlight | Needed once a project has many events | Phase 7 |
| UI outputs (activated, focused, clip finished, timeline markers) | Menus need a way back to game logic | Phase 6 |
| Host unit tests, a runtime test project and an example project | No emulator needed for the core logic | Every phase |
| Sticky events (a late listener gets the last value) | Useful for state such as "paused" | Later |
| Typed parameter lists and pin colours by type | One value is enough to start | Later |

## 3. How it works

### 3.1 Vocabulary

- **Event:** a named signal such as `scene::Loaded`. It has no state; it happens and is delivered.
- **Pin:** an input or an output declared by an object script, a global script, an engine
  component, an object or a UI canvas. An output pin **fires**. An input pin **receives**.
- **Binding:** one line of data that ties a pin to an event name.
- **Wire:** what the graph draws when an output binding and an input binding in the same scope
  use the same event name. A wire is not stored separately.
- **Scope:** the set of listeners an event can reach. The namespace decides it.
- **Scope instance:** one particular prefab instance or object that owns a copy of a scoped
  event.
- **Post:** to send an event by name from code, without a pin.
- **Send:** to deliver to one object's input by pin name from code, without any event name.

### 3.2 Names and ids

- A name is `namespace::Name`. The namespace is lower case. The name part uses letters, digits,
  `_` and `.`, and is case-sensitive. The build warns when two names differ only by case.
- **Typing a name declares it.** Nothing has to be registered first. `scene::DoorsLocked` exists
  as soon as one pin is bound to it or one line of code posts it.
- **`global` is the exception.** Every global event is a pin declared by a named system, and its
  name includes that system: `global::LevelDriver.TransitionStarted`. The engine's own events use
  the system name `Engine`. A `global::` name that no system declares is a build error (5.5).
- The id is the CRC-32 of the full text. The engine, the editor and Python's `zlib.crc32` all
  compute the same value (`n64/engine/include/lib/types.h:19`, `src/utils/hash.h:27`). There is
  one exception: `prefab_0`, `prefab_1` and so on are hashed as `prefab` (3.4), so that a parent's
  `prefab::Alarm` and a child's `prefab_1::Alarm` are the same event.
- The build compares every name it knows and fails if two different names produce the same id.
- Project files always store the text. Baked binaries store only the id.
- In C++, `"scene::Loaded"_event` becomes the id, the scope and, for a numbered prefab namespace,
  the number of levels to go up, all at compile time. No generated header is involved, so adding
  an event never recompiles scripts. The build scans the project's sources for these literals so
  that names posted from code take part in the checks and show up in the editor's pickers.
- Debug builds include an id-to-name table for logs and the debug menu. Release builds leave it
  out.

### 3.3 Scopes

| Namespace | Reaches | Instance | Typical use |
|---|---|---|---|
| `global` | everything in the game | one | pins of the game's systems and of the engine: saves, hardware, scene changes |
| `scene` | everything in the current scene | one per scene | loading finished, level changes, lighting, level-wide alarms |
| `prefab` (also written `prefab_0`) | objects of the same prefab instance | the instance root object | a trigger opening its own door |
| `prefab_1`, `prefab_2`, ... | the prefab instance one, two, ... levels above the current one | that instance's root object | a nested door hearing its encounter's alarm |
| `object` | components on the same object | the object | a collider driving a sibling audio component |
| `ui` | canvases and anything else | one | HUD and menu communication |
| namespaces the game declares, such as `mission` | everything in the game | one | mission scripting |

- **There is no `level` scope.** A level is a prefab, so events inside a level are `prefab::`
  events of the level's instance, and level loading stays game code. Prefabs nested in the level
  hear those events through the numbered namespaces (3.4).
- There is no built-in `custom::` namespace. A game declares the extra namespaces it wants (6.4),
  and an undeclared namespace is a build error, which catches typos such as `scnee::`.
- Scene objects subscribe when they load and unsubscribe when they are destroyed. Listeners
  registered from code (global scripts, systems) stay until they unsubscribe.

### 3.4 Which instance a `prefab::` event belongs to, and hearing parent prefabs

Nothing at runtime records which prefab instance an object came from, and instances placed in a
scene are flattened at build time (FINDINGS section 2). The parent links survive both cases, so the
plan uses them:

- The build sets a new object flag, `PREFAB_ROOT`, on every prefab instance root. Bits 8 to 15 of
  the object flags are free.
- **`object::`** uses the object itself.
- **`prefab::`** is the prefab the binding is written in. `prefab_0::` means the same.
- **`prefab_1::`** is the prefab instance that contains it, **`prefab_2::`** the one above that,
  and so on. The number says how many levels up the event lives, so a name shows where an event
  comes from.
- The number is not part of the event's identity. A parent's `prefab::Alarm` and a child's
  `prefab_1::Alarm` are the same event in the same instance's scope; the number only says how far
  up that instance is.
- The build stores how many `PREFAB_ROOT` ancestors to skip, and the runtime climbs the parents to
  find the root. This works the same for instances placed in a scene, nested in a prefab, or
  spawned from code under another object.
- Plain `prefab::` names stay local. A nested prefab hears nothing from its parents unless it asks
  with a number.

Example:

```
Level_Kokiri          a level is just a prefab     PREFAB_ROOT
└─ Courtyard          instance of "Encounter"      PREFAB_ROOT
   ├─ TriggerZone
   └─ Gate            instance of "Door"           PREFAB_ROOT
      └─ Hinge
```

- `prefab::X` written in the Door prefab reaches only this Gate.
- `prefab_1::Alarm` written in the Door prefab is the Courtyard's scope. It hears an alarm that the
  Encounter prefab fires as `prefab::Alarm`, and so does every other prefab nested in that
  Courtyard that asks for it.
- `prefab_2::Alarm` written in the Door prefab is Level_Kokiri's scope. The Encounter prefab
  reaches the same scope with `prefab_1::Alarm`, and the level itself with `prefab::Alarm`. A
  level-wide alarm is therefore a `prefab::` event of the level that nested prefabs pick up by
  number.
- **The number is relative to the prefab it is written in**, so by default it follows the
  nesting: a Door's `prefab_1::Alarm` is whichever prefab the Door sits in. When that is not what
  you want for one placement, the containing graph repoints it (see "Keeping the scope when
  nesting changes" below).
- A number that points above the outermost prefab matches nothing. The build warns about it for
  instances placed in scenes and prefabs. For prefabs spawned from code it only shows at runtime,
  in the debug trace.
- A nested prefab fires into a parent's scope the same way it listens, for example an enemy
  firing `prefab_1::EnemyDied` for its encounter to count.
- A binding written in an outer graph on an exposed pin (6.2) counts from that outer graph:
  `prefab::Y` written in the Encounter's graph on one of Gate's exposed pins is the Courtyard's
  scope.
- Exposed pins remain the way to wire one particular nested instance. Numbered names reach every
  nested instance that asks.
- `prefab::` and the numbered forms are rejected in bindings written in a scene graph, because a
  scene is not a prefab.

#### Keeping the scope when nesting changes

A prefab's definition is shared by all its instances, so its numbers cannot be edited for one
placement. Two rules keep events pointing at the right scope when content moves to another depth.

**Parent links can be repointed per instance.** Every numbered name a prefab uses is a *parent
link* of its instances. By default a link goes one level up under the same name: a Door's
`prefab_1::Alarm` is `prefab::Alarm` in the graph that contains the Door. That containing graph can
point the link somewhere else for one instance, for example at `prefab_1::Alarm` (its own parent)
or at an event with another name. The choice is stored on the instance node (6.2) and applied by
the build, so it costs nothing at runtime.

**The editor repoints automatically whenever an operation changes the depth**, so that every
reference still reaches the scope it reached before:

| Operation | What happens to event references |
|---|---|
| Nesting an instance one level deeper (wrapping it in a new prefab, or moving it into a nested prefab while editing) | Each of its parent links that reached a real scope is repointed one level further up. The prefab's definition is not touched. |
| "To Prefab" on plain objects inside a prefab | Events used only by the selected objects stay `prefab::` and become local to the new prefab. Events shared with objects that stay outside get their number raised by one on the moved objects (`prefab::X` becomes `prefab_1::X`). |
| "To Prefab" on plain objects in a scene | `scene::` names stay as they are. Events used only by the selected objects become `prefab::`, so that copies of the new prefab do not affect each other. |
| "Unpack" | The reverse: numbers go down by one, repointed links are applied, and the instance's local `prefab::` events become events of the containing scope, renamed with the instance's name if they would clash. |
| Copy, paste, duplicate | Names are kept as written. `prefab::` keeps meaning "the prefab I am in". |

A reference that would end up above the outermost prefab is reported instead of being changed
silently.

### 3.5 When events are delivered

Events are queued and delivered in order, in the same frame. The queue is emptied at three points;
the existing frame order is in FINDINGS section 2.

1. `onReady` calls for objects created last frame
2. The global `SCENE_UPDATE` hook
3. Pending spawns, component init, enable and disable changes
4. Existing addressed messages (unchanged)
5. **Drain 1:** everything posted since the last frame, and events posted from interrupts
6. Fixed update and physics steps, including the new enter/exit detection
7. **Drain 2:** everything fired in `fixedUpdate` and by collisions
8. `update` on every component
9. **Drain 3:** everything fired in `update`
10. Deferred deletes, then drawing

So a trigger entered in step 6 opens its door before the door's `update` in step 8. An event fired
by a handler during a drain is delivered in the same drain. Kentucky Castle's systems post from
the `SCENE_UPDATE` hook and are delivered in drain 1, the same frame, as they are today.

Loop protection:

- Every event counts its hops. An event fired from a handler has one more hop than the event being
  handled. Past 16 hops the event is dropped and a warning names it. This is the loose recursion
  detection: a node may send to itself, and a chain that never ends stops after 16 steps.
- A drain delivers at most 256 events. The rest wait for the next drain point and a warning is
  logged.
- A delay object fires from its own `update`, which starts a new chain. A blinking light built
  from two delay objects is therefore legal.
- The build also warns about loops that contain no delay object.

Events that the engine fires outside the frame loop are delivered immediately instead of being
queued, because no drain follows: `global::Engine.GameInit`, `global::Engine.SceneLoading`,
`scene::Unloading`, `global::Engine.SceneUnloading` and `global::Engine.SceneUnloaded`. While no
scene exists, only code listeners can receive them.

### 3.6 What an event carries

```cpp
struct Event {
  uint32_t id;          // which event this is
  uint16_t sender;      // object that fired it, 0 for the engine or a system
  uint16_t instigator;  // object that started the chain, carried along every hop
  uint32_t value;       // one number: a count, an object id, a hash or float bits
};
```

For a collider's `Enter`, the sender is the trigger and the instigator and value are the object
that walked in. A door three hops later still sees that object as the instigator.

### 3.7 The door example in detail

What the prefab file contains after you draw the two wires (other fields left out):

```json
{ "name": "TriggerZone",
  "components": [
    { "id": 5, "name": "Collider", "uuid": 111,
      "events": { "out": { "Enter": ["prefab::TriggerZone.Enter"],
                           "Exit":  ["prefab::TriggerZone.Exit"] } } } ] }

{ "name": "Door",
  "components": [
    { "id": 0, "name": "Code", "uuid": 222, "data": { "script": 333 },
      "events": { "in": { "Open":  ["prefab::TriggerZone.Enter"],
                          "Close": ["prefab::TriggerZone.Exit"] } } } ] }
```

The door script:

```cpp
namespace P64::Script::C0000000000000333
{
  P64_DATA( float target; );

  P64_EVENTS_IN(Open, Close);
  P64_EVENTS_OUT(Opened, Closed);

  void onInput(Object& obj, Data* data, In input, const Event& ev)
  {
    switch(input) {
      case In::Open:  data->target = 1.0f; fire(obj, data, Out::Opened); break;
      case In::Close: data->target = 0.0f; fire(obj, data, Out::Closed); break;
    }
  }
}
```

To close the door half a second after the exit, you place a Delay object between `Exit` and
`Close`. Wires themselves carry no options.

## 4. Runtime (engine)

### 4.1 New and touched files

The feature is fork-only. New code goes into new files, and hooks in existing upstream files stay
small and carry the fork's `// Fork:` marker so that upstream merges stay easy.

| File | Change |
|---|---|
| `n64/engine/include/events/types.h` (new) | `Event`, ids, the `_event` and `_pin` literals, scope kinds |
| `n64/engine/include/events/bus.h` (new) | public API (4.3) |
| `n64/engine/include/events/pins.h` (new) | pin tables of engine components, shared with the editor |
| `n64/engine/src/events/busCore.cpp` (new) | queue and subscriptions; no libdragon includes, so host tests can compile it |
| `n64/engine/src/events/bus.cpp` (new) | scope resolution, object delivery, direct sends, interrupt mailbox |
| `n64/engine/include/scene/components/eventLinks.h` and `.cpp` (new) | hidden component, fork id 26 (4.4) |
| `n64/engine/Makefile` | add `src/events` to the source list |
| `n64/engine/include/scene/objectFlags.h` | `PREFAB_ROOT` (bit 8), `HAS_EVENT_LINKS` (bit 9) |
| `n64/engine/include/scene/componentTable.h`, `src/scene/componentTable.cpp` | `onInput` per component; entry for id 26 |
| `n64/engine/include/script/scriptTable.h`, `scene/components/code.h` | `onInput`, `onCollisionEnter`, `onCollisionExit` and the input-name table in `ScriptEntry` |
| `n64/engine/include/script/userScript.h` | `P64_EVENTS_IN`, `P64_EVENTS_OUT`, `fire()` |
| `n64/engine/include/script/globalScript.h`, `data/scripts/globalScriptTable.cpp` | `P64_GLOBAL_EVENTS_IN`, `P64_GLOBAL_EVENTS_OUT` and the generated glue for global script pins (5.5) |
| `n64/engine/src/scene/scene.cpp` | three drain calls, life-cycle posts, keep the new flags on a spawned root |
| `n64/engine/src/scene/sceneManager.cpp`, `src/main.cpp` | global life-cycle posts, reset-button handler, controller and pak polling |
| `n64/engine/src/scene/object.cpp` | fire `Destroyed` when removal is requested |
| `n64/engine/src/collision/collisionScene.cpp` and `.h` | enter/exit pair tracking (4.6) |
| component headers and sources | pins (4.7) |

### 4.2 Data and memory budget

All storage is fixed-size. Sizes are compile-time settings passed through `ENGINE_CXXFLAGS`, the
same way the physics tuning flags are.

| Structure | Default | Memory |
|---|---|---|
| Event queue (ring) | 128 entries of 16 bytes | 2 KB |
| Subscriptions (pool with a hash index over id plus scope instance) | 512 entries of 20 bytes, 512 index slots | 11 KB |
| Interrupt mailbox | 16 entries | under 1 KB |
| Per object with bindings | 8 bytes plus 8 bytes per binding | data dependent |

Measured after phase 1: the whole bus is 13,600 bytes on the N64.

Objects without bindings cost nothing: they have no hidden component, and `fire()` returns after
testing one flag. Enter/exit tracking only runs for colliders that have a bound pin or a script
callback. There is no timer table, because delays live in delay objects.

### 4.3 C++ API

```cpp
namespace P64::Events
{
  // Fire a declared output pin. Scripts use the fire(obj, data, Out::X) helper instead.
  void fire(Object& obj, const void* component, uint8_t outPin,
            uint32_t value = 0, uint16_t instigator = 0);

  // Post by name. The namespace decides the scope.
  void post(EventId id, uint32_t value = 0, uint16_t sender = 0);     // global, scene, ui, declared
  void postTo(Object& inScopeOf, EventId id, uint32_t value = 0,
              uint16_t sender = 0);                                   // prefab, object
  bool postFromInterrupt(EventId id, uint32_t value = 0);             // global only

  // Send to one object's input by pin name. Replaces Scene::sendEvent.
  void send(Object& target, PinName input, uint32_t value = 0, uint16_t sender = 0);

  // Listen from code that is not wired in the editor.
  Listener listen(EventId id, ListenerFn fn, void* user = nullptr);
  Listener listenIn(Object& inScopeOf, EventId id, ListenerFn fn, void* user = nullptr);
  void unlisten(Listener listener);
}
```

- `postTo` is how code reaches into one spawned prefab:
  `postTo(*spawned, "prefab::Activate"_event)` resolves the instance from the object it is given.
  With a numbered name it goes up from there: `postTo(obj, "prefab_1::EnemyDied"_event)` posts
  into the scope of the prefab that contains `obj`'s prefab.
- `send(*target, "TakeDamage"_pin, handle, obj.id)` reaches every script on `target` that declares
  an input named `TakeDamage`, and no other component. It goes through the same queue and drains
  as everything else. Nothing has to be authored in the editor for it.
- A reply is another `send` to `ev.sender`.
- Global scripts declare pins for what they offer (5.5). `listen` is for hearing someone else's
  event from code, for example a system that reacts to another system's output or to an engine
  event.

### 4.4 The hidden EventLinks component

- The build adds one `EventLinks` component (fork id 26) to every object that has bindings. It is
  never shown in "Add Component" and never stored in project files, so its id can change later
  without a migration.
- Its init data is the list of baked bindings: component index, pin index, direction, event id,
  scope kind and how many prefab roots to skip.
- On init it resolves each scope instance by climbing the parents, subscribes the inputs and keeps
  the outputs in a small sorted table. On destroy it unsubscribes.
- It also carries the object's own output pins `Ready`, `Enabled`, `Disabled` and `Destroyed`.
- A component record is limited to 1020 bytes (`src/build/sceneBuilder.cpp`), which is about 120
  bindings on one object. The build reports an error beyond that.
- Inputs are delivered to disabled objects too, as the existing messages are, so `Enable` works.

### 4.5 Events the engine fires by itself

| Event | When | Value |
|---|---|---|
| `global::Engine.GameInit` | after the game-init hook | |
| `global::Engine.SceneLoading` | before a scene is created | scene id |
| `global::Engine.SceneLoaded` | first frame, after every `onReady` | scene id |
| `global::Engine.SceneUnloading` | before the scene is deleted, delivered immediately | scene id |
| `global::Engine.SceneUnloaded` | after it is deleted | scene id |
| `global::Engine.ResetPressed` | N64 reset button, from libdragon's reset interrupt | |
| `global::Engine.ControllerConnected`, `global::Engine.ControllerDisconnected` | joypad state changed | port |
| `global::Engine.PakInserted`, `global::Engine.PakRemoved` | Rumble Pak or Controller Pak changed | port and pak type |
| `scene::Loaded` | first frame, after every `onReady` | scene id |
| `scene::Unloading` | before the scene is deleted, delivered immediately | |
| `prefab::Spawned` | the instance and all its objects are ready | |
| `prefab::Destroying` | removal of the instance root was requested | |
| `prefab::Enabled`, `prefab::Disabled` | the instance root changed its active state | |

- The scene code posts these directly for every flagged root. When nobody listens the cost is one
  failed lookup.
- Removal events fire when `remove()` is called, not when the object is deleted at the end of the
  frame, so listeners still find the object.
- Controllers and paks are polled once per frame and compared with the previous state.
- A level gets no events of its own. Its prefab receives `prefab::Spawned` and
  `prefab::Destroying` like any other prefab, and the game's LevelDriver posts whatever `scene::`
  events it wants around a level change.
- There is no DMA-done event. A game that needs one registers its own handler and calls
  `postFromInterrupt`.
- The engine's global events carry the system name `Engine`, just as every other global event
  carries the name of the system that declares it.
- Everything else is the game's. Saves are an example: the engine has no save system and gets
  none here, so the game's own save system declares the pins, for example an input
  `global::Save.Requested`. For Kentucky Castle the level transition events are outputs of its
  `LevelDriver` system (5.5).
- A global script that wants an engine event uses its existing hooks, or calls `Events::listen`.

### 4.6 Collider enter and exit

- `CollisionScene::dispatchCollisionCallbacks` already builds a list of unique object pairs every
  physics step. The plan keeps the previous step's list and compares the two. A pair that is new
  is an enter; a pair that is gone is an exit.
- Pairs are stored as object ids, so an exit is still reported when one object was removed or
  disabled while inside.
- Two sleeping rigid bodies stop producing contacts. For solid (non-trigger) pairs such a pair is
  carried over instead of being reported as an exit.
- The Collider and Collision-Mesh components get four outputs: `Enter` and `Exit` for every
  object, and `Occupied` and `Emptied` for the first object in and the last object out. The value
  and the instigator are the other object's id.
- Scripts get two optional callbacks, `onCollisionEnter` and `onCollisionExit`, next to the
  existing `onCollision`.
- The existing rule stays: a collider only notices objects whose layer it reacts to. Filtering to
  "the player only" is done with collision layers, as the game does today.

### 4.7 Pins on engine components

Pin tables live in one engine header that the editor also includes, so both sides agree. The
editor already includes engine headers this way (`src/build/sceneBuilder.cpp:16`).

| Component | Inputs | Outputs | Phase |
|---|---|---|---|
| Object (every object) | Enable, Disable, ToggleEnabled, Destroy, Show, Hide | Ready, Enabled, Disabled, Destroyed | 3 |
| Collider, Collision-Mesh | | Enter, Exit, Occupied, Emptied | 3 |
| Audio (2D) | Play, Stop | Finished | 3 |
| Particle Emitter | Play, Stop | | 3 |
| Model (Animated) | PlayMain, PlayBlend (value is the clip index) | MainFinished | 3 |
| Camera | Activate | | 3 |
| Light | TurnOn, TurnOff | | 3 |
| Culling | | BecameVisible, BecameHidden | later |
| Rigid-Body | | Sleep, Wake | later |
| Character-Body | | Landed, LeftGround | later |
| Code | declared by the script | declared by the script | 2 |
| Node Graph | none: node graphs stay as they are | | |

### 4.8 Retiring the old addressed messages

The goal is that nothing in the game uses `Scene::sendEvent` and `onEvent` any more. They are not
removed from the engine, because upstream code, the engine's own enable and ready notifications
and the node-graph "Send Event" node use them.

1. **Both systems run side by side** from phase 1. Nothing breaks.
2. **Direct sends** (phase 2) give every addressed message a replacement: the message type becomes
   an input name, `onEvent`'s switch becomes `onInput`'s switch, and only scripts that declare the
   input receive it. That also removes the problem recorded in `KC/docs/LevelSystem.md` §11.6,
   where every component on the target reacted.
3. **The Message Bridge object** (phase 3) converts in both directions while scripts are moved one
   at a time.
4. **Kentucky Castle migrates** in phases 8 and 9: first the notifications that fit wires, then
   the request-and-reply and handle-carrying messages onto direct sends.
5. **Deprecation** (end of phase 9): `Scene::sendEvent` gets a `[[deprecated]]` attribute and the
   build warns about any script that still defines `onEvent`. Engine-internal callers and the
   node-graph node switch to an internal function with the same behaviour, so they stay silent and
   node graphs behave exactly as before. The engine Makefile gets
   `-Wno-error=deprecated-declarations`, so that a future upstream merge that adds a new caller
   warns instead of breaking the build.

## 5. Scripts

### 5.1 Declaring pins

```cpp
P64_EVENTS_IN(Open, Close);       // becomes: enum class In  : uint8_t { Open, Close };
P64_EVENTS_OUT(Opened, Closed);   // becomes: enum class Out : uint8_t { Opened, Closed };

void onInput(Object& obj, Data* data, In input, const Event& ev);   // one handler, one switch
fire(obj, data, Out::Opened);                                       // optional value argument
```

- One macro per direction keeps parsing simple and costs one function pointer per script. It
  mirrors the existing `onEvent` switch. Macros also avoid the unknown-attribute warning that
  engine code turns into an error.
- Saved bindings refer to pins by name. The build maps names to indices each time. A binding whose
  pin no longer exists is kept in the file, shown in red in the editor, and reported by the build.
- `Utils::CPP::hasFunction` matches `onInput` anywhere in the file, including a helper struct
  method with the same name. The new-script template says so.

### 5.2 What the build generates

- `src/utils/codeParser.cpp` gets `parseEventPins`, tested in a new fork test file.
- `src/build/scriptBuilder.cpp` adds `.onInput`, `.onCollisionEnter` and `.onCollisionExit` to
  each `ScriptEntry`, using the same text detection as the other callbacks, plus a small table of
  the script's input-name ids for direct sends.
- `data/scripts/defaultObject.cpp` (the new-script template) gains a commented example.

### 5.3 Engine-shipped scripts

No mechanism for this exists today (FINDINGS section 5). The plan adds one:

- Scripts live in `n64/engine/scripts/`. The editor already copies `n64/engine` into every project,
  so they arrive in `<project>/engine/scripts/`.
- `data/build/baseMakefile.mk` compiles that folder with the project's flags plus
  `-Wno-attributes`. They are compiled per project because `userScript.h` includes the project's
  generated asset and scene tables.
- The asset manager scans the folder, marks the scripts read-only and lists them under "Engine" in
  the script list. Each has a fixed UUID.
- They use the same pin macros as game scripts, so a game can copy one as a starting point.

### 5.4 Built-in event objects

Each is an object with one engine script. Options are ordinary exposed script values, edited on
the node or in the inspector. Because one output pin can be bound to several events, a single
output already fans out. All of them ship in phase 3.

Every object that keeps state has a `Reset` input. Reset returns it to the state its options
describe and cancels anything pending.

| Object | Inputs | Outputs | Options |
|---|---|---|---|
| Relay (your event repeater) | Trigger, Enable, Disable, Reset | Fired | starts enabled |
| Delay | Start, Cancel, Reset | Elapsed | seconds, repeat |
| Frame Delay | Start, Cancel, Reset | Elapsed | frames, repeat |
| Once | Trigger, Reset | Fired | |
| Counter | Add, Subtract, Reset | Reached, Changed | start, target |
| All | A, B, C, D, Reset | Complete | inputs needed, auto reset |
| Compare | Test | Equal, Less, Greater | constant |
| Gate | Trigger, Open, Close, Toggle, Reset | Passed, Blocked | starts open |
| Toggle | Trigger, SetA, SetB, Reset | A, B | |
| Sequence | Next, Reset | Step1 to Step8 | steps, loop |
| Random | Trigger, Reset | Out1 to Out4 | outputs, no repeats |
| Log | Trigger | | text; prints the event and value in debug builds |
| Scene Loader | Load | | scene |
| Spawner | Spawn, DespawnAll, Reset | Spawned | prefab |
| Message Bridge | Trigger | Received | sends or receives an old addressed message; target self, parent or children |

- The two delay objects pass on the value and the instigator of the event that started them.
- "Relay" is the name because "repeater" already means a list repeater in the canvas editor.
- The Message Bridge is removed from the game once phase 9 is done.

### 5.5 Pins on global scripts: systems

A global script is not an object, so it has no bindings to store. It declares its pins as part of
a named **system**, and every pin is one global event whose name includes that system.

```cpp
namespace P64::GlobalScript::CCE819444C0320B6
{
  // the system name comes first, then the pins
  P64_GLOBAL_EVENTS_IN(LevelDriver, LoadDefault, Reload);
  P64_GLOBAL_EVENTS_OUT(LevelDriver, TransitionStarted, TransitionFinished);
  P64_GLOBAL_EVENTS_OUT(SpawnDirector, WaveCleared);

  void onInput(LevelDriverIn input, const Event& ev)   // one handler per system that has inputs
  {
    switch(input) {
      case LevelDriverIn::LoadDefault: /* ... */ break;
      case LevelDriverIn::Reload:      /* ... */ break;
    }
  }

  void onSceneUpdate()
  {
    if(fadeOutJustBegan) fire(LevelDriverOut::TransitionStarted, levelNameHash);
  }
}
```

- **A pin `X` of system `S` is the event `global::S.X`.** The example declares
  `global::LevelDriver.LoadDefault`, `global::LevelDriver.Reload`,
  `global::LevelDriver.TransitionStarted`, `global::LevelDriver.TransitionFinished` and
  `global::SpawnDirector.WaveCleared`.
- **There are no ambiguities.** A system name can be declared by one global script only, and a pin
  name once per system; the build fails otherwise. Two systems may both have a pin called `Reset`,
  because the event names differ: `global::LevelDriver.Reset` and `global::SpawnDirector.Reset`.
- **Every global event has an owner.** A `global::` name must be a declared pin of a system, or one
  of the engine's events under the system name `Engine`. A free `global::Something` typed into a
  pin is a build error. A game-wide event that belongs to no system uses a namespace the game
  declares, such as `mission::Started`, where typing still declares.
- The system name is written in the macro and does not come from the file name. Renaming or moving
  the file never renames an event, and one global script can declare several systems. That is how
  Kentucky Castle's single global script exposes each of its systems separately.
- **Direction is enforced.** An object's output can be bound to a system's input, and an object's
  input to a system's output. The other two combinations are build errors.
- The build reads the declarations while it parses global scripts and writes the glue into
  `src/p64/globalScriptTable.cpp`: the id of each output, and one listener per input. The
  listeners are registered once at game start, before `global::Engine.GameInit`, and stay for the
  whole game, across scene changes.
- A system's own code in another file fires the same event by name:
  `Events::post("global::LevelDriver.TransitionStarted"_event, levelNameHash)`.
- The macros have their own names because one file may include both script headers, as Kentucky
  Castle's global script does. The generated enum names start with the system name
  (`LevelDriverIn`, `LevelDriverOut`) so that they cannot hide an existing `LevelDriver` namespace.
- In a graph these pins appear on the Global node, one collapsible region per system (8.2).

## 6. Project files

### 6.1 Bindings

- A component entry gets an optional `events` field with `in` and `out` maps from pin name to a
  list of event names (example in 3.7). Object pins use the same field on the object.
- A binding is only a name. There are no per-wire options.
- Keys are written in sorted order so that files diff cleanly.
- The field is optional and old files load unchanged, so `FILE_VERSION` does not need a bump
  (`src/project/scene/migration.h`).

### 6.2 Prefab instances: exposed pins and overrides

- The pins of a prefab's root object are exposed automatically. In the prefab's graph any other
  pin can be marked **exposed**, optionally with an alias.
- An instance node in an outer graph shows the exposed pins. Wiring to one adds a binding for that
  instance only, stored on the instance node:

```json
"eventOverrides": [
  { "path": [3952883393], "comp": 14031328811151779769, "pin": "Open", "dir": "in",
    "add": ["scene::Lever.Pulled"],
    "hint": "MinionSpawner / Code (Door)" } ]
```

- `path` holds object uuids from the instance root down to the target and `comp` is the component
  uuid; these identify the pin. `hint` is rewritten on every save for human readers and ignored on
  load.
- An override can add bindings (`add`) and can silence a binding that comes from the prefab
  (`mute`).
- A parent link (3.4) is repointed with a `link` entry in the same list. `link` is the name as the
  nested prefab writes it, and `to` is a name in the graph that contains the instance. In this
  example the Gate's `prefab_1::Alarm` would normally land on the Encounter's `prefab::Alarm`; the
  entry sends it one level further up, to the Encounter's own parent:

```json
"eventOverrides": [
  { "link": "prefab_1::Alarm", "to": "prefab_1::Alarm",
    "hint": "Gate hears the alarm of the Encounter's parent" } ]
```
- The existing property overrides use opaque hashed keys (FINDINGS section 2). Event overrides get
  this readable form instead because you asked for text that can be merged.
- The scene builder, the inspector and the graph all have to walk overrides the same way. The
  walking code is written once and shared.

### 6.3 Graph layout

Node positions, collapsed categories, comment groups and the view are editor-only data. They go in
a sidecar file next to the scene (`data/scenes/<id>/events.layout.json`) or prefab
(`<name>.prefab.events.json`), so moving nodes never touches the scene file. Node moves are not
covered by undo.

### 6.4 The optional catalog

The catalog is never required for an event to exist. It does two things:

- It declares the game's extra namespaces. This is the only place they can come from.
- It holds descriptions, shown as tooltips and in pickers.

Engine defaults ship in the editor's `data/events/*.json`. A game adds files in
`<project>/events/*.json`. Several small files merge better than one large one.

```json
{ "namespaces": { "mission": { "doc": "Mission scripting" } },
  "events": {
    "global::LevelDriver.TransitionStarted": { "doc": "The fade-out began",
                                               "value": "level name hash" },
    "mission::Started": { "doc": "A mission began", "value": "mission id" } } }
```

## 7. Build

New steps in `Build::buildProject` (`src/build/projectBuilder.cpp`):

1. Load the engine and project catalogs.
2. Read pin declarations while object scripts and global scripts are parsed, and scan the
   sources for `_event` and `_pin` literals.
3. While each scene and prefab is written, collect every object's bindings and overrides, follow
   each numbered name up through the containing instances (applying repointed links, 3.4), check
   them, compute scope kind and roots to skip, map pin names to indices, hash the names, write
   the EventLinks record and set the new object flags.
4. Write `src/p64/eventNames.cpp`, the debug name table. It is one file that no script includes.
5. Print a report and keep it for the editor.

Checks, each reported with the object and pin or the source line:

| Check | Severity |
|---|---|
| Two different names with the same id | error |
| Malformed name, or a namespace that is neither built in nor declared | error |
| `prefab::` or a numbered prefab namespace used in a binding written in a scene | error |
| A `global::` name that is not a declared pin of a system or of the engine | error |
| A system name declared by two global scripts, a pin declared twice in one system, or a system named `Engine` | error |
| An output bound to a system's output, or an input bound to a system's input | error |
| More bindings on one object than a record can hold | error |
| Binding to a pin the script or component no longer has | warning |
| Input bound to a scoped event that nothing in that scope fires | warning |
| A numbered prefab namespace that points above the outermost prefab of a placed instance | warning |
| A repointed link whose prefab no longer uses that name | warning |
| Loop that contains no delay object | warning |
| Two names that differ only by case, or by one character | warning |
| A name that is listened to but never fired or posted anywhere in the project | warning |
| Direct send to a pin name that no script declares | warning |
| Output bound to an event nothing listens to | info |

The checks for near-identical names, for names that are listened to but never fired, and for
outputs nobody listens to are how a typo gets caught now that names need no declaration.

## 8. Editor

### 8.1 Events section in the object inspector (first)

Each component in the inspector gets a foldable "Events" block that lists its pins. Each pin shows
its bindings as editable rows: a name field that completes from every name used in the project and
from the catalog. Prefab instances show overrides with the same lock control used for properties.
This arrives in phase 2, before the graph, and stays afterwards for quick edits.

### 8.2 The Event Graph window

- A new dockable window titled **Events**. The hierarchy window is already titled "Graph", so
  "scene graph" would be ambiguous.
- It shows the graph of the current scope: the open scene, or the prefab being edited in prefab
  edit mode. A focus mode shows a single object and uses `object::` events.
- **One node per object.** Inside it, one category for the object's own pins and one per
  component; a Code component is titled with its script name. A collapsed category shows only
  pins that have wires. Inputs sit on the left, outputs on the right.
- **Which objects appear:** every object with at least one binding, plus any object you add by
  dragging it from the hierarchy or through the search menu.
- **Drawing a wire** from an output to an input binds both pins to one event. If the output
  already has an event created by a wire in this graph, the input joins it. Otherwise the editor
  creates `<scope>::<ObjectName>.<Pin>`, made unique within the scope. Renaming the object later
  does not rename the event.
- The namespace of a created event follows the graph: `scene::` in a scene graph, `prefab::` in a
  prefab graph, `object::` in focus mode.
- **Event nodes** stand for a name you type or pick, such as `scene::Loaded` or `scene::Alarm`.
  Wiring an output into one fires that event; wiring it into an input listens.
- **Scope nodes** list the automatic events of the scope: scene events in a scene graph and
  prefab events in a prefab graph.
- **The Global node** can be added to any graph from the search menu. It has one collapsible
  region per system, showing that system's inputs and outputs, and a region named `Engine` for
  the events the engine fires. A collapsed region shows only the pins that have wires. Wiring an
  output to one of its inputs, or one of its outputs to an input, binds the other end to that
  pin's name, for example `global::LevelDriver.TransitionStarted`. Nothing else is stored: the
  node is not an object in the scene and adds nothing to the ROM. A pin bound to such a name by
  hand is drawn as a wire to the Global node as well.
- A wire from a node to itself is allowed. Loops with no delay object are outlined as a warning.
- **Built-in objects** can be added from the search menu; this creates the object in the scene or
  prefab and its node in one step.
- Selecting a node selects the object in the hierarchy and inspector, and the other way round.
- Selecting a pin highlights everything downstream of it.
- Broken pins and suspicious names are drawn in red with the build's message as a tooltip.

### 8.3 Prefab graphs and instance nodes

- In prefab edit mode the window shows the prefab's own graph and lets you mark pins as exposed.
- In an outer graph a prefab instance is one node showing its exposed pins, grouped by inner
  object, with a button that opens the prefab's graph.
- An instance node also lists the parent-scope events its prefab uses, with one level taken off
  the number. A Door that listens to `prefab_1::Alarm` shows an `Alarm` link on its node in the
  Encounter's graph, drawn as a wire to whatever fires `prefab::Alarm` there. A `prefab_2::` name
  shows as `prefab_1::` and is passed on upward. This is how you trace where an event comes from.
- A parent link on an instance node is a pin like any other: drag it to another event to repoint
  it for that instance. When you nest content deeper, the editor repoints the links for you (3.4).
- Inside a prefab's own graph the name picker suggests parent events: it looks at the prefabs
  that nest this one and offers their `prefab::` events as `prefab_1::` names.
- A Kentucky Castle level is a prefab, so its graph is an ordinary prefab graph.

### 8.4 Reuse of the node-graph code

- Reused as is: the ImNodeFlow fork in `vendored/ImNodeFlow` (zoom, pins, links, waypoints,
  groups, box select). Event nodes subclass `ImFlow::BaseNode` directly.
- Copied as patterns: the search menu, the unsaved-changes handling, window restore.
- Not reused and not changed: `Graph`, `Node::Base`, `ScriptNode`, the node registry,
  `NodeEditor`. They are tied to code generation.
- Two things the node-graph format does that would hurt here are avoided: wires keyed by pin
  position, and nodes saved in memory-address order.

### 8.5 Undo and saving

- Bindings written in a scene live in `scene.json`, so the existing scene undo covers them.
- Edits to a prefab definition are probably not undoable today, and the same would hold for prefab
  graph edits. Extending undo to prefab definitions is listed as optional work in phase 5.
- Layout sidecars are saved with the scene or prefab and are not part of undo.

## 9. UI canvases

Facts: a canvas is a global asset, not an object; data flows only from the game into the canvas;
canvas events exist but only reach element scripts and can only be sent from inside the model
callback (FINDINGS section 3).

The UI both receives and emits events. Canvas data stays one-way: events never write canvas
variables.

- Each canvas gets pins generated from its content.

| Direction | Pin | Meaning |
|---|---|---|
| in | `Show`, `Hide` | enable or disable the canvas |
| in | `PlayClip.<clip>`, `StopClip.<clip>` | control a timeline clip |
| in | `Event.<name>` | deliver one of the canvas's own events to its element scripts |
| out | `Activated.<element>`, `Focused.<element>`, `Blurred.<element>` | focus navigation on focusable elements |
| out | `ClipFinished.<clip>` | a clip reached its end |
| out | `Marker.<name>` | a marker on a clip's timeline was reached |

- **Timeline markers.** The timeline gets a third marker action, "Fire output", next to the
  existing "Fire event" and "Seek". Each named marker becomes a `Marker.<name>` output pin.
- Canvas bindings are stored in the `.canvas` file. A canvas has no place in the object tree, so it
  can only use `ui::`, `scene::`, `global::` and game-declared namespaces. A wire from an object to
  a canvas pin therefore creates a `ui::` event, whichever graph it is drawn in.
- Generated code registers listeners in `uiInitializeBindings()`. The runtime gains
  `UI::post<Scene>(event, value)`, which queues a canvas event from outside the model callback.
- A canvas appears in a graph as a canvas node that you add from the search menu.
- Canvas event ids currently start at 0 and overlap the old message range. The new pins use bus
  ids, so the overlap stops mattering for anything wired through the bus.

## 10. Other integrations

- **Kentucky Castle AI scripts.** Two new opcodes appended to the opcode list: `FireEvent(name)`
  and `IfEvent(name, label)`. The AI compiler is Python, and `zlib.crc32` gives the same ids as
  the engine. `FireEvent` posts by name, resolving `prefab::`, the numbered parent namespaces and
  `object::` from the enemy object. For `IfEvent`, the enemy script calls `listenIn` at init for
  every name its AI script tests and latches what arrives.
- **Debug.** A debug-menu page lists the most recent events with names, senders, values and
  listener counts, plus counters for the queue, dropped events and subscriptions in use. A
  compile flag prints every delivery through `debugf`.
- **Node graphs** are left exactly as they are.

## 11. Phases

Every phase ends with the editor tests green and the runtime test project showing OK for its new
scenes. Commits follow the fork's style (`engine: ...`, `editor: ...`, `docs: ...`) and carry no
co-author or AI attribution.

### Phase 0: approval

- **Done when** you have approved this plan.

### Phase 1: runtime core

- Bus core, API, literals, three drain points, scene and global life-cycle events, interrupt
  mailbox, reset-button, controller and pak events, trace flag.
- Host unit tests for the core: order, scope isolation by instance, hop limit, budget carry-over,
  unsubscribing during delivery.
- **Done when** a global script can listen to `scene::Loaded` and post an event that another
  listener receives the same frame, in the emulator.

### Phase 2: script pins, bindings, direct sends and the inspector

- Pin macros, parser and tests, `onInput` and the input-name table in the script table, pins on
  global scripts with their generated glue, direct sends, EventLinks component, `events` field in
  component and object JSON, bake step for scene and object scope, catalog loading, source scan,
  debug name table, first checks, the inspector's Events section.
- **Done when** the door example works in one scene with a hand-written trigger script and
  bindings typed in the inspector, a mistyped name is reported by the build, one script can
  `send` to another by pin name, and a global script input receives an event that an object fired.

### Phase 3: engine pins and built-in objects

- Enter/exit tracking, collider pins, script enter/exit callbacks, object pins, the component pins
  marked phase 3 in 4.7, the engine-script mechanism, all built-in objects in 5.4.
- **Done when** the door example needs no custom trigger code, and a Relay, a Delay and a Frame
  Delay can be placed, wired and reset from the inspector.

### Phase 4: prefab scope

- The two object flags, scope resolution including the numbered parent namespaces, prefab
  life-cycle events, exposed pins, `eventOverrides` with repointed parent links, nested prefabs,
  `postTo` and `listenIn`.
- Scope-preserving fix-ups in every editor operation that moves content across a prefab boundary:
  "To Prefab", "Unpack", and nesting or moving objects while editing a prefab (3.4).
- **Done when** runtime tests pass for: five door instances that do not affect each other; a door
  in a nested prefab that hears its parent through `prefab_1::` and its grandparent through
  `prefab_2::`; the same door with no parent, where the numbered binding stays silent; and a
  prefab spawned from code. In the editor, wrapping a nested door in a new prefab and then
  unpacking it again must leave the door hearing the same alarm both times.

### Phase 5: the Event Graph window

- Window, object nodes with collapsible categories, wires derived from shared names, automatic
  naming, event nodes, scope nodes and the Global node, layout sidecar, selection sync, search
  menu, prefab graphs, instance nodes with parent links, focus mode, red marking of problems.
  Optional: undo for prefab definitions.
- **Done when** the example can be built entirely by dragging wires, and the resulting diff of
  the scene and prefab files contains only binding lines.

### Phase 6: UI canvases

- Canvas pins and bindings, generated glue, `UI::post`, outputs for focus and clip end, the
  "Fire output" timeline marker, the canvas node.
- **Done when** an object's output plays a HUD clip, a marker on that clip fires an event a script
  receives, and a focusable element's activation reaches a script input.

### Phase 7: tools, manual and example

- Debug-menu page, event browser with find usages and rename, downstream highlight, manual pages
  for events and for every component with pins, an example project `n64/examples/events`,
  changelog entry.
- **Done when** the example project builds from a clean checkout and the manual describes every
  pin.

### Phase 8: Kentucky Castle adoption (on the game's branch)

1. The global script declares a `LevelDriver` system with the outputs `TransitionStarted` and
   `TransitionFinished`, and LevelDriver fires them. The `isTransitioning()` polling in
   EnemySpawner and SpawnDirector becomes listening. Level loading itself stays game code and the
   engine gets no notion of a level.
2. LevelTransition, VisibilityVolume, StatusEffectVolume and EncounterPlayer use engine
   enter/exit instead of their own schemes.
3. Pins on EncounterVolume (Begin, Reset, Complete), EnemySpawner (Activate, Pause, Stop),
   Damagable (Died), Player (SetControl) and CameraBoom (Reset, Lock, ClimbMode, EncounterMode).
   The relays in `KC/src/user/scripts/Player.cpp:3029-3043` go away.
4. HUD hookups through `ui::` events.
5. `FireEvent` and `IfEvent` opcodes in the AI compiler and the enemy script.

- **Done when** the game plays as before with those five items in place and the notification-style
  messages listed in FINDINGS section 4 are no longer sent.

### Phase 9: retire the old messages (both repos)

- Move the remaining Kentucky Castle messages onto direct sends: target requests and replies,
  attack slots, spawner holds, damage and status-effect handles, projectile setup, despawn.
- Remove the Message Bridge from the game and delete `KC/src/user/global/CustomEvents.h`.
- Apply the deprecation described in 4.8.
- **Done when** the game builds with no deprecation warning.

### Merge order

The game's CI builds with the latest released Pyrite64 AppImage. So `feature/event-graph` in
`pyrite64` has to be merged into `stream/full-frontal` and released before the game's branch can
pass CI. Locally the game branch builds against the editor built from the engine branch.

## 12. Testing

- **Host unit tests** (doctest, `tests/`): pin parsing for object and global scripts, name rules
  and CRC parity with the engine,
  catalog merging, the bake step including roots to skip and repointed links, the scope fix-ups
  for nesting and unpacking, and the bus core. The bake logic and the
  bus core are written without ImGui or libdragon so the test target can compile them. Today that
  target only builds the code parser.
- **Runtime test project** `n64/tests/test_events`, modelled on `n64/tests/test_obj_states`: each
  scene records what it received and compares it with the expected sequence, printing OK or FAIL
  on screen and through `debugf`. It is run by hand in an emulator, like the existing one. `ares`
  is installed on this machine and the game's Makefile already pipes its output, so a scripted
  run that searches for FAIL is possible but not yet proven.
- **Example project** `n64/examples/events`: the door prefab placed five times, a scene graph, a
  nested prefab and a HUD clip.
- **Kentucky Castle** itself is the integration test for phases 8 and 9.

## 13. Risks and limits

- **Upstream merges.** The plan touches `scene.cpp`, `object.cpp`, `collisionScene.cpp`,
  `components.h`, `sceneBuilder.cpp`, `scriptBuilder.cpp` and `canvasBuilder.cpp`. Hooks are kept
  to a few marked lines each.
- **Timing differs from the old messages.** New events arrive the same frame; old messages sent
  from a script arrive the next frame. Each script moved in phases 8 and 9 has to be checked for
  code that relied on the one-frame delay.
- **Names need no declaration, so a typo creates a new event.** The name checks in section 7
  exist for this, and they only run at build time.
- **Object ids are reused.** Spawned ids come from a 16-bit counter that is never reset and has no
  guard against wrapping. Subscriptions are removed when their object is destroyed, so a recycled
  id cannot inherit them, but a queued direct send could reach a new object that took the id
  within the same frame. The counter itself is an existing weakness.
- **Text detection of callbacks** can be fooled by a helper with the same name.
- **Override walking** exists in four places today and events add another consumer. The shared
  walker in 6.2 is there to stop them drifting apart.
- **Prefab undo** and **layout undo** are missing (8.5).
- **UI component scripts** may skip their init when the model is bound late (FINDINGS section 3).
  Phase 6 has to check this before relying on element scripts.
- **Numbered prefab namespaces are relative.** Repointed links and the editor's fix-ups keep them
  right when nesting changes, but only for changes made through the editor. A hand edit, a merge,
  or an editor operation that was missed can leave a number pointing at the wrong parent. The build
  reports a number that reaches nothing; it cannot know that a number reaches the wrong scope.
- **Prefabs spawned from code have no instance node**, so their links cannot be repointed. Kentucky
  Castle's enemies are spawned under different parents, and their numbers have to be checked case
  by case in phase 8.
- **Deprecation meets upstream.** After phase 9 an upstream merge that adds a new `sendEvent`
  caller produces a warning that someone has to tidy up.

## 14. Decisions and remaining defaults

### Decided by the owner on 2026-11-23

| Topic | Decision |
|---|---|
| Delivery timing | Same frame, queued, three drain points |
| Payload | Sender, instigator and one value |
| Nested prefabs | A nested prefab hears a parent's events by number: `prefab::` (or `prefab_0::`) is the current prefab, `prefab_1::` its parent, `prefab_2::` the grandparent. Plain `prefab::` names stay local. (First answered as "isolated", then changed.) |
| Firing into a parent prefab | Allowed, with the same numbered names as listening |
| When nesting changes | Event references are offset automatically so that each keeps the scope it had |
| Global scripts | They declare pins too, always in the `global` namespace, and a Global node that can be added to any graph exposes all of them, one collapsible region per system |
| Global event names | Each includes the name of the system that declares it, `global::<System>.<Pin>`, so there are no ambiguities |
| What an instance shows | The root object's pins plus pins marked as exposed |
| Levels | No `level` scope and no engine notion of a level: a level is a prefab, and level loading stays game code. Nested prefabs hear the level's events by number. (First answered as "a flag on the prefab", then changed.) |
| Options on wires | None; delay and once-only behaviour use helper objects |
| Game-defined namespaces | Only declared ones, such as `mission::`; no built-in `custom::` |
| Pin declaration syntax | Two list macros and one `onInput` switch |
| Built-in objects | Engine scripts; every group in the first version; a time delay and a frame delay; a `Reset` input on each |
| Enter/exit for scripts | `onCollisionEnter` and `onCollisionExit` are added |
| UI direction | Receives and emits; canvas data stays one-way |
| UI inputs and outputs | Show and hide, clip play and stop, canvas events, and output events on a clip timeline; no variable writes |
| Hardware events | Reset button, controller plug and unplug, pak inserted and removed; no DMA-done event |
| Saves | Nothing in the engine; the game's own save system declares the pins |
| Graph layout | Sidecar file |
| Order | Runtime and inspector first, graph window after prefab scoping |
| Old messages | Aim to retire them |
| Sending to one object | By input pin name, with nothing authored in the editor |
| How far retirement goes | The game stops using them, then they are marked deprecated; not removed |
| Kentucky Castle scope | Level transition events posted by the game, engine triggers, pins on game scripts, HUD hookups, AI opcodes |
| Node graphs | Not touched; their behaviour stays the same |
| Upstream | Fork only |
| Disabled objects | Still receive inputs |
| Graph contents | Objects with wires plus objects you add |
| Names | The window is "Events"; the repeater is "Relay" |

### Defaults that were not asked and can still be changed

- Created events are named `<scope>::<ObjectName>.<Pin>` and keep that name when the object is
  renamed.
- Names are case-sensitive, with a warning for names that differ only by case.
- Limits: 16 hops, 256 deliveries per drain, 128 queued events, 512 subscriptions.
- Game-declared namespaces always behave like `global`.
- A numbered name that points above the outermost prefab matches nothing, and the build reports
  it where it can see it.
- The automatic offset is stored as a repointed parent link on the instance, and the containing
  graph can also repoint a link by hand.
- Only moves across a prefab boundary are offset. Copy, paste and duplicate keep names as written.
- On "To Prefab", events used only inside the selection become local to the new prefab.
- The system name is written in the macro, not taken from the file name, and one global script
  may declare several systems.
- Every `global::` event must belong to a system or to the engine. Free `global::` names are no
  longer accepted; an event with no owner uses a namespace the game declares.
- The engine's own global events are named with the system `Engine`.
- The Global node is a node in the graph, not an object in the scene.
- Global scripts use their own macros, `P64_GLOBAL_EVENTS_IN` and `P64_GLOBAL_EVENTS_OUT`.

### To settle while building

- The exact pin list of each engine component, reviewed when its phase starts.
- The full list of editor operations that move content across a prefab boundary, checked against
  `src/project/scene/scene.cpp` and `src/editor/selectionUtils.h` when phase 4 starts.
- Whether undo for prefab definitions is built in phase 5.
- Whether the runtime tests can be scripted through `ares`.

## 15. Out of scope

- Any change to node graphs.
- Removing `Scene::sendEvent` and `onEvent` from the engine.
- An engine save system.
- A `level` scope, and any engine notion of a level. Level loading stays in the game.
- Offering the feature to upstream Pyrite64.
- Options on wires, and writing canvas variables from events.
- Running the game inside the editor to watch events live.
- Sticky events and typed parameter lists (designed for, not built).
- Fixing unrelated issues found on the way; they are listed in FINDINGS.
