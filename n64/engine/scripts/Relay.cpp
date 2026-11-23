/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Relay (an "event repeater"). Passes Trigger on to Fired while enabled.
// Bind Fired to several events to fan one event out, for example a scene event into a prefab's own.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000001
{
  P64_DATA(
    [[P64::Name("Starts Enabled")]]
    bool startsEnabled = true;
    bool enabled;
  );

  P64_EVENTS_IN(Trigger, Enable, Disable, Reset);
  P64_EVENTS_OUT(Fired);

  void init(Object& obj, Data *data)
  {
    data->enabled = data->startsEnabled;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Trigger: if(data->enabled) fire(obj, data, Out::Fired, ev.value); break;
      case In::Enable:  data->enabled = true; break;
      case In::Disable: data->enabled = false; break;
      case In::Reset:   data->enabled = data->startsEnabled; break;
    }
  }
}
