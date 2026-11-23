#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// Drives the built-in objects of scene 5 with send(), by input pin name.
namespace P64::Script::C405162738495A6B
{
  using namespace P64::User::EventTest;

  P64_DATA(
    uint32_t pad;
  );

  void to(uint16_t id, Events::PinName pin, uint32_t value = 0)
  {
    if(auto *target = SceneManager::getCurrent().getObjectById(id)) Events::send(*target, pin, value, 2);
  }

  enum : uint16_t { ONCE = 3, COUNTER, ALL, COMPARE, GATE, TOGGLE, SEQUENCE, RANDOM, DELAY, RELAY, LOG, BRIDGE_A, BRIDGE_B, SPAWNER, FRAME_DELAY };

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_LOGIC) return;
    switch(currFrame)
    {
      case 2:
        to(ONCE, "Trigger"_pin, 1);
        to(COUNTER, "Add"_pin);
        to(ALL, "A"_pin);
        to(COMPARE, "Test"_pin, 5);
        to(GATE, "Trigger"_pin, 1);
        to(TOGGLE, "Trigger"_pin);
        to(SEQUENCE, "Next"_pin);
        to(RANDOM, "Trigger"_pin);
        to(DELAY, "Start"_pin, 9);
        to(RELAY, "Trigger"_pin, 4);
        to(LOG, "Trigger"_pin, 123);
        to(BRIDGE_A, "Trigger"_pin, 77);
        to(SPAWNER, "Spawn"_pin);
        to(FRAME_DELAY, "Start"_pin, 55);
        break;
      case 3:
        to(ONCE, "Trigger"_pin, 2);
        to(COUNTER, "Add"_pin);
        to(ALL, "B"_pin);
        to(COMPARE, "Test"_pin, 10);
        to(GATE, "Close"_pin);
        to(TOGGLE, "Trigger"_pin);
        to(SEQUENCE, "Next"_pin);
        to(RELAY, "Disable"_pin);
        break;
      case 4:
        to(ONCE, "Reset"_pin);
        to(COUNTER, "Subtract"_pin);
        to(ALL, "B"_pin);
        to(COMPARE, "Test"_pin, 20);
        to(GATE, "Trigger"_pin, 2);
        to(TOGGLE, "Trigger"_pin);
        to(SEQUENCE, "Next"_pin);
        to(RELAY, "Trigger"_pin, 5);
        break;
      case 5:
        to(ONCE, "Trigger"_pin, 3);
        to(COUNTER, "Reset"_pin);
        to(ALL, "A"_pin);
        to(GATE, "Reset"_pin);
        to(TOGGLE, "Reset"_pin);
        to(SEQUENCE, "Next"_pin);
        to(RELAY, "Enable"_pin);
        break;
      case 6:
        to(GATE, "Trigger"_pin, 3);
        to(TOGGLE, "Trigger"_pin);
        to(RELAY, "Trigger"_pin, 6);
        break;
      default: break;
    }
  }
}
