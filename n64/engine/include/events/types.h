/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event bus, names and ids (docs/todo/event-graph/PLAN.md, sections 3.2, 3.3 and 3.6).
// This header must stay free of engine and libdragon includes: the editor and the host unit
// tests include it too.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace P64::Events
{
  /** Who an event reaches. The namespace of the event name decides it. */
  enum class Scope : uint8_t
  {
    Global = 0, // global::, and namespaces a game declares (they behave like global)
    Scene  = 1, // scene::
    Prefab = 2, // prefab::, prefab_0::, prefab_1::, ...
    Object = 3, // object::
    UI     = 4, // ui::
    Direct = 5, // internal: send() to one object's inputs by pin name; never a namespace
  };

  /** What a listener receives. */
  struct Event
  {
    uint32_t id{};         // hash of the event name, see EventId::hash
    uint16_t sender{};     // object that fired it, 0 for the engine or a system
    uint16_t instigator{}; // object that started the chain, carried along every hop
    uint32_t value{};      // one number: a count, an object id, a hash or float bits
  };

  using ListenerFn = void(*)(const Event& event, void* user);

  /**
   * A parsed event name, as produced by the "..."_event literal.
   * Write names as text in code ("scene::Loaded"_event); the hash is computed at compile time.
   */
  struct EventId
  {
    uint32_t hash{};            // CRC-32 of the name; prefab_N:: names are hashed as prefab::
    Scope scope{Scope::Global};
    uint8_t up{0};              // prefab_N:: only: how many prefab instances to go up

    constexpr bool operator==(const EventId&) const = default;
  };

  /** Why a name was rejected, see parseName(). */
  enum class NameError : uint8_t
  {
    None,
    MissingSeparator,  // there is no "::" between namespace and name
    BadNamespace,      // the namespace is empty, does not start with a-z, or has characters other than a-z, 0-9, _
    BadName,           // the name is empty, has an empty part between dots, or characters other than A-Z, a-z, 0-9, _, .
    BadPrefabNumber,   // prefab_ is not followed by a number from 0 to 255 (without leading zeros)
    GlobalNeedsSystem, // global names are global::System.Pin, System and Pin being identifiers
  };

  struct ParsedName
  {
    EventId id{};
    NameError error{NameError::None};
  };

  namespace Detail
  {
    constexpr uint32_t crc32Update(uint32_t crc, const char* str, size_t len)
    {
      for(size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint8_t>(str[i]);
        for(int bit = 0; bit < 8; ++bit) {
          crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
      }
      return crc;
    }

    constexpr bool isLower(char c) { return c >= 'a' && c <= 'z'; }
    constexpr bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
    constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
    constexpr bool isNameChar(char c) { return isLower(c) || isUpper(c) || isDigit(c) || c == '_'; }

    constexpr bool equals(const char* str, size_t len, const char* literal)
    {
      size_t i = 0;
      for(; i < len; ++i) {
        if(literal[i] == '\0' || literal[i] != str[i]) return false;
      }
      return literal[i] == '\0';
    }

    /** An identifier: not empty, only A-Z a-z 0-9 _, and not starting with a digit. */
    constexpr bool isIdentifier(const char* str, size_t len)
    {
      if(len == 0 || isDigit(str[0])) return false;
      for(size_t i = 0; i < len; ++i) {
        if(!isNameChar(str[i])) return false;
      }
      return true;
    }

    // Not constexpr on purpose: "..."_event calls one of these for a bad name, which stops the
    // compile. The function's name in the compiler error says what is wrong with the event name.
    void eventName_needs_namespace_then_two_colons_then_name();
    void eventNamespace_must_be_lower_case_letters_digits_or_underscore();
    void eventName_must_be_letters_digits_underscore_and_dots_without_empty_parts();
    void eventNamespace_prefab_number_must_be_0_to_255_without_leading_zeros();
    void globalEventName_must_be_global_colon_colon_System_dot_Pin();
    void pinName_must_be_an_identifier();
    void pin_list_must_be_identifiers_separated_by_commas();
  }

  /**
   * Parses and hashes an event name. Usable at compile time and at runtime.
   * Rules (PLAN 3.2): "namespace::Name". The namespace is lower case. The name uses letters,
   * digits, '_' and '.', and is case-sensitive. global:: names must be global::System.Pin.
   */
  constexpr ParsedName parseName(const char* str, size_t len)
  {
    size_t sep = len;
    for(size_t i = 0; i + 1 < len; ++i) {
      if(str[i] == ':' && str[i + 1] == ':') { sep = i; break; }
    }
    if(sep == len) return {{}, NameError::MissingSeparator};

    // namespace: [a-z][a-z0-9_]*
    if(sep == 0 || !Detail::isLower(str[0])) return {{}, NameError::BadNamespace};
    for(size_t i = 1; i < sep; ++i) {
      char c = str[i];
      if(!Detail::isLower(c) && !Detail::isDigit(c) && c != '_') return {{}, NameError::BadNamespace};
    }

    // name: one or more non-empty parts of [A-Za-z0-9_], separated by '.'
    const char* name = str + sep + 2;
    size_t nameLen = len - sep - 2;
    if(nameLen == 0) return {{}, NameError::BadName};
    uint32_t parts = 1;
    size_t partLen = 0;
    size_t firstPartLen = 0;
    for(size_t i = 0; i < nameLen; ++i) {
      char c = name[i];
      if(c == '.') {
        if(partLen == 0) return {{}, NameError::BadName};
        if(parts == 1) firstPartLen = partLen;
        ++parts;
        partLen = 0;
      } else if(Detail::isNameChar(c)) {
        ++partLen;
      } else {
        return {{}, NameError::BadName};
      }
    }
    if(partLen == 0) return {{}, NameError::BadName};

    ParsedName res{};
    bool hashAsPrefab = false;

    if(Detail::equals(str, sep, "global")) {
      res.id.scope = Scope::Global;
      // global::System.Pin: exactly two identifier parts
      if(parts != 2) return {{}, NameError::GlobalNeedsSystem};
      size_t pinLen = nameLen - firstPartLen - 1;
      if(!Detail::isIdentifier(name, firstPartLen)) return {{}, NameError::GlobalNeedsSystem};
      if(!Detail::isIdentifier(name + firstPartLen + 1, pinLen)) return {{}, NameError::GlobalNeedsSystem};
    }
    else if(Detail::equals(str, sep, "scene"))  res.id.scope = Scope::Scene;
    else if(Detail::equals(str, sep, "object")) res.id.scope = Scope::Object;
    else if(Detail::equals(str, sep, "ui"))     res.id.scope = Scope::UI;
    else if(Detail::equals(str, sep, "prefab")) {
      res.id.scope = Scope::Prefab;
    }
    else if(sep > 7 && Detail::equals(str, 7, "prefab_")) {
      // prefab_N: N levels of prefab instances up from the prefab the name is written in
      size_t digits = sep - 7;
      if(digits > 1 && str[7] == '0') return {{}, NameError::BadPrefabNumber};
      uint32_t number = 0;
      for(size_t i = 7; i < sep; ++i) {
        if(!Detail::isDigit(str[i])) return {{}, NameError::BadPrefabNumber};
        number = number * 10 + static_cast<uint32_t>(str[i] - '0');
        if(number > 255) return {{}, NameError::BadPrefabNumber};
      }
      res.id.scope = Scope::Prefab;
      res.id.up = static_cast<uint8_t>(number);
      hashAsPrefab = true;
    }
    else if(sep == 7 && Detail::equals(str, 7, "prefab_")) {
      return {{}, NameError::BadPrefabNumber};
    }
    else {
      // any other namespace is one a game declares; those behave like global (PLAN 3.3)
      res.id.scope = Scope::Global;
    }

    uint32_t crc = 0xFFFFFFFFu;
    if(hashAsPrefab) {
      // the number only says how far up; prefab_1::Alarm is the same event as prefab::Alarm
      crc = Detail::crc32Update(crc, "prefab", 6);
      crc = Detail::crc32Update(crc, str + sep, len - sep);
    } else {
      crc = Detail::crc32Update(crc, str, len);
    }
    res.id.hash = ~crc;
    return res;
  }

  /** A pin name for send(), as produced by the "..."_pin literal: "TakeDamage"_pin. */
  struct PinName
  {
    uint32_t hash{}; // CRC-32 of the pin name

    constexpr bool operator==(const PinName&) const = default;
  };

  /** Hash of a pin name (CRC-32 of the text), the same value the build writes for scripts. */
  constexpr uint32_t pinHash(const char* str, size_t len)
  {
    return ~Detail::crc32Update(0xFFFFFFFFu, str, len);
  }

  namespace Detail
  {
    constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

    /** Counts the names in a stringized macro argument list ("A, B, C" has 3). */
    consteval size_t countPins(const char* list)
    {
      size_t count = 0;
      bool inName = false;
      for(size_t i = 0; list[i] != '\0'; ++i) {
        if(list[i] == ',') { inName = false; continue; }
        if(isSpace(list[i])) continue;
        if(!inName) { ++count; inName = true; }
      }
      return count;
    }

    /**
     * Hashes of 'prefix' + each name of a stringized macro argument list, in order.
     * Used by P64_GLOBAL_EVENTS_OUT: prefix "global::System." and list "A, B" give the ids of
     * global::System.A and global::System.B.
     */
    template<size_t N>
    consteval std::array<uint32_t, N> systemPinHashes(const char* prefix, const char* list)
    {
      std::array<uint32_t, N> res{};
      size_t prefixLen = 0;
      while(prefix[prefixLen] != '\0') ++prefixLen;
      const uint32_t prefixCrc = crc32Update(0xFFFFFFFFu, prefix, prefixLen);

      size_t idx = 0;
      size_t i = 0;
      while(list[i] != '\0') {
        while(isSpace(list[i]) || list[i] == ',') ++i;
        if(list[i] == '\0') break;
        size_t start = i;
        while(list[i] != '\0' && list[i] != ',' && !isSpace(list[i])) ++i;
        size_t len = i - start;
        if(idx >= N || !isIdentifier(list + start, len)) pin_list_must_be_identifiers_separated_by_commas();
        res[idx++] = ~crc32Update(prefixCrc, list + start, len);
      }
      if(idx != N) pin_list_must_be_identifiers_separated_by_commas();
      return res;
    }
  }

  /** Counters kept by the bus, see getStats(). */
  struct Stats
  {
    uint32_t posted{};            // events accepted into the queue, or delivered immediately
    uint32_t delivered{};         // events dispatched to their listeners
    uint32_t listenerCalls{};     // listener functions called
    uint32_t droppedQueueFull{};  // posts lost because the queue was full
    uint32_t droppedHopLimit{};   // posts lost because the chain went past the hop limit
    uint32_t droppedMailbox{};    // interrupt posts lost because the mailbox was full
    uint32_t droppedWrongScope{}; // posts or listens with a scope the call does not support
    uint32_t budgetHits{};        // drains that stopped at their budget with events left
    uint32_t subscribeFailed{};   // listens lost because every subscription slot was in use
    uint16_t queued{};            // events waiting right now
    uint16_t queuePeak{};
    uint16_t subscriptions{};     // subscription slots in use right now
    uint16_t subscriptionsPeak{};
  };
}

/**
 * Pin name literal for send(): "TakeDamage"_pin. Checked at compile time: an identifier.
 */
consteval P64::Events::PinName operator""_pin(const char* str, size_t len)
{
  if(!P64::Events::Detail::isIdentifier(str, len)) P64::Events::Detail::pinName_must_be_an_identifier();
  return {P64::Events::pinHash(str, len)};
}

/**
 * Event name literal: "scene::Loaded"_event.
 * Checked and hashed at compile time; a malformed name stops the compile (see NameError).
 */
consteval P64::Events::EventId operator""_event(const char* str, size_t len)
{
  using P64::Events::NameError;
  const auto res = P64::Events::parseName(str, len);
  switch(res.error)
  {
    case NameError::None: break;
    case NameError::MissingSeparator:  P64::Events::Detail::eventName_needs_namespace_then_two_colons_then_name(); break;
    case NameError::BadNamespace:      P64::Events::Detail::eventNamespace_must_be_lower_case_letters_digits_or_underscore(); break;
    case NameError::BadName:           P64::Events::Detail::eventName_must_be_letters_digits_underscore_and_dots_without_empty_parts(); break;
    case NameError::BadPrefabNumber:   P64::Events::Detail::eventNamespace_prefab_number_must_be_0_to_255_without_leading_zeros(); break;
    case NameError::GlobalNeedsSystem: P64::Events::Detail::globalEventName_must_be_global_colon_colon_System_dot_Pin(); break;
  }
  return res.id;
}
