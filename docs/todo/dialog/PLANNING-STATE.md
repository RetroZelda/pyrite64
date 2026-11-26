# Dialog and localization: planning state

Read this file first when resuming. The design is [PLAN.md](PLAN.md); the evidence is
[FINDINGS.md](FINDINGS.md). It builds on the event graph (`../event-graph/`), conditions
(`../conditionals/`) and interactions (`../interactions/`).

- **Status:** PLAN.md draft 2 (2026-11-26): the owner answered the six open questions of draft 1 and
  asked for the build, then videos, screenshots and a usage guide "like the others". Building phase
  by phase; see the phase log at the end.
- **Branches:** none yet. Building happens on `feature/dialog` in `pyrite64` and `kentucky-castle`,
  each phase committed with no co-author or AI attribution lines. Nothing is pushed until the owner
  asks.
- **Status board:** request `6ajshf` (http://mini-debian:8765/r/6ajshf). The owner asked for one
  request per thing they ask; follow-ups on this plan are added as tasks to `6ajshf`, never as new
  requests.

## The owner's request, word for word (2026-11-26)

> i want to create a dialog system that works with localized text. for localization, we can get
> strings using a ""_localized variable to get the id of a string, and then using that id we can map
> it to a table of id->string, and depending on the set language will be what table is loaded. we
> will want a way to scan code and objects to find all the localized strings to refresh the table by
> adding new strings that are missing. and we will want to be able to remove anything unused as
> well.
>
> the dialog system itsself should support branching dialog based on conditionals, and it should be
> able to ask for input by the player. player input will be simple things like "yes", "No", "maybe"
> etc so there will normally be 2 options, but we should be able to support more.
>
> we should also be able to include a speaker name as well - but this should be optional.
>
> we also should be able to change font, font color, bold, italics, etc. we alsoshould be able to
> change these things in hte middle of the string. we can do something like bbcode in brackets to do
> these kinds of things [].
>
> we also need to be able to support variables inside of strings. variables can be inside curly
> braces {}. variables can only be declared in the project in c++
>
> when dialog is displayed, we should also make sure we can support 2 active languages at the same
> time for the water element waterpark level idea.
>
> we should have a dialog speed global option that will adjust how quickly each letter will appear
> in the dialog window. holding a button should also always make it appear faster. if the button is
> held down when the string that should be displayed in the dialog box isnt done, we should expect it
> to be released and then pressed again to go to the next string
>
> we should make a dialog tree object that is functionally similar to the event graph, but we should
> make nodes be string texts that will print, and conditionals to branch from. we will want to make
> sure we run conditional checks first so the entry point text can be different. these dialog trees
> can be an engine/editor feature, but the actual dialog display and handling should be KC. so the
> dialog trees being the localization text and data is all that the engine needs.
>
> lets make a plan to accomplish all of this, but make sure to ask me questions that you see.
> especially for things that are common for games from this era and games that are recent.

## The owner's answers (2026-11-26)

Four rounds of questions. The options the owner picked, and their own words where they added any:

1. **Round 1, localization.** Symbolic keys. One ROM, language picked in game. Unused strings:
   list them, delete on confirm, with Keep. Variables: plural forms, localized values, values from
   the caller, number formatting.
2. **Round 2, trees.** Conditions both inside the tree and wired from the scene. Event nodes plus an
   event per choice option. Choices: B picks Cancel, hidden by a condition, greyed out by a
   condition, default cursor. Speakers: portrait, voice blips, own text style, talk animation, and:
   "voice overs may be something we do as well, so an audio clip might be good to be able to
   optionally link as well".
3. **Round 3, display.** Paired language: the speaker's default with a per-line override. A bottom
   box for talks and a smaller box for lines during gameplay. A tap while text appears: nothing
   extra. Tags: pauses and speed, button icons, animated text, sound at a point, and "font size to
   have bigger or smaller text".
4. **Round 4, the rest.** Modern conveniences: dialog log, skip read lines, readability options,
   auto-advance. Settings in RAM now, saved later. First languages: English, Japanese, the
   pseudo-language, Spanish and Portuguese. A missing translation shows the source text, with a
   marker in debug builds.

5. **Round 5, the open questions of draft 1:**

   > 1 - yes those look good! having that be displayed in the hud will be important. same with A
   > having "next" when its ready to proceed
   >
   > 2 - those fonts as temp are fine. make a note that we will eventually need official fonts
   >
   > 3 - plain dark with 3 lines should be fine for now. eventually we will want to stylize the box
   > based on who is speaking, but thats for later.
   >
   > 4 - key and language will be good.
   >
   > 5 - he might speak. but as long as teh option is open lets go with it for now and assume he does
   > speak
   >
   > 6 - yes kinsoku rules should be good. but we should also support \n properly too.
   >
   > also, when youre dont can you record videos and get screenshots of the various dialog features
   > and then make a usage guide like the others?

## Decisions made without the owner

Each is the option I recommend; change any on request.

1. **Who walks the tree.** KC walks the tree, because the owner put "handling" in KC and "data" in
   the engine. The engine provides read-only accessors and the one thing KC cannot do alone:
   evaluating a condition script with build-baked values. The alternative is a small engine
   "runner" that walks the tree and fires its events. It is easy to switch to if KC's walker turns
   out generic.
2. **Key format.** Keys are lower case with dots (`npc.tank.greet`). Tree lines get `dlg.<tree>.<n>`,
   stored on the node, so renaming a tree keeps its keys.
3. **Collisions.** Two keys with the same CRC-32 fail the build and name both keys.
4. **Tables on disk.** One JSON per language in `<project>/locale/`, with keys sorted. Every text
   lives only in its table: tree nodes store keys and edit the tables in place.
5. **Translator notes.** The source table can carry a note per key: context, a length limit, the
   speaker.
6. **Needs review.** Each translation stores the CRC-32 of the English it came from, so a reworded
   English flags every translation for review.
7. **Keep.** A Keep flag per key, plus keep rules (`item.*`) in `locale.json` for keys built at run
   time.
8. **What Refresh reads.**
   - **Sources:** C++ in `src/user` and `engine/scripts`, `LocText` fields in scenes and prefabs,
     dialog trees, the speakers list, canvases and the language names.
   - **Comments:** a literal-aware comment stripper runs first.
   - **Before builds:** Refresh runs before every build.
9. **`LocText` field type.** A new script field type `LocText` holds a key and shows a key picker.
10. **The pseudo-language.** It is generated by the build, never stored, and paired with Japanese.
11. **Tag kinds.** The engine knows tag kinds; `locale.json` declares which effect tags (`shake`,
    `wave`) and control tags (`wait`, `speed`, `sfx`) exist; KC gives them meaning. Unknown tags or
    bad values fail the build, naming the key and the language.
12. **Bold, italic and size.** These are baked font variants inside a font set. A missing variant
    falls back to regular. `[size]` steps through the set's sizes; the text size option moves the
    base one size up.
13. **Charsets.** Each baked font's charset is generated from the tables that use its set, plus
    digits and variable output.
14. **Font ids.** Language fonts get ids from 16 up, given out by the build, loaded and freed by
    `Loc::setLanguage` for both slots, and never at boot.
15. **How controls are stored.** Controls and page breaks are single private-use code points, which
    rdpq skips without width or count. `$` and `^` in text are doubled.
16. **Variables.**
    - **Declarations:** `P64_LOC_VAR(name, Type) { getter }` for globals and `P64_LOC_ARG(name,
      Type);` for values the caller passes.
    - **Types:** Int, Key (localized) and Text (raw).
    - **Reading:** values are read once, when a page starts.
17. **Plurals.** Plurals use an ICU message-format subset with CLDR rules: English one/other,
    Japanese other, Spanish and Portuguese one/many/other.
18. **The tree asset.** `.p64dlg` JSON with the uuid inside, engine asset type 13, built like an
    emitter into a `.dlg` blob.
19. **Nodes.**
    - **The set:** Start, Line, Choice, Branch, Condition, And/Or/Xor/Not, Input, Event, Go To and
      End.
    - **Loose wires:** a wire into nothing ends the dialog with value 0.
    - **Pages:** a Line spans as many pages as it needs, found at display time; `[page]` forces a
      break.
20. **Choices.** A choice shows in the same box as the line before it, with 2 to 8 options; more than
    fit in the box scroll. A hidden option takes no space; the cursor skips a greyed one.
21. **The condition's object.** Conditions in a tree run on the object that started the dialog.
22. **Input nodes last.** Input nodes, wired from the scene, come in the last engine phase because
    they need pins that come from an asset.
23. **The Dialog script.**
    - **Pins:** inputs Start and Cancel; outputs Started, Closed (the End's value) and Cancelled.
    - **Mode:** Talk or Gameplay.
    - **Prefabs:** TalkPoint and Sign switch to it.
24. **Speakers.** The fields are:
    - a name key;
    - a default language (Game or Paired);
    - named portraits (the first is the default);
    - a voice blip (sound, every n letters, pitch spread);
    - a style (name color, font set, box look);
    - a talk clip.

    A speaker's name always shows in the game language.
25. **Voice clips.** Clips are found by key and language (`assets/voice/<code>/<key>.wav`). A line can
    set None. Blips are silent while a clip plays.
26. **Holding A.** Holding A speeds the reveal four times (a tweakable). B is used only in choices,
    where it picks Cancel.
27. **Speeds.** Slow 20, Normal 40 and Fast 80 letters a second, or Instant. Each language has a
    `revealScale` (Japanese 0.6).
28. **The talk box.**
    - **Layout:** at the bottom of the screen, three lines at the normal size, with a name plate and
      a portrait on the left.
    - **Indicators:** a blinking arrow between pages and a square at the end.
29. **Gameplay lines.**
    - **Box:** a small box at the top of the screen.
    - **Timing:** 1 s plus 0.05 s per letter.
    - **Choices:** not allowed (a build error).
    - **Overlap:** one plays at a time, a higher priority cuts in, and a talk pauses it.
30. **Settings.**
    - **Where:** in RAM, on a new Dialog page of the debug overlay, which also has Pseudo and Show
      keys.
    - **Language change:** switching language closes an open dialog first.
31. **The log.** The last 40 lines, kept in a 6 KB ring buffer.
32. **Read lines.** One bit per line node per tree, in RAM only.
33. **Talk animation.** A KC **Dialog Actor** script ties a speaker to an NPC object, so the talk clip
    plays on that NPC's Model (Animated).
34. **Table memory.** Both slots' whole tables stay in RAM. The fallback is an index in RAM with
    strings read from ROM per page.
35. **Fork only.** The work is fork-only (not upstreamable), like the event graph.
36. **Pairs.** English with Japanese, Spanish with Portuguese, as the Number One design sets.

37. **Prefab instance overrides.** Override keys are salted per prefab layer, so Refresh reads a
    string override (`9:` values) as a key when it is a valid key with a dot and a letter. The
    build also records every LocText field it writes, exactly, and adds any key the tables lack.
38. **Fallbacks are filled in at build time.** Each language's `.loc` is complete: an untranslated
    key holds the source text with an entry flag, so the console never needs the source table
    loaded beside the current one.
39. **Debug marks are a run-time switch.** `Loc::setDebugMarks(fallbacks, missing)`, set by the game in
    debug builds, because the engine library is not built with the game's debug flags.
40. **`P64::LocText` is `P64::LocId`.** The field type is the id itself, so a script reads
    `Loc::text(data->line)`. The parser accepts LocText, LocId and the `P64::` forms; the default is
    written `= "key"_localized`.

## Phase log

See PLAN.md section 11 for the seven phases.

| Phase | Commit | What | Checked |
|---|---|---|---|
| 1 | pyrite64 (this commit) | `_localized` (engine `text/locId.h`); table format and reader (`text/locTable.h`, `locCore.cpp`); run time `text/loc.h` (two slots, fallback flag, debug marks), language list generated as `src/p64/locTable.cpp`, set to the source language at boot; editor model `src/project/locale/` (config, tables, status, refresh, unused, keep rules, pseudo-language, literal-aware C++ scan); scan of scenes and prefabs; build step `locBuilder.cpp` (refresh, id collisions, `filesystem/p64/loc/<code>.loc`); `LOC_TEXT` field type with a key picker; CLI `loc-refresh`, `loc-unused`, `loc-prune [--yes]`; Strings window (View > Strings) | `pyrite64_tests` 143/143 (11 new); `test_events` in ares 17/17, test 16 "Localized text" (en/ja/es/pseudo, pairs, fallback, `$` doubled, refused index); CLI on a scratch copy (unused listed, prune needs `--yes`, keep rule honoured); editor opened on the Strings window |

Known after phase 1: the editor's UI font has no Japanese glyphs (shows ?????); phase 2 merges a
Japanese font into it with the game fonts.
