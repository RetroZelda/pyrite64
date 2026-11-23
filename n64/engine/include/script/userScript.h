/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <libdragon.h>
#include <type_traits>
#include "script/scriptType.h"

// some generic includes most scripts will need
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "assets/assetManager.h"
#include "audio/audioManager.h"
#include "lib/math.h"
#include "collision/shapes.h"
#include "renderer/drawLayer.h"
#include "events/bus.h"

#include "p64/assetTable.h"
#include "p64/sceneTable.h"

#define P64_DATA_COMMON_(TYPE) \
  static_assert(sizeof(Data) < 0xFFFF); \
  constinit uint16_t DATA_SIZE = static_cast<uint16_t>( \
    std::is_empty_v<Data> ? 0 : sizeof(Data) \
  ); \
  constinit const void* DATA_TYPE = &::P64::Script::TYPE_TAG<TYPE>;

/**
 * Defines the data of a script inline.
 */
#define P64_DATA(...) struct Data { __VA_ARGS__ }; \
  P64_DATA_COMMON_(Data)

/**
 * Uses an existing struct (e.g. from a shared header) as the data of a script.
 */
#define P64_DATA_TYPE(T) struct Data : T {}; \
  static_assert(std::is_trivially_copyable_v<T>, "P64_DATA_TYPE: '" #T "' must be trivially copyable"); \
  static_assert(std::is_standard_layout_v<T>, "P64_DATA_TYPE: '" #T "' must be standard-layout"); \
  P64_DATA_COMMON_(T)

// Fork: event graph (docs/todo/event-graph/PLAN.md, section 5.1).

/**
 * Declares the script's input pins, in the order the editor shows them:
 *   P64_EVENTS_IN(Open, Close);
 * Handle them in one function:
 *   void onInput(Object& obj, Data *data, In input, const Events::Event& ev)
 */
#define P64_EVENTS_IN(...) enum class In : uint8_t { __VA_ARGS__ };

/**
 * Declares the script's output pins:
 *   P64_EVENTS_OUT(Opened, Closed);
 * and a fire() for them, which posts every event the pin is bound to:
 *   fire(obj, data, Out::Opened, value);
 */
#define P64_EVENTS_OUT(...) \
  enum class Out : uint8_t { __VA_ARGS__ }; \
  [[maybe_unused]] inline void fire(::P64::Object& obj, const void* data, Out pin, \
                                    uint32_t value = 0, uint16_t instigator = 0) { \
    ::P64::Events::fire(obj, data, static_cast<uint8_t>(pin), value, instigator); \
  }
