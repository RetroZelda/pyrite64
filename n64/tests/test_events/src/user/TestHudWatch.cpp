#include "script/userScript.h"
#include "./testLog.h"

// Hears the TestHud canvas's outputs in scene 9.
namespace P64::Script::C5A1B2C3D4E5F60A
{
  using namespace P64::User::EventTest;
  P64_DATA( uint32_t pad; );
  P64_EVENTS_IN(Focused, Mid, Done);

  void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
  {
    switch(input) {
      case In::Focused: add(Rec::HudFocused, ev.value, obj.id); break;
      case In::Mid:     add(Rec::HudMid, ev.value, obj.id); break;
      case In::Done:    add(Rec::HudDone, ev.value, obj.id); break;
    }
  }
}
