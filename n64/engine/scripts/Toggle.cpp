/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Toggle. Trigger fires the current side (A first) and switches sides.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000009
{
  P64_DATA(
    bool isB;
  );

  P64_EVENTS_IN(Trigger, SetA, SetB, Reset);
  P64_EVENTS_OUT(A, B);

  void init(Object& obj, Data *data)
  {
    data->isB = false;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Trigger:
      {
        const Out out = data->isB ? Out::B : Out::A;
        data->isB = !data->isB;
        fire(obj, data, out, ev.value);
        break;
      }
      case In::SetA:
      case In::Reset: data->isB = false; break;
      case In::SetB:  data->isB = true; break;
    }
  }
}
