/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Compare. Compares the value of Test (as a signed number) with a constant
// and fires Equal, Less or Greater, passing the value on.
#include "script/userScript.h"

namespace P64::Script::C0E0000000000007
{
  P64_DATA(
    [[P64::Name("Compare With")]]
    int32_t constant = 0;
  );

  P64_EVENTS_IN(Test);
  P64_EVENTS_OUT(Equal, Less, Greater);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    const auto value = (int32_t)ev.value;
    const Out out = value == data->constant ? Out::Equal : (value < data->constant ? Out::Less : Out::Greater);
    fire(obj, data, out, ev.value);
  }
}
