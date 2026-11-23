/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: conditions (docs/todo/conditionals/PLAN.md, section 3). Who provides which Result name:
// a fixed table, no libdragon or engine includes, so the host unit tests compile it as is.
#pragma once
#include <cstdint>

#include "events/config.h"
#include "events/types.h"

namespace P64::Conditions::Core
{
  /** One bound Result output of a live condition. */
  struct Provider
  {
    uint32_t hash{};     // the event the Result is bound to
    uint16_t instance{}; // its scope instance, as the bus keys it
    uint16_t object{};   // the condition's object
    uint8_t comp{};      // the condition script's component index on that object
    uint8_t scope{};     // Events::Scope
  };

  class Providers
  {
    public:
      static constexpr uint32_t MAX = P64_CONDITION_MAX_PROVIDERS;
      static_assert(MAX > 0 && MAX <= 0xFFFF, "P64_CONDITION_MAX_PROVIDERS out of range");

      /** @return false if the table is full (the entry already being there counts as added) */
      bool add(uint32_t hash, Events::Scope scope, uint16_t instance, uint16_t object, uint8_t comp);

      /** Removes what one condition script provides. @return entries removed */
      uint32_t remove(uint16_t object, uint8_t comp);

      void clear() { used = 0; }

      /**
       * The providers of a name, at most 'max' of them, in the order they were added.
       * @return how many were written to 'out'
       */
      uint32_t find(uint32_t hash, Events::Scope scope, uint16_t instance, Provider* out, uint32_t max) const;

      [[nodiscard]] uint32_t count() const { return used; }
      [[nodiscard]] uint32_t peak() const { return usedPeak; }

    private:
      Provider entries[MAX]{};
      uint32_t used{0};
      uint32_t usedPeak{0};
  };
}
