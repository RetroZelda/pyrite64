# Dialog and localization: findings

Evidence behind [PLAN.md](PLAN.md), gathered 2026-11-26 on pyrite64 `stream/full-frontal` (717f4d2)
and kentucky-castle `main` (6dcde08). Paths are relative to `pyrite64/` unless they start with
`kentucky-castle/` or `libdragon/` (`pyrite64/vendored/libdragon`).

## A. Literals and source scans (the template for `_localized`)

- A1. `"ns::Name"_event` is `consteval` at `n64/engine/include/events/types.h:300`.
  - It calls the `constexpr` `parseName` (`:120`): the CRC-32 of the text, except that `prefab_N::`
    hashes as `prefab::`.
  - Bad names fail to compile through a non-constexpr function named after the error (`:104-112`).
  - `types.h` has no libdragon includes, so the editor compiles it too (`src/project/events/eventRules.h:17`).
- A2. Other literals:
  - `_pin` (`types.h:290`);
  - `_crc32`, `_hash` and `_crc64` (`lib/types.h:51-64`);
  - generated `_asset`, `_prefab` and `_scene` (`data/scripts/assetTable.h`, `sceneTable.h`).
- A3. `Project::Events::scanSource` (`src/project/events/eventRules.cpp:268-302`) finds
  `"..."_(event|pin)` by regex.
  - `scanSourceDir` (`:317-339`) walks `src/user` (`.cpp`, `.h`, `.hpp`, `.cc`).
  - It does not strip comments, it is not cached, and it skips `engine/scripts`.
- A4. C++ declarations are read by text, in `src/utils/codeParser.cpp`.
  - `findMacroCalls` (`:724-753`) finds `NAME(`, skips `#define` lines, and splits on commas up to
    the first `)` (no nested parentheses).
  - `stripComments` (`:267`) knows nothing about string literals.
- A5. `AssetManager::reload` scans `<proj>/src/user/**/*.cpp` and `<proj>/engine/scripts/*.cpp`
  (`src/project/assetManager.cpp:494-524`).
  - `buildCodeEntry` (`:227-283`) runs the parsers and stores their results on the entry
    (`assetManager.h:98-103`).
  - `pollWatch` re-reads changed files every 2 s.
- A6. No localization code exists in either repo. `src/build/stringTable.h` is an unused
  string-dedupe helper.

## B. Event graph, node graph, assets

- B1. The event graph has no asset file. Its bindings live in scene, prefab and canvas JSON
  (`src/project/scene/object.cpp:53,201-222`; `src/project/canvas/canvas.cpp:439,465`). Its layout
  lives in editor-only sidecar files (`src/editor/pages/parts/eventGraphWindow.cpp:457-467`).
- B2. The graph UI is an in-tree ImNodeFlow fork (`vendored/ImNodeFlow`). It has one node class,
  `GraphNode` (`eventGraphWindow.cpp:99`). The model without UI is `src/project/events/eventGraph.{h,cpp}`.
- B3. Event links reach the ROM as a hidden component (id 26) of `{u32 hash, u8 comp, u8 pin, u8 kind,
  u8 up}` records (`src/build/eventBuilder.cpp:404-431`). The engine reads them in
  `n64/engine/src/scene/components/eventLinks.cpp:39-70`.
- B4. Node graphs:
  - **File:** a `.p64graph` file with a `.conf` sidecar.
  - **Nodes:** defined in JavaScript (`data/nodes/builtin/*.js`) and compiled to C++ coroutines
    (`nodeGraphBuilder.cpp:31-68`).
  - **Weaknesses:** links are saved by pin position, there is no undo, and there are no tests.
  - **Event checks:** the Fire Event node (`data/nodes/builtin/object.js:48-65`) is indexed for
    event checks (`eventBuilder.cpp:233-237`).
- B5. `docs/todo/event-graph/FINDINGS.md` §1 says to reuse the ImNodeFlow fork and the node editor's
  shell, not `Graph`, `Node::Base` or `ScriptNode` (tied to code generation).
- B6. Asset types (`n64/engine/include/assets/assetTypes.h:8-26`; `src/project/assetManager.h:35-52`).
  - The editor ordinal is packed into the top 4 bits of the ROM asset table entry, so there are at
    most 15 types.
  - `EDITOR_SCRIPT` (editor only) is 12, so the next free value is 13.
  - The runtime `assetHandler[]` (`n64/engine/src/assets/assetManager.cpp:84-100`) must stay
    contiguous.
  - The model to copy is the emitter: JSON with the uuid inside, built to a blob, loaded with
    `assetLoad`. The steps are listed in the session's survey (assetManager, builder, CMake list, asset
    browser, inspector, actions, migration).

## C. Conditions

- C1. `P64_CONDITION_DATA` puts a 4-byte header (startsEnabled, flags, autoCheck) in front of the
  script's fields (`n64/engine/include/script/userScript.h:81`, `events/conditionTypes.h:16-24`).
- C2. The build finds `bool evaluate(...)` by text and stores it in `ScriptEntry::evaluate`
  (`script/scriptTable.h:57`).
- C3. From code:
  - `P64::Conditions::evaluate(Object&)` needs the object that carries the condition
    (`n64/engine/include/events/conditions.h:25`).
  - `Conditions::Inputs{obj, data, pin}.all()/any()/odd()/none()` evaluates the conditions wired into
    a `P64_CONDITION_INPUTS` pin (`conditions.h:54-71`).
- C4. Nothing evaluates a condition script with a values blob that lives elsewhere.
  - `evaluateCode` is in an anonymous namespace (`n64/engine/src/events/conditions.cpp:74`).
  - Script indices change on every build (`scriptBuilder.cpp:164`), so a tree must have them
    resolved at build time.

## D. libdragon text

- D1. Inline codes: `$xx` switches font (and resets the style to 0), `^xx` switches style, and `$$`
  and `^^` print the character (`libdragon/include/rdpq_text.h:96-106,291-303`). A malformed code
  asserts (`libdragon/src/rdpq/rdpq_paragraph.c:521,538`).
- D2. A code point the font has no glyph for is skipped: no width, not counted by `max_chars`
  (`rdpq_paragraph.c:182-183`).
- D3. `max_chars`, "useful for typewriting effect", is at `rdpq_text.h:228`.
  - It counts drawn glyphs only (`rdpq_paragraph.c:241`).
  - It finishes the current word's layout (`:190-194`), so words do not jump lines while they
    reveal.
  - `rdpq_text_print` lays out on the stack (`rdpq_text.c:40`).
- D4. Wrapping:
  - modes NONE, ELLIPSES, CHAR and WORD (`rdpq_text.h:195-200`);
  - WORD breaks only at spaces, so CJK needs CHAR;
  - with a height set, a print returns the bytes it consumed, which is how pages are found
    (`rdpq_text.h:310-314`).
- D5. Fonts and styles:
  - a style is a color, an outline color and a `custom` callback (`libdragon/include/rdpq_font.h:121-126`);
  - 256 styles per font (`rdpq_font.c:30`), 255 font ids (`rdpq_text.c:14`);
  - glyphs are axis-aligned rectangles (`rdpq_font.c:493-495`), so there is no synthetic bold or
    italic.
- D6. mkfont options (`libdragon/tools/mkfont/mkfont.cpp:419-454`):
  - `--range`, `--charset`, `--outline`, `--monochrome`, `--var-axis`, `--ellipsis`, `--no-kerning`;
  - .ttf, .otf and BMFont .fnt input;
  - large glyph sets such as CJK switch to atlases loaded per glyph (`mkfont_out.cpp:830-865`).
- D7. An unknown code point inside a sparse range can return the wrong glyph (`rdpq_font.h:200-205`),
  which is one more reason to generate charsets from the tables.

## E. Pyrite64 fonts and UI

- E1. Font build (`src/build/fontBuilder.cpp:43-47`):
  - it runs only `mkfont -c -o -s [--charset]`;
  - import accepts only `.ttf` (`src/project/assetManager.cpp:151-153`);
  - fonts with `fontId` 1-15 are registered at boot and kept loaded (`fontBuilder.cpp:27-30`;
    `n64/engine/src/main.cpp:85-92`).
- E2. Canvas text elements print a bound string variable as the printf format
  (`src/build/canvasBuilder.cpp:520-625`), so a `%` in a translation would break.
- E3. There is no engine text or dialog helper.

## F. Kentucky Castle

- F1. Fonts:
  - KC has no TTF assets;
  - `src/user/global/systems/Fonts.cpp:13-25` registers `FONT_BUILTIN_DEBUG_MONO` as id 1 with
    styles Black (1) and Yellow (2) on every scene load;
  - a project font given id 1 would assert.
- F2. Text drawn today:
  - the HUD is generated from `assets/ui/HUD.canvas`;
  - 2D drawing happens in `onSceneDraw2D` (`src/user/global/GlobalSetup.cpp:213-275`);
  - HUD verbs are English strings in C++.
- F3. Saves:
  - there is no save data and no options menu;
  - EEPROM holds only the debug TweakableVars, at offset 0 (`GlobalSetup.cpp:51-52`), and only with
    the debug menu on;
  - `project.p64proj` has `"saveType": 0`, while `Makefile.custom:43` asks the ED64 for eeprom16k.
- F4. Input:
  - scripts call `joypad_get_buttons_pressed/held/released` on port 1 directly
    (`src/user/scripts/Player.cpp:1846-1849`);
  - there is no input wrapper.
- F5. TALK:
  - `beginInteract` (`Player.cpp:1400-1424`) leads to `Interactions::use()`, which fires Used
    (`src/user/global/systems/Interactions.cpp:603`);
  - Hold Until Finish waits for `finishInput` (`:621-635`) and gives up after Max Hold (30 s);
  - `end()` fires Finished, or Cancelled when interrupted (`:641-648`).
- F6. TalkPoint (`assets/prefabs/interaction/TalkPoint.prefab`):
  - Used goes to `prefab::Talk.Started`;
  - the child `DialogStandIn` (the engine's Delay, 2 s) fires `prefab::Talk.DialogClosed`, which
    reaches Finish.
- F7. The jam25 dialog (`n64/examples/jam25/src/user/systems/dialog.cpp`) has:
  - message chaining by hashed id;
  - a `"question\nA|B"` modal;
  - a `\x01` control byte;
  - A that completes the text, then advances.

  Its reveal writes `'\0'` per byte, which cuts UTF-8 and escapes, and the text is used as a printf
  format.

## G. Design requirements (kentucky-castle-designs)

- G1. Tourist Trap (`Designs/Levels/tourist-trap/README.md:197`) needs text boxes with a speaker name
  and portrait, branching yes/no prompts, and lines that play during gameplay (Tank talking while he
  walks).
- G2. Free Bin Junkyard (`Designs/Levels/free-bin-junkyard/README.md:128-129`) needs Ocarina of
  Time style text boxes and tutorial prompts.
- G3. Number One (`Designs/Levels/number-one/README.md:104-123`) pairs the manager's wife's language
  with the game's: EN↔JA, ES↔PT, ZH↔KO.
  - Her lines show untranslated.
  - Each release needs a font for both scripts.
  - Every language's lines need a native speaker.
  - Draft Japanese lines are in `Story/README.md:69-85`.
- G4. Story flags and save state are listed as a separate new system (Tourist Trap table). They do
  not exist yet.
