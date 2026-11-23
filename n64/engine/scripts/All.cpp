/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: All. Fires Complete once each of the first "Inputs Needed" inputs (A, B,
// C, D) has received an event, in any order. With Auto Reset it starts over after Complete.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000006
{
  P64_DATA(
    [[P64::Name("Inputs Needed"), P64::Range(1, 4)]]
    uint8_t needed = 2;
    [[P64::Name("Auto Reset")]]
    bool autoReset = true;
    uint8_t received;
    uint8_t done;
  );

  P64_EVENTS_IN(A, B, C, D, Reset);
  P64_EVENTS_OUT(Complete);

  void init(Object& obj, Data *data)
  {
    data->received = 0;
    data->done = 0;
  }

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    if(input == In::Reset) {
      data->received = 0;
      data->done = 0;
      return;
    }
    if(data->done)return;

    const uint8_t needed = data->needed < 1 ? 1 : (data->needed > 4 ? 4 : data->needed);
    const uint8_t mask = (uint8_t)((1u << needed) - 1u);
    data->received |= (uint8_t)(1u << (uint8_t)input);
    if((data->received & mask) != mask)return;

    if(data->autoReset) data->received = 0;
    else data->done = 1;
    fire(obj, data, Out::Complete, ev.value);
  }
}
