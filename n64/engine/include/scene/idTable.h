/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: finding an object by its id. The scene keeps a direct table for the first ids; every other
// id (every object of a spawned prefab, and scenes with many objects) went through a scan of all
// objects, a cache miss per object. This table finds them in one or two steps instead.
// No engine or libdragon includes: the host unit tests compile it.
#pragma once
#include <cstdint>
#include <vector>

namespace P64
{
  /**
   * Pointers by 16-bit id: open addressing with linear probing, grown as it fills. Ids and pointers
   * sit next to each other, so a lookup touches the table only, never the objects.
   */
  template<typename T>
  class IdTable
  {
    public:
      void insert(uint16_t id, T* obj)
      {
        // at most half full, removed entries counted: probes stay short
        if((used + 1) * 2 > slots.size()) rebuild();
        uint32_t i = indexOf(id);
        while(slots[i].state != EMPTY) i = (i + 1) & mask();
        slots[i] = {obj, id, USED};
        ++used;
        ++live;
      }

      /** Removes the entry of exactly this object. @return false if it was not there */
      bool erase(uint16_t id, const T* obj)
      {
        if(slots.empty()) return false;
        for(uint32_t i = indexOf(id); slots[i].state != EMPTY; i = (i + 1) & mask()) {
          if(slots[i].state == USED && slots[i].id == id && slots[i].obj == obj) {
            slots[i].state = REMOVED; // keeps the probe chains behind it intact
            slots[i].obj = nullptr;
            --live;
            return true;
          }
        }
        return false;
      }

      /** The object with this id, or nullptr. With two objects of one id: either of them. */
      [[nodiscard]] T* find(uint16_t id) const
      {
        if(slots.empty()) return nullptr;
        for(uint32_t i = indexOf(id); slots[i].state != EMPTY; i = (i + 1) & mask()) {
          if(slots[i].state == USED && slots[i].id == id) return slots[i].obj;
        }
        return nullptr;
      }

      void clear()
      {
        slots.clear();
        used = 0;
        live = 0;
      }

      [[nodiscard]] uint32_t size() const { return live; }
      [[nodiscard]] uint32_t capacity() const { return static_cast<uint32_t>(slots.size()); }

    private:
      static constexpr uint8_t EMPTY = 0, USED = 1, REMOVED = 2;
      static constexpr uint32_t MIN_CAPACITY = 64;

      struct Slot
      {
        T* obj{};
        uint16_t id{};
        uint8_t state{EMPTY};
      };

      std::vector<Slot> slots{}; // size is 0 or a power of two
      uint32_t used{0};          // USED and REMOVED slots
      uint32_t live{0};          // USED slots

      [[nodiscard]] uint32_t mask() const { return static_cast<uint32_t>(slots.size()) - 1; }
      // ids of one spawn are consecutive: the multiplier spreads them
      [[nodiscard]] uint32_t indexOf(uint16_t id) const { return (id * 40503u) & mask(); }

      // drops the removed entries; doubles the size when the live ones need it
      void rebuild()
      {
        uint32_t wanted = MIN_CAPACITY;
        while((live + 1) * 2 > wanted) wanted *= 2;
        std::vector<Slot> old{};
        old.swap(slots);
        slots.resize(wanted);
        used = 0;
        live = 0;
        for(const auto &slot : old) {
          if(slot.state != USED) continue;
          uint32_t i = indexOf(slot.id);
          while(slots[i].state != EMPTY) i = (i + 1) & mask();
          slots[i] = slot;
          ++used;
          ++live;
        }
      }
  };
}
