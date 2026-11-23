/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventPins.h"

#include <algorithm>

#include "../project.h"
#include "../component/components.h"
#include "../canvas/canvas.h"
#include "../../../n64/engine/include/events/pins.h"

int Project::Events::PinList::indexOf(bool input, const std::string &pin) const
{
  const auto &list = input ? in : out;
  auto it = std::find(list.begin(), list.end(), pin);
  if(it == list.end())return -1;
  const int pos = static_cast<int>(it - list.begin());
  const int conditionStart = input ? conditionIn : conditionOut;
  if(conditionStart >= 0 && pos >= conditionStart) return P64::Events::Pins::CONDITION_PIN_BASE + (pos - conditionStart);
  return pos;
}

bool Project::Events::PinList::isConditionPin(bool input, const std::string &pin) const
{
  const int index = indexOf(input, pin);
  return index >= P64::Events::Pins::CONDITION_PIN_BASE;
}

bool Project::Events::PinList::readsConditions(const std::string &pin) const
{
  return std::find(conditionInputs.begin(), conditionInputs.end(), pin) != conditionInputs.end();
}

Project::Events::PinList Project::Events::pinsOf(Project &project, const Component::Entry &comp)
{
  PinList res{};
  if(comp.id == 0) // Code
  {
    auto uuid = Component::Code::getScriptUUID(comp);
    if(auto *script = project.getAssets().getEntryByUUID(uuid)) {
      res.in = script->pins.in;
      res.out = script->pins.out;
      // Fork: conditions, the pins every condition has come after the script's own
      if(script->condition.isCondition) {
        res.conditionIn = static_cast<int>(res.in.size());
        res.conditionOut = static_cast<int>(res.out.size());
        for(const char* name : P64::Events::Pins::CONDITION_IN) res.in.emplace_back(name);
        for(const char* name : P64::Events::Pins::CONDITION_OUT) res.out.emplace_back(name);
        res.conditionInputs = script->condition.inputs;
        res.scriptTriggers = script->condition.triggers;
        // "Auto Check" as the component stores it; the build asks again with instance overrides
        const auto autoCheck = Component::Code::getStoredArg(comp, "conditionAutoCheck");
        if(autoCheck != "0" && autoCheck != "false") res.triggers = res.scriptTriggers;
      }
    }
    return res;
  }

  // engine components: the table the runtime uses too
  if(const auto *pins = P64::Events::Pins::find((uint32_t)comp.id)) {
    for(uint8_t i = 0; i < pins->in.count; ++i) res.in.emplace_back(pins->in.names[i]);
    for(uint8_t i = 0; i < pins->out.count; ++i) res.out.emplace_back(pins->out.names[i]);
  }
  return res;
}

std::string Project::Events::displayName(Project &project, const Component::Entry &comp)
{
  if(comp.id == 0) {
    if(auto *script = project.getAssets().getEntryByUUID(Component::Code::getScriptUUID(comp))) {
      auto name = script->name;
      auto dot = name.rfind('.');
      return dot == std::string::npos ? name : name.substr(0, dot);
    }
  }
  return comp.name;
}

const Project::Events::PinList& Project::Events::objectPins()
{
  static const PinList pins = []{
    PinList res{};
    for(const char* name : P64::Events::Pins::OBJECT_IN) res.in.emplace_back(name);
    for(const char* name : P64::Events::Pins::OBJECT_OUT) res.out.emplace_back(name);
    return res;
  }();
  return pins;
}

bool Project::Events::elementFocusable(const CanvasElement &element)
{
  auto it = element.props.find("focusable");
  return it != element.props.end() && !it->second.isBound
      && it->second.value.is_boolean() && it->second.value.get<bool>();
}

namespace
{
  void focusPins(const Project::CanvasElement &element, std::vector<std::string> &out)
  {
    if(Project::Events::elementFocusable(element)) {
      for(const char* kind : {"Focused.", "Blurred.", "Activated."}) {
        const auto pin = std::string{kind} + element.name;
        if(std::find(out.begin(), out.end(), pin) == out.end()) out.push_back(pin);
      }
    }
    for(const auto &child : element.children) focusPins(child, out);
  }
}

Project::Events::PinList Project::Events::canvasPins(const Canvas &canvas)
{
  PinList res{};
  res.in = {"Show", "Hide"};
  for(const auto &clip : canvas.animations) res.in.push_back("PlayClip." + clip.name);
  for(const auto &clip : canvas.animations) res.in.push_back("StopClip." + clip.name);
  for(const auto &ev : canvas.events) res.in.push_back("Event." + ev.name);

  for(const auto &element : canvas.elements) focusPins(element, res.out);
  for(const auto &clip : canvas.animations) res.out.push_back("ClipFinished." + clip.name);
  for(const auto &clip : canvas.animations) {
    for(const auto &ev : clip.events) {
      if(ev.action != CanvasTimelineEvent::Action::FireOutput || ev.eventName.empty())continue;
      const auto pin = "Marker." + ev.eventName;
      if(std::find(res.out.begin(), res.out.end(), pin) == res.out.end()) res.out.push_back(pin);
    }
  }
  return res;
}
