/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Counter. Add and Subtract change the count by one; Changed fires with the
// new count, Reached fires each time the count becomes the target. Reset goes back to the start.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000005
{
  P64_DATA(
    [[P64::Name("Start")]]
    int32_t start = 0;
    [[P64::Name("Target")]]
    int32_t target = 1;
    int32_t count;
  );

  P64_EVENTS_IN(Add, Subtract, Reset);
  P64_EVENTS_OUT(Reached, Changed);

  void init(Object& obj, Data *data)
  {
    data->count = data->start;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Add:      ++data->count; break;
      case In::Subtract: --data->count; break;
      case In::Reset:    data->count = data->start; return;
    }
    fire(obj, data, Out::Changed, (uint32_t)data->count);
    if(data->count == data->target) fire(obj, data, Out::Reached, (uint32_t)data->count);
  }
}
