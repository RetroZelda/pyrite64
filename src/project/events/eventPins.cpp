/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventPins.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "../project.h"
#include "../component/components.h"
#include "../canvas/canvas.h"
#include "../../../n64/engine/include/events/pins.h"

namespace fs = std::filesystem;

namespace
{
  constexpr int ANIM_MODEL_ID = 10; // P64::Comp::AnimModel::ID
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
      // Fork: interactions, any script's input pins may read conditions
      res.conditionInputs = script->condition.inputs;
      // Fork: conditions, the pins every condition has come after the script's own
      if(script->condition.isCondition) {
        res.conditionIn = static_cast<int>(res.in.size());
        res.conditionOut = static_cast<int>(res.out.size());
        for(const char* name : P64::Events::Pins::CONDITION_IN) res.in.emplace_back(name);
        for(const char* name : P64::Events::Pins::CONDITION_OUT) res.out.emplace_back(name);
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

  // Fork: animation events, the events the catalog lists for this component's model
  if(comp.id == ANIM_MODEL_ID) {
    if(auto *model = project.getAssets().getEntryByUUID(Component::AnimModel::getModelUUID(comp))) {
      if(auto *list = projectCatalog(project).catalog.findAnimEvents(model->projectPath)) res.addAnimEvents(*list);
    }
  }
  return res;
}

namespace
{
  // every *.json of the catalog folders with its file time: a change in any of them reloads
  using Stamp = std::vector<std::pair<std::string, int64_t>>;

  void addStamps(const fs::path &dir, Stamp &out)
  {
    std::error_code ec;
    if(!fs::is_directory(dir, ec))return;
    for(const auto &entry : fs::directory_iterator(dir, ec)) {
      if(!entry.is_regular_file(ec) || entry.path().extension() != ".json")continue;
      const auto time = fs::last_write_time(entry.path(), ec);
      out.emplace_back(entry.path().string(), static_cast<int64_t>(time.time_since_epoch().count()));
    }
  }

  struct CatalogCache
  {
    std::string projectPath{};
    Stamp stamp{};
    std::chrono::steady_clock::time_point checked{};
    bool loaded{false};
    Project::Events::CachedCatalog data{};
  };
}

const Project::Events::CachedCatalog& Project::Events::projectCatalog(Project &project, bool refresh)
{
  static CatalogCache cache{};
  const auto now = std::chrono::steady_clock::now();
  const bool sameProject = cache.loaded && cache.projectPath == project.getPath();
  if(sameProject && !refresh && now - cache.checked < std::chrono::milliseconds(500)) return cache.data;
  cache.checked = now;

  const fs::path engineDir{"data/events"};
  const fs::path projectDir = fs::path{project.getPath()} / "events";
  Stamp stamp{};
  addStamps(engineDir, stamp);
  addStamps(projectDir, stamp);
  std::sort(stamp.begin(), stamp.end());
  if(sameProject && stamp == cache.stamp) return cache.data;

  cache.projectPath = project.getPath();
  cache.stamp = stamp;
  cache.loaded = true;
  cache.data = {};
  cache.data.catalog.loadDir(engineDir, true, cache.data.errors);
  cache.data.catalog.loadDir(projectDir, false, cache.data.errors);
  return cache.data;
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
