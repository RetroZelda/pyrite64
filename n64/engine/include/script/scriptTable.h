/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include "nodeGraph.h"
#include "../events/types.h"

namespace P64 { class Object; struct ObjectEvent; }
namespace P64::Coll { struct CollEvent; }

namespace P64::Script
{
  typedef void(*FuncObjInit)(Object&, void*);
  typedef void(*FuncObjDataDelta)(Object&, void*, float);
  typedef void(*FuncObjDataEvent)(Object&, void*, const ObjectEvent&);
  typedef void(*FuncObjDataColl)(Object&, void*, const P64::Coll::CollEvent&);
  typedef void(*FuncObjDataInput)(Object&, void*, uint8_t, const Events::Event&);
  typedef void(*FuncObjContact)(Object&, void*, uint16_t otherId);
  typedef bool(*FuncObjCondition)(Object&, void*);

  struct ScriptEntry
  {
    FuncObjInit init;
    FuncObjInit destroy;
    FuncObjInit onEnable;
    FuncObjInit onDisable;
    FuncObjInit onReady;
    FuncObjDataDelta update;
    FuncObjDataDelta fixedUpdate;
    FuncObjDataDelta draw;
    FuncObjDataEvent onEvent;
    FuncObjDataColl onColl;

    // UI component-script callbacks (used by the canvas focus system, Object-script
    // path ignores them). onFocus/onBlur fire when focus enters/leaves a focusable
    // UI element; onActivate fires on the A button while focused.
    FuncObjInit onFocus;
    FuncObjInit onBlur;
    FuncObjInit onActivate;

    const void* const* dataType; // points to the 'DATA_TYPE' of the script, see 'TYPE_TAG'

    // Fork: event graph. Input pins declared with P64_EVENTS_IN, and their names hashed
    // (Events::pinHash) in declaration order, so send() can find an input by name.
    FuncObjDataInput onInput;
    const uint32_t* inputHashes;
    uint8_t inputCount;

    // Fork: event graph. Collision enter/exit, see events/contactEvents.h.
    FuncObjContact onCollEnter;
    FuncObjContact onCollExit;

    // Fork: conditions. Set for scripts declared with P64_CONDITION_DATA, see events/conditions.h.
    FuncObjCondition evaluate;
  };

  // Note: generated and implement in the project:
  ScriptEntry &getCodeByIndex(uint32_t idx);
  uint16_t getCodeSizeByIndex(uint32_t idx);
  NodeGraph::GraphFunc getGraphFuncByUUID(uint64_t uuid);
}

namespace P64::Script {
    extern const char* const scriptNames[];
    extern const size_t numNames;
}