/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include "event.h"
#include "events/types.h"

namespace P64::Coll
{
  struct CollEvent;
}

namespace P64
{
  class Object;

  typedef uint32_t(*FuncGetAllocSize)(void*);
  typedef void(*FuncInitDel)(Object&, void*, void*);
  typedef void(*FuncEnabled)(Object&, void*);
  typedef void(*FuncDisabled)(Object&, void*);
  typedef void(*FuncReady)(Object&, void*);
  typedef void(*FuncUpdate)(Object&, void*, float deltaTime);
  typedef void(*FuncFixedUpdate)(Object&, void*, float fixedDeltaTime);
  typedef void(*FuncDraw)(Object&, void*, float deltaTime);
  typedef void(*FuncOnEvent)(Object&, void*, const ObjectEvent&);
  typedef void(*FuncOnColl)(Object&, void*, const P64::Coll::CollEvent&);
  // Fork: event graph, an input pin of the component received an event
  typedef void(*FuncOnInput)(Object&, void*, uint8_t pin, const Events::Event&);

  struct ComponentDef
  {
    FuncInitDel initDel{};
    FuncEnabled onEnable{};
    FuncDisabled onDisable{};
    FuncReady onReady{};
    FuncUpdate update{};
    FuncFixedUpdate fixedUpdate{};
    FuncDraw draw{};
    FuncOnEvent onEvent{};
    FuncOnColl onColl{};
    FuncOnInput onInput{};
    FuncGetAllocSize getAllocSize{};
  };

  // 0..23 belong to upstream components, 24+ are reserved for the fork's own components
  constexpr uint32_t COMP_TABLE_SIZE = 32;
  extern const ComponentDef COMP_TABLE[COMP_TABLE_SIZE];
}
