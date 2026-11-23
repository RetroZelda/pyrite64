/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Sequence. Each Next fires the next step output, Step1 first. After the
// last step it starts over with Loop, otherwise further Next events do nothing until Reset.
#include "script/userScript.h"

namespace P64::Script::C0E000000000000A
{
  P64_DATA(
    [[P64::Name("Steps"), P64::Range(1, 8)]]
    uint8_t steps = 2;
    [[P64::Name("Loop")]]
    bool loop = true;
    uint8_t index;
    uint8_t pad;
  );

  P64_EVENTS_IN(Next, Reset);
  P64_EVENTS_OUT(Step1, Step2, Step3, Step4, Step5, Step6, Step7, Step8);

  void init(Object& obj, Data *data)
  {
    data->index = 0;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Reset) {
      data->index = 0;
      return;
    }
    const uint8_t steps = data->steps < 1 ? 1 : (data->steps > 8 ? 8 : data->steps);
    if(data->index >= steps)return; // finished, not looping

    const auto step = (Out)data->index;
    ++data->index;
    if(data->index >= steps && data->loop) data->index = 0;
    fire(obj, data, step, ev.value);
  }
}
