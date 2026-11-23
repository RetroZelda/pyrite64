/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Delay. Fires Elapsed a number of seconds after Start, with the value and
// instigator Start received. With Repeat it keeps firing at that interval until Cancel or Reset.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000002
{
  P64_DATA(
    [[P64::Name("Seconds"), P64::Min(0)]]
    float seconds = 1.0f;
    [[P64::Name("Repeat")]]
    bool repeat = false;
    bool running;
    uint16_t instigator;
    float left;
    uint32_t value;
  );

  P64_EVENTS_IN(Start, Cancel, Reset);
  P64_EVENTS_OUT(Elapsed);

  void init(Object& obj, Data *data)
  {
    data->running = false;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input)
    {
      case In::Start:
        data->running = true;
        data->left = data->seconds;
        data->value = ev.value;
        data->instigator = ev.instigator;
        break;
      case In::Cancel:
      case In::Reset:
        data->running = false;
        break;
    }
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(!data->running)return;
    data->left -= deltaTime;
    if(data->left > 0.0f)return;

    if(data->repeat && data->seconds > 0.0f) {
      data->left += data->seconds;
    } else {
      data->running = false;
    }
    fire(obj, data, Out::Elapsed, data->value, data->instigator);
  }
}
