/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventBuilder.h"

#include <filesystem>
#include <format>
#include <stdexcept>

#include "projectBuilder.h"
#include "../project/project.h"
#include "../project/events/eventPins.h"
#include "../project/events/eventRules.h"
#include "../utils/fs.h"
#include "../utils/logger.h"

namespace fs = std::filesystem;
using Project::Events::Message;

namespace
{
  constexpr uint8_t EVENT_LINKS_ID = 26;   // P64::Comp::EventLinks::ID
  constexpr uint8_t COMP_OBJECT = 0xFF;    // the object's own pins
  constexpr uint32_t MAX_RECORD_WORDS = 255;

  void addRecords(Build::SceneCtx &ctx, std::vector<Build::Events::Record> &res,
                  const Project::Events::Bindings &bindings, const Project::Events::PinList &pins,
                  uint8_t comp, const std::string &where)
  {
    for(const bool input : {true, false})
    {
      for(const auto &[pin, names] : input ? bindings.in : bindings.out)
      {
        const int pinIdx = pins.indexOf(input, pin);
        if(pinIdx < 0) {
          ctx.eventMessages.push_back({Message::Level::Warning, where + ": has no " +
            std::string(input ? "input" : "output") + " pin '" + pin + "' any more; its bindings are kept but do nothing"});
          continue;
        }

        for(const auto &name : names)
        {
          auto problem = Project::Events::nameProblem(name);
          if(!problem.empty()) throw std::runtime_error(where + "." + pin + ": " + problem);

          const auto parsed = Project::Events::parse(name);
          if(parsed.id.scope == Project::Events::Scope::Prefab) {
            throw std::runtime_error(where + "." + pin + ": '" + name + "': prefab:: events are not supported yet");
          }

          problem = Project::Events::bindingProblem(ctx.eventIndex, ctx.eventCatalog, name, input);
          if(!problem.empty()) throw std::runtime_error(where + "." + pin + ": " + problem);

          ctx.eventIndex.add(name, input ? Project::Events::UseKind::BoundIn : Project::Events::UseKind::BoundOut,
                             where + "." + pin);
          res.push_back({
            .hash = parsed.id.hash,
            .comp = comp,
            .pin = static_cast<uint8_t>(pinIdx),
            .kind = Project::Events::recordKind(input, parsed.id.scope),
            .up = parsed.id.up,
          });
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
  }
  ctx.eventIndex.addSourceUses(Project::Events::scanSourceDir(fs::path{project.getPath()} / "src" / "user"));
}

std::vector<Build::Events::Record> Build::Events::collect(SceneCtx &ctx, const Project::Object &obj,
  const std::vector<Project::Component::Entry*> &compList, const Project::Events::Bindings &objectBindings)
{
  std::vector<Record> res{};
  if(!ctx.project)return res;

  const std::string objWhere = ctx.eventWhere + " / " + obj.name;
  for(size_t i = 0; i < compList.size(); ++i)
  {
    const auto &comp = *compList[i];
    if(comp.events.empty())continue;
    auto pins = Project::Events::pinsOf(*ctx.project, comp);
    // the EventLinks record is written in front of the components, so component i is i + 1
    addRecords(ctx, res, comp.events, pins, static_cast<uint8_t>(i + 1),
               objWhere + " / " + Project::Events::displayName(*ctx.project, comp));
  }
  if(!objectBindings.empty()) {
    addRecords(ctx, res, objectBindings, Project::Events::objectPins(), COMP_OBJECT, objWhere);
  }
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
