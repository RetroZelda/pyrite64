# Event graph: investigation findings

Facts gathered for the plan in [PLAN.md](PLAN.md). Paths are relative to the `pyrite64` repo unless
they start with `kentucky-castle/`. Each section says how the fact was obtained: "verified" means
read directly in the code by the planner, "reported" means it came from a read-only explorer and
was not all re-read.

## 0. What the engine does today (verified 2026-11-23)

### Object scripts

- A script is a C++ file in a project's `src/user/`, in namespace `P64::Script::C<UUID>`, attached
  through a Code component (`n64/engine/include/scene/components/code.h`, component ID 0).
- Per-instance data comes from `P64_DATA(...)` or `P64_DATA_TYPE(T)`
  (`n64/engine/include/script/userScript.h`). Members with `[[P64::Name("...")]]` are editable in
  the editor.
- Callbacks are found by text match: the build strips comments and whitespace and searches for
  `void<name>(` (`src/utils/codeParser.cpp:659`, used by `src/build/scriptBuilder.cpp:45-88`).
  A misspelled callback is never called and produces no error.
- The generated `ScriptEntry` (`n64/engine/include/script/scriptTable.h`) holds one function
  pointer per callback: `init, destroy, onEnable, onDisable, onReady, update, fixedUpdate, draw,
  onEvent, onColl`, plus `onFocus, onBlur, onActivate` for UI component scripts.
- Global scripts (`n64/engine/include/script/globalScript.h`) only get stage hooks: `GAME_INIT`,
  `SCENE_PRE_LOAD`, `SCENE_POST_LOAD`, `SCENE_PRE_UNLOAD`, `SCENE_POST_UNLOAD`, `SCENE_UPDATE`,
  `SCENE_PRE_DRAW`, `SCENE_PRE_DRAW_3D`, `SCENE_POST_DRAW_3D`, `SCENE_DRAW_2D`. They cannot receive
  object events.

### The existing ("legacy") object events

- `Scene::sendEvent(targetId, senderId, type u16, value u32)`
  (`n64/engine/include/scene/scene.h:194`). One event goes to one object id. There is no
  broadcast and no subscription.
- Events are queued in one of two `std::vector` queues and delivered at the start of the next
  frame, after pending spawns and enable/disable changes and before physics and `update`
  (`n64/engine/src/scene/scene.cpp:255` and `:452`). An event sent from a handler arrives one
  frame later (the queues swap).
- Every component of the target that has `onEvent` receives it. Disabled objects still receive
  events. An unknown target id is dropped silently.
- Custom types use `0x0000..0xF000`; the engine reserves the top of the range
  (`EVENT_TYPE_ENABLE/DISABLE/READY/PARTICLE_PLAY`, `n64/engine/include/scene/event.h`).
  `MAX_EVENT_COUNT = 128` is declared but not enforced.
- The Particle component (ID 24) already reacts to a legacy event (`EVENT_TYPE_PARTICLE_PLAY`),
  which is a precedent for engine components consuming events.

### Collision callbacks

- `onCollision` fires once per object pair per physics step while contacts exist
  (`n64/engine/src/collision/collisionScene.cpp:601`). There are no enter or exit callbacks.
  Physics may run zero, one or several steps per frame.
- An object only gets the callback when its collider reads the other collider's layer. Two
  triggers never produce a contact. Disabled objects get no callbacks.
- A Character Body is invisible to other colliders
  (`docs/docs/manual/editor/collision/characterBody.md:31`); the player needs a normal collider to
  set off triggers.
- Kentucky Castle derives enter/exit by hand: clear a flag in `fixedUpdate`, set it in
  `onCollision`, compare with the previous step
  (`kentucky-castle/src/user/scripts/encounter/EncounterPlayer.cpp:37`).

### Enter/exit is feasible inside the engine

- The collision scene keeps contact constraints across steps, keyed by collider pair
  (`ContactConstraintKey`, `n64/engine/include/collision/contact.h:38`). Each step marks all
  constraints inactive, re-detects, then removes the inactive ones
  (`collisionScene.cpp:1091`, `:933`).
- `dispatchCollisionCallbacks` already builds a per-step list of unique object pairs
  (`pendingDispatchKeys_`). Comparing that list with the previous step's list gives exact
  enter/exit per object pair.
- Caveats to handle: the list holds `Object*` (store ids so an exit can still be reported after
  one object is removed); two sleeping non-trigger rigid bodies stop producing constraints
  (`collisionScene.cpp:1139`), so for solid contacts a sleeping pair must be carried over instead
  of reported as an exit; pairs are only tracked today when one side has an `onColl` handler.

### Node graphs

- A node graph is transpiled to one C++ coroutine. The coroutine is created once when the
  component initialises (`n64/engine/src/script/nodeGraph.cpp:43`) and destroyed when the graph
  finishes; after that `run()` does nothing (`:56`). "Repeat" makes every dead-end path jump back
  to Start one frame later (`src/project/graph/graph.cpp:433`).
- The NodeGraph component (ID 9) has no `onEvent`. A graph can send a legacy event (`core.objEvent`
  in `data/nodes/builtin/object.js`) but cannot wait for one.
- Kentucky Castle uses no node graphs and no custom nodes.

### Components that could offer event pins

| Component | ID | What it has today | Candidate pins |
|---|---|---|---|
| Code | 0 | script callbacks | declared by each script |
| Model | 1 | static mesh | in: Show, Hide |
| Light | 2 | light settings | in: TurnOn, TurnOff |
| Camera | 3 | camera settings | in: Activate |
| CollMesh | 4 | mesh collider | out: Enter, Exit |
| CollBody | 5 | primitive collider, trigger flag | out: Enter, Exit |
| Audio2D | 6 | `Audio::Handle` with `stop()`, `isDone()`, auto-play flag | in: Play, Stop; out: Finished |
| Constraint | 7 | transform constraint | none at first |
| Culling | 8 | visibility culling in `draw` | out: BecameVisible, BecameHidden |
| NodeGraph | 9 | `run()`, `isRunning()` | in: Run; out: Finished, plus pins declared in the graph |
| AnimModel | 10 | `setMainAnim`, `setBlendAnim`, durations | in: PlayAnim; out: AnimFinished |
| RigidBody | 11 | physics body | out: Sleep, Wake |
| CharBody | 12 | character controller | out: Landed, LeftGround |
| Surface | 13 | render surface | none at first |
| Particle | 24 (fork) | auto-play flag, legacy play event | in: Play, Stop |
| Spline | 25 (fork) | path queries | none at first |

IDs 24 and up are reserved for fork components (`n64/engine/include/scene/componentTable.h`), so a
new event component would take the next free fork ID.

## 1. Node-graph editor (reported by explorer n29b4r-2, 2026-11-23)

**Summary:** the canvas layer is a patched in-tree fork of ImNodeFlow that knows nothing about code
generation and can be reused. Everything above it (`Graph`, `Node::Base`, `ScriptNode`,
`NodeEditor`, the JS host) is tied to code generation. The graph editor has no undo of its own and
there are no graph tests.

### Canvas library

- `vendored/ImNodeFlow/` (`include/ImNodeFlow.h`, `src/ImNodeFlow.cpp`, `.inl`,
  `context_wrapper.h`). Pyrite added groups, link waypoints, box select, animated links, mirrored
  nodes, a `force` flag on `createLink`, `setSingleLink`, `drawBottom()` and pop-ups drawn outside
  the zoom.
- Each ImNodeFlow instance creates its own nested ImGui context for zoom
  (`context_wrapper.h:118-190`).
- `BaseNode::update()` (`ImNodeFlow.cpp:155-333`) lays out header, input column, `draw()`, output
  column, `drawBottom()`. Pins have a filter lambda and a custom-renderer hook
  (`ImNodeFlow.h:1080`). Links support waypoints and are hit-tested as polylines
  (`ImNodeFlow.cpp:10-111`). Groups are purely visual (`ImNodeFlow.h:273-297`).
- Mirroring treats a pin as a logic pin only when `socket_shape == 3` (`ImNodeFlow.cpp:179-194`),
  so an event pin drawn as a triangle would trigger it.

### Data model and file format

- `Project::Graph::Graph` (`src/project/graph/graph.h:49`) owns the ImNodeFlow instance.
  `Node::Base : ImFlow::BaseNode` has pure virtual `build(BuildCtx&)`, `serialize`, `deserialize`
  (`nodes/baseNode.h:159`, `:203-205`). `ScriptNode` is the only concrete node.
- `.p64graph` JSON anywhere under `<project>/assets/`, with a sidecar `.p64graph.conf` holding the
  asset uuid. **Layout and logic share one file** (`pos`, `view`, link `points`, `groups`).
- **Links are keyed by positional pin index** (`src`/`srcPort`/`dst`/`dstPort`,
  `graph.cpp:249-252`), so reordering pins breaks saved links. Nodes serialize in
  `unordered_map<pointer>` order, so files diff noisily.
- Node uuids are random u64 values written as raw JSON numbers.

### Editor integration

- `Editor::Scene` (`src/editor/pages/editorScene.h:57`) holds a list of `NodeEditor` windows; each
  is a free-floating `ImGui::Begin(assetName)` window (`nodeEditor.cpp:229`). Opened through the
  `OPEN_NODE_GRAPH` action (`editorScene.cpp:39-48`). Open windows are restored per project.
  Opening the same graph twice is not prevented.
- Dirty state is a per-frame compare of `serialize(false)` with the saved string
  (`nodeEditor.cpp:264-277`); Ctrl+S saves (`editorScene.cpp:870-873`, `:916-919`).
- No copy/paste. Duplicate copies one node without links. Selection is internal to ImNodeFlow and
  not linked to the scene selection (`ctx.selObjectUUIDs`).
- The create menu has a search box and filters by pin compatibility when opened by dropping a wire
  (`nodeEditor.cpp:95-206`).
- **Undo:** two systems exist and the graph editor uses neither. `Editor::UndoRedo::History`
  (`src/editor/undoRedo.h/.cpp`) takes whole-scene snapshots via `scene->serialize(true)`;
  `Editor::CanvasHistory` takes per-canvas snapshots. Ctrl+Z in a graph window undoes the scene
  (`editorScene.cpp:858-868`). **If event data lives inside the scene JSON, scene undo covers it
  for free.**

### JS host

- quickjs-ng (`vendored/quickjs-ng`), one global runtime behind a recursive mutex
  (`src/project/graph/jsNodeHost.cpp:30-35`). A second, separate runtime runs editor scripts
  (`src/editor/script/scriptHost.cpp:109`).
- Loads `data/nodes/_icons.js`, `_prelude.js`, `_types.js`, `data/nodes/builtin/*.js`, then
  `<project>/nodes/*.js`. Project node files hot-reload by mtime polling.

### Build

- `Build::buildNodeGraphAssets` (`src/build/nodeGraphBuilder.cpp:31-68`, called at
  `projectBuilder.cpp:188`) writes `<project>/src/p64/<UUIDHEX>.cpp` and a 16-byte `.pg` binary
  (u64 uuid, u16 stackSize, u16 pad, u32 varBytes). `scriptBuilder.cpp:111-124` fills
  `getGraphFuncByUUID` from the template `data/scripts/scriptTable.cpp`.
- NodeGraph component init data is written in
  `src/project/component/types/compNodeGraph.cpp:88-167`.

### Object references

- The graph stores no objects. The NodeGraph component maps variable names to object uuids and
  resolves them at build with `ctx.scene->getObjectByUUID(uuid)->runtimeId`
  (`compNodeGraph.cpp:124-125`). The picker is `ImTable::addObjectVecComboBox`.
- Runtime ids are assigned per build in pre-order (`src/project/scene/scene.cpp:557-579`);
  objects inside expanded prefab instances get ids from `ctx.nextRuntimeId`
  (`src/build/sceneBuilder.cpp:184-201`) and cannot be picked.

### Tests

- `tests/` holds `codeParser.cpp`, `codeParserFork.cpp`, `layer.cpp` (doctest). The
  `pyrite64_tests` target links only `src/utils/codeParser.cpp` and `logger.cpp`
  (`CMakeLists.txt:402-415`). Nothing covers graphs, and `Graph` cannot be tested without ImGui.

### Reuse verdict

- **Reuse as is:** the ImNodeFlow fork (subclass `ImFlow::BaseNode` directly), the object picker,
  uuid-to-runtime-id resolution.
- **Copy the pattern:** create-menu search, dirty compare, unsaved-close modal, window list and
  session restore, groups/view serialization.
- **Do not reuse:** `Graph`, `Node::Base`, `ScriptNode`, `NodeSpec`, the node registry, the JS
  build callbacks, `NodeEditor`, `nodeGraphBuilder`.

### Unrelated bug noticed

- A `quat` graph variable default writes 4 bytes while the layout reserves 16
  (`compNodeGraph.cpp:144-165`), and the inspector offers `vec4` instead of `quat`.

## 2. Scene/prefab model, build pipeline, runtime loader (reported by explorer n29b4r-3, 2026-11-23)

**The two facts that matter most:**

1. **Prefab instances placed in a scene are flattened at build time.** Their expanded objects do
   not get ids next to the instance root.
2. **Nothing at runtime records which prefab instance an object came from.**

### Editor data model

- `Project` (`src/project/project.h:79-108`) owns `AssetManager` and `SceneManager`. A scene is the
  folder `data/scenes/<int id>/`; `scene.json` is `{"version":2,"conf":{...},"graph":<root Object>}`
  (`src/project/scene/scene.cpp:448-454`).
- `Object` (`src/project/scene/object.h:24-165`): `name`, `uint32_t uuid`, build-only
  `uint16_t runtimeId`, `uuidPrefab` (non-zero means prefab instance), transform, `propOverrides`,
  `children`, `components`, `enabled`.
- `Component::Entry` (`src/project/component/components.h:32-39`):
  `{int id; uint64_t uuid; name; enabled; shared_ptr<void> data}`. JSON:
  `{"id","uuid","name","enabled","data":{...}}`.
- A prefab asset is `assets/**/*.prefab`: `{"version":2,"uuid":<u32>,"obj":<Object>}`.
- A prefab instance in a scene is a thin node: `uuidPrefab` set, `components` holds only components
  added to the instance, `children` only objects added to the instance, and `propOverrides` holds
  overrides keyed by `combine(pathHash, crc64(propName))` with values like `"1:0.0,0.0,0.0"`
  (`src/utils/prop.h:105-269`). The keys are opaque u64 hashes. Nested prefabs use the same thin
  node inside a prefab's `obj` tree; each nesting level pushes a `PrefabLayer` and the outermost
  layer wins.
- **Four walkers must stay in sync** for overrides: `sceneBuilder::writeObject`, the nested section
  of the object inspector, viewport3D and sceneGraph.

### Component registry

- `components.h:186-416`: a `constexpr std::array<CompInfo>` indexed by id. Ids 14-23 are reserved
  placeholders; fork components are 24 (Particle Emitter) and 25 (Spline). The next free fork id
  is 26.
- A component implements `init`, `serialize`, `deserialize`, `draw` (inspector), `build` (binary
  init data) and optional viewport hooks (`MAKE_COMP`, `components.h:88-104`). Its `.cpp` must be
  listed in `CMakeLists.txt` (no glob), and the runtime needs a `SET_COMP` entry
  (`n64/engine/src/scene/componentTable.cpp:68-88`).

### Build pipeline

- Entry: `Build::buildProject` (`src/build/projectBuilder.cpp:67`). The GUI runs it as a separate
  `--cli --cmd build` process after saving.
- Order: migration check, asset table, node graphs, global scripts, object scripts, each scene,
  `sceneTable.h/.cpp`, asset builders (T3DM, Font, Texture, Audio, Emitter, Canvas, Prefab last),
  `assetTable.h`, Makefile, `make`.
- Object record (`sceneBuilder.cpp:33-204`, read by `sceneLoader.cpp:15-25`): 36 bytes of header
  (`u16 flags, u16 id, u16 group (parent id), u8 visMask, pad, pos, scale, packed quat`), then per
  component `u8 compId, u8 sizeWords (max 255), u16 flags, init data`, then a zero terminator.
- Runtime ids: `Scene::assignRuntimeIds` (`scene.cpp:557-579`) numbers the scene tree in pre-order
  (root 0, then 1..N). When an instance is flattened, the definition's children are written inline
  with ids from `ctx.nextRuntimeId++` (`sceneBuilder.cpp:182-188`), which is a separate range after
  all scene ids. Objects added to the instance keep their scene ids.
- Prefab binary (`prefabBuilder.cpp:15-52`): `u32 count`, then the object stream with local ids
  0..count-1 in pre-order; `group` holds the prefab-local parent index. Nested prefabs are
  flattened into it.
- `ObjectRef` is 0 inside prefabs because the build resolves references with
  `ctx.scene->getObjectByUUID(uuid)->runtimeId` (`compCode.cpp:252-256`) and `ctx.scene` is null
  when building a `.pf`.

### Runtime

- Scene load (`n64/engine/src/scene/sceneLoader.cpp:227-265`): all object records are loaded, then
  a parents-active pass, then `runPendingComponentInit` (per component: `initDel`, `onEnable` if
  enabled, queue `onReady`).
- Frame order (`Scene::update`, `scene.cpp:174-349`): pending `onReady` calls (193), `SCENE_UPDATE`
  hook (196), spawn pending objects (201-238), component init (240), enable/disable pass
  (245-251), `runPendingEvents` (255), fixed update and physics (266-284), `update` (296-325),
  deferred deletes (333-345).
- Runtime spawn (`Scene::addObject`, `scene.cpp:548-573`): `rootId = nextId + 1`, then
  `obj.id = objectId + i` and `group = objectId + storedLocalParent` (`:204-236`). `nextId` is a
  file-static starting at 0xFF, never reset between scenes, with no guard against wrapping or
  against scene ids reaching 256.
- **Object flags** (`n64/engine/include/scene/objectFlags.h`): bits 0-7 are used, **bits 8-15 are
  free**. The editor writes only ACTIVE and HAS_CHILDREN; the spawn code overwrites the root's
  flags (`scene.cpp:210`).
- Global hook call sites: `GAME_INIT` (`main.cpp:96`), `SCENE_PRE_LOAD` and `SCENE_POST_LOAD`
  (`sceneManager.cpp:38`, `:43`; post-load runs after init and onEnable but before onReady),
  `SCENE_PRE_UNLOAD` and `SCENE_POST_UNLOAD` (`sceneManager.cpp:53`, `:55`), `SCENE_UPDATE`
  (`scene.cpp:196`), and the draw hooks (`scene.cpp:355`, `373`, `415`, `432`).

### Editor UX

- Prefab edit mode: `ctx.prefabEditUUID` (`src/context.h:77`), toggled in the object inspector
  (`objectInspector.cpp:612-623`). Edits change the shared `prefab->obj` in place and mark the
  prefab dirty.
- Windows are hardcoded members of `Editor::Scene` drawn in `editorScene.cpp:506-598`; the default
  dock layout is at `:271-304`. The hierarchy window is titled "Graph" and the scene inspector
  "Scene".
- Undo (`src/editor/undoRedo.h/.cpp`) snapshots `scene->serialize(true)` plus the selection, 100
  entries at most. Prefab-definition edits are probably not undoable (inferred, not tested).
- There is no autosave. Saving happens on Ctrl+S, Project > Save, before a build and when switching
  scenes. A new JSON field does not force a migration; the version policy is documented in
  `src/project/scene/migration.h:14-44`.

### Side notes

- Object uuids are u32 but `scene.cpp:132` and `:407` assign `randomU64()`, so the value is
  truncated.

## 3. UI canvas system (reported by explorer n29b4r-4, 2026-11-23)

`KC` = the `kentucky-castle` repo.

### Architecture

- A canvas is one `.canvas` JSON asset (`src/project/canvas/canvas.h:214-239`) with `conf`,
  `variables`, `events`, canvas-scoped `animations` (clips) and a tree of `elements` (Sprite, Text,
  ColorRect, TextureRect; `canvas.h:171-200`). An element can carry a `scriptUuid` (an object
  script used as a component script) and a `repeater`.
- `src/build/canvasBuilder.cpp` (`buildCanvasAssets`, 1753-1788) generates three project files in
  `src/p64/`: `UISceneTypes.h` (data structs, `UISceneType` enum, per-canvas `Anim` and `Event`
  enums), `uiDriver.cpp` (from the template `data/scripts/uiDriver.cpp`) and `uiBindings.cpp`
  (draw, behavior and animation functions). Canvas codegen runs before the script build
  (`src/build/projectBuilder.cpp:37-38`).
- The engine ships only headers (`n64/engine/include/ui/{UI.h,uiEval.h,uiElemState.h}`). The whole
  runtime is generated project code.
- **The engine does not drive the UI; the game's global script does**
  (`KC/src/user/global/GlobalSetup.cpp`): `UI::init()` at scene pre-load (51),
  `uiInitializeBindings()` at post-load (71-72), `UI::update` at pre-draw (166), `UI::draw_2D` at
  draw-2D (215-218), `UI::destroy()` at post-unload (83).
- **A canvas is not attached to anything.** It is not a component or a scene setting; every canvas
  exists in every scene and game code shows it with `UI::setSceneState(scene, enabled, layer)`
  (`UI.h:119`).

### Data binding

- Canvas variables become fields of `<Name>Data` (Float, Int, Bool, ColorRGBA, StringPtr,
  SpriteRef, FontRef). An element property is a literal, bound to a variable, a comparison op,
  or (inside a repeater) an item field or the index (`canvas.h:49-70`,
  `canvasBuilder.cpp:204-240`).
- One model callback per canvas writes the data each frame:
  `UI::bindModel<UISceneType::HUD>(UI::ModelCallback<...>{obj, data, fn})`. It is an
  `ObjectDelegate` bound to an object (`n64/engine/include/utility/ObjectDelegate.h:73`). A second
  `bindModel` on the same canvas fails (`uiDriver.cpp:122`).
- **Data flows strictly game -> UI.** Draw code only reads `data`. Nothing carries data back.
- Real use: `KC/src/user/scripts/Player.cpp:1315-1380` (bound at 1542, unbound at 1552).

### The existing canvas "events"

- A per-canvas list of names (`CanvasEventDef`, `canvas.h:37-45`; editor panel
  `src/editor/pages/parts/canvasEvents.cpp`). The row index is the id, so deleting an event
  renumbers the later ones. The generator emits `enum class Event : uint16_t` per canvas.
- They can only be sent from inside the model callback (through its `EventSink`, `UI.h:19-38`) or
  by a timeline FireEvent marker (value always 0). Controller input does not produce events.
- They are delivered the same frame to element component scripts' `onEvent`. They never enter the
  scene event queue and **game code never receives them**. Their ids start at 0, the same range as
  custom object events, so a handler cannot tell the two apart.
- Focus: an element marked Focusable with a component script gets `onFocus`, `onBlur`,
  `onActivate` from generated input code (D-pad up/down and A on port 1;
  `canvasBuilder.cpp:1202-1222`).
- Clips: `UI::playAnimation / stopAnimation / seekAnimation<Scene>(Data::Anim)` (`UI.h:99-106`);
  at most 16 clips per canvas; a finished clip raises no notification.
- Kentucky Castle has one canvas (`HUD`), no menus, no focus use, and no element script, so its
  one canvas event (`testEvent`) is dropped every time it fires.

### Gaps relevant to the event graph

- There is no public API to send an event into a canvas from outside its model callback.
- Nothing forwards anything from the UI to the game.
- Possible risk: component-script `Init` may be skipped if the model is bound after scene
  post-load (`uiDriver.cpp:181`); untested because no canvas uses scripts.
- `docs/todo/ui-phase6-draw-efficiency.md` describes `isElementStatic` as checking scripts,
  focusable, repeaters and timeline targets, but the code only checks bound props and nothing
  calls it yet (`canvasBuilder.cpp:385-394`).

### Hook points

- Into the UI: a new `UI::post<Scene>(Data::Event, value)` merged into the outbox after its reset
  (`uiDriver.cpp:53`); clip play/stop/seek; `setSceneState` for show/hide and layer.
- Out of the UI: forward outbox events in the drain loop (`uiDriver.cpp:67-68`); emit
  Focus/Blur/Activate from the generated focus code without needing a script; fire a clip-finished
  event where `finished = 1` is set (`canvasBuilder.cpp:1349`, `1412`).

## 4. Kentucky Castle systems (reported by explorer n29b4r-5, 2026-11-23)

`KC` = the `kentucky-castle` repo; unprefixed paths in this section are under `KC/src/user/`.

### Levels

- **A level is a prefab spawned into one persistent scene** (`KC/docs/LevelSystem.md:17-21`). It is
  not a scene. The persistent core (Player, BoomCamera, HUD) sits directly in the scene. Level
  prefabs are in `KC/assets/prefabs/levels/`. Scene 2 also places a level prefab statically with
  no LevelDriver.
- The level system is game code, not engine code: `global/systems/LevelDriver.{h,cpp}` (registry
  of 16 levels and 16 spawn points, `ObjectRef currentLevel`, state machine
  `Idle, FadeOut, Swap, WaitSpawn, Snap, FadeIn`) plus four scripts in `scripts/level/`
  (LevelDriver, LevelDefinition, PlayerSpawnPoint, LevelTransition).
- Everything is looked up by `fnv32` of `char[32]` editor fields. There are no level ids or
  object references.
- Swap: `currentLevel->remove()` then `addObject(prefabIdx)` (`LevelDriver.cpp:132-143`). The old
  and new level coexist for one frame. Projectiles are spawned at the root and outlive the level.
- Other systems poll `LevelDriver::isTransitioning()` instead of reacting (EnemySpawner,
  SpawnDirector, LevelTransition).
- Data mistakes found by reading the data, which a build-time checker would catch: three default
  or target spawn names that do not match any spawn point (`spawn_kokiri_start` vs `kokiri_start`,
  `spawn_hyrule_field_start`, `spawn_sp_from_jp` vs `sp_from_jp`) and one level
  (`level_poopshoot_cavern`) that is never registered. All fall back silently.

### The 36 existing custom events (`global/CustomEvents.h`)

- `ECustomEvent_T` is a plain enum numbered from 0; removing an entry renumbers the rest. That is
  harmless today only because no asset stores event ids.
- A message goes to every component of the target object. `LevelSystem.md` §11.6 records an event
  that was removed because several components on one object all reacted.
- Timing: a message sent from a global system inside the `SCENE_UPDATE` hook arrives the same
  frame (Damage, StatusFx and SpawnDirector rely on this); a message sent from a script arrives
  the next frame.
- **Fit plain output-to-input wiring:** VISIBILITY_CHANGED, ENCOUNTER_VISIBLE / HIDDEN / BEGIN /
  RESET / COMPLETE, ENCOUNTER_PLAYER_ENTER / EXIT, PLAYER_SET_CONTROL, CAMERA_RESET / LOCK /
  CLIMB_MODE / SET_POSITION_MODE, and OBJECT_DIED as a "Died" output.
- **Relays a graph would remove:** the Player forwarding encounter events to the camera
  (`scripts/Player.cpp:3029-3043`); EncounterNPC and EncounterPlayer forwarding DIED.
- **Broadcast to children:** EncounterVolume's `sendEventTo*` helpers
  (`scripts/encounter/EncounterVolume.cpp:63-103`) and VisibilityVolume.
- **To sibling components on the same object:** OBJECT_DIED sent to self.
- **Must stay addressed messages with payloads:** NPC_REQUEST_TARGET / SET_TARGET,
  REPORT_ATTACK_READY / CAN_ATTACK, SPAWNER_HOLD, DAMAGE_RECIEVED, STATUS_EFFECT_APPLY / REMOVE,
  PROJECTILE_INIT / LOB, NPC_DESPAWN.
- Registration handshakes (one side remembers the sender id): CAMERA_BOOM_ATTACHED, VOLUME_HELLO,
  NPC_JOINED, TARGET_FOCUSED. Static authored pairs could become wires; pairs formed at runtime
  cannot.
- CAMERA_BOOM_DETACHED is never sent.

### Trigger volumes

- Collision layers (`global/Constants.h:33-40`): 0 WORLD, 1 OBJECT, 2 STATUS_EFFECT_TRIGGER,
  5 CLIMB_TRIGGER, 6 VISIBILITY_TRIGGER, 7 ENCOUNTER_TRIGGER.
- Two directions are in use. Either the volume reads and its own script gets the callback
  (Visibility, LevelTransition, StatusEffect), or the volume only writes and the player-side script
  gets the callback (Encounter, Climb). **A trigger with Enter/Exit outputs needs the first
  direction, or engine-side pair tracking.**
- Five different hand-written enter/exit schemes exist: a 2-bit history
  (`VisibilityVolume.cpp:52-80`), 8 occupant slots with seen/pending marks
  (`StatusEffectVolume.cpp:114-208`, the most complete), a single inside/was-inside flag
  (`EncounterPlayer.cpp:37-79`, breaks with overlapping volumes), a level flag (Player climb), and
  repeat-and-ignore (`LevelTransition.cpp:29-41`).

### AI scripts, spawning, animation

- `KC/ai/*.ai` is a DSL compiled by `KC/tools/ai_compiler.py` into constexpr bytecode; the VM runs
  in `scripts/Enemy.cpp`. **The DSL cannot send or receive engine messages.** Its event-like
  opcodes use global state (`AlarmOn/AlarmOff/IfAlarmOn` through `Perception::setAlarm`).
- Animation events come from trait files (`DefineEvent`) and reach scripts through
  `ObjectDelegate` callbacks (`scripts/Minion.cpp:422`, `scripts/Player.cpp:1523`,
  `scripts/Enemy.cpp:2505`).
- SpawnDirector does the `addObject` calls from the update hook and tracks spawned objects in
  tables; deaths arrive through direct `reportDeath/reportGone` calls, not messages.
- `ObjectDelegate` (`n64/engine/include/utility/ObjectDelegate.h`) is the existing same-frame
  direct-call binding: object ref, data pointer and function. About 136 bytes each. Used by the UI
  model/view/controller callbacks, Damage and the animation drivers.

### Global systems

- `global/GlobalSetup.cpp`: init of all systems at scene pre-load, destroy at post-unload; update
  order in the update hook is Damage, Navigation, Perception, StatusFx, LevelDriver, SpawnDirector
  (the order matters); the LevelDriver fade draws last.

### Tooling

- `KC/Makefile.custom` adds the trait compiler, the AI compiler and deploy targets.
- **`KC/.github/workflows/build-rom.yml` builds with the latest Pyrite64 release AppImage**, so an
  engine feature must ship in a Pyrite64 release before the game's CI can build against it.
- `KC/.vscode/tasks.json` points at `../../Pyrite64/source/pyrite64/pyrite64`, which does not
  exist in this folder layout.
- Scenes: 1 TestLevel, 2 bob-omb-battlefield, 3 TestEnemy, 4 TestAnimation, 5 Kokiri,
  6 HyruleField, 7 Junker_Prime, 8 Satellite_Plateu, 9 kentucky-castles-start (boot).

### Hookups the event graph would replace or enable

1. LevelTransition: trigger Enter wired to a level transition with per-wire constants, with names
   checked at build time.
2. Level lifecycle outputs (loaded, unloading, transition started, fade-in done) replacing the
   `isTransitioning()` polling and the PLAYER_SET_CONTROL / CAMERA_RESET messages.
3. VisibilityVolume Visible/Hidden wired to anything, not only to children.
4. EncounterVolume Begin/Reset/Complete wired to doors, music, UI and the camera mode.
5. EnemySpawner inputs (Activate, Pause, Stop, Despawn) from any output.
6. A `Died` output for objectives, crates and chains.
7. StatusEffectVolume, climb volumes and ProximityBomb as general triggers.
8. AI: an alarm output/input and new DSL opcodes to fire and test events.
9. Animation event outputs for sound and particles; gameplay outputs into the UI.

## 5. Script codegen, tests, CLI, conventions (reported by explorer n29b4r-6, 2026-11-23)

**The two facts that matter most:**

1. **Script table indices change between builds** (position in a name-sorted list), so anything
   saved to disk must identify a script by UUID.
2. **Engine code is built with `-Werror` and without `-Wno-attributes`**, so a builtin script
   compiled as engine code cannot use `[[P64::...]]` attributes without a build change.

### The code parser (`src/utils/codeParser.{h,cpp}`)

- Hand-written scanning plus `std::regex`. Comments are stripped by regex (`:265-269`) without
  knowing about string literals.
- `parseDataStruct` (`:596-621`) finds the first `P64_DATA(` and parses its fields.
  `parseScriptData` (`:623-657`) handles `P64_DATA_TYPE(T)` by finding the struct in the script or
  its quoted includes (`findStructInIncludes`, `:417-446`).
- One regex per member (`:457`); stacked `[[...]]` blocks are merged; `parseAttributes` (`:213`)
  is safe for commas and brackets inside quotes.
- Attributes: `Name`, `Range`, `Min`, `Max`, `Enum`, `Bitmask`, `Group` here;
  `RequireComponent` is resolved in `compCode.cpp:143-172`.
- `hasFunction` (`:659`) searches the whole file for `void<name>(` after removing comments and
  whitespace. It ignores namespaces and classes, so a same-named helper method is a false
  positive.

### Script discovery and codegen

- `<proj>/src/user/**` is scanned for `.cpp` files containing `::Script::` (object script) or
  `::GlobalScript::` (global script); the next 16 characters are the UUID
  (`src/project/assetManager.cpp:478-495`, `227-267`).
- `buildScripts` (`src/build/scriptBuilder.cpp:17-128`) fills the template
  `data/scripts/scriptTable.cpp` into `<proj>/src/p64/scriptTable.cpp`: per script a forward
  declaration block, a `ScriptEntry` initialiser, a size entry and a name.
- Code component init data (`compCode.cpp:223-303`): `u16 scriptIdx, u16 0`, then every parsed
  field back to back. The runtime copies the blob straight into `Data` (`code.h:59-66`).
- New-script templates live in `data/scripts/` (`defaultObject.cpp`, `defaultObjectShared.cpp`,
  `defaultObjectData.h`, `defaultGlobal.cpp`).
- **There is no mechanism for engine-shipped C++ scripts.** The script table only lists files from
  `src/user`.

### How code is compiled

- The editor copies `n64/engine` into `<proj>/engine` every time a project is constructed
  (`src/project/project.cpp:257`), comparing by content. It never deletes files removed upstream.
- `data/build/baseMakefile.mk` is rewritten as `<proj>/Makefile` on every build
  (`projectBuilder.cpp:274-290`). Sources: `src/*.cpp`, `src/p64/*.cpp`, `src/user/**`. Flags:
  `-std=gnu++20 -fno-exceptions -O3`. `-Wno-attributes` applies to `src/user` objects only.
- The engine is built by `make -C engine` into `engine/build/engine.a` with
  `-Wall -Wextra -Werror -Wshadow -Wfatal-errors`; its Makefile lists source dirs explicitly.

### Hashes and generated headers

- Engine: `P64::crc32` and `crc64` are `consteval` (`n64/engine/include/lib/types.h:19`, `:34`);
  `"..."_hash` is crc32 (`:56`). `fnv32`/`fnv16` are `constexpr` (`include/lib/hash.h`).
- Editor: `Utils::Hash::crc32`/`crc64` are `constexpr` (`src/utils/hash.h:27-55`); JS has `crc32`.
  There is no editor fnv32.
- `p64/assetTable.h` and `p64/sceneTable.h` are generated (`projectBuilder.cpp:225-252`) and offer
  `consteval` lookups plus the literals `_asset`, `_prefab`, `_scene`. **An unknown name fails at
  compile time.** `userScript.h` includes both, so every script recompiles when they change.

### Tests, CI, CLI

- doctest; target `pyrite64_tests` (`CMakeLists.txt:402-415`) compiles only `codeParser.cpp` and
  `logger.cpp`. Fork tests live in their own file (`tests/codeParserFork.cpp`). 32 cases. Run with
  `./build/pyrite64_tests`.
- `n64/tests/test_obj_states` is a full project whose global script records lifecycle events and
  compares them with expected sequences, reporting OK/FAIL on screen and through `debugf`. It is
  built and run in an emulator by hand; CI does not build it.
- CI (`.github/workflows/editor.yml`) runs on pushes to `stream/full-frontal`: builds for Windows,
  Linux and macOS, runs `pyrite64_tests` on Linux, then publishes a release.
- CLI (`src/cli.cpp:30-123`): `--cli --cmd {build|clean|migrate} <project>`.

### Conventions

- Fork commit style: `area: lowercase summary` with area `engine`, `editor` or `docs`, and a
  wrapped body explaining why. Upstream merges are `Merge upstream <sha> (Sn: ...)`.
- `// Fork:` comment prefix marks fork-only code. Fork component ids start at 24.
- Changelog (`docs/docs/version/changelog.md`) has no fork entries; the latest is v0.10.0 while
  CMake says 0.11.0. Breaking changes go in `breakingChanges.md`.
- Building the editor on Linux: `cmake --preset linux-release` then
  `cmake --build --preset linux-release` (`docs/docs/dev/build.rst`).

### Debug facilities

- Debug menu pages: add a `Menu` in `n64/engine/src/debug/overlay.cpp`, set `onDraw`, implement it
  in `src/debug/overlay/ovlX.cpp` (`ovlParticles.cpp` is a model). Games can add pages with
  `Overlay::addCustomMenu(name)`.
- Logger (`include/lib/logger.h`): `P64::Log::info/warn/error` through `debugf`; no categories.
- `FIFO` (`include/lib/fifo.h`) and `RingBuffer` are not interrupt-safe; the one FIFO user wraps
  access in `disable_interrupts()`. `ObjectEventQueue` is a heap `std::vector`.
