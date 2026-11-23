/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventBuilder.h"
#include "../project/canvas/canvas.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>

#include "projectBuilder.h"
#include "../project/project.h"
#include "../project/scene/object.h"
#include "../project/component/components.h"
#include "../project/events/eventPins.h"
#include "../project/events/eventRules.h"
#include "../../n64/engine/include/events/pins.h"
#include "../utils/fs.h"
#include "../utils/logger.h"

namespace fs = std::filesystem;
using Project::Events::Message;

namespace
{
  constexpr uint8_t EVENT_LINKS_ID = 26;   // P64::Comp::EventLinks::ID
  constexpr uint8_t COMP_OBJECT = 0xFF;    // the object's own pins
  constexpr uint32_t MAX_RECORD_WORDS = 255;

  // one binding of one pin, before it is resolved
  struct Entry
  {
    uint8_t comp;          // index in the object's record, or COMP_OBJECT
    uint64_t compUuid;     // 0 for the object's own pins
    const Project::Events::PinList* pins;
    bool input;
    std::string pin;
    std::string name;
    size_t level;          // the graph that wrote it
    std::string where;
    bool trigger{false};   // Fork: conditions, a P64_CONDITION_TRIGGERS event bound to Check by the build
  };

  // every graph from the scene (or the spawned prefab's root) in to the object being written
  std::vector<Project::Events::ScopeLevel> scopeLevels(const Build::SceneCtx &ctx)
  {
    std::vector<Project::Events::ScopeLevel> levels{};
    const int current = (int)ctx.eventPath.size() - 1;
    for(const auto &c : ctx.eventContexts)
    {
      Project::Events::ScopeLevel lvl{};
      lvl.isScene = c.isScene;
      lvl.spawnedRoot = c.spawnedRoot;
      uint32_t skip = 0;
      for(int i = c.rootIndex + 1; i <= current; ++i) {
        if(i >= 0 && ctx.eventPath[(size_t)i].prefabRoot) ++skip;
      }
      lvl.skip = (uint8_t)std::min<uint32_t>(skip, 255);
      if(c.instance) {
        for(const auto &ovr : c.instance->eventOverrides) {
          if(ovr.isLink()) lvl.links.push_back({ovr.link, ovr.to});
        }
      }
      levels.push_back(lvl);
    }
    return levels;
  }

  // instance overrides that target the object being written: add and mute bindings
  void applyOverrides(Build::SceneCtx &ctx, std::vector<Entry> &entries,
                      const std::vector<Project::Events::PinList> &compPins,
                      const std::vector<std::pair<Project::Component::Entry*, size_t>> &comps,
                      const Project::Events::PinList &objectPins, const std::string &objWhere)
  {
    const int current = (int)ctx.eventPath.size() - 1;
    for(size_t f = 1; f < ctx.eventContexts.size(); ++f)
    {
      const auto &frame = ctx.eventContexts[f];
      if(!frame.instance || frame.rootIndex > current)continue;

      std::vector<uint32_t> relPath{};
      for(int i = frame.rootIndex + 1; i <= current; ++i) relPath.push_back(ctx.eventPath[(size_t)i].uuid);

      for(const auto &ovr : frame.instance->eventOverrides)
      {
        if(ovr.isLink() || ovr.path != relPath)continue;

        // the target pin: a component of this object, or the object's own pins
        uint8_t comp = COMP_OBJECT;
        const Project::Events::PinList* pins = &objectPins;
        if(ovr.comp != 0) {
          bool found = false;
          for(size_t i = 0; i < comps.size(); ++i) {
            if(comps[i].first->uuid != ovr.comp)continue;
            comp = static_cast<uint8_t>(i + 1);
            pins = &compPins[i];
            found = true;
            break;
          }
          if(!found) {
            ctx.eventMessages.push_back({Message::Level::Warning, objWhere + ": an override on instance '" +
              frame.instance->name + "' targets a component that no longer exists; it is ignored"});
            continue;
          }
        }

        for(const auto &muted : ovr.mute) {
          std::erase_if(entries, [&](const Entry &e) {
            return e.comp == comp && e.input == ovr.input && e.pin == ovr.pin && e.name == muted;
          });
        }
        for(const auto &added : ovr.add) {
          entries.push_back({comp, ovr.comp, pins, ovr.input, ovr.pin, added, f - 1,
                             objWhere + " (override on instance '" + frame.instance->name + "')"});
        }
      }
    }
  }

  std::string escape(const std::string &str)
  {
    std::string res{};
    for(char c : str) {
      if(c == '\\' || c == '"') res += '\\';
      res += c;
    }
    return res;
  }

  void writeNameTable(Project::Project &project, Build::SceneCtx &ctx)
  {
    std::string src = "// NOTE: Auto-Generated File!\n// Fork: event graph, event names for debug output.\n\n#include \"events/bus.h\"\n\n";
    auto names = ctx.eventIndex.allNames();
    if(project.conf.debugMenu && !names.empty()) {
      src += "namespace\n{\n  constexpr P64::Events::NameEntry NAMES[] = {\n";
      for(const auto &name : names) {
        src += std::format("    {{0x{:08X}u, \"{}\"}},\n", Project::Events::parse(name).id.hash, escape(name));
      }
      src += "  };\n}\n\nvoid P64::Events::Generated::registerNames()\n{\n";
      src += "  P64::Events::setNameTable(NAMES, sizeof(NAMES) / sizeof(NAMES[0]));\n}\n";
    } else {
      // names are left out without the debug menu, like other debug data
      src += "void P64::Events::Generated::registerNames() {}\n";
    }
    Utils::FS::saveTextFileIfChanged(project.getPath() + "/src/p64/eventNames.cpp", src);
  }
}

void Build::Events::begin(Project::Project &project, SceneCtx &ctx)
{
  ctx.eventCatalog = {};
  ctx.eventIndex = {};
  ctx.eventMessages.clear();

  std::vector<std::string> errors{};
  ctx.eventCatalog.loadDir("data/events", true, errors);
  ctx.eventCatalog.loadDir(fs::path{project.getPath()} / "events", false, errors);
  for(const auto &err : errors) ctx.eventMessages.push_back({Message::Level::Error, "event catalog: " + err});
  ctx.eventIndex.addCatalog(ctx.eventCatalog);

  for(const auto &script : project.getAssets().getTypeEntries(Project::FileType::CODE_GLOBAL)) {
    for(const auto &sys : script.systemPins) {
      ctx.eventIndex.addSystem({sys.system, script.name, sys.in, sys.out}, ctx.eventMessages);
    }
  }
  for(const auto &script : project.getAssets().getTypeEntries(Project::FileType::CODE_OBJ)) {
    for(const auto &pin : script.pins.in) ctx.eventIndex.declaredInputPins.insert(pin);
    // Fork: conditions, send(obj, "Check"_pin) reaches the pins every condition has
    if(script.condition.isCondition) {
      for(const char* pin : P64::Events::Pins::CONDITION_IN) ctx.eventIndex.declaredInputPins.insert(pin);
    }
  }
  ctx.eventIndex.addSourceUses(Project::Events::scanSourceDir(fs::path{project.getPath()} / "src" / "user"));

  // UI canvases (PLAN 9): checked like an object's bindings, and counted in the index
  for(const auto &entry : project.getAssets().getTypeEntries(Project::FileType::CANVAS)) {
    if(!entry.canvas)continue;
    const auto &canvas = *entry.canvas;
    const auto pins = Project::Events::canvasPins(canvas);
    for(const bool input : {true, false}) {
      for(const auto &[pin, names] : input ? canvas.bindings.in : canvas.bindings.out) {
        const std::string where = "canvas " + canvas.name + "." + pin;
        if(pins.indexOf(input, pin) < 0) {
          ctx.eventMessages.push_back({Message::Level::Warning, where + ": the canvas has no " +
            std::string(input ? "input" : "output") + " pin '" + pin + "' any more; its bindings do nothing"});
          continue;
        }
        for(const auto &name : names) {
          auto problem = Project::Events::nameProblem(name);
          if(problem.empty()) {
            const auto scope = Project::Events::parse(name).id.scope;
            if(scope == Project::Events::Scope::Prefab || scope == Project::Events::Scope::Object) {
              problem = "'" + name + "': a canvas is not in the object tree, so it can only use ui::, scene::, global:: or a game namespace";
            }
          }
          if(problem.empty()) problem = Project::Events::bindingProblem(ctx.eventIndex, ctx.eventCatalog, name, input);
          if(!problem.empty()) {
            ctx.eventMessages.push_back({Message::Level::Error, where + ": " + problem});
            continue;
          }
          ctx.eventIndex.add(name, input ? Project::Events::UseKind::BoundIn : Project::Events::UseKind::BoundOut, where);
        }
      }
    }
  }
}

std::vector<Build::Events::Record> Build::Events::collect(SceneCtx &ctx, const ObjectBindings &input)
{
  std::vector<Record> res{};
  if(!ctx.project || ctx.eventContexts.empty())return res;

  const auto &obj = input.obj;
  // the whole path, so a message names the instance too ("K1 / Door", not just "Door")
  std::string objWhere = ctx.eventWhere;
  for(const auto &step : ctx.eventPath) objWhere += " / " + (step.name ? *step.name : std::string{"?"});
  if(ctx.eventPath.empty() || ctx.eventPath.back().uuid != obj.uuid) objWhere += " / " + obj.name;

  // 1. every binding, with the graph that wrote it
  std::vector<Project::Events::PinList> compPins{};
  std::vector<Entry> entries{};
  for(size_t i = 0; i < input.comps.size(); ++i)
  {
    auto [comp, level] = input.comps[i];
    compPins.push_back(Project::Events::pinsOf(*ctx.project, *comp));
  }
  for(size_t i = 0; i < input.comps.size(); ++i)
  {
    auto [comp, level] = input.comps[i];
    const auto where = objWhere + " / " + Project::Events::displayName(*ctx.project, *comp);
    for(const bool in : {true, false}) {
      for(const auto &[pin, names] : in ? comp->events.in : comp->events.out) {
        for(const auto &name : names) {
          // the EventLinks record is written in front of the components, so component i is i + 1
          entries.push_back({static_cast<uint8_t>(i + 1), comp->uuid, &compPins[i], in, pin, name, level, where});
        }
      }
    }
  }
  const auto &objectPins = Project::Events::objectPins();
  for(const auto &[bindings, level] : input.objectPins) {
    for(const bool in : {true, false}) {
      for(const auto &[pin, names] : in ? bindings->in : bindings->out) {
        for(const auto &name : names) entries.push_back({COMP_OBJECT, 0, &objectPins, in, pin, name, level, objWhere});
      }
    }
  }

  // 2. what the instances around this object change
  applyOverrides(ctx, entries, compPins, input.comps, objectPins, objWhere);

  // Fork: conditions. The events a condition script names in P64_CONDITION_TRIGGERS are bound to its
  // Check input here; they are written in the graph that holds the component.
  for(size_t i = 0; i < input.comps.size(); ++i)
  {
    auto [comp, level] = input.comps[i];
    const auto compIndex = static_cast<uint8_t>(i + 1);
    for(const auto &trigger : compPins[i].triggers) {
      const bool alreadyBound = std::any_of(entries.begin(), entries.end(), [&](const Entry &e) {
        return e.comp == compIndex && e.input && e.pin == "Check" && e.name == trigger;
      });
      if(alreadyBound)continue;
      entries.push_back({compIndex, comp->uuid, &compPins[i], true, "Check", trigger, level,
        objWhere + " / " + Project::Events::displayName(*ctx.project, *comp), true});
    }
  }
  if(entries.empty())return res;

  // 3. resolve each binding through the prefabs around it
  const auto levels = scopeLevels(ctx);
  for(const auto &e : entries)
  {
    const int pinIdx = e.pins->indexOf(e.input, e.pin);
    if(pinIdx < 0) {
      ctx.eventMessages.push_back({Message::Level::Warning, e.where + ": has no " +
        std::string(e.input ? "input" : "output") + " pin '" + e.pin + "' any more; its bindings are kept but do nothing"});
      continue;
    }

    // a trigger comes from the script's code, so the message points there
    const std::string pinWhere = e.trigger ? e.where + ", trigger '" + e.name + "' (P64_CONDITION_TRIGGERS)"
                                           : e.where + "." + e.pin;

    auto problem = Project::Events::nameProblem(e.name);
    if(!problem.empty()) throw std::runtime_error(pinWhere + ": " + problem);

    const auto resolved = Project::Events::resolveName(e.name, levels, std::min(e.level, levels.size() - 1));
    if(!resolved.problem.empty()) {
      // a script with a prefab:: trigger used outside a prefab: the designer cannot fix the script
      if(e.trigger) {
        ctx.eventMessages.push_back({Message::Level::Warning, pinWhere + ": " + resolved.problem + "; here it does nothing"});
        continue;
      }
      throw std::runtime_error(pinWhere + ": " + resolved.problem);
    }
    if(!resolved.warning.empty()) {
      ctx.eventMessages.push_back({Message::Level::Warning, pinWhere + ": " + resolved.warning});
      continue;
    }

    problem = Project::Events::bindingProblem(ctx.eventIndex, ctx.eventCatalog, e.name, e.input);
    if(!problem.empty()) throw std::runtime_error(pinWhere + ": " + problem);

    // the name it ends up as: a link repointed to scene::X listens to scene::X
    const auto &finalName = resolved.name.empty() ? e.name : resolved.name;
    ctx.eventIndex.add(finalName,
                       e.input ? Project::Events::UseKind::BoundIn : Project::Events::UseKind::BoundOut,
                       pinWhere);

    // Fork: conditions, who provides a Result and who reads one
    if(!e.input && e.pin == "Result" && e.pins->isConditionPin(false, e.pin)) {
      ctx.eventIndex.conditionResults.insert(Project::Events::canonicalName(finalName));
    }
    if(e.input && e.pins->readsConditions(e.pin)) {
      ctx.eventIndex.conditionReads.emplace_back(finalName, pinWhere);
    }
    res.push_back({
      .hash = resolved.hash,
      .comp = e.comp,
      .pin = static_cast<uint8_t>(pinIdx),
      .kind = Project::Events::recordKind(e.input, resolved.scope),
      .up = resolved.up,
    });
  }

  // Fork: conditions. Listeners of one event are called in record order: a condition's Check comes
  // last, so an event bound to Enable (or Disable) and to Check is handled in that order.
  const uint8_t checkPin = P64::Events::Pins::CONDITION_PIN_BASE + static_cast<uint8_t>(P64::Events::Pins::ConditionIn::Check);
  std::stable_partition(res.begin(), res.end(), [&](const Record &rec) {
    const bool isInput = rec.kind & 1; // EventLinks::KIND_INPUT
    return !(isInput && rec.comp != COMP_OBJECT && rec.pin == checkPin);
  });
  return res;
}

void Build::Events::writeLinks(SceneCtx &ctx, const Project::Object &obj, const std::vector<Record> &records)
{
  auto compPos = ctx.fileObj.getPos();
  ctx.fileObj.skip(2);
  ctx.fileObj.skip(2); // flags

  ctx.fileObj.write<uint16_t>(static_cast<uint16_t>(records.size()));
  ctx.fileObj.write<uint16_t>(0);
  for(const auto &rec : records) {
    ctx.fileObj.write<uint32_t>(rec.hash);
    ctx.fileObj.write<uint8_t>(rec.comp);
    ctx.fileObj.write<uint8_t>(rec.pin);
    ctx.fileObj.write<uint8_t>(rec.kind);
    ctx.fileObj.write<uint8_t>(rec.up);
  }

  ctx.fileObj.align(4);
  auto size = (ctx.fileObj.getPos() - compPos) / 4;
  if(size > MAX_RECORD_WORDS) {
    throw std::runtime_error("Object '" + obj.name + "' has " + std::to_string(records.size()) +
      " event bindings; at most " + std::to_string((MAX_RECORD_WORDS * 4 - 8) / 8) + " fit on one object");
  }

  ctx.fileObj.posPush(compPos);
  ctx.fileObj.write<uint8_t>(EVENT_LINKS_ID);
  ctx.fileObj.write<uint8_t>(static_cast<uint8_t>(size));
  ctx.fileObj.posPop();
}

bool Build::Events::finish(Project::Project &project, SceneCtx &ctx)
{
  auto messages = ctx.eventMessages;
  auto checks = ctx.eventIndex.check(ctx.eventCatalog);
  messages.insert(messages.end(), checks.begin(), checks.end());

  bool ok = true;
  for(const auto &msg : messages)
  {
    switch(msg.level)
    {
      case Message::Level::Error:
        ok = false;
        Utils::Logger::log("[events] " + msg.text, Utils::Logger::LEVEL_ERROR);
        break;
      case Message::Level::Warning:
        Utils::Logger::log("[events] " + msg.text, Utils::Logger::LEVEL_WARN);
        break;
      case Message::Level::Info:
        Utils::Logger::log("[events] " + msg.text, Utils::Logger::LEVEL_INFO);
        break;
    }
  }

  writeNameTable(project, ctx);
  return ok;
}
