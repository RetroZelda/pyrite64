/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Once. Passes the first Trigger on to Fired and ignores the rest until Reset.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000004
{
  P64_DATA(
    bool fired;
  );

  P64_EVENTS_IN(Trigger, Reset);
  P64_EVENTS_OUT(Fired);

  void init(Object& obj, Data *data)
  {
    data->fired = false;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Reset) {
      data->fired = false;
      return;
    }
    if(data->fired)return;
    data->fired = true;
    fire(obj, data, Out::Fired, ev.value);
  }
}
