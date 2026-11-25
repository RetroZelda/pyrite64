/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 4.7). The pins of every object and of
// the engine components. The editor includes this header too, so pin names and their order (the
// index the build writes) can never differ between the two. Only add pins at the end of a list.
#pragma once
#include <cstddef>
#include <cstdint>

namespace P64::Events::Pins
{
  struct List
  {
    const char* const* names;
    uint8_t count;
  };

  struct ComponentPins
  {
    uint8_t compId;
    List in;
    List out;
  };

  // every object, whatever its components
  inline constexpr const char* OBJECT_IN[]  = {"Enable", "Disable", "ToggleEnabled", "Destroy", "Show", "Hide"};
  inline constexpr const char* OBJECT_OUT[] = {"Ready", "Enabled", "Disabled", "Destroyed"};
  enum class ObjectIn : uint8_t { Enable, Disable, ToggleEnabled, Destroy, Show, Hide };
  enum class ObjectOut : uint8_t { Ready, Enabled, Disabled, Destroyed };

  // Collider (5) and Collision-Mesh (4). Value and instigator: the other object's id.
  inline constexpr const char* COLLIDER_OUT[] = {"Enter", "Exit", "Occupied", "Emptied"};
  enum class ColliderOut : uint8_t { Enter, Exit, Occupied, Emptied };

  // Audio (2D) (6)
  inline constexpr const char* AUDIO_IN[]  = {"Play", "Stop"};
  inline constexpr const char* AUDIO_OUT[] = {"Finished"};
  enum class AudioIn : uint8_t { Play, Stop };
  enum class AudioOut : uint8_t { Finished };

  // Particle Emitter (24)
  inline constexpr const char* PARTICLE_IN[] = {"Play", "Stop"};
  enum class ParticleIn : uint8_t { Play, Stop };

  // Model (Animated) (10). Inputs take the clip index as value.
  inline constexpr const char* ANIM_IN[]  = {"PlayMain", "PlayBlend"};
  inline constexpr const char* ANIM_OUT[] = {"MainFinished"};
  enum class AnimIn : uint8_t { PlayMain, PlayBlend };
  enum class AnimOut : uint8_t { MainFinished };

  // Fork: animation events (docs/todo/anim-events/PLAN.md). A Model (Animated) component also has
  // one output pin per animation event that its model lists in the project's event catalog
  // ("animationEvents" in events/*.json). Game code fires them with AnimModel::fireEvent(). Their
  // index is ANIM_EVENT_PIN_BASE + the position in that list, so the outputs above can still grow.
  inline constexpr uint8_t ANIM_EVENT_PIN_BASE = 0x80;
  inline constexpr uint8_t ANIM_EVENTS_MAX = 64; // per model

  // Camera (3)
  inline constexpr const char* CAMERA_IN[] = {"Activate"};
  enum class CameraIn : uint8_t { Activate };

  // Light (2)
  inline constexpr const char* LIGHT_IN[] = {"TurnOn", "TurnOff"};
  enum class LightIn : uint8_t { TurnOn, TurnOff };

  // Fork: conditions (docs/todo/conditionals/PLAN.md). A script declared with P64_CONDITION_DATA
  // has these pins besides its own. Their index is CONDITION_PIN_BASE + the position here, so they
  // never collide with the script's own pins (which count from 0).
  inline constexpr uint8_t CONDITION_PIN_BASE = 0x80;
  inline constexpr const char* CONDITION_IN[]  = {"Enable", "Disable", "Check", "Reset"};
  inline constexpr const char* CONDITION_OUT[] = {"True", "False", "NewlyTrue", "NewlyFalse", "Result"};
  enum class ConditionIn : uint8_t { Enable, Disable, Check, Reset };
  enum class ConditionOut : uint8_t { True, False, NewlyTrue, NewlyFalse, Result };
  inline constexpr List CONDITION_IN_LIST{CONDITION_IN, 4};
  inline constexpr List CONDITION_OUT_LIST{CONDITION_OUT, 5};

  inline constexpr ComponentPins COMPONENTS[] = {
    {2,  {LIGHT_IN, 2},    {nullptr, 0}},
    {3,  {CAMERA_IN, 1},   {nullptr, 0}},
    {4,  {nullptr, 0},     {COLLIDER_OUT, 4}},
    {5,  {nullptr, 0},     {COLLIDER_OUT, 4}},
    {6,  {AUDIO_IN, 2},    {AUDIO_OUT, 1}},
    {10, {ANIM_IN, 2},     {ANIM_OUT, 1}},
    {24, {PARTICLE_IN, 2}, {nullptr, 0}},
  };

  inline constexpr List OBJECT_IN_LIST{OBJECT_IN, 6};
  inline constexpr List OBJECT_OUT_LIST{OBJECT_OUT, 4};

  /** The pins of an engine component, or nullptr if it has none. */
  constexpr const ComponentPins* find(uint32_t compId)
  {
    for(const auto &entry : COMPONENTS) {
      if(entry.compId == compId) return &entry;
    }
    return nullptr;
  }
}
