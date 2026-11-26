/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4). The language the game is played in,
// its paired language, and the text of a key in either of them.
//
//   const char* talk = P64::Loc::text("hud.verb.talk"_localized);
//
// The build writes one table per language (rom:/p64/loc/<code>.loc) and the list of languages
// (src/p64/locTable.cpp). At boot the first language, the project's source language, is set.
#pragma once
#include <cstdint>
#include "text/locId.h"
#include "text/locFormat.h"

namespace P64::Loc
{
  /** Which loaded language a text comes from. */
  enum class Slot : uint8_t
  {
    Game   = 0, // the language the game is played in
    Paired = 1, // the language paired with it (the manager's wife in Number One)
  };

  constexpr uint8_t NO_LANGUAGE = 0xFF;

  constexpr uint8_t LANG_PSEUDO   = 1 << 0; // generated test text
  constexpr uint8_t LANG_WRAP_CHAR = 1 << 1; // no spaces between words: wrap at any character (kinsoku rules apply)

  /** One language of the project, in the order of locale/locale.json (the source language first). */
  struct Language
  {
    const char* code;   // "en"
    const char* name;   // its own name, "日本語"
    const char* file;   // "rom:/p64/loc/en.loc"
    uint8_t pair;       // index of the paired language, NO_LANGUAGE for none
    uint8_t flags;      // LANG_*
    uint8_t plural;     // a PluralRule
    uint8_t fontSet;    // index in the project's font sets, 0xFF: none
    float revealScale;  // how fast letters appear in this language, 1 = the setting's speed
    char groupSep;      // digit grouping: 1,000 or 1.000
    uint8_t groupMin;   // numbers with at least this many digits are grouped
  };

  /** One baked font: loaded while a language of its set is in a slot. */
  struct FontFile
  {
    const char* path; // "rom:/p64/loc/f/latin_0_1.font64"
    uint8_t id;       // rdpq font id (16 and up)
    uint8_t set;
    uint8_t variant;  // 0 regular, 1 bold, 2 italic, 3 bold italic
    uint8_t colored;  // a color font (button icons): styles do not apply
  };

  /** A control tag ([wait=0.5]) as the project declares it. */
  struct ControlInfo
  {
    const char* name;
    uint8_t kind;              // CONTROL_NONE, ...
    const char* const* names;  // kind CONTROL_NAME: the values, by arg
    uint16_t nameCount;
  };
  constexpr uint8_t CONTROL_NONE = 0, CONTROL_SECONDS = 1, CONTROL_NUMBER = 2, CONTROL_NAME = 3;

  /** What locale.json says about fonts, colors and tags (generated, src/p64/locTable.cpp). */
  struct ProjectData
  {
    const FontSet* sets;
    uint8_t setCount;
    const FontFile* fonts;
    uint16_t fontCount;
    const uint32_t* palette;   // RGBA; [0] is the default text color
    uint8_t paletteCount;
    const char* const* effects; // [0] is "" (none)
    uint8_t effectCount;
    const ControlInfo* controls; // op = index + 1
    uint8_t controlCount;
    const char* const* icons;
    uint8_t iconCount;
    uint8_t iconSet;           // 0xFF: none
    uint32_t iconBase;
    uint8_t fallbackColor;     // palette index for untranslated text in debug builds
  };

  namespace Generated
  {
    /** Defined by the generated src/p64/locTable.cpp, called once at boot. */
    void registerLanguages();
  }

  /** Called by the generated code. */
  void setLanguageList(const Language* list, uint8_t count);
  void setProjectData(const ProjectData* data);
  const ProjectData* projectData();

  uint8_t languageCount();
  const Language* language(uint8_t index);
  /** The index of a language code ("ja"), or NO_LANGUAGE. */
  uint8_t findLanguage(const char* code);

  /**
   * Loads a language's table and its paired language's table, freeing the ones before.
   * Returns false (and keeps the current languages) when the index is out of range or a table
   * cannot be read. The fonts of both languages' font sets are loaded too.
   */
  bool setLanguage(uint8_t index);

  /** The language set with setLanguage, NO_LANGUAGE before. */
  uint8_t current();
  /** The language loaded in the Paired slot: the current one's pair, or the current one without a pair. */
  uint8_t paired();
  /** The language index of a slot. */
  uint8_t languageOf(Slot slot);

  /** Counts up on every setLanguage, so cached text can tell it is stale. */
  uint32_t epoch();

  /**
   * The text of a key in a slot's language, as stored: compiled, with tokens for tags and
   * variables, so use format() to show it. Never null: an unknown id gives "" in release builds and "#<id>" with a log
   * line when markMissing is on.
   */
  const char* text(LocId id, Slot slot = Slot::Game);

  /** Whether the slot's language has a text for the id at all. */
  bool has(LocId id, Slot slot = Slot::Game);

  /** Whether the slot's language has no translation yet, so text() gives the source language's. */
  bool isFallback(LocId id, Slot slot = Slot::Game);

  // ---------------------------------------------------------------- formatted text (phase 2)

  /**
   * The text of a key in a slot's language, ready for rdpq_text_print: [tags] become font and style
   * codes, {variables} their values (from 'args' first, then P64_LOC_VAR), plurals the right form.
   * Pass any font id to rdpq: the text starts with its own font code when the project has fonts.
   */
  FormatResult format(char* out, int size, LocId id, Slot slot = Slot::Game, const Args* args = nullptr, int8_t sizeOffset = 0);

  /** Formats a compiled text of the slot's language (a dialog line held by id elsewhere). */
  FormatResult formatCompiled(char* out, int size, const char* compiled, bool fallback, Slot slot, const Args* args = nullptr, int8_t sizeOffset = 0);

  /** The font of a slot's language for a variant (0 regular, 1 bold) and size step from the current base; 0 without fonts. */
  uint8_t fontFor(Slot slot, uint8_t variant, int8_t sizeOffset);

  /** The text size setting: font sizes up (or down) from each set's normal size. */
  void setTextSize(int8_t steps);
  int8_t textSize();

  /** The regular font of a slot's language at the current size, 0 when the project has no fonts. */
  uint8_t baseFont(Slot slot = Slot::Game);

  /**
   * Inserts line breaks into formatted text so no line is wider than 'width' pixels, for languages
   * that wrap by character (LANG_WRAP_CHAR), following kinsoku rules: no line starts with a closing
   * mark or a small kana, none ends with an opening mark. Existing '\n' stay. Returns the length.
   */
  int breakLines(char* text, int size, float width);

  /** Control tags: the op of a name ("wait"), 0 when the project has none by that name. */
  uint8_t controlOp(const char* name);
  const ControlInfo* control(uint8_t op);
  /** A control code point in formatted text: its op and arg; false for anything else. */
  bool decodeControl(uint32_t codepoint, uint8_t &op, uint16_t &arg);
  /** The value of a kind-name control ("slow" for [speed=slow]), or "". */
  const char* controlValueName(uint8_t op, uint16_t arg);
  inline float controlSeconds(uint16_t arg) { return float(arg) / 100.0f; }

  /** The index of an effect tag ("shake"), 0 when there is none; styleEffect(style) gives it back. */
  uint8_t effectIndex(const char* name);

  /**
   * Walks formatted text the way rdpq counts letters for max_chars: one Glyph per letter the font
   * has (spaces and codes are not counted), with the controls in between.
   */
  struct Scan
  {
    enum class Kind : uint8_t { End, Glyph, Space, Newline, Control };
    Kind kind{Kind::End};
    uint32_t codepoint{0};
    uint8_t op{0};      // Control
    uint16_t arg{0};    // Control
    uint8_t font{0};    // the font at this point
    uint8_t style{0};   // the style at this point
    const char* at{};   // where this item starts
  };
  /** Reads the next item from 'p' (advanced past it); 'font' and 'style' carry the current state. */
  Scan scanNext(const char*& p, uint8_t &font, uint8_t &style);

  /**
   * Debug aids, off by default; the game turns them on in debug builds. With markFallbacks the
   * formatter tints untranslated text; with markMissing an unknown id prints its hash.
   */
  void setDebugMarks(bool markFallbacks, bool markMissing);
  bool marksFallbacks();
}
