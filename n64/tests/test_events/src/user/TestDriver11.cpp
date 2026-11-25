#include "script/userScript.h"
#include "scene/sceneManager.h"
#include "scene/components/animModel.h"
#include "./testLog.h"

// Drives scene 11 (docs/todo/anim-events/PLAN.md): fires the animation-event pins of three Model
// (Animated) objects from code, the way a game's animation code does. See checkAnimEvents() in
// EventTests.cpp for what each frame must cause.
namespace P64::Script::CA0E11E5A1B2C3D4
{
  using namespace P64::User::EventTest;

  P64_DATA( uint32_t pad; );

  // runtime ids, in the order of the scene's objects
  enum : uint16_t { ANIM_A = 3, ANIM_B, ANIM_C };
  constexpr uint16_t INSTIGATOR = 9;
  constexpr int16_t CLIP_WALK = 1;

  // pins of the model in events/anim_test.json; a game makes these its own way (Kentucky Castle's
  // trait compiler writes them), the engine only needs the hash of the full pin name
  constexpr Events::PinName WALK_STEP{Events::pinHash("Walk.STEP", 9)};
  constexpr Events::PinName ANY_STEP{Events::pinHash("Any.STEP", 8)};
  constexpr Events::PinName NOT_LISTED{Events::pinHash("Walk.JUMP", 9)};

  Comp::AnimModel* animOf(uint16_t id, Object* &obj)
  {
    obj = SceneManager::getCurrent().getObjectById(id);
    return obj ? obj->getComponent<Comp::AnimModel>() : nullptr;
  }

  void fireOn(uint16_t id, Events::PinName pin, uint32_t value, uint16_t call)
  {
    Object* obj = nullptr;
    auto *anim = animOf(id, obj);
    const bool fired = anim && Comp::AnimModel::fireEvent(*obj, anim, pin, value, INSTIGATOR);
    add(Rec::AnimFired, fired ? 1 : 0, call);
  }

  void update([[maybe_unused]] Object& obj, [[maybe_unused]] Data *data, [[maybe_unused]] float deltaTime)
  {
    if(activeTest != T_ANIM_EVENTS) return;
    switch(currFrame)
    {
      case 2:
        fireOn(ANIM_A, WALK_STEP, 11, 1);
        fireOn(ANIM_A, NOT_LISTED, 12, 2); // the model does not list it
        fireOn(ANIM_B, WALK_STEP, 21, 3);  // B's own binding, not A's
        fireOn(ANIM_C, WALK_STEP, 31, 4);  // listed, but nothing is bound on C
        fireOn(ANIM_A, ANY_STEP, 13, 5);
        break;
      case 3: // A plays Walk once: its MainFinished (pin 0) must still fire
      {
        Object* a = nullptr;
        if(auto *anim = animOf(ANIM_A, a)) {
          anim->setMainAnim(CLIP_WALK);
          if(auto *clip = anim->getMainAnim()) {
            t3d_anim_set_looping(clip, false);
            t3d_anim_set_time(clip, 0.0f);
            t3d_anim_set_playing(clip, true);
          }
        }
      } break;
      default: break;
    }
  }
}
