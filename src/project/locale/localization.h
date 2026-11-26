/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4). The project's languages and string
// tables as the editor keeps them: locale/locale.json and one locale/<code>.json per language.
// Everything here works on values in memory, so the unit tests can run it; reading the files and
// scanning a project is in locProject.h.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "json.hpp"

namespace Project::Loc
{
  /** One language of locale/locale.json. */
  struct Language
  {
    std::string code{};
    std::string name{};
    std::string fonts{"latin"}; // font set (phase 2)
    std::string wrap{"word"};   // "word", or "char" for languages without spaces (kinsoku rules)
    std::string plural{};       // plural rule, the code by default (phase 2)
    float revealScale{1.0f};    // how fast letters appear, 1 = the speed setting
    bool pseudo{false};         // generated from the source language by the build, never stored
  };

  /** locale/locale.json */
  struct Config
  {
    std::string source{"en"};
    std::vector<Language> languages{};
    std::map<std::string, std::string> pairs{}; // language code -> paired language code
    std::vector<std::string> keep{};             // keys never reported unused: "item.sword", or "item.*"
    nlohmann::json raw = nlohmann::json::object(); // the whole file, so parts read elsewhere survive a save

    [[nodiscard]] int indexOf(const std::string &code) const;
    [[nodiscard]] const Language* find(const std::string &code) const;
    [[nodiscard]] std::string pairOf(const std::string &code) const;
    [[nodiscard]] bool keeps(const std::string &key) const;
  };

  /** One key of a table. */
  struct Entry
  {
    std::string text{};
    std::string note{};  // source table only: context for translators (who speaks, a length limit)
    uint32_t from{0};    // translations only: sourceHash() of the source text it was translated from
    bool keep{false};    // source table only: never reported unused
  };

  /** locale/<code>.json, keys sorted. */
  struct Table
  {
    std::string code{};
    std::map<std::string, Entry> entries{};
  };

  enum class Status : uint8_t
  {
    Ok,
    Missing,       // a translation is empty
    MissingSource, // the source language's text is empty
    NeedsReview,   // the source text changed since the translation was made
  };

  /** Where a key is used. */
  struct Use
  {
    std::string where{}; // "C++", "scene 3 (Main)", "prefab assets/x.prefab", "tree ..."
    std::string file{};
    int line{0};
  };
  using Uses = std::map<std::string, std::vector<Use>>;

  bool parseConfig(const nlohmann::json &doc, Config &out, std::vector<std::string> &errors);
  std::string serializeConfig(const Config &config);

  bool parseTable(const nlohmann::json &doc, Table &out, std::vector<std::string> &errors);
  std::string serializeTable(const Table &table);

  /** The value a translation's "from" holds for a source text. */
  uint32_t sourceHash(const std::string &text);

  /** Whether a key is well formed, with the reason when it is not. */
  bool isValidKey(const std::string &key, std::string *why = nullptr);

  /** The project's languages and tables, in the order of the config (the source language first). */
  struct Localization
  {
    Config config{};
    std::vector<Table> tables{}; // one per language, same order; pseudo languages have an empty one

    [[nodiscard]] int sourceIndex() const;
    [[nodiscard]] const Table* source() const;
    Table* table(const std::string &code);
    [[nodiscard]] const Table* table(const std::string &code) const;

    /** All keys, from every table. */
    [[nodiscard]] std::set<std::string> keys() const;

    [[nodiscard]] Status status(const std::string &key, size_t lang) const;
    [[nodiscard]] bool isKept(const std::string &key) const;

    /** Adds every used key that a table does not have yet, to every stored table. Returns the keys added. */
    std::vector<std::string> addMissing(const Uses &uses);
    /** Keys that nothing uses and that are not kept. */
    [[nodiscard]] std::vector<std::string> unused(const Uses &uses) const;
    /** Removes keys from every table. */
    void remove(const std::vector<std::string> &keys);

    /** Sets a translation and marks it as made from the current source text. */
    void setText(const std::string &key, size_t lang, const std::string &text);
    /** Marks a translation as checked against the current source text. */
    void confirm(const std::string &key, size_t lang);
  };

  /**
   * Makes the pseudo-language text of a source text: accented look-alike letters, about a third
   * longer, inside brackets. [tags], {variables} and escapes are kept as they are.
   */
  std::string pseudoText(const std::string &source);

  /**
   * Comments and the text of raw string literals replaced with spaces (newlines kept, so line numbers
   * stay); ordinary string and char literals kept.
   */
  std::string blankComments(const std::string &code);

  /** Finds "key"_localized in C++ source. A malformed key is reported in 'errors' and not added. */
  void scanCpp(const std::string &code, const std::string &file, Uses &out, std::vector<std::string> &errors);

  /** One string of a built table. */
  struct BuiltEntry
  {
    uint32_t id{};
    std::string text{};
    uint8_t flags{};
  };

  /** The bytes of a table file (engine text/locTable.h). Entries are sorted by id here. */
  std::vector<uint8_t> writeTable(std::vector<BuiltEntry> entries, uint16_t fileFlags);

  /** Text made safe for rdpq: '$' and '^' start codes there, so they are doubled. */
  std::string escapeForRdpq(const std::string &text);
}
