/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 6.1).
// The event bindings of one component or object, as stored in scene and prefab files:
//   "events": { "in": { "Open": ["prefab::TriggerZone.Enter"] }, "out": { "Opened": ["scene::DoorOpened"] } }
#pragma once
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace Project::Events
{
  /** Pin name to the event names it is bound to. A std::map, so files list pins sorted. */
  using PinMap = std::map<std::string, std::vector<std::string>>;

  struct Bindings
  {
    PinMap in{};
    PinMap out{};

    [[nodiscard]] bool empty() const;
    [[nodiscard]] size_t count() const;

    /** {"in": {...}, "out": {...}}, leaving out empty pins and directions. */
    [[nodiscard]] nlohmann::json toJson() const;
    static Bindings fromJson(const nlohmann::json &doc);

    /** Adds 'eventName' to a pin unless it is already there. */
    void add(bool input, const std::string &pin, const std::string &eventName);
    /** Removes empty entries and pins without bindings. */
    void prune();

    bool operator==(const Bindings&) const = default;
  };
}
