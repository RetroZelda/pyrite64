# Changelog

## Unreleased (Full-Frontal fork)

### Event graph
* Named events on an engine event bus (`"scene::Door.Opened"_event`), delivered in the same frame, with global, scene, prefab, object and UI scopes, numbered parent scopes for nested prefabs (`prefab_1::`), and events the engine fires (scene load, controllers, paks, reset)
* Script pins (`P64_EVENTS_IN/OUT`, `P64_GLOBAL_EVENTS_IN/OUT` for global script systems), `send()` to an input by name, pins on objects and engine components, collider enter/exit
* 15 built-in event objects (Relay, Delay, Frame Delay, Once, Counter, All, Compare, Gate, Toggle, Sequence, Random, Log, Scene Loader, Spawner, Message Bridge)
* Per-instance overrides of prefab bindings and repointed parent links; "To Prefab" and "Unpack" keep every binding reaching what it reached
* The Events window: a node graph of the wiring, with an event browser (find usages, rename), focus mode and canvas nodes
* UI canvas pins (Show, Hide, PlayClip, StopClip, Event in; Focused, Blurred, Activated, ClipFinished, Marker out) and the "Fire output" timeline marker
* Command line: `--cmd graph`, `wire`, `unwire`, `events`, `rename-event`, `to-prefab`, `unpack`; `--ui-script` plays input into the editor
* Debug overlay page "Events"; example project `n64/examples/events`
* `Events::send(targetId, "Input"_pin, value, sender)`: a direct send by object id, looked up on delivery. A send to an id that `addObject()` returned this frame waits for the object and arrives at the start of the next frame, after its `init()`
* The old addressed messages are deprecated: `Scene::sendEvent` has `[[deprecated]]` (engine code, the Message Bridge and the node-graph "Send Event" node use `Scene::sendEventInternal`, which behaves the same), the build warns about scripts that define `onEvent` (not engine or UI element scripts), new-script templates no longer contain `onEvent`, and the engine Makefile has `-Wno-error=deprecated-declarations`

### Conditions
* A condition is a script declared with `P64_CONDITION_DATA` that answers in `bool evaluate()`. It gets the pins Enable, Disable, Check, Reset and True, False, NewlyTrue, NewlyFalse, Result; its fields are static values, not pins
* `P64_CONDITION_TRIGGERS("ns::Name"_event, ...)`: the events that make a condition check itself, decided in code and bound to its Check input by the build; "Auto Check" switches them off for one object or prefab instance
* Built-in logic nodes And, Or, Xor and Not: wire any number of Result outputs into their Inputs pin; `P64_CONDITION_INPUTS` and `Conditions::Inputs` for game scripts that combine conditions
* `Conditions::evaluate()`, `check()`, `setEnabled()`, `reset()` from code (`events/conditions.h`)
* Events window: condition nodes show their values, a "Condition" entry in the add menu, Result wires in their own colour, triggers shown locked on Check
* Node graphs: a "Fire Event" node posts a named event
* Fix: an instance override of a script argument that the prefab file never stored is now found by the build (it built the default before)
* The build's "fires 'X', but nothing listens to it" note appears once per event, with a count of the other outputs that fire it

### Support for interactions (docs/todo/interactions)
* `P64_CONDITION_INPUTS` works in any object script, not only in conditions: an input pin that reads conditions (a game's "Can Use" gate) is drawn as such in the Events window, and the build warns when no condition's Result is behind it. Read it with `Conditions::Inputs{obj, data, In::Pin}`
* `P64_EDITOR_ARROW()`, `P64_EDITOR_ARROW(r, g, b)` or `(r, g, b, length)`: the viewport draws an arrow along the object's +Z (the way actors face) over a ring at its feet, for every object with that script, so a spot and a facing can be placed by eye

### Animation events (docs/todo/anim-events)
* A Model (Animated) component gets one output pin per animation event its model lists, after MainFinished. The list is a new `animationEvents` section of the event catalog (`events/*.json`): per model asset path, the pin names (letters, digits, `_` and dots: `Run.STEP_LEFT`) with a doc and value text for the tooltip. At most 64 per model (`ANIM_EVENTS_MAX`), at pin index 0x80 + position (`ANIM_EVENT_PIN_BASE`)
* Game code fires one with `AnimModel::fireEvent(obj, anim, pin, value, instigator)`, by the hash of the full pin name (`PinName{pinHash("Run.STEP_LEFT", 13)}`); the engine never decides when, and has no idea where the names come from. The build writes `src/p64/animEvents.cpp` (per model asset index, the pin hashes in order) from the same list the editor numbers the pins from
* Build messages for the list: errors for a bad or repeated name, a model listed twice, more than 64; warnings for a path that is no asset or not a model
* Pin tooltips in the Events window and the inspector show the catalog's doc and value text
* New wires keep the dots of a pin name: `prefab::Player.Run.STEP_LEFT`, `ui::Hud.ClipFinished.Intro` (they were dropped before)
* The command line (`wire`, `unwire`) finds pins whose names have dots
* Test scene 11 in `n64/tests/test_events`; the check scripts copy the project's `events/` folder, and `nestingChecks.sh` runs ares on a copy of the settings file

### Localization and dialog trees (docs/todo/dialog)
* Symbolic text keys: `"npc.tank.greet"_localized` is a CRC-32 id (`text/locId.h`); one table per language in `locale/` (source language, translations, notes), built to `filesystem/p64/loc/<code>.loc`, the language picked at run time (`text/loc.h`)
* Two language slots at once (the game's and a paired one), a missing translation falls back to the source text with a debug mark, and a pseudo-language for testing layouts
* Text tags in brackets, compiled at build time: `[b]`, `[i]`, `[size]`, `[font]`, `[color]`, `[shake]`, `[wave]`, pauses and speed, `[btn=...]` button icons, `[sfx=...]`, `[page]`, and `\n`; errors name the key and the language
* Variables in braces, declared only in C++ (`P64_LOC_VAR`, caller values with `P64_LOC_ARG`): numbers with formatting, plurals with CLDR rules and `=N` cases, keys of other texts; `Loc::format()` (`text/locFormat.h`)
* Font sets per language with bold and italic, baked with only the characters the texts use; line breaks measured with rdpq and kinsoku rules for Japanese
* Page breaks (`Loc::splitPages`, `text/locPages.h`): whole sentences stay on one page, a sentence longer than a page is split evenly, at a comma or line break near the even point; the game and the Strings window's estimate share it. `Loc::breakLines` takes the font and style at its start
* The Strings window's page estimate measures at locale.json `box.pageSizeStep`, the text size the game works pages out at
* The Strings window (View > Strings): every key in every language with errors while typing, notes, keep rules, unused keys, CSV export and import for translators (`locale/strings.csv`), and how many lines and pages a dialog line takes in each language
* The build scans code, scenes, prefabs and dialog trees for keys; command line `loc-refresh`, `loc-unused`, `loc-prune --yes`, `loc-export`, `loc-import`
* `LOC_TEXT` script fields: a key picked from the table, built as its id
* Dialog tree assets (`.p64dlg`, New: Dialog Tree) and the Dialog Tree window: Start, Line, Choice, Branch, Condition, Logic, Input, Event, GoTo and End nodes, texts edited in the preview language, Play; the engine only holds the data (`dialog/dialog.h`, `dialog/dialogData.h`), the game walks the trees
* The Speakers window: name, language, colour, fonts, portraits, voice blips and talk clip per speaker
* `Conditions::evaluateScript()` answers a condition script on any object; `Loc::voiceClip()` finds a voice clip by key and language; `AssetManager::findByPath()`
* Tests 16 "Localized text" and 17 "Dialog tree data" in `n64/tests/test_events`, and unit tests for tables, tags, plurals, CSV and trees

### Performance
* `Scene::getObjectById()` finds objects with ids from 128 up through a hash table (`scene/idTable.h`) instead of going through every object: 0.5 µs instead of 60 µs for the last of 237 objects. Every object of a spawned prefab has such an id
* `n64/tests/perf_events`: a benchmark of the event bus and the conditions, with its results in the README (about 4 µs an event, 6 µs a condition check, in the ares emulator)

## v0.10.0

### Editor
* VSCode project generator (on new project + manual) by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/329
* Add template-selection during project creation by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/330
* Fix: Correct lock-Icon size when zoomed by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/331
* Support editor side bool and color picker arguments in code components, add Range & Min/Max attributes by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/336
* fix excessive history with input/color picker by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/338
* Preview Fog in the editor + setting to not do so by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/341
* Texture Picker (preview and search textures) by @KevinLKs in https://github.com/HailToDodongo/pyrite64/pull/342
* Add P64::Group attribute to group arguments by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/348
* fix: broken asset table when toggling exclude setting by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/333
* Allow Meshes with no material for collision by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/334

### Features & Engine changes
* Fix skinned meshes using bigtex by @FlorentDeloison in https://github.com/HailToDodongo/pyrite64/pull/328
* draw 3D/particle layers per camera instead of once per frame by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/335
* Treat collision as double sided for character-body, capsule & sphere sweeps by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/337
* Added world space offset to Camera component by @thekovic in https://github.com/HailToDodongo/pyrite64/pull/340
* Stable Layer names / access via hash by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/347
* Shared C++ Script data & methods by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/346
  * https://hailtododongo.github.io/pyrite64/docs/manual/script/objScript.html#shared-data

## v0.9.0

### Materials & Rendering
* Material setting for t3d Z-offset (decal alternative) by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/305
* Add Visibility Layers to define what cameras render by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/312
* Added "Surface" component by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/311
* Allow Cameras to target offscreen surfaces by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/313

### Features & Engine changes
* [FIX] Camera Component now responds to object enable/disable state correctly by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/308
* Use Meters for Objects and components, decouple rendering scale + auto model import scaling by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/315

### Physics System
* Small physics memory optimizations by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/307
* chg: Optimizations in collision detection by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/324
* chg: Build time Mesh Collider BVHs by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/325

### Editor
* Add search filter to scene graph by @LlanerasJ in https://github.com/HailToDodongo/pyrite64/pull/280
* Asset browser improvements by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/296
* Allow to disable components by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/294
* feat: Allow to drag-drop a 3D model into the scene by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/310
* chg: trigger rebuilds on header changes if needed by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/314
* Scene graph tree multi selection / dragging by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/316
* Fix: handle collider shape changes properly & expose sensible collider api by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/322
* Improve Editor-performance with huge amounts of image assets by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/326
* Preview Constraints in Editor (e.g. billboards, skyboxes) by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/327

### Docs & Build
* docs: Add info about SDL dependencies to build docs by @thekovic in https://github.com/HailToDodongo/pyrite64/pull/319

**Full Changelog**: https://github.com/HailToDodongo/pyrite64/compare/v0.8.0...v0.9.0

## v0.8.0

### Node-Graph
* Node Graph Rewrite + User defined nodes by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/256
  * Node Docs: https://hailtododongo.github.io/pyrite64/docs/manual/script/nodeGraph.html
  * Custom Node Docs: https://hailtododongo.github.io/pyrite64/docs/manual/script/nodeGraphCustom.html

### Features & Engine changes
* Prefab support for nested objects / other prefabs by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/268
  * Docs: https://hailtododongo.github.io/pyrite64/docs/manual/editor/prefabs.html

### Physics System
* Physics API: Add swept-sphere function by @Aaronmac64 in https://github.com/HailToDodongo/pyrite64/pull/276
* improved collision system heap traffic, introduced better bookkeeping… by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/288
* big collision optimizations by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/293
* Auto fit colliders by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/295
* Change VEC3_FORWARD direction in math.h by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/300
* fix: let rigidbodies adopt changes from externally changed transforms, prevent visual interpolation leaking into physics by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/301
* feat: Allow to set the collider colour in the preferences by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/302
* correctly scale the velocity of the rigidbody by the graphicsscale wh… by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/303

### Editor
* Editor Themes (dark, light, high-contrast, red & blue dark) by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/261
* Save and check editor version in Project by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/258
* Editor for ROM Metadata + Homebrew-Header by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/259
* Remember and show recently opened Projects by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/263
* Multiple Viewports + Game-Camera preview by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/264
* Fix not opening Ares emulator after the second usage of "Build & Run" by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/269
* Allow to drag&drop a Script to add a Code component by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/272
* Allow to open the selected Script from the Code component by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/273
* Project setting to disable Debug-Menu hotkey by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/275
* Fixed editing parent transforms by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/274
* Allow to reorder the object components by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/277
* Implement proper wayland session restoration by @Lilaa3 in https://github.com/HailToDodongo/pyrite64/pull/279
* Add viewport lock mode for touchpad/laptop navigation by @LlanerasJ in https://github.com/HailToDodongo/pyrite64/pull/278
* toolchain: Show N64_INST location on Linux to help validate the environment by @thekovic in https://github.com/HailToDodongo/pyrite64/pull/286
* Add audio preview for sound assets in the asset inspector by @LlanerasJ in https://github.com/HailToDodongo/pyrite64/pull/285
* Change UI Pos/Rot/Scale order to make it consistent by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/291
* Fix quote emulator and ROM paths so paths with spaces work by @LlanerasJ in https://github.com/HailToDodongo/pyrite64/pull/290
* Add projection settings camera component (Ortho / Perspective) by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/304

### Docs & Build
* Mention MacOS by @seacat17 in https://github.com/HailToDodongo/pyrite64/pull/254
* Fix typo in docs by @seacat17 in https://github.com/HailToDodongo/pyrite64/pull/260

## v0.7.0


### Material System
* New Material System / Placeholder and dynamic textures by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/179
* Additive-Light option in Draw-Layer / Fix point lights by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/226

 Docs: https://hailtododongo.github.io/pyrite64/docs/manual/editor/materials.html

### Physics System
* New Physics-System + Rigid-Body simulation by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/207
  * Make applyForceAtPoint actually accumulate force by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/225
  * Decouple physics scale from visual scale by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/228
  * Add an applyLinearForce method by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/231
  * Don't change velocity on hit during swept substep detection by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/232
  * Add a localCenterOfMassOffset property to RigidBody by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/233
  * Add method for creating custom collision mesh by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/234 
  * Fix raycast box hit_face tracking and report origin when starting inside by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/227
  * Allow a MeshCollider to have no owner by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/238
  * Add an AABB tree of MeshColliders to speed up several checks by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/239

* Character Body Component by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/247
  * Docs: https://hailtododongo.github.io/pyrite64/docs/manual/editor/collision/characterBody.html

### Features & Engine changes
* New Billboard constraint type (Y or XYZ)  by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/200
* Support for .XM audio files by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/202
* New Debug Overlay + Allow User defined entries by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/213
* enable objectRefs, prefabs, and sprite asset refs to be instanced when using a prefab in a scene by @RetroZelda in https://github.com/HailToDodongo/pyrite64/pull/215
* object will recursivly remove children by default by @RetroZelda in https://github.com/HailToDodongo/pyrite64/pull/217
* Add ability to temporarily disable a RigidBody by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/242
* Added check to clamp diagonal movement input length by @Aaronmac64 in https://github.com/HailToDodongo/pyrite64/pull/243
* Add ability to toggle visibility of Object by @andmatand in https://github.com/HailToDodongo/pyrite64/pull/244
* Bitmask support in C++ Script props by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/251
* Refactor Object Events / Lifecycle + Docs by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/223
  * Docs: https://hailtododongo.github.io/pyrite64/docs/manual/editor/objLifecycle.html

### Editor
* Enable setting Prefabs in the editor / code-component arguments by @RetroZelda in https://github.com/HailToDodongo/pyrite64/pull/210
* Disallow dropping an object into a descendant by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/219
* Move selected object to the position of the 3D viewport camera by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/224
* Allow to edit the name of an object on the scene tree by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/218
* Show per-row icons in scene-graph hierarchy, remove lines for cool clean UI by @Prazon in https://github.com/HailToDodongo/pyrite64/pull/237
* Fixes the start position of the TextBox to edit a tree node by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/240
* Consistent AABB rendering across component types by @floridaman in https://github.com/HailToDodongo/pyrite64/pull/241
* 3D Model Thumbnails / Preview in asset browser by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/249
* Allow to filter in the "Script" ComboBox of the "Code" component by @MoisesMlg in https://github.com/HailToDodongo/pyrite64/pull/245
* Remove Object ID in editor by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/248
* Adding option to modify camera move speed while in flycam mode in viewport by @DekeDev in https://github.com/HailToDodongo/pyrite64/pull/204

### Docs & Build
* Docs: instruct MYSYS2 users install UCRT Python3 by @JordanMajd in https://github.com/HailToDodongo/pyrite64/pull/208
* fix: update libdragon for Linux build by @cyphercodes in https://github.com/HailToDodongo/pyrite64/pull/230
* Linux AppImage build by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/250

## v0.6.0

- Editor zoom (by default: Ctrl + Scroll) by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/159
- Editor: Add anti-alias option by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/161
- Editor: Add VSync option and custom FPS Limit by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/162
- Rotation can be edited as euler angles, scaling can be proportionally edited
  - Fixes value jumps when editing rotation as euler angles by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/171
  - Replaces uniform scale with proportional scale by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/174
- mingw: Add useful toolchain location log by @thekovic in https://github.com/HailToDodongo/pyrite64/pull/173
- defer the object initialization that might use a later object reference (fixes #177) by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/178
- Create global-scripts and node-graphs from the asset-browser by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/180
- Fix scene name not updating in the scene browser by @MessyComposer in https://github.com/HailToDodongo/pyrite64/pull/191
- Delete scenes via right-click context menu by @MessyComposer in https://github.com/HailToDodongo/pyrite64/pull/192

## v0.5.0

- Migrated documentation to sphinx, added new docs
  - Available online here  https://hailtododongo.github.io/pyrite64/index.html
- fix: keymap was only getting applied if it failed to load from file by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/126
- Better handling of unsaved assets and settings, ask before closing node-graph by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/111
- Trackpad pan and orbit by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/124
- fix: handle dragging objects into/near the root node in the scene-graph by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/131
- Collision mesh now generated on demand, remove "Collision" asset setting by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/139
- editor: Lose focus from InputText when using 3D viewport by @thekovic in https://github.com/HailToDodongo/pyrite64/pull/137
- Transforming Object now trans. attached camera + new option to toggle this behaviour by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/142
- Asset browser context popup by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/132
  - Right click on assets to show more options (open file, open directory, rename, delete)
- Project wide shortcuts now use reassignable keymap by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/140
- Refactor C++ Scripts to have separate init / destroy function + add scripting docs by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/145
- editor: Fix 'Show in Explorer' not opening correct folder sometimes by @thekovic in https://github.com/HailToDodongo/pyrite64/pull/150
- Remember window layout between sessions by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/148
- Remember window size and position across sessions by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/147
- Handle WSL in Asset Browser by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/154
- Open application directly from project file on Linux by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/146
- Viewport navigation preferences, fixes Linux pinch zoom by @Q-Bert-Reynolds in https://github.com/HailToDodongo/pyrite64/pull/156
- Add Tooltips to Toggle Buttons by @Byterset in https://github.com/HailToDodongo/pyrite64/pull/157
- Check for Pyrite64 updates + Link to new release page by @HailToDodongo in https://github.com/HailToDodongo/pyrite64/pull/158
- Show error if assets have duplicate UUIDs.
- Show error model-components have missing models.
- Fix temp. broken model references at runtime in prefabs during asset changes

**Full Changelog**: https://github.com/HailToDodongo/pyrite64/compare/v0.4.0...v0.5.0

```{admonition} This version introduced breaking changes!
:class: warning

Checkout [Breaking Changes](./breakingChanges) for more information.
```

## v0.4.0
- MacOS / Metal support (by [rasky](https://github.com/rasky), #106)
- Runtime-Engine
  - Fix rendering issue where the camera region / scissor is not set for all draw-layers
- Editor - General
  - Fix clean-build under windows
  - Automatically force a clean-build if engine code changed
  - Configurable keybindings and editor preferences (by [@Q-Bert-Reynolds](https://www.github.com/Q-Bert-Reynolds), #95)
  - Fix issue where snapping during scaling would collapse objects to zero size
  - Improved performance of asset browser  
- Toolchain manager:
  - Existing installations can now be updated too (by [@thekovic](https://www.github.com/thekovic), #11)
- CLI
  - New command to clean a project (`--cmd clean`)

## v0.3.0
- Editor - General
  - Auto-Save before build & run
  - Instantiating prefabs now places them in front of camera
  - Fix issue where adding new scripts could temp. mess up asset associations during build
  - Opus audio now working
  - New ROM Inspector for asset sizes (by [@proverbiallemon](https://www.github.com/proverbiallemon), #100)
  - Show Project state in title and ask before exiting with unsaved changes (by  [@Byterset](https://www.github.com/Byterset), #103)
- Editor - Viewport:
  - Multi-Selection support: (by  [@Byterset](https://www.github.com/Byterset), #7)
    - Click and drag left mouse to multi-select objects
    - Hold "CTRL" to add to selection
    - Transform tools can be used on multiple objects at once
  - Camera improvements: (by [@Q-Bert-Reynolds](https://www.github.com/Q-Bert-Reynolds), #40)
    - Focus objects by pressing "F" 
    - Orbit objects by holding "ALT" and left-clicking
    - 3D axis-gizmo label and orientation fixes
- Editor - Log Window:
  - Buttons for clear / copy to clipboard / save to file
  - Properly strip ANSI codes
- Editor - Scene:
  - New scene setting for audio-mixer frequency (default: 32kHz)
- Model Converter (tiny3d):
  - fix issue where multiple animations with partially matching names could lead to them being ignored
- Various toolchain and build-setup improvements (by [@thekovic](https://www.github.com/thekovic))

## v0.2.0
- Toolchain-Manager:
  - fixed build failure if MSYS2 home path contains spaces
  - check for existing N64_INST / toolchain installation (windows)
  - auto-update old MSYS2 installations 
  - keep installer terminal open in case of errors 
- Editor:
  - Fix DPI scaling issues
  - Show error-popup if Vulkan is not supported 

## v0.1.0
Initial release
