#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "./testLog.h"

// Drives scene 10: sets the "game state" the test conditions answer from, fires their trigger
// events and sends to their pins by name. See checkConditions() in EventTests.cpp for what each
// frame must cause.
namespace P64::Script::C5A1B2C3D4E5F614
{
  using namespace P64::User::EventTest;

  P64_DATA( uint32_t pad; );

  // runtime ids, in the order of the scene's objects
  enum : uint16_t { COND_A = 3, COND_B, COND_M, AND, OR, XOR, NOT, NAND, ORPHAN, GATED, K10, K20, SPAWNER, GRAPH, COND_G, QUIET };
  enum : uint32_t { SLOT_A = 0, SLOT_B, SLOT_M, SLOT_GATED, SLOT_G = 5 };

  void to(uint16_t id, Events::PinName pin, uint32_t value = 0)
  {
    Events::send(id, pin, value, 2);
  }

  void update(Object& obj, Data *data, float deltaTime)
  {
    if(activeTest != T_CONDITIONS) return;
    auto &scene = SceneManager::getCurrent();
    switch(currFrame)
    {
      case 1:
        for(auto &slot : condSlots) slot = false;
        kitValue = 0;
        // CondG is checked by scene::C.GraphKick, which the node graph of 'Graph' fires as it starts
        condSlots[SLOT_G] = true;
        break;
      case 2: // A and B are false
        Events::post(COND_TICK, 5, 2);
        break;
      case 3: // A becomes true
        condSlots[SLOT_A] = true;
        Events::post(COND_TICK, 6, 2);
        break;
      case 4: // B becomes true
        condSlots[SLOT_B] = true;
        Events::post(COND_TICK, 7, 2);
        break;
      case 5: // A is disabled and becomes false: it fires nothing, but And still reads it
        to(COND_A, "Disable"_pin);
        condSlots[SLOT_A] = false;
        Events::post(COND_TICK, 8, 2);
        break;
      case 6: // enabled and reset: the true answer is new again
        to(COND_A, "Enable"_pin);
        to(COND_A, "Reset"_pin);
        condSlots[SLOT_A] = true;
        Events::post(COND_TICK, 9, 2);
        break;
      case 7: // manual checks, a name nothing provides, Enable and Check on one event
        condSlots[SLOT_M] = true;
        to(COND_M, "Check"_pin, 77);
        to(ORPHAN, "Check"_pin, 1);
        condSlots[SLOT_GATED] = true;
        Events::post(COND_KICK, 9, 2);
        // Quiet has A's script and slot with "Auto Check" off: the ticks never checked it, this does
        to(QUIET, "Check"_pin, 4);
        break;
      case 8: // from code: evaluate() answers at once and fires nothing
        if(auto *m = scene.getObjectById(COND_M)) {
          condSlots[SLOT_M] = false;
          add(Rec::CondEval, Conditions::evaluate(*m) ? 1 : 0, COND_M);
          condSlots[SLOT_M] = true;
          add(Rec::CondEval, Conditions::evaluate(*m) ? 1 : 0, COND_M);
        }
        add(Rec::CondEval, Conditions::evaluate(obj) ? 1 : 0, obj.id); // not a condition: false
        // a logic node reads its input now, though M itself is not checked
        condSlots[SLOT_M] = false;
        to(NOT, "Check"_pin, 3);
        break;
      case 9: // check() from code is the Check input
        condSlots[SLOT_M] = true;
        if(auto *m = scene.getObjectById(COND_M)) Conditions::check(*m, 5);
        break;
      case 10: // prefab instances: each Not reads the condition of its own instance
        kitValue = 10;
        Events::post(KIT_TICK, 0, 2);
        to(SPAWNER, "Spawn"_pin);
        break;
      case 11:
        kitValue = 20;
        Events::post(KIT_TICK, 0, 2);
        break;
      case 12: // the spawned instance (at the spawner, x = 30) exists by now
        kitValue = 30;
        Events::post(KIT_TICK, 0, 2);
        break;
      default: break;
    }
  }
}
