/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include "event.h"

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
  typedef void(*FuncUpdate)(Object&, void*, float deltaTime);
  typedef void(*FuncFixedUpdate)(Object&, void*, float fixedDeltaTime);
  typedef void(*FuncDraw)(Object&, void*, float deltaTime);
  typedef void(*FuncOnEvent)(Object&, void*, const ObjectEvent&);
  typedef void(*FuncOnColl)(Object&, void*, const P64::Coll::CollEvent&);

  struct ComponentDef
  {
    FuncInitDel initDel{};
    FuncEnabled onEnable{};
    FuncDisabled onDisable{};
    FuncUpdate update{};
    FuncFixedUpdate fixedUpdate{};
    FuncDraw draw{};
    FuncOnEvent onEvent{};
    FuncOnColl onColl{};
    FuncGetAllocSize getAllocSize{};
  };

  constexpr uint32_t COMP_TABLE_SIZE = 16;
  extern const ComponentDef COMP_TABLE[COMP_TABLE_SIZE];
}
