/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventPins.h"

#include <algorithm>

#include "../project.h"
#include "../component/components.h"

int Project::Events::PinList::indexOf(bool input, const std::string &pin) const
{
  const auto &list = input ? in : out;
  auto it = std::find(list.begin(), list.end(), pin);
  return it == list.end() ? -1 : static_cast<int>(it - list.begin());
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
    }
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
  static const PinList pins{};
  return pins;
}
