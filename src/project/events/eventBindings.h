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

  /**
   * A change to one prefab instance, stored on the instance node (PLAN 6.2). Either a binding
   * change on a pin inside the instance, or a repointed parent link (PLAN 3.4).
   *   {"path": [uuid, ...], "comp": uuid, "pin": "Open", "dir": "in", "add": [...], "mute": [...]}
   *   {"link": "prefab_1::Alarm", "to": "prefab_1::Alarm"}
   */
  struct Override
  {
    std::vector<uint32_t> path{}; // object uuids from the instance root down to the target; empty: the root
    uint64_t comp{0};             // component uuid; 0: the object's own pins
    std::string pin{};
    bool input{true};
    std::vector<std::string> add{};
    std::vector<std::string> mute{};

    std::string link{}; // a numbered name as the instance's prefab writes it
    std::string to{};   // the name it means in the graph that contains the instance

    std::string hint{}; // for people reading the file; rewritten on save, ignored on load

    [[nodiscard]] bool isLink() const { return !link.empty(); }
    [[nodiscard]] bool empty() const { return isLink() ? false : (add.empty() && mute.empty()); }
    bool operator==(const Override&) const = default;
  };

  /** A pin of an object inside a prefab, shown on the prefab's instance nodes in outer graphs. */
  struct Exposed
  {
    std::vector<uint32_t> path{};
    uint64_t comp{0};
    std::string pin{};
    bool input{true};
    std::string alias{};
    bool operator==(const Exposed&) const = default;
  };

  nlohmann::json overridesToJson(const std::vector<Override> &list);
  std::vector<Override> overridesFromJson(const nlohmann::json &doc);
  nlohmann::json exposedToJson(const std::vector<Exposed> &list);
  std::vector<Exposed> exposedFromJson(const nlohmann::json &doc);
}
