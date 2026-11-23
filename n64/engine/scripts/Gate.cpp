/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Gate. While open, Trigger fires Passed; while closed it fires Blocked.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000008
{
  P64_DATA(
    [[P64::Name("Starts Open")]]
    bool startsOpen = true;
    bool open;
  );

  P64_EVENTS_IN(Trigger, Open, Close, Toggle, Reset);
  P64_EVENTS_OUT(Passed, Blocked);

  void init(Object& obj, Data *data)
  {
    data->open = data->startsOpen;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Trigger: fire(obj, data, data->open ? Out::Passed : Out::Blocked, ev.value); break;
      case In::Open:    data->open = true; break;
      case In::Close:   data->open = false; break;
      case In::Toggle:  data->open = !data->open; break;
      case In::Reset:   data->open = data->startsOpen; break;
    }
  }
}
