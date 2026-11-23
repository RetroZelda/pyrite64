/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 4.4).
#pragma once
#include "scene/object.h"
#include "events/bus.h"

namespace P64::Comp
{
  /**
   * The event bindings of an object: which of its pins are bound to which events.
   * The build adds it to every object that has bindings; it is never shown in the editor's
   * "Add Component" and never stored in project files. It is the first component of its object,
   * so its outputs work from the other components' init().
   */
  struct EventLinks
  {
    static constexpr uint32_t ID = 26;

    static constexpr uint8_t COMP_OBJECT = 0xFF;   // 'comp' value of the object's own pins
    static constexpr uint8_t KIND_INPUT  = 1 << 0; // 'kind' bit 0: input pin (else output)
    static constexpr uint8_t KIND_SCOPE_SHIFT = 1; // 'kind' bits 1-3: Events::Scope
    static constexpr uint8_t KIND_SCOPE_MASK  = 0x7;

    /** One record of the build's init data. */
    struct Record
    {
      uint32_t hash;
      uint8_t comp; // component index on the object, or COMP_OBJECT
      uint8_t pin;  // pin index within that component
      uint8_t kind;
      uint8_t up;   // prefab levels to go up (prefab_N::)
    };
    static_assert(sizeof(Record) == 8);

    struct Binding
    {
      Record rec;
      uint16_t instance; // resolved scope instance: 0, or an object id
      Events::Listener listener;
      uint16_t pad;
      Object* owner;

      [[nodiscard]] bool isInput() const { return rec.kind & KIND_INPUT; }
      [[nodiscard]] Events::Scope scope() const {
        return static_cast<Events::Scope>((rec.kind >> KIND_SCOPE_SHIFT) & KIND_SCOPE_MASK);
      }
    };

    uint16_t count{};
    uint8_t ready{}; // onReady ran: Enabled / Disabled fire from now on
    uint8_t pad{};

    Binding* bindings() { return reinterpret_cast<Binding*>(this + 1); }
    const Binding* bindings() const { return reinterpret_cast<const Binding*>(this + 1); }

    // init data: u16 count, u16 reserved, then 'count' Records
    static uint32_t getAllocSize(uint16_t* initData) {
      return sizeof(EventLinks) + initData[0] * sizeof(Binding);
    }

    static void initDelete(Object& obj, EventLinks* data, uint16_t* initData);

    // the object's own outputs: Ready, Enabled, Disabled
    static void onReady(Object& obj, EventLinks* data);
    static void onEnable(Object& obj, EventLinks* data);
    static void onDisable(Object& obj, EventLinks* data);
  };
}
