/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in condition: Or. True when at least one condition wired into Inputs is true.
// It reads its inputs when it is checked, and checks itself whenever one of them was checked.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000011
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
    return Conditions::Inputs{obj, data, In::Inputs}.any();
  }
}
