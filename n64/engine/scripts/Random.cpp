/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Random. Trigger fires one of the first "Outputs" outputs at random.
// With No Repeats the same output never fires twice in a row.
#include "script/userScript.h"
#include <cstdlib>

namespace P64::Script::C0E000000000000B
{
  P64_DATA(
    [[P64::Name("Outputs"), P64::Range(1, 4)]]
    uint8_t outputs = 2;
    [[P64::Name("No Repeats")]]
    bool noRepeats = false;
    uint8_t last;
    uint8_t pad;
  );

  P64_EVENTS_IN(Trigger, Reset);
  P64_EVENTS_OUT(Out1, Out2, Out3, Out4);

  void init(Object& obj, Data *data)
  {
    data->last = 0xFF;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Reset) {
      data->last = 0xFF;
      return;
    }
    const uint32_t outputs = data->outputs < 1 ? 1 : (data->outputs > 4 ? 4 : data->outputs);
    uint32_t pick = (uint32_t)rand() % outputs;
    if(data->noRepeats && outputs > 1 && pick == data->last) {
      pick = (pick + 1 + (uint32_t)rand() % (outputs - 1)) % outputs;
    }
    data->last = (uint8_t)pick;
    fire(obj, data, (Out)pick, ev.value);
  }
}
