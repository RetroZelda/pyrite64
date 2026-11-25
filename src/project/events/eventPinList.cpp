/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph. The PinList methods, with no project code, so the host unit tests use them too.
#include "eventPins.h"

#include <algorithm>

#include "../../../n64/engine/include/events/pins.h"

int Project::Events::PinList::indexOf(bool input, const std::string &pin) const
{
  const auto &list = input ? in : out;
  auto it = std::find(list.begin(), list.end(), pin);
  if(it == list.end())return -1;
  const int pos = static_cast<int>(it - list.begin());
  const int conditionStart = input ? conditionIn : conditionOut;
  if(conditionStart >= 0 && pos >= conditionStart) return P64::Events::Pins::CONDITION_PIN_BASE + (pos - conditionStart);
  // Fork: animation events
  if(!input && animEventOut >= 0 && pos >= animEventOut) return P64::Events::Pins::ANIM_EVENT_PIN_BASE + (pos - animEventOut);
  return pos;
}

bool Project::Events::PinList::isConditionPin(bool input, const std::string &pin) const
{
  // by position: animation events use the same index range on another component
  const int conditionStart = input ? conditionIn : conditionOut;
  if(conditionStart < 0)return false;
  const auto &list = input ? in : out;
  auto it = std::find(list.begin(), list.end(), pin);
  return it != list.end() && (it - list.begin()) >= conditionStart;
}

bool Project::Events::PinList::readsConditions(const std::string &pin) const
{
  return std::find(conditionInputs.begin(), conditionInputs.end(), pin) != conditionInputs.end();
}

void Project::Events::PinList::addAnimEvents(const AnimEventList &list)
{
  if(list.events.empty())return;
  animEventOut = static_cast<int>(out.size());
  for(const auto &ev : list.events) {
    out.push_back(ev.name);
    std::string doc = ev.doc;
    if(!ev.value.empty()) doc += (doc.empty() ? "" : "\n") + std::string{"Value: "} + ev.value;
    if(!doc.empty()) docs[ev.name] = doc;
  }
}

std::string Project::Events::PinList::docOf(const std::string &pin) const
{
  auto it = docs.find(pin);
  return it == docs.end() ? std::string{} : it->second;
}
