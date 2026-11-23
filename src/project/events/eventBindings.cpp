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

namespace
{
  nlohmann::json pathToJson(const std::vector<uint32_t> &path)
  {
    auto arr = nlohmann::json::array();
    for(auto uuid : path) arr.push_back(uuid);
    return arr;
  }

  std::vector<uint32_t> pathFromJson(const nlohmann::json &doc)
  {
    std::vector<uint32_t> res{};
    if(doc.is_array()) for(const auto &v : doc) if(v.is_number_unsigned() || v.is_number_integer()) res.push_back(v.get<uint32_t>());
    return res;
  }

  std::vector<std::string> namesFromJson(const nlohmann::json &doc)
  {
    std::vector<std::string> res{};
    if(doc.is_array()) for(const auto &v : doc) if(v.is_string() && !v.get<std::string>().empty()) res.push_back(v.get<std::string>());
    return res;
  }
}

nlohmann::json Project::Events::overridesToJson(const std::vector<Override> &list)
{
  auto arr = nlohmann::json::array();
  for(const auto &ovr : list)
  {
    if(ovr.empty())continue;
    nlohmann::json o = nlohmann::json::object();
    if(ovr.isLink()) {
      o["link"] = ovr.link;
      o["to"] = ovr.to;
    } else {
      o["path"] = pathToJson(ovr.path);
      o["comp"] = ovr.comp;
      o["pin"] = ovr.pin;
      o["dir"] = ovr.input ? "in" : "out";
      if(!ovr.add.empty()) o["add"] = ovr.add;
      if(!ovr.mute.empty()) o["mute"] = ovr.mute;
    }
    if(!ovr.hint.empty()) o["hint"] = ovr.hint;
    arr.push_back(o);
  }
  return arr;
}

std::vector<Project::Events::Override> Project::Events::overridesFromJson(const nlohmann::json &doc)
{
  std::vector<Override> res{};
  if(!doc.is_array())return res;
  for(const auto &o : doc)
  {
    if(!o.is_object())continue;
    Override ovr{};
    if(o.contains("link") && o["link"].is_string()) {
      ovr.link = o["link"];
      if(o.contains("to") && o["to"].is_string()) ovr.to = o["to"];
    } else {
      if(o.contains("path")) ovr.path = pathFromJson(o["path"]);
      if(o.contains("comp") && o["comp"].is_number()) ovr.comp = o["comp"].get<uint64_t>();
      if(o.contains("pin") && o["pin"].is_string()) ovr.pin = o["pin"];
      ovr.input = !(o.contains("dir") && o["dir"] == "out");
      if(o.contains("add")) ovr.add = namesFromJson(o["add"]);
      if(o.contains("mute")) ovr.mute = namesFromJson(o["mute"]);
    }
    if(!ovr.empty()) res.push_back(ovr);
  }
  return res;
}

nlohmann::json Project::Events::exposedToJson(const std::vector<Exposed> &list)
{
  auto arr = nlohmann::json::array();
  for(const auto &e : list) {
    nlohmann::json o = nlohmann::json::object();
    o["path"] = pathToJson(e.path);
    o["comp"] = e.comp;
    o["pin"] = e.pin;
    o["dir"] = e.input ? "in" : "out";
    if(!e.alias.empty()) o["alias"] = e.alias;
    arr.push_back(o);
  }
  return arr;
}

std::vector<Project::Events::Exposed> Project::Events::exposedFromJson(const nlohmann::json &doc)
{
  std::vector<Exposed> res{};
  if(!doc.is_array())return res;
  for(const auto &o : doc) {
    if(!o.is_object() || !o.contains("pin") || !o["pin"].is_string())continue;
    Exposed e{};
    if(o.contains("path")) e.path = pathFromJson(o["path"]);
    if(o.contains("comp") && o["comp"].is_number()) e.comp = o["comp"].get<uint64_t>();
    e.pin = o["pin"];
    e.input = !(o.contains("dir") && o["dir"] == "out");
    if(o.contains("alias") && o["alias"].is_string()) e.alias = o["alias"];
    res.push_back(e);
  }
  return res;
}
