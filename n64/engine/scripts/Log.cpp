/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Built-in event object: Log. Prints what Trigger received to the debug log.
#include "script/userScript.h"
#include <cstring>

namespace P64::Script::C0E000000000000C
{
  P64_DATA(
    [[P64::Name("Text")]]
    char text[32];
  );

  P64_EVENTS_IN(Trigger);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    const char* name = Events::nameOf(ev.id);
    char text[33];
    memcpy(text, data->text, 32);
    text[32] = '\0';
    debugf("[Log %u] %s %s value=%lu sender=%u instigator=%u\n", (unsigned)obj.id, text,
      name ? name : "", (unsigned long)ev.value, (unsigned)ev.sender, (unsigned)ev.instigator);
  }
}
