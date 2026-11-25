# Animation events: plan

Game code can give a **Model (Animated)** component named output pins, one per animation event, and
fire them. The engine learns the names only from a list the project provides, and it never times
anything itself. Kentucky Castle fills that list from its traits and fires the pins from its
animation drivers. Builds on the event graph (`../event-graph/`). Status and decisions:
[PLANNING-STATE.md](PLANNING-STATE.md).

## 1. What the owner asked for (2026-11-25)

- The animation events of an object's trait (`DefineEvent`) are available in the event graph, as
  output pins of the object's Model (Animated) component.
- The trait system stays in Kentucky Castle. The engine gets no trait concept: the traits forward
  their events through the animation component.
- Both pin shapes: one pin per clip and event (`Leap.LAND`), and one per event name across clips
  (`Any.LAND`). The value is the clip index.
- Guards (`RoleAnimDriver`) fire every trait event as a pin, not only the events that have a role.
- Every `DefineEvent` becomes a pin; nothing has to be marked.
- The pin constants that game code fires come from the trait generation, not from an engine helper
  (`animEventPin()` is gone from the engine).
- The test: RetroStar's footstep sounds and dirt particles move out of `Player.cpp` into his prefab,
  driven by the new pins.

## 2. What the code does today

| Fact | Where |
|---|---|
| `DefineEvent(clip, name, seconds)` becomes a per-clip array of times, `EventDescriptor {times, count}`. A body's `AnimBody` reuses the same descriptors | `KC/tools/trait_compiler.py` (`_generate_event_*`), `KC/src/user/global/constants/AnimDefs.h`, `KC/src/user/objects/AnimBody.h` |
| Two drivers detect the crossings and hand each event to one callback. `AnimationDriver<Traits>` (Player, Minion) reports every event; `RoleAnimDriver` (Enemy) drops events without a role | `KC/src/user/objects/AnimationDriver.h:243,303,315,322`, `RoleAnimDriver.cpp:226,247` |
| Both drivers skip all event work while no callback is bound | the `if (!m_eventCallback ...) return;` lines in both |
| The drivers hold the `Comp::AnimModel*` but no object. `ObjectDelegate` keeps an `ObjectRef` and resolves it on each call | `KC/src/user/objects/*Driver*`, `n64/engine/include/utility/ObjectDelegate.h` |
| `Events::fire(obj, componentData, pin, value)` takes any `uint8_t` pin and posts every output record of that component and pin. Conditions already use pins from 0x80 on Code components | `n64/engine/src/events/bus.cpp:399`, `include/events/pins.h:64` |
| Model (Animated) has fixed pins: PlayMain, PlayBlend in, MainFinished out | `include/events/pins.h`, `src/scene/components/animModel.cpp:284` |
| The editor gets component pins from `pinsOf()`; canvas pins are built from the canvas's content, which is the precedent for pins that come from data | `src/project/events/eventPins.cpp` |
| Bindings are saved by pin name. A pin that disappears keeps its bindings, shown greyed ("This pin is no longer declared") | `src/project/events/eventGraph.cpp:170`, `eventInspector.cpp:281` |
| The catalog (`events/*.json`) is read in four places: build, Events window, nesting, inspector. Unknown top-level keys are ignored, so an older engine reads a newer file without complaint | `src/project/events/eventRules.cpp:112` |
| The build knows each model's runtime asset index (`ctx.assetUUIDToIdx`) and project path (`AssetManagerEntry::projectPath`). At run time the component keeps `assetIdx`; `swapModel()` changes it | `src/project/component/types/compAnimModel.cpp:78`, `n64/engine/include/scene/components/animModel.h` |
| `"..."_pin` accepts identifiers only. `pinHash(str, len)` is constexpr and gives the same CRC-32 as the editor (and Python's `zlib.crc32`). `PinName` is `{uint32_t hash}` | `n64/engine/include/events/types.h` |
| The trait header names events as `AnimEvents::Type::<Clip>::<Event>`; game code reaches them as `AnimationTraits::EventType::Run::STEP_LEFT` | `KC/tools/trait_template.h.in` |
| The Player prefab's Model (Animated) uses `assets/retrostar.glb` | `KC/assets/prefabs/Player.prefab`, `KC/assets/retrostar.glb.conf` |
| Footsteps today: `Run.STEP_LEFT` and `STEP_RIGHT` play `runleft.wav` / `runright.wav` at volume 0.3 and replay a held dirt emitter at the foot bone (`left_Leg_Ball_SH_Jnt` 41, `right_Leg_Ball_SH_Jnt` 49). `Dodge.TAKEOFF` replays both emitters. The emitters are created in `init()` and destroyed on delete | `KC/src/user/scripts/Player.cpp:1155-1172`, `:1258-1271`, `:1722-1725`, `:1766-1768` |
| Audio (2D)'s Play stops the component's own sound and starts it again. Particle Emitter's Play replays at its object's position | `n64/engine/src/scene/components/audio2d.cpp:68`, `particle.cpp` (`onInput`) |
| A child object does not follow its parent's position (parenting only passes on enabled and visible); `HardAttach` copies another object's position each frame | `n64/engine/include/scene/object.h`, `KC/src/user/scripts/utility/HardAttach.cpp` |
| Inside a prefab, wires use `prefab::` names, heard only inside the same instance | `KC/docs/EventGraph/README.md` (Names and scopes) |
| The editor generates its code before `make` runs, and the trait compiler runs inside `make`. Anything the editor reads from the traits must exist before the build | `KC/Makefile.custom` |
| Both trait editing tools keep calls they do not model (`extraCalls` in TraitEditor, `Raw` records in anim_def_editor) | `KC/tools/trait_edit/trait-parser.js`, `KC/tools/trait_reader/anim_def_editor.py` |
| Kentucky Castle's CI builds with the latest `RetroZelda/pyrite64` release | `KC/.github/workflows/build-rom.yml` |

## 3. The design

### 3.1 Engine: the animation events of a model

A project lists, per model asset, the names of its animation events. Every Model (Animated)
component that uses that model gets one output pin per name, after its own pins. Game code fires
them by name. The engine does not know where the names come from or when they should fire.

**The list** is a new section of the event catalog, in any file of the project's `events/` folder:

```json
{
  "animationEvents": {
    "assets/retrostar.glb": {
      "doc": "RetroStar (traits/retrostar.trait). Generated by tools/trait_compiler.py, do not edit.",
      "events": [
        {"name": "Run.STEP_LEFT", "doc": "Run, at 0.65 s", "value": "the clip index"},
        {"name": "Leap.LAND",     "doc": "Leap, at 0.415 s", "value": "the clip index"},
        {"name": "Any.LAND",      "doc": "LAND of any clip: Dodge, Leap", "value": "the clip index"}
      ]
    }
  }
}
```

- The key is the model's project path, as the asset list shows it.
- A name uses letters, digits, `_` and dots, with no empty part (like canvas pins). Names are unique
  per model. At most `P64_ANIM_EVENTS_MAX` (64) per model.
- The file order is the order of the pins in the editor. Nothing outside one build depends on it:
  the build resolves every pin to its index and writes the runtime table from the same list.
- A model listed in two places is an error.

**Pins.** Model (Animated) keeps its inputs (PlayMain, PlayBlend) and MainFinished (index 0). The
events follow at index `ANIM_EVENT_PIN_BASE` (0x80) + their position in the list, so the
component's own output list can still grow.

**Firing from code:**

```cpp
#include "scene/components/animModel.h"

// fire the pin "Leap.LAND" of this object's animated model, with the clip index as the value
P64::Comp::AnimModel::fireEvent(obj, anim, pin, clip);
```

- `pin` is a `P64::Events::PinName` whose hash is `pinHash()` of the full pin name
  (`"Leap.LAND"`), the same CRC-32 the editor and the build compute. The engine has no helper for
  making one: the project supplies its pin constants. Kentucky Castle's trait compiler generates
  them (section 3.2); a project without traits can write
  `P64::Events::PinName{P64::Events::pinHash("Leap.LAND", 9)}`.
- `fireEvent(obj, data, pin, value = 0, instigator = 0)` returns at once when the object has no
  bindings, finds the current model's list, then calls `Events::fire()` with that pin's index. It
  returns false when the model does not list the name. That is not an error and gets no warning,
  because a model without a list is normal.
- **Run-time table.** The build writes `src/p64/animEvents.cpp`: for each listed model, its asset
  index and the pin hashes in list order. It is registered at boot next to the name table
  (`n64/engine/src/main.cpp:98`) and is always generated, empty when nothing is listed.
- The lookup is linear: first over the listed models, then over that model's names.
- After `swapModel()` the pins follow the model that is current at run time. Bindings are made
  against the model set in the editor, so they only match that model.

**Editor:**

- `pinsOf()` appends the model's events to a Model (Animated) component's outputs. `PinList` records
  where they start, like `conditionOut`, and `indexOf()` turns a position into 0x80 + n.
- `pinsOf()` has no catalog parameter, so the event lists live in a small cache on the project. It
  is re-read when a file in `events/` changes (by file time) and is used by `pinsOf()`, the build
  and the checks.
- The Events window and the inspector show the pins in the Model (Animated) section, with the
  `doc` text as a tooltip. Wiring, prefab exposure and per-instance overrides work as for any
  component pin. A new wire gets a name like `scene::Player.Leap.LAND`.
- A pin that disappears keeps its bindings greyed, as a renamed script pin does. This happens when a
  list drops a name or when the component is given another model.
- **Build messages.** Errors: a malformed or duplicate name, a model listed twice, more than the
  limit. Warnings: a key that names no asset, or one that names an asset that is not a model.
- The command-line `graph` text lists the pins like the others.

### 3.2 Kentucky Castle: the traits fill the list

- **`DefineModel("assets/retrostar.glb")`** in a trait names the model it animates (RetroStar,
  Minion and BigMinion each get one). A trait without it gets no pins, and the compiler prints a note.
  The path must exist; two traits naming one model is an error.
- **Pins per model, in trait order:** `<Clip>.<Event>` for every `DefineEvent`, then `Any.<Event>`
  for each event name, in order of first appearance. A clip named `Any` is an error. RetroStar gets
  19 + 11 pins, Minion and BigMinion 4 + 4 each.
- **The list file.** `tools/trait_compiler.py --anim-events events/anim_events.json traits/*.trait`
  writes it. `make` runs it next to `--registry`, and the file is committed, because the editor
  reads it while editing, before any build.
- **Pin constants in the trait headers.** The trait compiler writes one constant per pin, next to
  the event types it writes today, and the traits struct gets `using EventPin = ...`:

  ```cpp
  struct Pin    // in AnimEvents, next to Type
  {
      struct Run { static constexpr P64::Events::PinName STEP_LEFT{P64::Events::pinHash("Run.STEP_LEFT", 13)}; /* ... */ };
      struct Any { static constexpr P64::Events::PinName LAND{P64::Events::pinHash("Any.LAND", 8)}; /* ... */ };
  };
  ```

  Game code writes `AnimationTraits::EventPin::Leap::LAND`, never a string. The compiler checks the
  names (it already rejects bad clip and event names). The value is the engine's own `pinHash()`
  of the same text the compiler writes into `anim_events.json`, so it cannot drift from the
  editor, and Python never computes a hash. The constants cost no ROM unless used.
- **Pin arrays.** `EventDescriptor` gets two arrays, `clipPins` and `anyPins`: per event, the hash
  of its clip pin and of its `Any.` pin, taken from those constants. `AnimBody` reuses
  `EventDescriptor`, so `RoleAnimDriver` gets them too.
- **Drivers.** `init()` takes the object and keeps an `ObjectRef`. At the one point where each driver
  hands an event to its callback, it also fires the clip pin and the `Any.` pin through
  `AnimModel::fireEvent`. `RoleAnimDriver` does this for every event, also those without a role.
  The callbacks keep exactly today's events. The event work runs whenever the driver has a model;
  the early returns on "no callback" go. Pins are only queued, so whether they or the callback
  run first makes no difference to listeners.
- **Call sites:** `Player.cpp:1720`, `Minion.cpp:403`, `Enemy.cpp:2834` pass the object to `init()`.
- **Timing.** The drivers update in their script's `update()`, so the events reach listeners in the
  third drain of the same frame. The Player, Minion and Enemy see exactly the events their scripts
  already see: the blend threshold, the blend event modes, the end-of-clip flush and the status
  slow-down all apply.

### 3.3 What a designer does

1. Add `DefineModel(...)` to the trait once, and `DefineEvent` lines as before.
2. Run `make traits`, or any build. `events/anim_events.json` is rewritten.
3. In the Events window, the object's Model (Animated) section shows `Leap.LAND`, `Any.LAND`, and so
   on. Wire them like any other output, for example `Run.STEP_LEFT` into an Audio (2D)'s Play.

A build right after a trait change still uses the previous list for its own code (the editor
generates before `make`). The only effect is that a brand-new event has no pin yet, and it could not
have been wired anyway.

### 3.4 The test: RetroStar's footsteps in his prefab

The footstep sounds and dirt puffs move from `Player.cpp` into `assets/prefabs/Player.prefab`. This
is a real change that stays in the game, not a throw-away test.

**Two new child objects** of the Player, `StepFx Left` and `StepFx Right`. Each has:

| Component | Settings |
|---|---|
| Code: **Follow Bone** (new, `KC/src/user/scripts/utility/FollowBone.cpp`) | Bone 41 (left) / 49 (right), offset 0. Each frame it puts its object at that bone of its parent's Model (Animated). Without a parent, model or bone it uses the parent's position, as `Player.cpp` does today |
| Particle Emitter | `Particles/footstep_dirt.emitter`, auto-play off |
| Audio (2D) | `audio/sfx/sonic-r-rips/runleft.wav` / `runright.wav`, volume 0.3, no loop, no auto-play |

Follow Bone uses its parent rather than an object field, so every Player instance follows its own
body. The bone maths moves out of `AnimationDriver::getBonePosition` into a small helper
(`KC/src/user/objects/BonePosition.h`) that the driver and Follow Bone both call.

**Wires** in the prefab's graph, all `prefab::` names:

| Model (Animated) pin | Goes to |
|---|---|
| `Run.STEP_LEFT` | `StepFx Left`: Particle Emitter Play, Audio (2D) Play |
| `Run.STEP_RIGHT` | `StepFx Right`: Particle Emitter Play, Audio (2D) Play |
| `Dodge.TAKEOFF` | the Particle Emitter Play of both (no sound, as today) |

**`Player.cpp`** keeps the gameplay of those events (`attackComboCount = 0` on a step, the dodge
impulse) and loses the effects: the two `play2D` calls, the three emitter replays, the
`footstepEmitter`, `stepFxLeft` and `stepFxRight` fields, their `create()` in `init()` and
`destroy()` on delete. The hand-glow fallback (`Player.cpp:2798-2806`) names the emitter asset itself
and does not change.

**What this proves:** the pins appear on a prefab's component and wire inside the prefab; `prefab::`
keeps them per instance; one pin feeds several inputs; and the pins fire on the same frames the
callback sees, since sound and dirt are where they were.

**Differences to check by ear and eye:**

- A component's Play cuts its own previous sound, where `play2D` let them overlap. Left and right
  are separate components, so only a step of the same foot can cut it, a full run cycle later.
- The dirt may sit one frame behind the bone (Follow Bone's update and the event drain run at
  different points of the frame). The foot is planted at a step, so this should not show.

## 4. What it costs

| | |
|---|---|
| ROM, Kentucky Castle | two hashes per trait event: 27 events, 216 bytes |
| ROM, engine table | 4 bytes per listed pin: 46 pins, 184 bytes, plus a few bytes per model |
| RAM | an `ObjectRef` per driver; nothing per Model (Animated) component |
| CPU per trait event | two `fireEvent` calls: an EventLinks lookup and, only when the object has bindings, a scan of 3 models and up to 30 hashes. Characters fire a few events per second |
| Footsteps (3.4), per Player | two more objects with three components each, for the two emitters and two sounds the Player held before (its data shrinks by 12 bytes). Two bone lookups per frame in Follow Bone: a read of the bone matrix and one rotation each |

## 5. Not in this plan

- Inputs that play a clip or start a state from the graph. They would have to go through the
  driver, because playing directly on the AnimModel would leave the driver wrong about which clip
  is playing.
- The engine timing events itself, for models that no driver plays (props). The list format would
  allow it later.
- Role pins (`DAMAGE_START` for every body) and opt-in events: the owner chose every event, as clip
  and `Any.` pins.

## 6. Phases

1. **Engine runtime:** `ANIM_EVENT_PIN_BASE` and the limit, `AnimModel::fireEvent`, registering the
   generated table; a host unit test that `pinHash()` of a dotted name matches the editor's hash.
2. **Editor:** the `animationEvents` catalog section, the project cache, `pinsOf()`, build records
   and `animEvents.cpp`, build messages; unit tests in `tests/eventEditor.cpp` (parsing, errors,
   pins of a component with and without a list, indices, a dropped name greyed, the generated
   table).
3. **Editor UI:** tooltips with the doc text, the command-line text, a check with `--ui-script` that
   the pins appear and wire.
4. **Runtime test** (`n64/tests/test_events`, scene 11): a small animated model with a test list. A
   script fires a listed and an unlisted name (pins made with `pinHash()`, since the engine test has
   no traits); the listed one reaches its binding with the right
   value and sender, the unlisted one returns false. Two instances each hear only their own pins,
   and MainFinished still works. New cases in `buildChecks.sh` for the build messages.
5. **Kentucky Castle:** `DefineModel` in the three traits; the trait compiler (`--anim-events`, the
   `EventPin` constants, the pin arrays, the checks); `Makefile.custom`; both drivers and the three
   call sites; `events/anim_events.json` committed.
6. **Footsteps in the Player prefab** (3.4): the bone helper and Follow Bone, the two child objects
   and their wires in `Player.prefab`, the effects removed from `Player.cpp`. The command line can
   wire a prefab (`--cmd wire --prefab`) but not add objects, so the two objects are added in the
   editor (a `--ui-script` run) or in the prefab file, then checked by opening the prefab in the
   editor. `project.p64proj` is not committed.
7. **Verify** in ares. On the real game: run, walk into a run, dodge, and check that every step
   sounds and puffs dirt at the right foot, as before. On a copy of Kentucky Castle, Log objects on
   the Player's `Any.DAMAGE_START` and the last event of a non-looping clip (the end-of-clip flush),
   a Minion's `Walk.STEP_LEFT`, and a guard's event without a role (the `RoleAnimDriver` path).
   Then the owner's playtest.
8. **Docs:** the guide (`KC/docs/EventGraph/README.md`: the Component pins row, a new "Animation
   events" section with the footsteps as its example, the Part 3 files), the trait docs
   (`KC/tools/trait_reader/README.md`, `KC/tools/Animation/README.md`: `DefineModel` and `EventPin`)
   and the engine changelog.
9. **Release order:** Kentucky Castle's drivers call `AnimModel::fireEvent`, so its CI (the latest
   Pyrite64 release) fails until a release has the feature. Release the engine first, then merge
   Kentucky Castle.
