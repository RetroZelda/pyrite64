/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventBindings.h"

#include <algorithm>

namespace
{
  nlohmann::json pinMapToJson(const Project::Events::PinMap &map)
  {
    auto res = nlohmann::json::object();
    for(const auto &[pin, names] : map) {
      auto arr = nlohmann::json::array();
      for(const auto &name : names) {
        if(!name.empty())arr.push_back(name);
      }
      if(!arr.empty())res[pin] = arr;
    }
    return res;
  }

  Project::Events::PinMap pinMapFromJson(const nlohmann::json &doc)
  {
    Project::Events::PinMap res{};
    if(!doc.is_object())return res;
    for(const auto &[pin, value] : doc.items()) {
      auto &names = res[pin];
      if(value.is_string()) {
        names.push_back(value.get<std::string>());
      } else if(value.is_array()) {
        for(const auto &entry : value) {
          // a binding is a name; an object form { "event": "..." } is accepted for forward compatibility
          if(entry.is_string()) names.push_back(entry.get<std::string>());
          else if(entry.is_object() && entry.contains("event") && entry["event"].is_string()) names.push_back(entry["event"].get<std::string>());
        }
      }
    }
    return res;
  }
}

bool Project::Events::Bindings::empty() const
{
  return count() == 0;
}

size_t Project::Events::Bindings::count() const
{
  size_t total = 0;
  for(const auto &[pin, names] : in) total += names.size();
  for(const auto &[pin, names] : out) total += names.size();
  return total;
}

nlohmann::json Project::Events::Bindings::toJson() const
{
  auto res = nlohmann::json::object();
  auto jsonIn = pinMapToJson(in);
  auto jsonOut = pinMapToJson(out);
  if(!jsonIn.empty())res["in"] = jsonIn;
  if(!jsonOut.empty())res["out"] = jsonOut;
  return res;
}

Project::Events::Bindings Project::Events::Bindings::fromJson(const nlohmann::json &doc)
{
  Bindings res{};
  if(!doc.is_object())return res;
  if(doc.contains("in"))res.in = pinMapFromJson(doc["in"]);
  if(doc.contains("out"))res.out = pinMapFromJson(doc["out"]);
  res.prune();
  return res;
}

void Project::Events::Bindings::add(bool input, const std::string &pin, const std::string &eventName)
{
  auto &names = (input ? in : out)[pin];
  if(std::find(names.begin(), names.end(), eventName) == names.end()) {
    names.push_back(eventName);
  }
}

void Project::Events::Bindings::prune()
{
  for(auto *map : {&in, &out}) {
    for(auto it = map->begin(); it != map->end();) {
      auto &names = it->second;
      names.erase(std::remove(names.begin(), names.end(), std::string{}), names.end());
      it = names.empty() ? map->erase(it) : std::next(it);
    }
  }
}
