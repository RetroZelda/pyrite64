/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 5.3). The codes inside a text.
// This header must stay free of engine and libdragon includes: the editor and the host unit
// tests include it too.
//
// Two private-use planes of Unicode carry everything that is not a letter. rdpq has no glyph for
// them, so it skips them: they take no space and are not counted by max_chars.
//
//   Plane 15 (U+F0000 + op << 12 + arg): CONTROLS, left in the text the game draws.
//     op 0: built in, arg CTRL_PAGE = a page break
//     op 1-14: the project's control tags in the order of locale.json "controls" (wait, speed, sfx)
//   Plane 16 (U+100000 + op << 12 + arg): TOKENS of the compiled text in a table. Loc::format
//     turns them into rdpq font and style codes ($xx^yy), numbers and words; they never reach rdpq.
#pragma once
#include <cstddef>
#include <cstdint>

namespace P64::Loc
{
  constexpr uint32_t CONTROL_BASE = 0xF0000;
  constexpr uint32_t TOKEN_BASE = 0x100000;
  constexpr uint32_t CODE_ARG_MAX = 0xFFD; // op 15 arg 0xFFE/0xFFF are not characters

  constexpr uint8_t CTRL_BUILTIN = 0;  // op of the built-in controls
  constexpr uint16_t CTRL_PAGE = 0;    // [page]
  constexpr uint8_t CONTROLS_MAX = 14; // project control tags

  /** Tokens of a compiled text (plane 16). */
  enum class Tok : uint8_t
  {
    Pop       = 0,  // arg: the Push it closes (Group)
    Bold      = 1,
    Italic    = 2,
    Size      = 3,  // arg: 0x800 + steps (signed)
    Font      = 4,  // arg: font set index
    Color     = 5,  // arg: palette index
    Effect    = 6,  // arg: effect index (1 and up)
    Icon      = 7,  // arg: icon index
    Var       = 8,  // arg: VAR_* format flags; 3 Data tokens follow with the name's hash
    Plural    = 9,  // 3 Data tokens follow with the name's hash; then Case ... End
    Case      = 10, // arg: a PluralCat, or CASE_EXACT + the number
    End       = 11, // ends a Plural
    Number    = 12, // '#' inside a plural case
    Data      = 15, // 12 bits of the token before
  };

  /** What a Pop closes. */
  enum class Group : uint8_t { Bold, Italic, Size, Font, Color, Effect, Count };

  constexpr uint16_t SIZE_ZERO = 0x800;
  constexpr uint16_t CASE_EXACT = 0x10;

  constexpr uint16_t VAR_NO_GROUPING = 1 << 0; // {n:n}
  constexpr uint16_t VAR_PAD_SHIFT = 1;         // {n:03}: bits 1-4, zero pad to this many digits
  constexpr uint16_t VAR_PAD_MASK = 0xF << VAR_PAD_SHIFT;

  /** CLDR plural categories. */
  enum class PluralCat : uint8_t { Zero, One, Two, Few, Many, Other };

  /** Plural rules by family (generated per language from its "plural" or code). */
  enum class PluralRule : uint8_t
  {
    OtherOnly = 0, // ja, zh, ko: one form
    OneOther  = 1, // en, de, nl, sv: 1 is one, the rest other
    Spanish   = 2, // es, it: 1 one; millions many; other
    French    = 3, // fr, pt: 0 and 1 one; millions many; other
  };

  constexpr PluralCat pluralCategory(PluralRule rule, int64_t n)
  {
    const int64_t a = n < 0 ? -n : n;
    const bool millions = a != 0 && a % 1000000 == 0;
    switch(rule) {
      case PluralRule::OtherOnly: return PluralCat::Other;
      case PluralRule::OneOther:  return a == 1 ? PluralCat::One : PluralCat::Other;
      case PluralRule::Spanish:   return a == 1 ? PluralCat::One : millions ? PluralCat::Many : PluralCat::Other;
      case PluralRule::French:    return a <= 1 ? PluralCat::One : millions ? PluralCat::Many : PluralCat::Other;
    }
    return PluralCat::Other;
  }

  // ---- code points

  constexpr bool isControl(uint32_t cp) { return cp >= CONTROL_BASE && cp < CONTROL_BASE + 0x10000; }
  constexpr bool isToken(uint32_t cp) { return cp >= TOKEN_BASE && cp < TOKEN_BASE + 0x10000; }

  constexpr uint32_t control(uint8_t op, uint16_t arg) { return CONTROL_BASE + (uint32_t(op) << 12) + arg; }
  constexpr uint32_t token(Tok op, uint16_t arg) { return TOKEN_BASE + (uint32_t(op) << 12) + arg; }

  constexpr uint8_t codeOp(uint32_t cp) { return uint8_t((cp >> 12) & 0xF); }
  constexpr uint16_t codeArg(uint32_t cp) { return uint16_t(cp & 0xFFF); }

  /** Writes a code point as UTF-8; returns the bytes written (0 if 'room' is too small). */
  constexpr int utf8Encode(uint32_t cp, char* out, int room)
  {
    if(cp < 0x80) {
      if(room < 1) return 0;
      out[0] = char(cp);
      return 1;
    }
    if(cp < 0x800) {
      if(room < 2) return 0;
      out[0] = char(0xC0 | (cp >> 6));
      out[1] = char(0x80 | (cp & 0x3F));
      return 2;
    }
    if(cp < 0x10000) {
      if(room < 3) return 0;
      out[0] = char(0xE0 | (cp >> 12));
      out[1] = char(0x80 | ((cp >> 6) & 0x3F));
      out[2] = char(0x80 | (cp & 0x3F));
      return 3;
    }
    if(room < 4) return 0;
    out[0] = char(0xF0 | (cp >> 18));
    out[1] = char(0x80 | ((cp >> 12) & 0x3F));
    out[2] = char(0x80 | ((cp >> 6) & 0x3F));
    out[3] = char(0x80 | (cp & 0x3F));
    return 4;
  }

  /** Reads one code point and advances 'p'. Malformed bytes read as themselves. */
  constexpr uint32_t utf8Decode(const char*& p)
  {
    const auto b0 = uint8_t(p[0]);
    if(b0 < 0x80) { ++p; return b0; }
    int len = (b0 >= 0xF0) ? 4 : (b0 >= 0xE0) ? 3 : (b0 >= 0xC0) ? 2 : 1;
    if(len == 1) { ++p; return b0; }
    uint32_t cp = b0 & (0x7F >> len);
    for(int i = 1; i < len; ++i) {
      const auto b = uint8_t(p[i]);
      if((b & 0xC0) != 0x80) { ++p; return b0; }
      cp = (cp << 6) | (b & 0x3F);
    }
    p += len;
    return cp;
  }

  /**
   * Kinsoku: characters a line must not start with (closing marks, small kana, the prolonged
   * sound mark) and must not end with (opening marks), for languages that wrap by character.
   */
  constexpr bool kinsokuNoStart(uint32_t cp)
  {
    switch(cp) {
      case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0x30FB: case 0xFF1A: case 0xFF1B:
      case 0xFF1F: case 0xFF01: case 0x309B: case 0x309C: case 0x30FC: case 0x2015: case 0x2010:
      case 0x301C: case 0xFF5E: case 0x2026: case 0x2025: case 0x300D: case 0x300F: case 0xFF09:
      case 0x3011: case 0x3009: case 0x300B: case 0xFF3D: case 0xFF5D: case 0x3015: case 0x3017:
      case 0x3019: case 0x301B: case 0x201D: case 0x2019: case 0x30FD: case 0x30FE: case 0x309D:
      case 0x309E: case 0x3005: case ')': case ']': case '}': case ',': case '.': case '!':
      case '?': case ':': case ';':
        return true;
      default: break;
    }
    // small hiragana and katakana: ぁぃぅぇぉっゃゅょゎ ァィゥェォッャュョヮヵヶ
    switch(cp) {
      case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083:
      case 0x3085: case 0x3087: case 0x308E: case 0x3095: case 0x3096: case 0x30A1: case 0x30A3:
      case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3: case 0x30E5: case 0x30E7:
      case 0x30EE: case 0x30F5: case 0x30F6:
        return true;
      default: return false;
    }
  }

  constexpr bool kinsokuNoEnd(uint32_t cp)
  {
    switch(cp) {
      case 0x300C: case 0x300E: case 0xFF08: case 0x3010: case 0x3008: case 0x300A: case 0xFF3B:
      case 0xFF5B: case 0x3014: case 0x3016: case 0x3018: case 0x301A: case 0x201C: case 0x2018:
      case '(': case '[': case '{':
        return true;
      default: return false;
    }
  }
}
