/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4.1). Text keys and their ids.
// This header must stay free of engine and libdragon includes: the editor and the host unit
// tests include it too.
#pragma once
#include <cstddef>
#include <cstdint>

namespace P64::Loc
{
  /**
   * The id of a localized text: the CRC-32 of its key, written "npc.tank.greet"_localized.
   * An id of 0 means "no text".
   */
  struct LocId
  {
    uint32_t hash{};

    constexpr bool operator==(const LocId&) const = default;
    constexpr bool empty() const { return hash == 0; }
  };

  /** Why a key was rejected, see checkKey(). */
  enum class KeyError : uint8_t
  {
    None,
    Empty,     // the key is empty
    BadChar,   // a character other than a-z, 0-9, _ and .
    EmptyPart, // two dots in a row, or a dot at the start or the end
  };

  namespace Detail
  {
    constexpr uint32_t crc32(const char* str, size_t len)
    {
      uint32_t crc = 0xFFFFFFFFu;
      for(size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint8_t>(str[i]);
        for(int bit = 0; bit < 8; ++bit) {
          crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
      }
      return ~crc;
    }

    constexpr bool isKeyChar(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

    // Not constexpr on purpose: "..."_localized calls one of these for a bad key, which stops the
    // compile. The function's name in the compiler error says what is wrong with the key.
    void localizedKey_must_not_be_empty();
    void localizedKey_must_be_lower_case_letters_digits_and_underscore();
    void localizedKey_must_not_have_empty_parts_between_dots();
  }

  /**
   * Checks a key: one or more parts of a-z, 0-9 and _, separated by dots ("npc.tank.greet").
   * Usable at compile time and at runtime.
   */
  constexpr KeyError checkKey(const char* str, size_t len)
  {
    if(len == 0) return KeyError::Empty;
    bool partEmpty = true;
    for(size_t i = 0; i < len; ++i) {
      const char c = str[i];
      if(c == '.') {
        if(partEmpty) return KeyError::EmptyPart;
        partEmpty = true;
        continue;
      }
      if(!Detail::isKeyChar(c)) return KeyError::BadChar;
      partEmpty = false;
    }
    return partEmpty ? KeyError::EmptyPart : KeyError::None;
  }

  /** The id of a key, the same value "key"_localized gives. */
  constexpr LocId keyId(const char* str, size_t len) { return {Detail::crc32(str, len)}; }

  /** The hash of a variable name, the same value "name"_var gives (phase 2: {variables}). */
  constexpr uint32_t varHash(const char* str, size_t len) { return Detail::crc32(str, len); }
}

namespace P64
{
  using LocId = Loc::LocId;
  /** A script field that holds a localized text: the editor shows a key picker for it. */
  using LocText = Loc::LocId;
}

/**
 * Localized text id: "npc.tank.greet"_localized. Checked and hashed at compile time; a
 * malformed key stops the compile (see KeyError).
 */
consteval P64::Loc::LocId operator""_localized(const char* str, size_t len)
{
  using P64::Loc::KeyError;
  switch(P64::Loc::checkKey(str, len))
  {
    case KeyError::None: break;
    case KeyError::Empty:     P64::Loc::Detail::localizedKey_must_not_be_empty(); break;
    case KeyError::BadChar:   P64::Loc::Detail::localizedKey_must_be_lower_case_letters_digits_and_underscore(); break;
    case KeyError::EmptyPart: P64::Loc::Detail::localizedKey_must_not_have_empty_parts_between_dots(); break;
  }
  return P64::Loc::keyId(str, len);
}
