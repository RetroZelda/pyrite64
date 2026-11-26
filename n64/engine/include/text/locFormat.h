/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 5 and 6). Variables declared in C++, and
// the formatter that turns a compiled text into what rdpq draws: font and style codes ($xx^yy),
// numbers, plural forms and the values of variables.
// This header must stay free of engine and libdragon includes: the host unit tests include it.
#pragma once
#include <cstddef>
#include <cstdint>

#include "text/locId.h"
#include "text/locCodes.h"

namespace P64::Loc
{
  enum class VarType : uint8_t { Int, Key, Text };

  /** A variable declared with P64_LOC_VAR. Registered by its constructor, before main(). */
  struct VarReg
  {
    using IntFn = int32_t (*)();
    using KeyFn = LocId (*)();
    using TextFn = const char* (*)();

    uint32_t hash;
    VarType type;
    union { IntFn i; KeyFn k; TextFn t; } fn;
    const VarReg* next;

    VarReg(uint32_t nameHash, IntFn f);
    VarReg(uint32_t nameHash, KeyFn f);
    VarReg(uint32_t nameHash, TextFn f);
  };

  /** The variable registered under a name's hash, or null. */
  const VarReg* findVar(uint32_t hash);

  /** Values passed by whoever shows a text (declared with P64_LOC_ARG); they win over globals. */
  struct Args
  {
    static constexpr uint8_t MAX = 8;
    struct Item
    {
      uint32_t hash;
      VarType type;
      union { int32_t i; uint32_t key; const char* text; } v;
    };
    Item items[MAX]{};
    uint8_t count{0};

    Args& set(uint32_t hash, int32_t value);
    Args& set(uint32_t hash, LocId value);
    Args& set(uint32_t hash, const char* value);
    const Item* find(uint32_t hash) const;
  };

  constexpr uint8_t SIZES_MAX = 4;
  constexpr uint8_t VARIANTS = 4; // regular, bold, italic, bold italic

  /** The fonts of one font set: rdpq font ids per variant and size, 0 where none was baked. */
  struct FontSet
  {
    const char* name;
    uint8_t ids[VARIANTS][SIZES_MAX];
    uint8_t sizeCount;
    uint8_t baseSize;     // index of the normal size
    uint32_t outline;     // outline color RGBA (for fonts baked with an outline)
  };

  /** What the formatter needs about the language and the project's fonts. */
  struct FormatEnv
  {
    const FontSet* sets{};
    uint8_t setCount{};
    uint8_t baseSet{0xFF};     // the language's font set, 0xFF: emit no font codes
    int8_t sizeStep{0};        // the text size setting: sizes up from the set's base size
    PluralRule plural{PluralRule::OneOther};
    char groupSep{','};        // digit grouping
    uint8_t groupMin{4};       // group numbers with at least this many digits
    uint8_t fallbackColor{0};  // palette index that tints untranslated text, 0: none
    uint8_t iconSet{0xFF};     // font set of the button icons
    uint32_t iconBase{0xE000}; // code point of icon 0 in that set
    const Args* args{};
    /** The compiled text of a key in the same language, for Key variables. */
    const char* (*keyText)(uint32_t id, bool* fallback, void* user){};
    void* user{};
  };

  struct FormatResult
  {
    int length{0};          // bytes written, without the terminating zero
    bool truncated{false};  // the buffer was too small; the text stops at a whole character
    uint8_t firstFont{0};   // the font the text starts with (0: no font codes)
  };

  /**
   * Formats a compiled text (as a table stores it) into 'out', always zero-terminated.
   * 'fallback' says the text is the source language's (tinted when env.fallbackColor is set).
   */
  FormatResult formatText(const char* compiled, bool fallback, char* out, int size, const FormatEnv &env);

  /** The style id rdpq uses for a palette color and an effect. */
  constexpr uint8_t styleId(uint8_t color, uint8_t effect) { return uint8_t((color & 31) + 32 * (effect & 7)); }
  constexpr uint8_t styleColor(uint8_t style) { return style & 31; }
  constexpr uint8_t styleEffect(uint8_t style) { return style >> 5; }
}

/** Variable name literal for Loc::Args: "price"_var. Checked at compile time: an identifier. */
consteval uint32_t operator""_var(const char* str, size_t len)
{
  bool ok = len > 0 && !(str[0] >= '0' && str[0] <= '9');
  for(size_t i = 0; i < len; ++i) {
    const char c = str[i];
    if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) ok = false;
  }
  if(!ok) P64::Loc::Detail::textVariable_must_be_an_identifier();
  return P64::Loc::varHash(str, len);
}

namespace P64::Loc::Detail
{
  template<typename T> struct VarFnOf;
  template<> struct VarFnOf<int32_t> { using Fn = VarReg::IntFn; };
  template<> struct VarFnOf<LocId> { using Fn = VarReg::KeyFn; };
  template<> struct VarFnOf<const char*> { using Fn = VarReg::TextFn; };

  using VarType_Int = int32_t;
  using VarType_Key = P64::LocId;
  using VarType_Text = const char*;
}

/**
 * Declares a variable that every text can show as {name}. Only C++ declares variables.
 * The braces after it are the getter, called when a page with the variable starts:
 *
 *   P64_LOC_VAR(coins, Int) { return Inventory::coins(); }
 *   P64_LOC_VAR(item, Key)  { return Inventory::lastItemName(); }  // another text, in the line's language
 *   P64_LOC_VAR(name, Text) { return saveName; }                   // raw text, not localized
 */
#define P64_LOC_VAR(NAME, TYPE) \
  static P64::Loc::Detail::VarType_##TYPE p64LocVarGet_##NAME(); \
  [[maybe_unused]] static const P64::Loc::VarReg p64LocVarReg_##NAME{P64::Loc::varHash(#NAME, sizeof(#NAME) - 1), &p64LocVarGet_##NAME}; \
  static P64::Loc::Detail::VarType_##TYPE p64LocVarGet_##NAME()

/**
 * Declares a variable that the code showing a text passes in with Loc::Args, like a shop's price:
 *   P64_LOC_ARG(price, Int);
 *   Loc::Args args; args.set("price"_var, 20);
 * The editor reads the declaration to check {price} in every language; it compiles to nothing.
 */
#define P64_LOC_ARG(NAME, TYPE) static_assert(true, "")
