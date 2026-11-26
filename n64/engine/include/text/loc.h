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
    uint8_t plural;     // plural rule (phase 2)
    uint8_t reserved;
    float revealScale;  // how fast letters appear in this language, 1 = the setting's speed
  };

  namespace Generated
  {
    /** Defined by the generated src/p64/locTable.cpp, called once at boot. */
    void registerLanguages();
  }

  /** Called by the generated code. */
  void setLanguageList(const Language* list, uint8_t count);

  uint8_t languageCount();
  const Language* language(uint8_t index);
  /** The index of a language code ("ja"), or NO_LANGUAGE. */
  uint8_t findLanguage(const char* code);

  /**
   * Loads a language's table and its paired language's table, freeing the ones before.
   * Returns false (and keeps the current languages) when the index is out of range or a table
   * cannot be read. Fonts per language follow in phase 2.
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
   * The text of a key in a slot's language, as stored (phase 2 adds Loc::format for tags and
   * variables). Never null: an unknown id gives "" in release builds and "#<id>" with a log
   * line when markMissing is on.
   */
  const char* text(LocId id, Slot slot = Slot::Game);

  /** Whether the slot's language has a text for the id at all. */
  bool has(LocId id, Slot slot = Slot::Game);

  /** Whether the slot's language has no translation yet, so text() gives the source language's. */
  bool isFallback(LocId id, Slot slot = Slot::Game);

  /**
   * Debug aids, off by default; the game turns them on in debug builds. With markFallbacks the
   * formatter tints untranslated text (phase 2); with markMissing an unknown id prints its hash.
   */
  void setDebugMarks(bool markFallbacks, bool markMissing);
  bool marksFallbacks();
}
