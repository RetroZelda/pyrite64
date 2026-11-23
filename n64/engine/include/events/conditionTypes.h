/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: conditions (docs/todo/conditionals/PLAN.md). The few bytes a condition keeps. This header
// must stay free of engine and libdragon includes: the editor and the host unit tests include it.
#pragma once
#include <cstdint>

namespace P64::Conditions
{
  /**
   * In front of the data of every condition script (P64_CONDITION_DATA puts it there). The editor
   * writes the same four bytes: "Starts Enabled" is the only one a designer sets.
   */
  struct Header
  {
    uint8_t startsEnabled{1};
    uint8_t flags{0};      // Flag::*, set up by the engine when the object is created
    uint16_t reserved{0};
  };
  static_assert(sizeof(Header) == 4);

  namespace Flag
  {
    inline constexpr uint8_t ENABLED = 1 << 0; // reacts to Check and to its triggers
    inline constexpr uint8_t LAST    = 1 << 1; // the answer of the last check (starts false)
    inline constexpr uint8_t PENDING = 1 << 2; // a re-check is on its way through the queue
  }

  /** What a check has to fire. */
  struct Outcome
  {
    bool checked{false}; // false: the condition is disabled, nothing happens
    bool answer{false};
    bool changed{false}; // the answer differs from the last check: NewlyTrue or NewlyFalse
  };

  /** The state a condition starts in, and returns to on Reset. */
  constexpr void reset(Header& header)
  {
    header.flags = header.startsEnabled ? Flag::ENABLED : 0;
  }

  constexpr void setEnabled(Header& header, bool enabled)
  {
    header.flags = static_cast<uint8_t>(enabled ? (header.flags | Flag::ENABLED) : (header.flags & ~Flag::ENABLED));
  }

  [[nodiscard]] constexpr bool isEnabled(const Header& header) { return header.flags & Flag::ENABLED; }

  /** Records the answer of a check and says what to fire. A disabled condition records nothing. */
  constexpr Outcome applyAnswer(Header& header, bool answer)
  {
    if(!isEnabled(header)) return {};
    const bool last = header.flags & Flag::LAST;
    if(answer != last) header.flags ^= Flag::LAST;
    return {.checked = true, .answer = answer, .changed = answer != last};
  }
}
