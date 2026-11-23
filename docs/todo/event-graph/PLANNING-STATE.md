# Event graph: planning state

Read this file first when resuming the planning work. It records what was asked, what is done,
and what is next. The plan itself is [PLAN.md](PLAN.md). Raw investigation notes are in
[FINDINGS.md](FINDINGS.md).

- **Status:** PLAN APPROVED 2026-11-23. PHASE 1 (runtime event bus) DONE AND TESTED. Phase 2 is
  next and has not started. Nothing is committed. Earlier status: planning done, PLAN.md draft 7. Waiting for the owner's approval before any implementation starts.
- **Started:** 2026-11-23
- **Branches:** `feature/event-graph` in `pyrite64` (from `stream/full-frontal`) and in
  `kentucky-castle` (from `main`). Nothing is committed yet.
- **Status board:** http://mini-debian:8765/r/n29b4r (request `n29b4r`)
- **Reminders:** a session-only recurring cron job (`4 1,6,11,16,21 * * *`, created 2026-11-23
  04:12, id d0d881b2) wakes the session every 5 hours so the work resumes after a usage reset. It
  dies with the session and expires after 7 days. Delete it once everything is done.
- **Working mode (owner, 2026-11-23 04:12):** "you are going to be working autonomously so complete
  the entire plan. if you have questions, use the recommended and then at the end we can followup
  when I return - so leave all that in a final report." Commit each phase on `feature/event-graph`
  with no co-author lines. Log every autonomous choice under "Decisions made autonomously" below.

## The brief (owner's request, restated)

An "event graph": a view similar to the node-graph editor, but a graph of events instead of logic
nodes that generate C++.

- **Scopes.** Scene (a scene event graph), prefab (a prefab event graph), level (the levels that
  load and unload inside a scene), object (local, object-only events), and UI. The UI part must
  work with the existing UI data connection so that events trigger UI functionality; UI is
  "essentially read only".
- **Declared pins.** Objects, through their components, declare input events and output events,
  so the output of one object can be wired to the input of another. Example: a collider has
  object-enter and object-exit outputs, a door has open and close inputs; enter opens the door and
  exit closes it.
- **Automatic scene events.** Scenes fire events by themselves: finished loading, closing, a level
  transition, and so on. The engine defines a default set and any game can add more.
- **Automatic prefab events.** The same for prefabs: created, destroyed, and so on.
- The scene graph offers everything relative to the scene; the prefab graph offers everything
  relative to the prefab.
- **Prefab-local events.** Five instances of a collider-plus-door prefab must each handle their
  own connection without affecting the other instances. This is done with event namespacing.
- **Namespaces**, written `namespace::event`:
  - `global`: game-wide broadcasts (triggering saves, triggering UI, game load/save, hardware
    events such as DMA done)
  - `scene`: scene broadcasts (levels loading/unloading, lighting changes, loading started/ended)
  - `level`: broadcasts within a loaded level
  - `prefab`: one prefab instance only
  - `object`: only on the object, to reach the other scripts attached to it
  - `ui`: user-interface communication
  - `custom`: a "player-defined" global, for example mission-specific events
- **Authoring.** Making a connection in a graph automatically creates an event at the scope of the
  graph being edited. An event can also be typed in manually as a string. Names are stored as plain
  text including the namespace (for debugging and text merges) and compiled to an integer for
  speed.
- **Built-in event objects.** The engine ships a few objects for event logic. Example: an event
  repeater with one input and several outputs, used when a prefab expects an input event from the
  level or scene and re-fires it as an internal prefab event.
- **Script hook.** C++ scripts declare their input and output events. An `event_repeater` C++
  class should be all that is needed for the repeater logic.
- **Graph node layout.** All scripts on an object form ONE node showing every input and output.
  Each script is a collapsible category; when collapsed, pins without connections are hidden.
- **Self-sends.** A node may send events to itself. Loose recursion detection may be needed.
- **Deliverables.** A plan on a new branch in both repos, all open questions asked, and any
  features the owner missed.

## Steps

- [x] Set reminders (06:04 and 11:07)
- [x] Create `feature/event-graph` in both repos
- [x] Investigation by five read-only explorers (results go to FINDINGS.md as they arrive):
  - [x] node-graph editor (board task n29b4r-2)
  - [x] scene/prefab model, build pipeline, runtime loader (n29b4r-3)
  - [x] UI canvas system and its data binding (n29b4r-4)
  - [x] Kentucky Castle systems: levels, existing events, triggers, AI, spawning (n29b4r-5)
  - [x] script parsing/codegen, tests, CLI, docs and fork conventions (n29b4r-6)
- [x] Own check: can the collision scene produce enter/exit events? (yes, see FINDINGS section 0)
- [x] Verify the load-bearing findings by reading the code directly
- [x] Draft PLAN.md (n29b4r-7), draft 1 assumes every recommended answer
- [x] Put the open questions to the owner (n29b4r-8): six rounds, all answered, see the end of this file
- [x] Fold the answers into PLAN.md (draft 2), delete the reminders and close the board request
- [x] Owner approves the plan (2026-11-23: "the plan looks good. lets start implementation and testing")
- [x] Phase 1: runtime event bus (see "Implementation log" below)
- [x] Phase 2: script pins, bindings, direct sends, inspector section (commit fc57e44; game
  rename commit 2941954 in kentucky-castle)
- [x] Phase 3: engine pins, collider enter/exit, object pins, engine scripts, built-in objects
- [x] Phase 4: prefab scope, numbered namespaces, exposed pins, overrides, nesting fix-ups
- [x] Phase 5: Event Graph window
- [x] Phase 6: UI canvases
- [x] Phase 7: tools (debug page, event browser), example project (the manual is the final guide)
- [ ] Phase 8: Kentucky Castle adoption
- [ ] Phase 9: retire the old messages
- [ ] Final: finish the guide in kentucky-castle/docs/EventGraph/README.md, final report, delete the cron job

## Notes for whoever resumes

- The owner approved the plan and asked for the whole of it to be built and tested autonomously,
  committing each phase without co-author or AI attribution lines. Open questions take the
  recommended option and go into "Decisions made autonomously" for the final report.
- Never commit the owner's own change to `kentucky-castle/project.p64proj`. The guide draft in
  `kentucky-castle/docs/EventGraph/` stays uncommitted until the end.
- The existing engine event system and what it lacks are summarised at the top of FINDINGS.md.

## Owner's answers (2026-11-23)

Question numbers refer to PLAN.md section 14.

- Q1 delivery timing: same frame, queued (recommended)
- Q2 payload: sender, instigator, value (recommended)
- Q3 nested prefabs: isolated (recommended)
- Q4 instance interface: root pins plus exposed pins (recommended)
- Q5 levels: a flag on the prefab; LevelDriver stays game code (recommended)
- Q7 custom namespaces: ONLY namespaces a game declares (for example `mission::`); there is no
  literal `custom::` namespace
- Q13 hardware events by default: reset button, controller plug/unplug, pak inserted/removed.
  No DMA-done event.
- Q14 saves: nothing in the engine. In the owner's words: "game projects should be able to declare
  them, which is essentially just typing global::something in any output and input event space".
  Consequence for the plan: typing a name IS declaring it. A catalog entry must never be required,
  and a name must not be reported just for being absent from a catalog.
- Q8 pin syntax: two list macros and one `onInput` switch (recommended)
- Q6 per-wire options: NONE. Wires stay plain; delay and once-only behaviour use helper objects.
  Consequence: a binding is just a name, and the bus needs no timer table.
- Q9 built-in objects: shipped as engine scripts (recommended). ALL four groups in the first
  version: Delay, Once, Counter, All, Compare, Gate, Toggle, Sequence, Random, Scene Loader,
  Spawner, Log, plus the repeater and the message bridge. Owner added: "lets have time delay and
  frame delay. and make sure to give an in event to reset."
- Q11 UI: receives and emits; canvas data stays one-way (recommended)
- Q12 UI inputs in the first version: show/hide a canvas, play/stop a clip, send a canvas event.
  Owner added: "output events on a clip timeline" (timeline markers fire output events).
  Setting a canvas variable from an event is NOT wanted.
- Q15 graph layout: sidecar file (recommended)
- Q16 order: runtime and inspector first, graph window after prefab scoping (recommended)
- Q17 old messages: AIM TO RETIRE the old system. Both exist for now with a bridge, but the new
  system must be able to replace every use, including addressed and payload flows.
- Q18 Kentucky Castle scope: all of it (level events, engine triggers, pins on game scripts, HUD
  hookups and AI opcodes)
- Q19 node graphs: LEAVE THEM ALONE. Owner: "kentucky castle doesnt use node graphs. also node
  graph functionality should remain the same - this is something new". No pins, no new nodes, no
  restart change.
- Q20 upstream: FORK ONLY. The owner first picked "keep it upstreamable" and then changed it in a
  follow-up message: "actually lets make it fork only and not upstreamable". Small marked hooks in
  upstream files are fine, and the work may build on fork-only features.
- Q10 enter/exit: scripts also get `onCollisionEnter` / `onCollisionExit` (recommended)
- Direct sends (new question raised by Q17): code can send to one object's input by pin name;
  nothing has to be authored in the editor (recommended)
- Retire depth (new question raised by Q17): Kentucky Castle moves off the old messages, then
  `sendEvent` / `onEvent` get a deprecation warning in the fork. They are not removed.
- Smaller defaults D1, D3, D6: all accepted as planned
- CHANGE AFTER DRAFT 2 (same day): the owner dropped the `level` scope. In the owner's words:
  "actually because levels are prefabs, lets remove level because its functionally the same thing
  as prefab, which should leave the level loading a game only feature". This replaces the Q5 answer
  above. Consequences in PLAN.md draft 3: no `level::` namespace, no `LEVEL_ROOT` flag, no
  `markLevelRoot`, no engine-fired level events; a level-wide broadcast uses a `scene::` name.
- SECOND CHANGE AFTER DRAFT 2 (same day): nested prefabs may hear their parents. In the owner's
  words: "lets allow nested prefabs to hear their parents events. we can have a numbering system
  so we can trace where they come from. so prefab will be the current prefab, prefab_0 will also
  mirror that; prefab_1 will be the parent, prefab_2 the grand parent, etc this should help show
  where they come from". This replaces the Q3 answer above. In PLAN.md draft 4 the number is not
  part of the event's id; it only selects how many prefab roots to go up. Firing into a parent
  scope by number is the planner's assumption, not something the owner asked for.
- THIRD CHANGE (same day): the owner confirmed firing into a parent and added a requirement. In
  the owner's words: "having a nested prefab fire into its parent is good and what I expected. we
  do need to make sure we handle when we nest a prefab into another one by making sure its events
  get offset to retain the same scope at the new depth". PLAN.md draft 5 answers this in 3.4
  ("Keeping the scope when nesting changes"): parent links are repointed per instance, and the
  editor applies the offset in To Prefab, Unpack and moves across a prefab boundary. Storing the
  offset as a repointed link, and the copy/paste and To Prefab details, are the planner's design.
- FOURTH CHANGE (same day): pins on global scripts. In the owner's words: "lets let global
  scripts also have pins where their inputs and outputs always use the global namespace. we can
  then have the even scripts utilize a Global object that can be added that will expose all the
  inputs and outputs." PLAN.md draft 6 answers this in 5.5 and 8.2. The planner's own choices
  there: a pin named X is the event `global::X` with no script name in it; the Global "object" is
  a node in the graph and not an object in the scene; separate macro names for global scripts.
- FIFTH CHANGE (same day): system names in global events. In the owner's words: "global events
  should include the name of the system that declared it so there are NO ambiguities. ideally the
  global node in the editor will have each system's events as a collapsable region". PLAN.md draft 7
  answers this in 3.2, 5.5 and 8.2: a pin X of system S is `global::S.X`. The planner's own choices
  there: the system name is the first macro argument (not the file name) and one global script may
  declare several systems; every `global::` name must belong to a system or to the engine, so free
  `global::` names are rejected (this narrows the earlier "just typing global::something" answer);
  the engine's events use the system name `Engine`; pin direction is enforced.

## Implementation log

### Phase 1: runtime event bus (done 2026-11-23)

The owner also asked for a README with a how-to guide (mostly for people) and a technical overview
for AI agents, with screenshots and videos where needed. On 2026-11-23 they said it is only needed
once everything is done and tested, and that it should live in the kentucky-castle repo for now:
the draft is `kentucky-castle/docs/EventGraph/README.md` (uncommitted until the end). Finish it
after phase 9, with editor screenshots and short videos.

**What was built** (all uncommitted on `feature/event-graph` in `pyrite64`):

- `n64/engine/include/events/{types.h,config.h,bus.h}`, `n64/engine/src/events/{busCore.h,busCore.cpp,bus.cpp}`
- hooks marked `// Fork: event bus` in `n64/engine/src/main.cpp`, `src/scene/sceneManager.cpp`,
  `src/scene/scene.cpp`, and `src/events` added to `n64/engine/Makefile`
- `tests/eventBus.cpp` and the test target changes in `CMakeLists.txt`
- runtime test project `n64/tests/test_events` (scenes migrated with `--cmd migrate`)
- README draft and `img/test_events_results.png`, moved to `kentucky-castle/docs/EventGraph/`

**Verified:**

- The engine builds with the N64 toolchain and its `-Werror` flags.
- `pyrite64_tests`: 60 of 60 cases pass (28 new event cases).
- Malformed `_event` names fail to compile with a message naming the rule.
- `test_events.z64` in ares: 7 of 7 tests OK (boot events, same-frame chain, drain after physics
  and after update, hop limit, drain budget, scene change keeps only global events, post from a
  real VI interrupt).
- Regression: `test_obj_states` gives identical output with and without the event bus. Its 15
  mismatches are older than this work: its expectations predate the fork's onEnable/onReady.

**Differences from PLAN.md:**

- A subscription is 20 bytes, not 16; the bus is 13,600 bytes in total (PLAN 4.2 updated).
- Extra API: `getLimits()` (game code cannot see the engine's `P64_EVENT_*` overrides),
  `listenerCount()`, `setNameTable()` / `nameOf()`.
- `scene::Loaded` and `scene::Unloading` are `Builtin::CurrentSceneLoaded` and
  `Builtin::CurrentSceneUnloading` in C++, to keep them apart from the `global::Engine.*` events.
- `post()` and `listen()` refuse `prefab::` and `object::` names until phase 4 adds `postTo` and
  `listenIn`.
- Controllers already connected at boot send no event; an accessory still being identified
  (UNKNOWN) is ignored until libdragon knows its type.
- Order around an unload: pre-unload hook, `global::Engine.SceneUnloading`, `scene::Unloading`,
  delete the scene, drop scene-local queued events and instance listeners,
  `global::Engine.SceneUnloaded`, post-unload hook. Loading: pre-load hook,
  `global::Engine.SceneLoading`, create the scene, post-load hook, then `SceneLoaded` and
  `scene::Loaded` are queued for the first frame.

**Notes for phase 2:**

- **Kentucky Castle name clash.** 16 game files alias `namespace Events = P64::User::Globals::Events;`
  and use `Events::EVENT_...` inside `P64::Script::...`. Once a file includes `events/bus.h`, the
  lookup of `Events` finds `P64::Events` first and those lines stop compiling. Either keep
  `events/bus.h` out of `userScript.h`, or rename the game alias (for example `GameEvents`) on the
  game branch before including it.
- The CLI build takes the path of `project.p64proj`, not the folder, and needs `N64_INST` in the
  environment when the project does not set `pathN64Inst`.
- ares works for unattended runs (`--setting Audio/Mute=true --setting Input/Defocus=Allow`,
  wait for `[event test] DONE` on stdout), but it opens a window on the owner's display.

### Phase 4: prefab scope (done 2026-11-23)

**Built:** scope resolution through `PREFAB_ROOT` objects with numbered parent names, prefab
life-cycle events (`prefab::Spawned`, `Destroying`, `Enabled`, `Disabled`, in the engine
catalog), `postTo` and `listenIn`, per-instance `eventOverrides` (add, mute, repointed links)
applied by the build, the build's scope levels for nested prefabs, the "To Prefab" and "Unpack"
fix-ups (`src/project/events/eventNesting.*`), headless `--cmd to-prefab` and `--cmd unpack`,
the inspector's override editing, parent links and expose toggles, and `--scene`/`--object`
to open the editor with a selection.

**Verified:**

- `pyrite64_tests`: 1,759 assertions pass (prefab scope, link resolution, wrap and unpack rules).
- `test_events.z64` in ares: 12 of 12. Test 10 covers four door kits and one spawned kit that do
  not affect each other, a kit nested in an encounter hearing it through `prefab_1::`, a kit in
  an encounter in a wing hearing the wing through `prefab_2::` (the others stay silent),
  per-instance overrides, a repointed link and `postTo`. Test 11 is scene 7 for the round trip.
- `nestingChecks.sh`: 11 of 11. Scene 7's Group is wrapped with To Prefab, built and run, then
  unpacked, built and run: the door hears the same alarm every time, and the names are rewritten
  as expected (the alarm only the group uses becomes `prefab::`, names shared with code or a
  watcher outside stay `scene::`, and unpacking restores them).
- Unpacking K1, K2, E1 and W1 of scene 6 headless gives a project that builds, with clashing
  names renamed (`scene::K2.Plate.Pressed`, `scene::W1.Lockdown`) and life-cycle events moved to
  the root's pins.
- `buildChecks.sh` still 5 of 5. Kentucky Castle (scratch copy) builds and boots into gameplay.
- The inspector was checked by screenshots only: the screen was locked, so clicks could not reach
  the editor. Parent Links on K2 and the nested plate's overrides render as designed; the mute,
  add and expose buttons were not clicked.

### Phase 5: the Event Graph window (done 2026-11-23)

**Built:** `src/project/events/eventGraph.*`, the graph model without UI (nodes, pins, wires
derived from names, connect/bind/unbind/disconnect, automatic names), used by the window and by
`--cmd graph`, `--cmd wire` and `--cmd unwire`; the Events window
(`src/editor/pages/parts/eventGraphWindow.*`) on ImNodeFlow with one node per object and a
collapsible section per component, instance nodes with their pins and parent links, event nodes,
the scope node, the Global node with one region per system, layout files, selection sync, focus
mode, the canvas and dropped-wire menus, built-in objects from the menu, red problems, a loop
warning and highlighted wires under the pointer. `--ui-script` plays mouse and keyboard steps into
the editor for tests and recordings; `--window`, `--edit`, `--scene` and `--object` set it up.

**Verified:**

- `test_events.z64`: 13 of 13. Scene 8 was wired only by the graph's operations (Driver -> Relay ->
  Door, an output into a global system input, the scope node's scene::Loaded, an object:: wire in
  focus mode) and runs as expected.
- `graphChecks.sh`: 7 of 7. Removing every binding from scene 8 and drawing the same wires again
  gives the committed file byte for byte, so wiring adds binding lines and nothing else; removing
  wires drops the right bindings.
- In the editor, driven by `--ui-script` (the screen was locked, so the steps go into the editor's
  own event queue): four wires dragged, one removed with Delete, the canvas menu, an event node
  created and wired through, the dropped-wire menu, a built-in Counter added, focus mode. The saved
  scene had exactly the expected bindings each time; screenshots checked by eye.
- Unit tests 1,759 assertions; `buildChecks.sh` 5 of 5.

### Phase 6: UI canvases (done 2026-11-23)

**Built:** canvas pins from content (`canvasPins`), bindings stored in the `.canvas` file
(`eventBindings`), the "Fire output" timeline marker, generated glue in `uiBindings.cpp` (output
pins post from clip ends, markers and focus changes; input pins listen from
`uiInitializeBindings()`), `UI::post<Scene>()` and `UI::setSceneEnabled()`, build checks and
index entries for canvas bindings, the canvas editor's Event Pins section, and canvas nodes in the
Events window and the graph command line (`canvas:<Name>|<Pin>`).

**Verified:**

- `test_events.z64`: 14 of 14. Scene 9: an object's output shows the TestHud canvas and another
  plays its clip; the canvas's Focused output, its Fire output marker and its clip end each reach
  a script input, in that order. Wired with `--cmd wire`, shown in the Events window.
- Unit tests, `graphChecks.sh` 7 of 7, `buildChecks.sh` 5 of 5.
- Kentucky Castle (scratch copy) builds and boots with its HUD drawn.

### Phase 7: tools and example (done 2026-11-23)

**Built:** the debug overlay page "Events" (counters and the 12 latest deliveries), the event
browser in the Events window (every name with its uses in scenes, prefabs, canvases and code;
rename everywhere) and its command line (`--cmd events`, `--cmd rename-event`), the example
project `n64/examples/events` with a README, and the changelog entry.

**Verified:** the example builds from a copy of its tracked files and runs in ares (the Delay
ticks, the Counter opens the door on the third tick, the banner shows, the Score system counts);
rename on a copy keeps the wires and reports code uses; the browser and its rename are visible in
the editor. The debug page was compiled, not looked at: opening the overlay needs a controller.

## Decisions made autonomously

Questions that came up while the owner was away, answered with the recommended option. List them
all in the final report.

1. (Phase 1, 2026-11-23) The how-to guide draft moved to `kentucky-castle/docs/EventGraph/` as
   asked; the planning documents stay in `pyrite64/docs/todo/event-graph/`.
2. (Phase 2) Kentucky Castle's own `Events` aliases (16 namespace aliases and 3 type aliases)
   are renamed to `GameEvents`, because `P64::Events` hides them once `userScript.h` includes
   the event headers. Recommended option from the phase 1 notes.
3. (Phase 2) The local editor build directory `pyrite64/build` was reconfigured with
   `-DSDL_X11_XCURSOR=OFF -DSDL_X11_XINPUT=OFF -DSDL_X11_XRANDR=OFF -DSDL_X11_XFIXES=OFF
   -DSDL_X11_XSCRNSAVER=OFF -DSDL_KMSDRM=OFF`, because this machine lacks the development
   headers (libxcursor-dev, libxi-dev, libxrandr-dev, libxfixes-dev, libxss-dev, libgbm-dev).
   The editor binary `pyrite64/pyrite64` was rebuilt this way and replaces the 2026-11-02 one.
   Install those packages and rebuild to get the editor with every SDL feature again.
4. (Phase 2) The EventLinks component is written first in each object's component list, so a
   component can fire outputs from init(); the other components' indices shift by one.
5. (Phase 2) `prefab::` bindings are rejected by the build until phase 4 implements them.
6. (Phase 2) The debug name table is generated only when the project's debug menu is enabled,
   like other debug data; otherwise warnings show hex ids.
7. (Phase 2) The "differ by one character" check only warns when one name is only listened to
   and the other only fired, so deliberate families like Wave1/Wave2 stay quiet.
8. (Phase 2) send() uses an internal scope value (`Scope::Direct`) on the same queue, so its
   timing matches every other event.
9. (Phase 3) Collider enter/exit is tracked per (collider, other object) and only for objects that
   need it (bound collider pins or onCollisionEnter/Exit); the flag is `WANTS_CONTACTS` (bit 10).
   onCollisionEnter/Exit take the other object's id, because on exit it may already be gone.
10. (Phase 3) The object's Enabled/Disabled outputs only report changes after onReady, not the
   initial enable at spawn. Disable/Enable inputs take effect at the next frame's state change,
   like obj.setEnabled().
11. (Phase 3) Built-in objects are engine scripts in `n64/engine/scripts/` with fixed UUIDs
   `C0E0000000000001` (Relay) to `C0E000000000000F` (Message Bridge); the project Makefile compiles
   them with the project's flags. Counter steps by one; Compare compares the value as signed;
   Toggle fires the current side then switches; Message Bridge ignores messages it sent itself.
12. (Phase 3) Pins added to engine components: Light TurnOn/TurnOff, Camera Activate, Audio (2D)
   Play/Stop/Finished, Particle Play/Stop, Model (Animated) PlayMain/PlayBlend/MainFinished.
   Culling, Rigid-Body and Character-Body pins stay "later" as planned.
13. (Phase 4) Only "To Prefab" and "Unpack" move content between graphs in this editor. Moving or
   adding nodes while editing a prefab stays inside the edited file, and objects added to a
   nested instance are still written in the edited prefab's graph, so those need no fix-up.
14. (Phase 4) To Prefab: a `scene::` name counts as used only inside the subtree when no other
   object of the scene, no prefab definition, no game code (`_event` literals) and no engine
   event uses it; only those become `prefab::`. Parent links that reached nothing in the scene
   are left alone and start working inside the new prefab, following the plan's "links that
   reached a real scope are repointed".
15. (Phase 4) Unpack: the prefab's own events become `scene::Name`, or `scene::Instance.Name`
   (then `Instance_2`, ...) when the scene, a prefab, the code or the engine already uses the
   name. A name that only one default link reaches connects to nothing and is left alone.
16. (Phase 4) Unpack: `prefab::Spawned`, `Enabled`, `Disabled` and `Destroying` become
   `scene::Instance.Spawned` etc., fired by the root object's Ready, Enabled, Disabled and
   Destroyed pins, which fire at the same moments with the same value (the root's id).
17. (Phase 4) Unpack: a binding with a numbered name that reached nothing (above the outermost
   prefab) is removed and logged, because a scene's own graph rejects `prefab::` names.
18. (Phase 4) Overrides made in the inspector are always stored as `eventOverrides` on the
   instance, also for the instance root's own pins; the editor never writes the instance-level
   `events` of an instance node (the build still reads them).
19. (Phase 4) Build messages name the object's whole path (`scene 6 / K1 / Door / ...`), and the
   project-wide checks count a repointed link under the name it ends up as, so a link to
   `scene::Alarm7` counts as listening to it.
20. (Phase 4) New command-line options: `--cmd to-prefab` and `--cmd unpack` with `--scene` and
   `--object` (and `--dir` for the new prefab's folder), and, in the editor, `--scene`/`--object`
   to open a scene with an object selected (`K1/Plate` continues into the prefab).
21. (Phase 4) "Object Events" opens by itself when the object has bindings or overrides, and
   "Parent Links" when a link is repointed.
22. (Phase 5) The graph logic lives in one UI-free module that the window and the command line
   share, so wiring can be tested and scripted without the editor's window.
23. (Phase 5) Removing a wire: the input stops listening; the output also stops firing the event
   when nothing else in the graph or the game code uses it and it is in the graph's own namespace
   (scene::, prefab:: or object::). Otherwise the output keeps it.
24. (Phase 5) An instance node shows its prefab root's pins, pins marked exposed, any pin this
   instance overrides (exposed or not, so no override is invisible), and its parent links: an
   input where the prefab listens to the link, an output where it fires it.
25. (Phase 5) An event node takes every wire of its name, so N senders and M listeners make N+M
   wires instead of N*M. The Global and scope nodes appear by themselves while a pin uses one of
   their events.
26. (Phase 5) The loop warning is a heuristic: a cycle among nodes (self-wires included) that passes
   no Delay or Frame Delay. Scripts are opaque, so it can warn about a loop that never fires.
27. (Phase 5) New nodes are placed in columns by wire order; nodes without wires go to a grid on
   the right. "Arrange" places everything again.
28. (Phase 5) Layout files: `data/scenes/<id>/events.layout.json` and `<file>.prefab.events.json`,
   written whenever the layout changed and no mouse button is held; not part of undo.
29. (Phase 5) ImNodeFlow gained one virtual, `BaseNode::customPinLayout()` (default false), so a
   node can place its own pins; nothing else in the library changed.
30. (Phase 5) Undo for prefab definitions (optional in the plan) was not done: edits in prefab edit
   mode still are not undoable, as before.
31. (Phase 5) The Events window is a tab beside the 3D viewport (View > Events), docked there each
   time it appears.
32. (Phase 6) Canvas bindings are stored under `eventBindings` in the `.canvas` file, because
   `events` already holds the canvas's own event list.
33. (Phase 6) Canvas pins: in Show, Hide, PlayClip.<clip>, StopClip.<clip>, Event.<event>; out
   Focused/Blurred/Activated.<element> for focusable elements, ClipFinished.<clip>, Marker.<name>.
   Only the canvas's own clips have pins; a Fire output marker in an element clip is reported by
   the build and does nothing.
34. (Phase 6) A focusable element without a script joins focus navigation when one of its focus
   pins is bound, and the pins work even when no game object bound the canvas's model (component
   scripts still need that object).
35. (Phase 6) Show and Hide keep the canvas's layer (new `UI::setSceneEnabled`); Event.<name> uses
   the new `UI::post<Scene>()`, which queues into a per-canvas inbox delivered with the next
   update's events.
36. (Phase 6) Canvas input pins are registered by the generated `uiInitializeBindings()`, after
   dropping the previous scene's listeners, so they need the game to call it like the rest of the
   generated UI bindings.
37. (Phase 6) The Activated output was not tested in the emulator: it needs a real A press. It
   uses the same code path as Focused, which is tested.
38. (Phase 6) Canvas pin edits made in the Events window are saved straight to the `.canvas` file
   and are not undoable there; canvas undo exists only in canvas mode.
39. (Phase 7) The manual (events, every pin, how-tos) is the guide in
   `kentucky-castle/docs/EventGraph/README.md`, as the owner asked; the engine repo gets the
   changelog entry and the example's README only.
40. (Phase 7) The debug page is always available; recording the latest deliveries costs a few
   stores per event. `P64_EVENT_TRACE=1` still prints every delivery through debugf.
41. (Phase 7) Rename changes bindings, override adds and mutes, link names and link targets in
   every scene, prefab and canvas, and lists game code uses instead of editing code. Other scenes
   are written at once; the open scene changes in memory (undoable) and is saved as usual.
42. (Phase 7) A new wire between two pins never reuses a global:: name (found with the example: an
   input cannot listen to a system input).
