/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in condition: Xor. True when an odd number of the conditions wired into Inputs is true
// (with two inputs: exactly one of them).
// It reads its inputs when it is checked, and checks itself whenever one of them was checked.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000012
{
  P64_CONDITION_DATA();

  P64_EVENTS_IN(Inputs);
  P64_CONDITION_INPUTS(Inputs);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    Conditions::notify(obj, data, ev);
  }

  bool evaluate(Object& obj, Data *data)
  {
    return Conditions::Inputs{obj, data, In::Inputs}.odd();
  }
}
