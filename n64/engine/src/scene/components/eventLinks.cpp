/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph, see eventLinks.h.
#include "scene/components/eventLinks.h"

#include <cstring>

#include "scene/componentTable.h"
#include "scene/scene.h"

namespace
{
  // a bound input pin received its event: hand it to the component that owns the pin
  void onBoundInput(const P64::Events::Event& ev, void* user)
  {
    auto &binding = *static_cast<P64::Comp::EventLinks::Binding*>(user);
    P64::Object &obj = *binding.owner;

    if(binding.rec.comp == P64::Comp::EventLinks::COMP_OBJECT) {
      P64::Events::Internal::objectInput(obj, binding.rec.pin, ev);
      return;
    }
    if(binding.rec.comp >= obj.compCount) return;

    const auto &ref = obj.getCompRefs()[binding.rec.comp];
    const auto &def = P64::COMP_TABLE[ref.type];
    if(def.onInput) {
      def.onInput(obj, reinterpret_cast<char*>(&obj) + ref.offset, binding.rec.pin, ev);
    }
  }
}

void P64::Comp::EventLinks::initDelete(Object& obj, EventLinks* data, uint16_t* initData)
{
  if(initData == nullptr)
  {
    for(uint32_t i = 0; i < data->count; ++i) {
      auto &binding = data->bindings()[i];
      if(binding.isInput()) Events::unlisten(binding.listener);
    }
    return;
  }

  data->count = initData[0];
  const auto* records = reinterpret_cast<const uint8_t*>(initData) + 4;

  for(uint32_t i = 0; i < data->count; ++i)
  {
    auto &binding = data->bindings()[i];
    memcpy(&binding.rec, records + i * sizeof(Record), sizeof(Record));
    binding.owner = &obj;
    binding.listener = {};
    binding.instance = Events::Internal::resolveInstance(obj, binding.scope(), binding.rec.up);

    if(binding.isInput()) {
      binding.listener = Events::Internal::listenBound(
        binding.rec.hash, binding.scope(), binding.instance, onBoundInput, &binding);
    }
  }
}
