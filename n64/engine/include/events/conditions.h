/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: conditions (docs/todo/conditionals/PLAN.md). A condition is a script with
//   bool evaluate(Object& obj, Data *data)
// declared with P64_CONDITION_DATA (script/userScript.h). It answers a yes/no question about the
// game; the engine gives it the pins Enable, Disable, Check, Reset and True, False, NewlyTrue,
// NewlyFalse, Result (events/pins.h) and remembers only the last answer.
//
//   bool low = P64::Conditions::evaluate(obj);   // the answer right now, nothing is fired
//   P64::Conditions::check(obj);                 // like the Check input: fires the outputs
#pragma once
#include "bus.h"
#include "conditionTypes.h"

namespace P64::Comp { struct Code; }

namespace P64::Conditions
{
  /**
   * The answer of the condition on 'obj' right now (its first condition script). Fires nothing and
   * changes nothing, also when the condition is disabled. False if 'obj' has no condition.
   */
  bool evaluate(Object& obj);

  /**
   * Fork: dialog trees. Runs a condition script by its index in the script table, with a values
   * blob baked elsewhere (a dialog tree's Condition node), on 'obj'. Fires nothing; false when
   * the script is not a condition.
   */
  bool evaluateScript(uint16_t scriptIndex, void* data, Object& obj);

  /**
   * Evaluates every condition on 'obj' and fires its outputs, like the Check input. A disabled
   * condition does nothing.
   * @param value passed on by True, False, NewlyTrue and NewlyFalse
   */
  void check(Object& obj, uint32_t value = 0);

  /** Like the Enable and Disable inputs. */
  void setEnabled(Object& obj, bool enabled);

  /** Like the Reset input: the last answer is false again, enabled as "Starts Enabled" says. */
  void reset(Object& obj);

  /** Implementation of notify(), 'data' is the script's Data. */
  void notifyData(Object& obj, void* data, const Events::Event& ev);

  /**
   * For a condition that combines other conditions: call it from onInput when an input pin that
   * reads conditions received an event. The condition then checks itself once the events of this
   * moment are handled, so several inputs changing together cause one check. That check's True,
   * False, NewlyTrue and NewlyFalse carry the answer as their value: 1 or 0.
   */
  template<typename Data>
  inline void notify(Object& obj, Data* data, const Events::Event& ev) { notifyData(obj, data, ev); }

  /**
   * The conditions wired into an input pin of a combining condition (mark the pin with
   * P64_CONDITION_INPUTS). Each one is evaluated on the spot:
   *   bool evaluate(Object& obj, Data *data) { return Conditions::Inputs{obj, data, In::Inputs}.all(); }
   * A binding whose name no condition provides counts as one false input.
   */
  struct Inputs
  {
    uint16_t total{0};  // inputs found
    uint16_t truthy{0}; // how many of them are true

    Inputs(Object& obj, const void* data, uint8_t pin);

    template<typename Pin>
    Inputs(Object& obj, const void* data, Pin pin) : Inputs(obj, data, static_cast<uint8_t>(pin)) {}

    [[nodiscard]] bool all() const  { return total > 0 && truthy == total; }
    [[nodiscard]] bool any() const  { return truthy > 0; }
    [[nodiscard]] bool odd() const  { return (truthy & 1) != 0; }
    [[nodiscard]] bool none() const { return truthy == 0; }
  };

  /** Counters for debugging and tests. */
  struct Stats
  {
    uint32_t providers;     // bound Result outputs of live conditions
    uint32_t providersPeak;
    uint32_t maxProviders;  // P64_CONDITION_MAX_PROVIDERS
    uint32_t checks;        // checks that evaluated, since the scene loaded
  };
  Stats getStats();

  /** Engine-internal entry points, called by the Code component and the bus. */
  namespace Internal
  {
    void onCodeInit(Object& obj, Comp::Code& code);    // a condition script was created
    void onCodeDestroy(Object& obj, Comp::Code& code);
    /** A condition pin received an event; 'pin' is the position in Pins::CONDITION_IN. */
    void onPin(Object& obj, Comp::Code& code, uint8_t pin, const Events::Event& ev);
    /** send() to a condition script by pin name, and the internal re-check. @return calls made */
    uint32_t onDirect(Object& obj, Comp::Code& code, const Events::Event& ev);
    void onSceneDestroyed();
  }
}
