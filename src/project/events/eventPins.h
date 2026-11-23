/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph. The input and output pins a component or an object offers.
#pragma once
#include <string>
#include <vector>

namespace Project { class Project; }
namespace Project::Component { struct Entry; }

namespace Project::Events
{
  struct PinList
  {
    std::vector<std::string> in{};
    std::vector<std::string> out{};

    [[nodiscard]] bool empty() const { return in.empty() && out.empty(); }
    [[nodiscard]] int indexOf(bool input, const std::string &pin) const;
  };

  /** Pins of a component: a Code component's script pins, or an engine component's own. */
  PinList pinsOf(Project &project, const Component::Entry &comp);

  /** Pins every object has, whatever its components (Enable, Ready, ...). */
  const PinList& objectPins();

  /** A component's name for messages and nodes: the script name for a Code component. */
  std::string displayName(Project &project, const Component::Entry &comp);
}
