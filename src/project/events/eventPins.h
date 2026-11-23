/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph. The input and output pins a component or an object offers.
#pragma once
#include <string>
#include <vector>

namespace Project { class Project; struct Canvas; struct CanvasElement; }
namespace Project::Component { struct Entry; }

namespace Project::Events
{
  struct PinList
  {
    std::vector<std::string> in{};
    std::vector<std::string> out{};

    // Fork: conditions (docs/todo/conditionals/PLAN.md). A condition script's lists end with the
    // condition pins; these are the positions they start at (-1: not a condition).
    int conditionIn{-1};
    int conditionOut{-1};
    std::vector<std::string> conditionInputs{}; // input pins that read conditions (P64_CONDITION_INPUTS)
    std::vector<std::string> scriptTriggers{};  // events the script binds to Check (P64_CONDITION_TRIGGERS)
    std::vector<std::string> triggers{};        // the same, or none when this component has "Auto Check" off

    [[nodiscard]] bool empty() const { return in.empty() && out.empty(); }
    [[nodiscard]] bool isCondition() const { return conditionOut >= 0; }

    /**
     * The pin's index as the runtime knows it, -1 if there is no such pin: the position in the
     * list, and for a condition pin P64::Events::Pins::CONDITION_PIN_BASE + its position among them.
     */
    [[nodiscard]] int indexOf(bool input, const std::string &pin) const;

    /** 'pin' is one of the pins every condition has (not one the script declares itself). */
    [[nodiscard]] bool isConditionPin(bool input, const std::string &pin) const;

    /** The input pin reads conditions: Result outputs are wired into it. */
    [[nodiscard]] bool readsConditions(const std::string &pin) const;
  };

  /** Pins of a component: a Code component's script pins, or an engine component's own. */
  PinList pinsOf(Project &project, const Component::Entry &comp);

  /** Pins every object has, whatever its components (Enable, Ready, ...). */
  const PinList& objectPins();

  /** A component's name for messages and nodes: the script name for a Code component. */
  std::string displayName(Project &project, const Component::Entry &comp);

  /**
   * Pins of a UI canvas (PLAN 9), from its content. Inputs: Show, Hide, PlayClip.<clip>,
   * StopClip.<clip>, Event.<event>. Outputs: Focused/Blurred/Activated.<element> for focusable
   * elements, ClipFinished.<clip>, Marker.<name> for "Fire output" timeline markers. Clips are the
   * canvas's own (not element clips); the order is fixed, generated code indexes it.
   */
  PinList canvasPins(const Canvas &canvas);

  /** A canvas element is focusable (its "focusable" property is set, not bound). */
  bool elementFocusable(const CanvasElement &element);
}
