# Animation events: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); it builds on the event graph
(`../event-graph/`).

- **Status:** BUILT (2026-11-25). Phases 1 to 6 and 8 are done and committed. Phase 7 (the
  footsteps in ares with real input, and the guard and minion pins) waits for the screen to be
  unlocked or for the owner's playtest. Phase 9 (release order) is the owner's.
- **Branches:** `feature/anim-events` in `pyrite64` (from `stream/full-frontal`) and in
  `kentucky-castle` (from `main`). Commits carry no co-author or AI attribution lines.

## The owner's request, word for word (2026-11-25)

> in KC we have animation traits and the event graph. i want to get the animation events to fire
> from the event graph, but im not sure the best way to do that. I essentially want to have specific
> events from the object's trait available. can you brainstorm some implementation methods for me so
> we can determine how we should accomplish this

Six options were offered. Option 5 was "trait-fed pins on the Model (Animated) component". The
owner's answer:

> number 5 looks good although the trait system is KC only and not engine, so lets make a plan to do
> that option but to enable the traits to forward the events through the animnation component
> instead of bringing traits to the engine

Answers to the three follow-up questions (2026-11-25):

| Question | Answer |
|---|---|
| Which pins should a model get for its trait's events? | Both: `Leap.LAND` for that clip and `Any.LAND` for every clip with that event; the value is the clip index |
| What should the Enemy's RoleAnimDriver fire? | Every event, not only those with a role |
| Every `DefineEvent`, or only marked ones? | Every event |

The owner's review of the first plan (2026-11-25):

> in the plan, lets make the animEventPin part of hte trait generation.
> as a test you can use the sound effects and particle emitting for retrostars footsteps use this in
> his prefab

## The owner's decisions

1. Option 5: the events are output pins of the Model (Animated) component.
2. **The engine gets no trait concept.** The engine offers named animation-event pins on Model
   (Animated); Kentucky Castle's traits supply the names and fire them.
3. Pins per clip and event, and per event name (`Any.`); the value is the clip index.
4. Guards fire every trait event as a pin.
5. Every `DefineEvent` is a pin.
6. The pin constants come from the trait generation; the engine has no `animEventPin()`.
7. The test is RetroStar's footsteps: their sounds and dirt particles move into his prefab.

## Decisions made without the owner

Each is my recommendation. Change any of them on request.

1. The list of a model's events is a new section, `animationEvents`, of the existing event catalog
   (`events/*.json`). It is keyed by the model's project path (`assets/retrostar.glb`). Each event
   has a `name`, and may have a `doc` and a `value` text. It is an array, so the file order is the
   pin order in the editor.
2. The contract between game code and the engine is the pin **name**, never its index: game code
   fires by name hash, and the build resolves names to indices and writes the run-time table from
   the same list. So a stale or reordered list can never make the wrong pin fire.
3. Event pins sit at index 0x80 + position on the component, so Model (Animated)'s own outputs can
   still grow. At most 64 per model.
4. Pin names use letters, digits, `_` and dots, like canvas pins. `fireEvent` takes a `PinName`
   (the `pinHash()` of the full name). The trait compiler writes the constants as
   `AnimEvents::Pin::<Clip>::<Event>` and `Pin::Any::<Event>` (game code:
   `AnimationTraits::EventPin::Leap::LAND`), each initialised with the engine's `pinHash()` of the
   name, so Python never computes a hash and the two cannot drift.
5. `fireEvent` returns false for a name the model does not list, with no warning (a model without a
   list is normal).
6. The editor keeps the event lists in a cache on the project, re-read when a file in `events/`
   changes, because `pinsOf()` has no catalog and is called from many places.
7. After `swapModel()` the run-time lookup follows the current model; bindings only match the
   model set in the editor. That is documented, not handled.
8. Kentucky Castle: a trait names its model with `DefineModel("assets/<model>.glb")`. Without it the
   trait gets no pins (the compiler prints a note). Two traits for one model is an error.
9. Pin order per model: every `<Clip>.<Event>` in trait order, then `Any.<Event>` in order of first
   appearance. A clip named `Any` is an error.
10. The trait compiler writes `events/anim_events.json` (`--anim-events`). `make` runs it, and the
    file is committed, because the editor reads it while editing, before any build.
11. The pin hashes go into the trait headers as two arrays on `EventDescriptor`, taken from the
    `EventPin` constants, so both drivers get them, `RoleAnimDriver` through `AnimBody`.
12. Driver `init()` takes the object and keeps an `ObjectRef`, the same way `ObjectDelegate`
    resolves the object. The drivers' event work no longer depends on a bound callback; the
    callbacks keep exactly the events they get today.
13. Out of scope: input pins (play a clip, start a state), engine-side timing for models that no
    driver plays, role pins.
14. The footsteps are a real change that stays in the game, not a test on a copy. Two child objects
    of the Player (`StepFx Left`, `StepFx Right`), each with Follow Bone, a Particle Emitter and an
    Audio (2D); wired with `prefab::` names.
15. A new Kentucky Castle script, **Follow Bone**, puts its object at a bone of its parent's model
    each frame (bone by index: 41 and 49). It uses the parent rather than an object field so every
    instance follows its own body. The bone maths becomes a helper shared with
    `AnimationDriver::getBonePosition`.
16. `Dodge.TAKEOFF`'s dirt moves too (wired to both emitters), so the Player can drop its held
    emitters altogether. The gameplay of those events stays in `Player.cpp`.
17. Accepted differences: a component's Play cuts its own previous sound (only the same foot can,
    a run cycle later), and the dirt may be one frame behind the bone.

## Decisions while building

1. (Phase 1) The limit is a fixed constant, `ANIM_EVENTS_MAX` (64) in `pins.h`, not a `P64_` setting:
   the editor checks it too, and `pins.h` is shared.
2. (Phase 1) `fireEvent()` also returns false when the object has no bindings.
3. (Phase 2) The `PinList` methods moved to `eventPinList.cpp` so the host tests link them.
   `isConditionPin()` goes by position: 0x80 is shared with the condition pins.
4. (Phase 2) An animation event named like Model (Animated)'s own output (`MainFinished`) is an error.
5. (Phase 2) The build reads the catalog through the editor's cache (`projectCatalog(project, true)`),
   so the pins and the runtime table come from one read of the files.
6. (Phase 3) No `--ui-script` check: the Events window and the command line share the graph model,
   so `graphChecks.sh` checks the graph text and unwires and rewires a dotted pin. The command line
   tried only the last dot to split `<category>.<pin>`; it now tries every split.
7. (Phase 3) New wires keep the dots of a pin name (`prefab::Player.Run.STEP_LEFT`). Canvas pins get
   the same (`ui::Hud.ClipFinished.Intro`, was `ClipFinishedIntro`); existing bindings do not change.
8. (Phase 4) The test model is the jam25 example's `player.glb` (Idle, Walk), its face texture made
   static (the example sets a placeholder from code) and its second texture copied.
9. (Phase 4) `nestingChecks.sh` ran ares on the owner's own settings file, which ares writes back on
   exit; it now runs on a copy, with the debug server off. The owner's file was rewritten once,
   with the same content (checked line by line).
10. (Phase 5) Without `DefineModel` a header gets an empty `struct Pin {}` and null pin arrays, so
    the drivers do no pin work and game code that fires such a pin does not compile.
11. (Phase 5) The constants are `PinName{pinHash("Run.STEP_LEFT", 13)}`: Python never hashes.
12. (Phase 5) The drivers fire the two pins before the callback (pins are queued, so listeners see
    no difference; the callback may change the clip).
13. (Phase 5) When the list is unchanged the compiler only touches the file, so `make` sees it up to
    date and git sees nothing. `traits.h` depends on it, so `make traits` writes it.
14. (Phase 5) The `DefineModel` lines have no trailing comment: the TraitEditor keeps unknown lines
    word for word, but its parser is not checked against comments.
15. (Phase 6) Follow Bone's Bone is a `uint16_t` skeleton index. It uses the maths of
    `AnimationDriver::getBonePosition` (`BonePosition.h`), so the dirt sits where it did.
16. (Phase 6) The two objects were added and wired on a copy with the command line (`wire --prefab
    Player.prefab`), and only `Player.prefab` was copied back; `project.p64proj` is untouched.
17. (Phase 7) Every event of the current guard bodies has a role, so "events without a role" cannot
    be shown with today's traits; the code path is the same `deliver()` as the others.

## Open questions

None.

## Phase log

| Phase | Commit | What |
|---|---|---|
| plan | pyrite64 cd04f6e | `PLAN.md`, this file, with the owner's review (pin constants in the trait generation, the footsteps) |
| 1 | pyrite64 1252751 | `ANIM_EVENT_PIN_BASE`, `ANIM_EVENTS_MAX`, `AnimModel::fireEvent`, `setEventLists`, `registerAnimEvents` at boot |
| 2, 3 | pyrite64 18afe7f | catalog section, cache, `pinsOf()`, tooltips, build checks and `animEvents.cpp`, command-line pin parsing, 4 unit tests (133 pass) |
| 4 | pyrite64 0b5fff4 | scene 11; in ares 16 of 16; `buildChecks.sh` 10 of 10, `graphChecks.sh` 10 of 10, `nestingChecks.sh` 11 of 11, `conditionChecks.sh` 22 of 22 |
| 3 | pyrite64 4ccf993 | new wires keep the dots of pin names |
| 5, 6 | kentucky-castle 7b1187f, 6b3d6e5 | `DefineModel`, `--anim-events`, `EventPin`, both drivers, Follow Bone, the footsteps in `Player.prefab`; builds on a copy with no new warning |
| 8 | pyrite64 ef2a23a, kentucky-castle bcb6054 | the guide's "Animation events", trait docs, changelog, the Model (Animated) manual page |
