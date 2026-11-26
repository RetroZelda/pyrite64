# Dialog and localization: plan

Draft 2 (2026-11-26). The owner answered 16 questions in four rounds, then the six open questions of
draft 1 (section 12). Every other open point carries the answer I recommend, numbered in
[PLANNING-STATE.md](PLANNING-STATE.md) so any of them can be changed. Evidence with file and line references: [FINDINGS.md](FINDINGS.md). Paths
start with `pyrite64/` or `kentucky-castle/` (KC).

Two features that depend on each other:

- **Localization** (engine and editor). Code and objects name text by key (`"hud.verb.talk"_localized`).
  Each language has a table from key to text. The editor finds every key in the project, adds the
  missing ones and removes the unused ones once they are confirmed. Text carries `[tags]` for fonts,
  color and effects, and `{variables}` declared in C++.
- **Dialog** (data in the engine, display in KC). A **dialog tree** asset holds lines, choices,
  conditions and events as nodes, edited like the event graph. KC's dialog system shows it: the box,
  the letter-by-letter reveal, the buttons and the choices.

## 1. What the owner asked for

1. Strings are fetched through a `""_localized` literal that gives the string's id. The id maps to a
   table of id to string, and the language setting picks which table is loaded.
2. A scan of code and objects finds every localized string and refreshes the table: missing strings
   are added, and unused ones can be removed.
3. Dialog branches on conditions and asks the player to choose: usually two options (yes, no), but
   more must work.
4. An optional speaker name.
5. Font, color, bold, italics and so on, changeable in the middle of a string with `[bbcode]`-like
   tags.
6. Variables inside strings in `{braces}`, declared only in the project's C++.
7. Two active languages at the same time, for the waterpark level (Number One), where the manager's
   wife speaks the language paired with the game's.
8. A global dialog speed option for how fast letters appear. Holding a button makes them appear
   faster. If the button is still held when the text finishes, it must be released and pressed again
   to go on.
9. A dialog tree object, like the event graph, whose nodes are texts to print and conditions to
   branch on. Conditions run first, so the opening line can differ.
10. Dialog trees are an engine and editor feature. Showing and handling dialog is KC's. The engine
    holds only the localized text and the tree data.

## 2. What exists today (short)

- **No localization at all**, in either repo. HUD verbs ("TALK", "READ") are English strings in KC's
  C++.
- **The `_event` literal is the template for `_localized`.** It is a `consteval` CRC-32 in a header
  that the editor also compiles (`pyrite64/n64/engine/include/events/types.h`). A regex scan finds
  every `"..."_event` in the project's C++ (`eventRules.cpp scanSource`). It does not skip comments.
- **The editor reads C++ declarations by text**, not with clang: `findMacroCalls` finds `P64_...(...)`
  calls, and the results are stored per script and re-read every 2 s (`assetManager.cpp`,
  `codeParser.cpp`). Variables declared in C++ follow the same path.
- **No graph asset to copy as is.** The event graph has no file of its own (its wires are bindings
  inside scenes and prefabs), and node graphs compile to C++ coroutines. A dialog tree takes the
  node graph's asset and window shell, the emitter's "JSON in, binary blob out" build, and the event
  graph's node drawing (the ImNodeFlow fork).
- **Conditions only run on scene objects.** `Conditions::evaluate(obj)` needs the object that carries
  the condition script. There is no call that runs a condition script with values baked into some
  other asset. Script indices change on every build, so the build must resolve them.
- **libdragon already does most of the text work** (`rdpq_text`, `rdpq_paragraph`):
  - word or character wrapping in a box;
  - pages: a print with a height returns how much text fitted;
  - inline `$xx` (font) and `^xx` (style) codes;
  - a `max_chars` limit made for letter-by-letter reveal, which counts glyphs only and finishes the
    current word's layout so words never jump lines;
  - UTF-8 text, with each font baked only with the characters you list.
- **libdragon cannot draw bold, italic or other sizes from one font.** Each is a separately baked font
  file. Malformed `$`/`^` codes assert at run time, so translated text must be checked at build time.
- **KC has no real font** (only libdragon's debug font), **no save data and no options menu**. The
  editor's font build only passes size and charset to `mkfont` (no outline, `.otf` or bitmap fonts).
- **The TalkPoint prefab is ready for dialog.** Its interaction holds the player until Finish; today
  a 2-second Delay (`DialogStandIn`) stands in for the dialog and fires Finish.
- **The jam25 example** has a small dialog system. Its ideas (message chaining, yes/no modal,
  control bytes, complete-then-advance on A, a corner-texture box) are worth keeping; its code
  (byte-wise reveal that cuts UTF-8, compiled-in English, text used as a printf format) is not.

## 3. Who owns what

The owner's line: "the dialog trees being the localization text and data is all that the engine
needs". So the engine stores, builds, looks up and formats; KC walks the trees and shows them.

| Piece | Engine and editor (pyrite64, fork only) | KC |
|---|---|---|
| Keys, `_localized`, string tables, refresh, unused list | yes | uses them |
| Language and paired language at run time, table loading, fallback | yes | sets the language from its settings |
| `[tags]`: parsing, checking, compiling | yes, from a tag list the project declares | declares which effect and control tags exist, and what they do |
| `{variables}`: the declaration macros, the scan, the formatter (plurals, numbers) | yes | declares the variables and their getters |
| Fonts per language (sets, variants, sizes, generated charsets) | yes | picks the fonts |
| Dialog tree asset, its editor window, its build | yes | |
| Speakers list (name, portraits, blip, style, talk clip, default language) | the list and its editor | gives each field its meaning |
| Reading a tree: nodes, and evaluating a branch's condition | data accessors, and the call that evaluates a baked condition | |
| Walking a tree: what comes next, firing its events | | yes |
| The box, reveal, buttons, choices, portraits, voice, log, settings | | yes |

Walking the tree lives in KC because the owner put "handling" there; the engine gives KC the data
and the one thing KC cannot do alone, evaluating a condition script with values the build baked in
(decision 1).

## 4. Localization

### 4.1 Keys and `_localized`

```cpp
rdpq_text_print(..., Loc::text("hud.verb.talk"_localized));
```

- The quotes hold a **symbolic key** (the owner's answer): lower case, dots between parts, for
  example `npc.tank.greet`. The English lives only in the English table, so rewording English never
  breaks another language.
- `"key"_localized` is `consteval` and returns a `LocId`, the CRC-32 of the key, like `_event`. A
  malformed key fails to compile.
- Dialog tree lines and choice options get keys made by the editor (`dlg.<tree>.<n>`), stored on the
  node. Renaming the tree does not rename its keys.
- Object fields can hold a key: a new script field type `LocText` shows a key picker with the source
  text beside it (a sign's text, an NPC's name).
- Two keys with the same CRC-32 fail the build and name both keys.

### 4.2 Languages and the project file

One ROM carries every language, and the player picks one in game (the owner's answer). The active
language and its **paired language** are loaded into RAM, nothing else.

`<project>/locale/locale.json`:

```json
{
  "source": "en",
  "languages": [
    { "code": "en", "name": "English",   "fonts": "latin",    "wrap": "word", "plural": "en" },
    { "code": "ja", "name": "日本語",     "fonts": "japanese", "wrap": "char", "plural": "ja", "revealScale": 0.6 },
    { "code": "es", "name": "Español",   "fonts": "latin",    "wrap": "word", "plural": "es" },
    { "code": "pt", "name": "Português", "fonts": "latin",    "wrap": "word", "plural": "pt" },
    { "code": "qps", "name": "Pseudo",   "fonts": "latin",    "pseudo": true }
  ],
  "pairs": { "en": "ja", "ja": "en", "es": "pt", "pt": "es", "qps": "ja" },
  "fontSets": { "latin": { ... }, "japanese": { ... }, "icons": { ... } },
  "colors":   { "red": "#ff5050", "place": "#50a0ff", "item": "#ffd040" },
  "effects":  [ "shake", "wave" ],
  "controls": { "wait": "seconds", "speed": "name", "sfx": "sound" },
  "keep":     [ "item.*" ]
}
```

- **The first languages** (the owner's answer): English (the source), Japanese, Spanish, Portuguese
  and a pseudo-language.
- **Pairs** come from the Number One design: English with Japanese, Spanish with Portuguese. Chinese
  and Korean can be added later the same way.
- **`wrap`:** Japanese has no spaces, so it wraps by character, with **kinsoku** rules (the owner's
  answer): no line starts with a closing mark (。、」）！？ー or a small kana), and no line ends with
  an opening mark (「（). rdpq has no such rule, so `Loc::format` breaks the lines itself for `char`
  languages, from the font's glyph widths. A `\n` in any language is always a hard line break (the
  owner's answer), in tables, trees and C++ alike.
- **`revealScale`:** slows the reveal for languages that say more per character.
- **`effects`, `controls` and `keep`** are explained in 5.1 and 4.5.

### 4.3 String tables on disk

One JSON file per language, `<project>/locale/<code>.json`, with keys sorted so diffs stay small:

```json
{
  "hud.verb.talk": { "text": "TALK", "note": "A button prompt, at most 7 letters" },
  "npc.wife.plea": { "text": "...", "note": "Number One: she speaks the paired language" }
}
```

- The **source table** (English) holds each key's text and an optional **note** for translators:
  context, a length limit, who speaks.
- The **other tables** hold the translation and `"from"`: the CRC-32 of the English it was
  translated from.
- A string can be marked `"keep": true`. Keep rules in `locale.json` (`item.*`) cover keys that code
  builds at run time and the scan cannot see.

A new editor window, **Strings**, is a grid:

- **Columns:** the key, the English, one column per language, a status, and where the key is used.
- **Filters:** by status (missing, needs review, unused, kept) and by text.
- **Editing:** cells are edited in place, with a preview of the text with its tags applied.

Dialog tree nodes edit the same tables: typing in a line node writes the English, or the preview
language's text, into its table. There is one copy of every text.

### 4.4 Refresh: finding every key

**Refresh** (a button in the Strings window, and `--cmd loc-refresh` on the CLI) collects every key
from:

| Source | What is found |
|---|---|
| The project's C++ (`src/user/**`, `engine/scripts/`) | `"key"_localized`, with comments stripped first (a literal-aware stripper, since today's `stripComments` ignores strings) |
| Scenes and prefabs | script fields of type `LocText` |
| Dialog trees | every line, choice option and Event-node text |
| The speakers list | each speaker's name key |
| Canvases | text elements marked Localized (the HUD) |
| `locale.json` | the language names |

A key that no table has yet is added to every table: an empty English with status **missing
source**, and empty translations with status **missing**. Refresh also runs before every build, so
the build's checks see the same list.

### 4.5 Unused strings

Refresh lists the keys that nothing uses. **Remove unused** shows that list, across every language,
and deletes only after you confirm (the owner's answer). Kept keys and keys matching a keep rule are
never listed. On the CLI, `--cmd loc-unused` prints the list and `--cmd loc-prune --yes` deletes it.

### 4.6 Missing and outdated translations

- **Missing** (the owner's answer): release builds show the English. Debug builds tint it with a
  marker color, so testers notice.
- **Needs review:** a translation whose `"from"` no longer matches its English. It still shows, and
  the Strings window flags it until someone edits or confirms it.
- The build prints a count per language: missing, needs review.

### 4.7 The pseudo-language

The pseudo-language (`qps`) is never stored. The build makes it from English:

- letters swapped for accented look-alikes;
- about 35% longer, inside brackets: `[Ťàłķ ţö Ťàñķ ······]`;
- tags and `{variables}` kept intact.

Switching to it in the debug overlay shows at a glance:

- text that is not localized: it stays plain English;
- text that overflows its box;
- text cut at the box edge: the closing bracket is missing.

### 4.8 Build output and the run-time API

The build writes `rom:/locale/<code>.loc` per language. The format:

- a header;
- the ids sorted, each with an offset;
- the compiled strings: UTF-8 with tags and variables already parsed (section 5).

Lookup is a binary search.

```cpp
namespace P64::Loc {
  void setLanguage(uint8_t lang);           // loads the language, its pair and their fonts
  enum class Slot : uint8_t { Game, Paired };
  const char* raw(LocId id, Slot s = Slot::Game);           // compiled text, for the formatter
  int format(char* out, int size, LocId id, Slot s = Slot::Game,
             const Args* args = nullptr);                   // rdpq-ready text, variables expanded
}
```

- Both slots' tables stay in RAM while a language is set. Section 10 has the sizes and the fallback
  plan if they are too big.
- A missing translation returns the English (tinted in debug builds), from the slot's own fallback.

## 5. Text markup

### 5.1 Tags

```text
[color=red]Careful![/color] The [b]staff hut[/b] is past the [i]wave pool[/i].[wait=0.5] You have {coins, plural, one {# coin} other {# coins}}.
```

| Tag | Does | Kind |
|---|---|---|
| `[b]` `[i]` | bold, italic (both together work) | font variant |
| `[size=small]` `[size=big]` | one baked size down or up from the current size | font variant |
| `[font=name]` | another font set from `locale.json` | font |
| `[color=red]` / `[color=#ff8000]` | a named palette color, or any color | style |
| `[shake]` `[wave]` | animated letters (the owner's answer) | effect: KC decides what it does |
| `[wait=0.5]` | pauses the reveal | control |
| `[speed=slow]` | reveal speed for the rest of the page (slow, normal, fast, or a number) | control |
| `[sfx=name]` | plays a sound when the reveal reaches it | control |
| `[btn=A]` | an inline button icon (A, B, Z, C-up...) that takes space like a letter | icon |
| `[page]` | starts a new page here | page break |
| `[[` `{{` | a literal `[` or `{` | escape |

- Closing tags are optional at the end of a string.
- Unknown tags, a bad value or a closing tag with no opening one **fail the build**, naming the key
  and the language. A translator's typo never reaches the console.
- The engine knows only the kinds. `effects` and `controls` in `locale.json` name which effect and
  control tags exist and what kind of argument each takes. KC gives them meaning, as anim events do.

### 5.2 Fonts, bold, italic and size

libdragon has no faux bold or italic, so each variant is a baked font. A **font set** in `locale.json`
names, per variant, a TTF or OTF (or a variable font with a weight axis), and lists the sizes to bake:

```json
"latin": { "regular": "fonts/Body-Regular.ttf", "bold": "fonts/Body-Bold.ttf",
           "italic": "fonts/Body-Italic.ttf", "boldItalic": "fonts/Body-BoldItalic.ttf",
           "sizes": [12, 16, 20], "base": 16, "outline": 1 }
```

- A missing variant falls back to regular (most Japanese fonts have no italic).
- **The charset of each baked font is generated:** every character used by the tables of the
  languages that use that set, plus digits and the characters a variable can produce. A Japanese
  font then holds only the few hundred kanji the game uses, not 7,000.
- `[size]` steps through `sizes`. The **text size** option moves the base one size up.
- **Font ids** are given out by the build, from 16 up, per set, variant and size. `Loc::setLanguage`
  loads and registers the fonts of both slots and frees the others. That keeps clear of KC's font
  1 and of the editor's fonts 1 to 15, which are loaded at boot.
- **The editor's font build gains** `.otf` input, `--outline`, `--var-axis` and BMFont `.fnt` input.
  The icon font uses `.fnt`: button pictures as glyphs at private-use code points.

### 5.3 How tags become something rdpq draws

The build compiles each string once, with tags and variables parsed. At display time `Loc::format`
expands variables and writes text that `rdpq_text_print` draws directly:

- **Font and style:** `$xx^yy`. Style ids are `color index + 32 × effect index`, registered on every
  loaded font.
- **Controls** (wait, speed, sfx) and **page breaks:** one Unicode **private-use code point** each,
  with the argument packed into it. rdpq skips a code point the font lacks: no width, and not
  counted by `max_chars`. KC's reveal reads them as it counts glyphs.
- **Icons:** a switch to the icon font, the icon's code point, and a switch back.
- **`$` and `^` in the text itself** are doubled, since rdpq reads them as codes. A translator who types
  `$5` gets "$5", not an assert.
- Fonts are resolved at display time, not build time, because the text size option can change them.

## 6. Variables

Declared only in the project's C++ (the owner's rule). The editor reads the declarations, so it can
check every `{name}` in every language and offer a picker:

```cpp
P64_LOC_VAR(coins, Int)    { return Inventory::coins(); }     // a global value with a getter
P64_LOC_VAR(item,  Key)    { return Inventory::lastItem(); }  // a localized value (another key)
P64_LOC_ARG(price, Int);                                      // a value the caller passes in
```

| In a string | Shows |
|---|---|
| `{coins}` | the number, with the language's digit grouping (1,000 / 1.000) |
| `{coins:03}` | zero padded to 3 digits |
| `{item}` | a `Key` variable's text in the same language as the line (so the wife's lines get the paired language's item name) |
| `{coins, plural, one {# coin} other {# coins}}` | the form the language's plural rule picks; `#` is the number |
| `{price}` | a value passed by whoever starts the dialog: `Loc::Args{{"price"_var, 20}}` |

- **Plurals** use the ICU message format's syntax and the CLDR plural rules, because translators and
  translation tools already know them. The engine carries the rules for the first languages:
  English one/other, Japanese other, Spanish and Portuguese one/many/other.
- **Unknown variables, or a plural with a form the language lacks, fail the build.** Missing forms
  that the language needs are a warning.
- **Types:** `Int`; `Key` (a localized value); `Text` (raw UTF-8, which is not localized and must use
  characters the fonts have).
- **Values are read once, when a page starts,** so a number does not change while it is being read.

## 7. Dialog trees

### 7.1 The asset and its window

- A dialog tree is a `.p64dlg` JSON asset, with its uuid inside, like emitters. It is engine asset
  type 13, the next free slot.
- Double-clicking it opens the **Dialog Tree** window. That window is the ImNodeFlow fork with event
  graph style nodes, plus the node editor's shell: search menu, dirty state, unsaved-close prompt,
  session restore.
- A line node shows its text in the **preview language** (a drop-down in the window) and edits it in
  place. A **Play** button walks the tree in the editor: you pick each condition's answer and each
  choice, and the text shows with tags applied.

### 7.2 Nodes

| Node | What it is | Pins |
|---|---|---|
| **Start** | the entry; one per tree. Wire it straight into a Branch so the opening line can differ | Next |
| **Line** | a page or more of text: speaker (optional), text, language (Speaker's, Game or Paired), portrait (optional, from the speaker's list), voice (Auto or None), Wait for A or Timed | Next |
| **Choice** | 2 to 8 options shown under the previous line. Per option: text, Cancel (B picks it), Default (the cursor starts there), Show if, Enabled if, an event to fire | one Next per option; Show if and Enabled if per option |
| **Branch** | goes one way or the other on a condition | Condition in; True, False out |
| **Condition** | a condition script (Player Health, Interactor...) with its values set on the node, like a condition object | Result |
| **And / Or / Xor / Not** | the event graph's logic nodes, any number of inputs | Inputs; Result |
| **Input** | a named yes/no the scene wires in (7.3) | Result |
| **Event** | fires a named event (`shop::Bought`, `prefab::Gate.Open`) with a value, then goes on | Next |
| **Go To** | jumps to another tree, or to a named Start point in this one | |
| **End** | ends the dialog with an exit value (which ending was reached) | |

- A line runs over as many pages as it needs. Pages are found at display time for the box and
  language in use, because translations differ in length. `[page]` forces a break.
- A choice shows in the same box as the line before it, the way Ocarina of Time asks Yes/No under its
  question. More options than fit in the box scroll.
- A wire into nothing ends the dialog, like an End with value 0.

### 7.3 Conditions in a tree (the owner chose both)

- **Inside the tree.** A Condition node holds a condition script and its values. The build bakes it
  into a small expression program: run script *s* with values blob *v*, then and/or/xor/not. The
  engine gains `Conditions::evaluate(scriptIndex, values, object)`, which KC calls through the tree
  accessor (decision 1). It runs on the **object that started the dialog** (the TalkPoint). So
  conditions that read their own object work, and ones that read the player work anywhere. The
  tree works the same in every scene.
- **Wired from the scene.** An **Input** node names a yes/no, such as `HasTicket`. The scene object
  that plays the tree shows each Input as a condition-input pin, and the event graph wires condition
  objects' Result into it, as with interactions' CanUse. This needs a new editor ability: pins that
  come from the asset a field points to. So it is the last engine phase.
- Choice options' **Show if** and **Enabled if** take the same wires. A hidden option takes no
  space. A greyed one shows dimmed, and the cursor skips it.

### 7.4 Events

- The **Event** node and each choice option can fire a named event. Scopes (`prefab::`, `scene::`)
  resolve from the object that started the dialog, so a TalkPoint prefab's tree reaches its own
  prefab.
- The event checks of the build (unknown and unheard events) include trees, as they do node graphs.
- The scene object that plays a tree fires **Started**, **Closed** (with the End's value) and
  **Cancelled**. Story flags do not exist yet; when they do, events are how trees set them.

### 7.5 Speakers

A project list, `<project>/locale/speakers.json`, edited in a **Speakers** window.

| Field | Meaning |
|---|---|
| Name | a key (`speaker.tank`), so each name is translated once |
| Default language | Game or Paired. The manager's wife is Paired, so all her lines are, unless a line says otherwise (the owner's answer) |
| Portraits | images by name (`normal`, `angry`, ...). A line can pick one, and the first is the default |
| Voice blip | a sound, how often it plays (every n letters) and its pitch spread, like Banjo-Kazooie's or Animal Crossing's voices |
| Style | the name plate color and font set. A box look per speaker comes later (the owner's answer); every box is the plain dark panel for now |
| Talk clip | the animation the speaker's model plays while its text appears |

- A line with no speaker shows no name plate.
- The speaker name is always in the game language, even when the line is in the paired language.
- **RetroStar is a speaker** (the owner's answer: "assume he does speak"). The Number One ending has
  him answer the wife, and the manager put his answer into her language.

### 7.6 Voice-over

- The owner asked for optional voice clips. A line's clip is found by key and language (the owner
  confirmed): `assets/voice/<code>/<key>.wav`, built to a compressed wav64.
- The Strings window shows, per language, which keys have a clip. A line plays the clip of the
  language it is shown in (the wife's Japanese line plays her Japanese clip in the English game).
- A line's Voice field set to None turns it off. While a clip plays, the speaker's blips are silent.

### 7.7 Build and run time

- The build writes `<tree>.dlg`. Its contents:
  - the nodes as a flat array, with next indices;
  - line and option keys as `LocId`s;
  - speaker indices;
  - condition programs (script indices resolved in this build, values blobs);
  - event ids with their scope.
- `speakers.bin` holds the list.
- The engine side is read-only: load the asset; `node(i)`, `start()`, `evaluate(conditionIndex,
  object)`.
- The engine's `n64/tests` gains a tree test: walk the tree, check branches and events.

## 8. KC: showing dialog

### 8.1 The Dialog script and the TalkPoint

`src/user/scripts/dialog/Dialog.cpp`: a field for the tree (an asset reference) and a mode (Talk or
Gameplay).

- **Inputs:** Start and Cancel.
- **Outputs:** Started, Closed and Cancelled.
- **The TalkPoint** prefab swaps its 2-second Delay for a Dialog: `Talk.Started` → Start, Closed →
  `Talk.DialogClosed` → Finish, and the interaction's Cancelled (hit, death, level change) →
  Cancel.
- **The Sign** gets the same treatment for READ.

`src/user/global/systems/Dialog.{h,cpp}` walks the tree and does everything else in this section.
`Dialog::start(tree, object, args)` works from C++ too.

### 8.2 The box, pages and the reveal

- **Talks** use one box at the bottom of the screen (the owner's answer, the Free Bin Junkyard
  design's Ocarina of Time boxes). They have a name plate, a portrait on the left when the line has
  one, and three lines of text at the normal size. The box is a plain dark panel for now (the owner's
  answer); styling it per speaker is later work.
- **The reveal** prints the page with `max_chars = n` each frame, where *n* grows by the speed
  setting. Controls are handled as *n* passes them: waits, speed changes, sounds, page breaks.
- **Speeds:**
  - the setting (8.7) gives the base;
  - `[speed]` changes it for the rest of the page;
  - the language's `revealScale` scales it;
  - holding A multiplies it.
- **Indicators:** a blinking arrow when a page is done and another follows, and a square on the last
  page (as Ocarina of Time does).
- **`[shake]` and `[wave]`:** the paragraph is built, its letters with that effect's style are moved
  each frame, then it is drawn.

### 8.3 Buttons (the owner's rules)

- **A, held while text appears:** letters appear faster (four times by default, a tweakable).
- **A, tapped while text appears:** nothing extra (the owner's answer).
- **When the page finishes with A still held:** nothing happens until A is released and pressed
  again. Then the next page, line or choice comes.
- **The stick or D-pad in a choice:** moves the cursor; disabled options are skipped.
- **A in a choice:** picks.
- **B in a choice:** jumps to the Cancel option and picks it.
- **C-Up during a talk:** opens and closes the dialog log (the owner's answer).
- **R held during a talk,** with Skip read lines on: runs through read lines to the next unread line
  or choice (the owner's answer).
- **The HUD shows what the buttons do** (the owner's answer): A reads **NEXT** once the page is done
  and the dialog is ready to go on (**CLOSE** on the last page, **PICK** in a choice), C-Up reads
  **LOG**, and R reads **SKIP** while there are read lines to skip. The HUD's A label is the one the
  interactions already use for TALK and READ.

### 8.4 Two languages at once

- When a language is set, the Game and Paired slots load their tables and fonts together. Each line
  is formatted from its slot: the speaker's language, unless the line overrides it.
- In the English game the wife's lines print in Japanese with the Japanese font. The manager's
  translation uses the Game slot. His words to her are a Line with language Paired.
- Numbers and `Key` variables in a paired line come out in the paired language.

### 8.5 Lines during gameplay (the owner's answer)

A Dialog in **Gameplay** mode (Tank talking while he walks) works differently from a talk:

- It does not lock the player.
- It shows in a smaller box at the top of the screen.
- Pages advance on a timer: 1 s plus 0.05 s per letter, scaled by the speed setting.
- Its trees may not contain Choices; the build reports one as an error.
- **One plays at a time.** A newer one with a higher priority cuts in, and a lower one waits.
- **A talk pauses it** until the talk closes.

### 8.6 Settings (RAM for now, the owner's answer)

One settings struct, changed on a new **Dialog** page of the debug overlay. It resets at every boot
until the save system exists.

| Setting | Values |
|---|---|
| Language | the five languages (switching closes an open dialog first) |
| Text speed | Slow (20 letters a second), Normal (40), Fast (80), Instant |
| Auto-advance | Off, On. Pages turn by themselves after the gameplay-line timer |
| Skip read lines | Off, On. A line already read appears at once; a hold-to-skip button runs on to the next unread line or choice |
| Text size | Normal, Large (one baked size up; pages get shorter, the box keeps its size) |
| Box opacity | 25, 50, 75, 100% |

The debug page also has a **Pseudo** shortcut and a **Show keys** switch, which prints each key in
place of its text.

### 8.7 Log, and which lines were read

- **The dialog log** keeps the last 40 lines shown (speaker and text, as formatted then) in a 6 KB
  ring buffer. It opens over a talk and scrolls with the stick.
- **Read lines** are one bit per line node per tree, in RAM, and saved later with the save system.

### 8.8 Talk animation, portraits and blips

- **Talk animation:** a small KC script, **Dialog Actor**, on an NPC names its speaker. While that
  speaker's text appears, the system plays the speaker's talk clip on that object's Model (Animated),
  and stops at the end of the page.
- **Portraits:** drawn from the speaker's images.
- **Blips:** play every n letters (not spaces or punctuation) with the pitch spread, silent during a
  voice clip.

## 9. Interaction with what exists

- **Interactions:** TALK and READ are wired through the Dialog script (8.1). No interaction code
  changes.
- **Event graph:**
  - trees fire named events;
  - the Dialog script's pins are ordinary pins;
  - the build's event index learns about trees.
- **Conditions:**
  - one new engine call: evaluate a script by index with a values blob;
  - the Input pins use `P64_CONDITION_INPUTS` as today.
- **HUD:**
  - the A button verbs and every other HUD string move to `_localized` keys;
  - the canvas builder stops passing bound text as a printf format (a `%` in a translation would
    break it).
- **Debug overlay:** a Dialog page (8.6).

## 10. Limits and cost

- **RAM.**
  - **Tables:** a guess of 1,500 strings at about 70 bytes is roughly 110 KB per language, so about
    220 KB for both slots on a 4 MB console. If that is too much, the fallback keeps only the id
    index in RAM and reads each string from ROM when a page starts (a DMA of under 1 KB).
  - **Fonts:** a Latin set is about 10 to 20 KB per variant and size. A Japanese set with a few
    hundred kanji at 16 px is about 100 KB, and libdragon then loads its glyph atlases per glyph,
    which is slower. That cost is checked on the SummerCart64 in phase 5.
- **Font ids:** rdpq allows 255; the language fonts use 16 and up.
- **Styles:** 256 per font, so up to 32 named colors with up to 8 effect modes.
- **Asset types:** the dialog tree takes 13, which leaves 14 and 15.
- **ROM:** voice-over is the big cost, since a minute of compressed speech is hundreds of KB per
  language. No voice is planned in the first phases.
- **Display cost:** printing the page each frame lays it out again. That is cheap for a few hundred
  letters, and is measured in phase 4.

## 11. Phases

Each phase is committed on `feature/dialog` in the repo it touches, with no co-author or AI lines.

| Phase | Repo | What | Checked by |
|---|---|---|---|
| 1 | pyrite64 | `_localized`; `locale.json` and tables; Strings window; Refresh (C++, `LocText` fields, canvases); unused list, confirm, keep; missing and review flags; CLI; `.loc` build; `Loc` run time with two slots and fallback; pseudo-language | `pyrite64_tests`; a test ROM prints keys in every language |
| 2 | pyrite64 | tags: parser, checks, compiler; font sets (variants, sizes, generated charsets; `.otf`, outline, var-axis and `.fnt` in the font build); variables (`P64_LOC_VAR`, `P64_LOC_ARG`, the scan, the registry); `Loc::format` with plurals, numbers, `Key` and caller values | `pyrite64_tests`; the test ROM |
| 3 | pyrite64 | dialog tree asset and window; every node in 7.2 but Input; Speakers window; conditions in trees (the new evaluate call); events; build; read-only accessors; Play in the editor | `pyrite64_tests`; a tree test in `n64/tests` |
| 4 | KC | real fonts (Latin and Japanese); settings and the debug page; Dialog system with box, pages, reveal, A rules, choices (Cancel, hidden, greyed, default); name plate and style; Dialog script in TalkPoint and Sign; HUD strings to keys; `level_dialog_test` in the debug scene | ares playtest |
| 5 | KC | tags at run time (wait, speed, sfx, buttons, shake, wave, size); portraits; blips; voice clips; Dialog Actor talk clip; paired-language lines (the wife, in the test level) | ares; the SummerCart64 for Japanese font speed |
| 6 | KC | gameplay lines (top box, timer, priority, pause during talks); log; auto-advance; skip read lines; text size; box opacity | ares |
| 7 | both | Input nodes wired from the scene (pins from the asset); CSV export and import of the tables for translators; text overflow check in the Strings window using the baked fonts; the guide in `kentucky-castle/docs/Dialog/` with images and video | ares; the guide |

## 12. The owner's answers (2026-11-26)

| Question | Answer |
|---|---|
| What goes in `""_localized` | a symbolic key |
| How the player gets a language | one ROM, picked in game |
| Unused strings | list them, delete on confirm, with Keep |
| Variable features | plural forms, localized values, values from the caller, number formatting (all four) |
| Conditions in a tree | both: inside the tree, and wired from the scene |
| Events from a tree | Event nodes, an event per choice option, and Started and Closed |
| Choice features | B picks Cancel, hidden by a condition, greyed out by a condition, default cursor (all four) |
| What a speaker carries | portrait, voice blips, own text style, talk animation (all four), and: "voice overs may be something we do as well, so an audio clip might be good to be able to optionally link as well" |
| How a line picks the paired language | the speaker's default, with a per-line override |
| Where dialog shows | a bottom box for talks, a smaller box for lines during gameplay |
| A tap while text appears | nothing extra |
| Tags beyond font, color, bold, italic | pauses and speed, button icons, animated text, sound at a point (all four), and "font size to have bigger or smaller text" |
| Modern conveniences | dialog log, auto-advance, skip read lines, readability options (all four) |
| Where settings live for now | RAM, saved later |
| First languages | English, Japanese, the pseudo-language, Spanish and Portuguese |
| A missing translation | the source text, with a marker in debug builds |

### The six open questions of draft 1 (answered the same day)

| Question | Answer |
|---|---|
| C-Up for the log, R held to skip read lines | "yes those look good! having that be displayed in the hud will be important. same with A having "next" when its ready to proceed" (8.3) |
| Fonts | Noto Sans and Noto Sans JP as placeholders: "make a note that we will eventually need official fonts" (section 13) |
| The box | "plain dark with 3 lines should be fine for now. eventually we will want to stylize the box based on who is speaking, but thats for later." |
| Voice clips | by key and language |
| Does RetroStar speak | "he might speak. but as long as teh option is open lets go with it for now and assume he does speak" |
| Japanese line breaking | "yes kinsoku rules should be good. but we should also support \n properly too." (4.2) |

The owner also asked, for when the work is done: "record videos and get screenshots of the various
dialog features and then make a usage guide like the others".

## 13. Notes for later

- **Official fonts are needed eventually** (the owner's note). Noto Sans and Noto Sans JP (SIL Open
  Font License) are placeholders. Replacing them is one font set entry each in `locale.json`.
- **A box look per speaker** is later work. Speakers keep a Style field for it.

## 14. Not in this plan

- Story flags and saves (settings and read lines are only kept in RAM here).
- Cutscenes and camera shots during dialog.
- The pause menu and a real options screen (the debug overlay stands in).
- Right-to-left scripts and complex shaping (Arabic, Hebrew, Thai).
- Typing text (player names).
- Chinese and Korean, and other language pairs (the design proposes French with Italian, German with
  Dutch); they are an entry in `locale.json` and a font set each.
