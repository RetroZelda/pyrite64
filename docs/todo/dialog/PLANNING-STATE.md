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

41. **Where codes live in a text.** Controls ([wait], [speed], [sfx], [page]) are Unicode plane 15
    private-use code points that stay in the formatted text: rdpq has no glyph for them, so they
    take no space and are not counted by max_chars. Compiled tokens (fonts, colors, variables,
    plurals) are plane 16 and never reach rdpq. The engine's `text/locCodes.h` defines both.
42. **Style ids.** `color + 32 x effect`: up to 31 palette colors (0 is "default", the rest sorted
    by name, then custom `#rrggbb` colors in order of first use, then `_fallback`, the debug tint)
    and 7 effects. Every loaded font gets every style.
43. **Order is the JSON order.** locale.json objects are read sorted by name, so font sets, colors
    and controls get their indices alphabetically; effects and icons keep their list order.
44. **Fonts per language.** The build gives each baked font (set x variant x size) an id from 16
    up; `setLanguage` loads the sets of both slots and the icon set, and frees the rest. Each
    font's characters are the ones its languages' compiled texts use, plus printable ASCII (for
    numbers and Text variables). The inputs are stamped in `build/loc/` so unchanged fonts are
    not baked again.
45. **Codes only before text.** The formatter writes a font or style code only when text follows,
    so a closing tag at the end writes nothing.
46. **The pseudo-language's bracket** is written `[[` so the build does not read it as a tag.
47. **Placeholder fonts.** Noto Sans (Latin subset) and Noto Sans CJK JP (kana and JIS level 1
    kanji for the game, levels 1 and 2 for the editor's UI, which merges it so Japanese shows in
    the Strings window). SIL Open Font License.
48. **Digit grouping** per language: en and ja "1,234", es "1234" but "12.345", pt and de "1.234",
    fr "1 234". A language entry can name its plural family ("plural": "french").
49. **`\n` written as two characters** in a table is a line break, so translators working in a
    spreadsheet can type it; `\\` is a backslash.

50. **How a script points at a tree.** A field `P64::AssetRef<P64::Dialog::dialog_t>` (a tree
    picker in the inspector, built as the asset index); the game reads it with
    `TreeView::init(ref.get(), Dialog::SIZE_UNKNOWN)`.
51. **Conditions in a tree** show the script's own values only (the condition header is of no use
    there); an ObjectRef value in a tree's condition is built as 0.
52. **Wires.** A flow output leads to one place (drawing a second wire replaces the first); several
    wires into a yes/no pin are ANDed.
53. **Tree text keys** are `dlg.<file name>.<node id>` and `.<option number>` for options, kept on
    the node, so renaming the file keeps the keys.
54. **Saving.** A tree window saves on Save, on the project's save, and when it closes; its texts
    are saved to locale/ as they are typed.
55. **Play** in the editor walks the tree and asks you to answer each Branch (it does not run the
    condition scripts) and pick each option.
56. **`--window dlg:<path>`** opens a tree when the editor starts (for screenshots and videos).
57. **The build syncs the tables.** Every translation table gets every key the source table has,
    each build, even when no key is new.

58. **KC's dialog system** is `src/user/global/systems/Dialog.*` (namespace `User::DialogSystem`,
    so it never hides the engine's `P64::Dialog`); the script is `scripts/dialog/Dialog.cpp`
    (`CD1A70E5A11E0001`). B picks Cancel only in a choice; R and C-Up are left for phase 6.
59. **Input during a talk.** The Player zeroes its buttons and stick while
    `DialogSystem::capturesInput()`: a talk is open, or one closed and A, B, Z, R or a C button is
    still held. Interactions' Max Hold (30 s) does not end a TALK while a dialog is open.
60. **The HUD's words** come from `HudText::get(key)`: formatted once per language at the small
    size, so the pointer stays valid for the HUD. Verbs are `hud.verb.*`; SLAP, DASH, THROW are
    `hud.slap`, `hud.dash`, `hud.throw`; dialog labels `dialog.hud.next|close|pick|cancel`.
61. **Box geometry is whole pixels.** Text on a half pixel samples the font atlas's neighbouring
    glyphs (h drew as b). Pages are three lines with half a line of slack, because rdpq drops a
    line whose descent touches the height exactly.
62. **Kinsoku on rdpq's measure.** `Loc::breakLines` lays out one line at a time with rdpq itself
    (a box 1.5 lines high), then moves the break back for kinsoku and inserts a line break.
63. **An empty char[] field stays empty.** The Code component built an empty text arg as "0" (a
    bug that also hit Log scripts); fixed in the editor.
64. **The test level** `level_dialog_test` (debug scene 10, "Warp: Load Level") is generated by a
    one-off script into JSON: five NPCs (Greeter, Shopkeeper, Guard, Teacher, Visitor) as inline
    copies of TalkPoint's objects with their own event names and tree, a Notice sign, and a spawn
    point in front of each. It is not the scene's default level.

65. **Button icons** are a BMFont font set ("icons": `locale/fonts/buttons.fnt` + `.png`, drawn by a
    script), glyphs at U+E000 in the order of `icons.names`; the build names the first glyph as
    mkfont's ellipsis.
66. **`[sfx=name]`** plays `audio/dialog/<name>.wav64`, found at run time with the new
    `AssetManager::findByPath`.
67. **Voice clips** play once per line (all its pages), in the language the line is shown in;
    blips are silent while one plays. The test clips are espeak placeholders.
68. **Talk clips.** A Dialog Actor (`CD1A70E5A11E0002`, speaker id + idle clip) on the NPC's object;
    while that speaker's page appears its model plays the speaker's talk clip by name, then the
    idle clip. No KC model has a real talk clip: the test minion uses `003_HitReaction`.
69. **Portraits** are 48x48 left of the text (the page is narrowed for them); talks only.

70. **The log** keeps a talk's lines as formatted when they started (cut at 160 bytes without
    splitting a code), in a 40-entry ring; it pauses the talk while open. B or C-Up closes it.
71. **Read lines** are kept by key (a 512-slot hash set in RAM). A line counts as read when its last
    page was shown; SKIP is offered only for lines read in an earlier talk.
72. **The C-Up and R hints** are drawn in the talk box's bottom-right corner with the button icons
    (the HUD canvas has no bound C-Up text); R's HUD label also says SKIP.

## After the hardware playtest (2026-11-26)

The owner, word for word:

> a number of changes that should happen.  we should try to avoid splitting a sentence between
> different dialog windows.  so if a sentence wont fit on a screen, we should just push it to the
> next one.  however if the sentence is too long, then we can split it, but we should try to split
> it evenly or by commas or newlines

Decisions made without the owner:

73. **One page breaker for the game and the editor.** `Loc::splitPages` (`text/locPages.h`, host
    code) takes a fit callback: KC measures with `rdpq_paragraph_build`, the Strings window's estimate
    with the font file. The estimate and the game cannot disagree on the rule, only on measurement.
74. **"Doesn't fit" is literal.** A sentence that does not fit on what is left of a page starts the
    next page, even when it is longer than a page and could have started on this one; then it is
    split evenly from the top of its own page.
75. **Evenly** is by letters: the sentence's letters divided by the pages it takes when each is
    filled. A comma or line break is used when it is within 4/10 of a share from the even point, fits
    on the page, and leaves no more than the remaining pages can hold; the nearest one wins, commas
    and line breaks alike. Otherwise the space (in Japanese, the letter boundary kinsoku allows)
    nearest the even point.
76. **Sentence ends** are `.` `!` `?` with a space or the end after them (`3.5` and `1,000` are not),
    and `。！？` on their own; closing quotes, brackets and codes stay with the sentence. An ellipsis
    (`…` or `...`) is a comma, not an end. A mark before a lowercase word (`"Go!" he said`, `e.g. this`)
    is a comma. Abbreviations before a capital (`Mr. Smith`) still end a sentence.
77. **Kinsoku per page.** `Loc::breakLines` takes the font and style in effect at its start; KC
    paginates one page at a time and re-breaks the rest of a Japanese line from the top of each page,
    so a page that starts inside a line of the whole text starts a new line.

Then, about text sizes (the owner's words, and their answers):

> i also want to try getting a smaller font scale as well. we jsut have normal and large but i want
> a small too if possible.  also font size shouldnt affect the page counts of the dialog - so small
> and large should always have the same text displayed

Asked how sizes should fit the same pages, the owner picked "Pages worked out at Large" (the box
keeps its lines; smaller sizes leave room). Then:

> lets make the normal size font the size of the large font, and make normal and smaller be smaler.
> and the current large cna be a giant

Asked for the scale: "small 9; normal 11; large 14; giant 18". Then:

> page sizes should still be on large, and then giant can resize the box

78. **Four text sizes**: Small 9, Normal 11, Large 14, Giant 18 px, steps -1 to +2 from the base
    (KC's font sets bake 9, 11, 14 and 18 with 11 as the base).
79. **Pages are worked out at Large** on a copy of the line formatted at that size. The line at the
    player's size is cut at the same places by counting the bytes that are not font or style codes
    ($xx ^xx), the only thing that differs between sizes. Kinsoku breaks are made per page at each
    size.
80. **Giant grows the box**: the text area is as many lines as the longest page of the line takes at
    the shown size (never fewer than the box's lines), so the box keeps one height for a line's pages.
81. **Labels follow the text size itself**: the name plate, the LOG/SKIP hints, the log's title and
    names, the HUD words and the gameplay box used one size below the text; they now use the text
    size, so at Normal they stay 11 px as before and only the talk text changed (14 to 11 px).
82. **The estimate's size** comes from locale.json "box": {"pageSizeStep": 1}; KC's
    `Settings::PAGE_SIZE_STEPS` must match it (both say so).

Then:

> lets move the font sizes down one more time.  so small should be 7; normal 9; large 12; giant 14

83. **Sizes 7, 9, 12 and 14 px** (base 9), replacing 78's. Pages are still worked out at Large (now
    12 px) and Giant still grows the box. The labels follow the text size (81), so at Normal they are
    9 px now too; there is no 11 px font left to keep them at.

Then, after trying 7, 9, 12 and 14 on the console:

> lets use size: 8 small; 10 normal; 12 large; 14 giant and then base the text on normal and resize
> for large and giant

84. **Sizes 8, 10, 12 and 14 px** (base 10), replacing 83's. **Pages are worked out at Normal**
    (`PAGE_SIZE_STEPS` 0, locale.json `box.pageSizeStep` 0), replacing 79's Large: Small leaves room
    in the box, Large and Giant grow it (80 already grows it for any size above the page size).

## Phase log

See PLAN.md section 11 for the seven phases.

| Phase | Commit | What | Checked |
|---|---|---|---|
| 1 | pyrite64 (this commit) | `_localized` (engine `text/locId.h`); table format and reader (`text/locTable.h`, `locCore.cpp`); run time `text/loc.h` (two slots, fallback flag, debug marks), language list generated as `src/p64/locTable.cpp`, set to the source language at boot; editor model `src/project/locale/` (config, tables, status, refresh, unused, keep rules, pseudo-language, literal-aware C++ scan); scan of scenes and prefabs; build step `locBuilder.cpp` (refresh, id collisions, `filesystem/p64/loc/<code>.loc`); `LOC_TEXT` field type with a key picker; CLI `loc-refresh`, `loc-unused`, `loc-prune [--yes]`; Strings window (View > Strings) | `pyrite64_tests` 143/143 (11 new); `test_events` in ares 17/17, test 16 "Localized text" (en/ja/es/pseudo, pairs, fallback, `$` doubled, refused index); CLI on a scratch copy (unused listed, prune needs `--yes`, keep rule honoured); editor opened on the Strings window |
| 2 | pyrite64 2ac343b | the tag compiler (`src/project/locale/locCompile.*`: b, i, size, font, color, effects, controls, btn, page, escapes, `\n`; variables `{n}`, `{n:n}`, `{n:0N}`, plurals with `=N` and CLDR categories; errors with key and language); `P64_LOC_VAR` / `P64_LOC_ARG` and their scan; the formatter (`text/locFormat.*`: font and style codes, numbers, plurals, Key and Text variables, caller Args, fallback tint, safe truncation); run time: fonts per language with styles, `format()`, `baseFont()`, text size, `scanNext()` (letters as rdpq counts them, controls), `breakLines()` with kinsoku, control and effect lookups; build: compile every text, bake font sets with generated charsets, generated font and style tables; Strings window: errors under each text while typing; editor UI font with Japanese | `pyrite64_tests` 150/150 (7 new); `test_events` in ares 17/17, test 16 now also checks a variable and bold as font codes, the fonts registered, plurals, the scan of letters and a wait control, the shake effect, and a kinsoku break |
| 3 | pyrite64 6eb0c96 | dialog tree asset (`.p64dlg`, type 13, `.dlg` built); engine `dialog/dialogData.h` (format, reader, expression bytecode) and `dialog/dialog.h` (speakers, `evaluate` on an object); `Conditions::evaluateScript`; editor model `src/project/dialog/` (nodes, pins, JSON, keys, build with errors and unreachable warnings; speakers list); build step `dialogBuilder.cpp` (trees, condition values like a Code component, `src/p64/dialogTable.cpp`); the Dialog Tree window (every node of PLAN 7.2 with Input, texts edited in the preview language, Play, Fit); the Speakers window; trees in the asset browser (Scripts tab, New: Dialog Tree); `AssetRef<dialog_t>` fields; trees and speakers in the strings scan | `pyrite64_tests` 154/154 (4 new); `test_events` in ares 18/18, test 17 "Dialog tree data" (a real condition script answers the branch on an object, both ways; a NOT node hides an option; speaker, events, end values); the tree window and Speakers window opened in the editor |
| 4 | KC f637aa3, pyrite64 49687d3 | KC: locale/ (5 languages, pairs, Noto placeholder fonts with a note that official fonts are needed), speakers, the Dialog system (box, name plate, pages, reveal with the speed setting and hold-A, release-and-press, choices with stick/A/B, hidden and greyed options, branches on conditions, events in the owner's scope, paired-language lines, kinsoku), the Dialog script, TalkPoint and Sign use it, input capture in the Player, localized HUD words and NEXT/CLOSE/PICK/CANCEL, the Dialog debug page (language, text speed, auto-advance, skip read lines, text size, box opacity), {talks} and {health} variables, six test trees, level_dialog_test. pyrite64: `format` size offset, `fontFor`, rdpq-measured kinsoku, empty text fields | ares playtest with real input on a scratch copy: Guard (branch on Player Health, {health}), Shopkeeper (three options, B to Cancel, no slap after it, stick to the third option, loop back, Yes), Greeter (pages, hold A, plural "1 time"), Teacher (bold, italic, colors, sizes), Visitor (Japanese lines in the English game, the guide in English, kinsoku), Notice (READ), the game switched to Japanese on the Dialog page (HUD はなす, 衛兵); `test_events` 18/18 |
| 5 | KC 9d2a789, pyrite64 aee2b66 | [shake]/[wave] (paragraph laid out, letters moved, drawn), [btn=...] icons (button font), [sfx=...], portraits, voice blips, voice clips by key and language (engine `Loc::voiceClip`, build table), Dialog Actor talk clips, `AssetManager::findByPath`; test data (portraits, sounds, espeak clips, Teacher's third line, a talking minion) | ares: shake and wave letters, the A icon inline, Tank's normal then happy portrait, the minion's talk clip and back to idle; a logged run showed the voice clip, 24 blips and the chime started |
| 6 | KC 123f857 | gameplay lines (top box, timer, no input capture, paused by talks, priority queue), the log (C-Up), Skip read lines (instant read lines, R held runs through), LOG/SKIP hints, text size and box opacity checked; a trigger zone and Tank's walking lines in the test level | ares: Tank's lines in the top box while the player walks; the log with three entries and scrolling; large text at 75% opacity; a second talk shows the read line at once and R ends it |
| 7 | pyrite64 ccb75b4, KC 11b8830, 9948644, 2e2b7bc | Input nodes answered by conditions the scene wires into the Dialog script (Input1-4); the Strings window exports every key, note and translation to `locale/strings.csv` and imports it back (CLI `loc-export`, `loc-import`); the lines and pages a dialog line takes in each language, measured with its font, a translation longer than its source flagged. KC fix: a line shown at once (Instant speed, a read line) is whole on its first frame. Usage guide `kentucky-castle/docs/Dialog/README.md` with 9 screenshots and 11 clips; the Interactions, Event Graph and LevelSystem docs updated | `pyrite64_tests` 155/155 (CSV round trip); `test_events` 18/18; ares: a tree Input wired from the scene picks the branch both ways; the fix built on a scratch copy, not yet seen in ares |
| 8 | pyrite64 (this commit), KC 04a084c | Page breaks keep whole sentences and split a long one evenly or at a comma or line break (`Loc::splitPages`, decisions 73-77); `breakLines` with a start font and style; KC paginates page by page; the Strings window estimate breaks pages the same way; Tank's page-break line in the test level, English and Japanese; the guide describes it with a screenshot | `pyrite64_tests` 159/159 (4 new: page breaks); ares: Tank's line in English (the second sentence moved whole, the long one in three even pages) and Japanese (pages start on a new line, the long sentence split evenly); Strings window: 3 pages English and Japanese; not on the SummerCart |
| 9 | pyrite64 (this commit), KC 04a084c | Text sizes Small 9, Normal 11, Large 14, Giant 18 px (decisions 78-82): pages worked out at Large and mapped onto the shown size; Giant grows the box; labels at the text size; the estimate at `box.pageSizeStep`; the guide with a picture of the four sizes | ares: the page-break line at all four sizes in English shows the same pages, Small and Giant in Japanese too, the Giant box 4 lines; `pyrite64_tests` 159/159; not on the SummerCart |
| 10 | pyrite64 (this commit), KC 04a084c | Text sizes 7, 9, 12 and 14 px (decision 83) | ares: the page-break line at all four sizes in English shows the same pages, Small and Giant in Japanese too, the Giant box 4 lines; a debug build of the branch made for the SummerCart |
| 11 | pyrite64 (this commit), KC 04a084c | Text sizes 8, 10, 12 and 14 px, pages worked out at Normal (decision 84) | ares: the page-break line at all four sizes in English shows the same pages, 8 and 14 px in Japanese too, the Large and Giant boxes 4 lines; on the SummerCart: the owner played 7/9/12/14, then this build booted and ran its levels |

