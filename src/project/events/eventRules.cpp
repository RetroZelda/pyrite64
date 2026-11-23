/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventRules.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

#include "json.hpp"

namespace fs = std::filesystem;

namespace
{
  std::string lower(const std::string &str)
  {
    std::string res = str;
    for(auto &c : res) c = (char)std::tolower((unsigned char)c);
    return res;
  }

  bool differsByOneChar(const std::string &a, const std::string &b)
  {
    if(a == b)return false;
    const size_t la = a.size(), lb = b.size();
    if(la == lb) {
      int diffs = 0;
      for(size_t i = 0; i < la; ++i) if(a[i] != b[i] && ++diffs > 1)return false;
      return diffs == 1;
    }
    if(la + 1 != lb && lb + 1 != la)return false;
    const std::string &s = la < lb ? a : b;
    const std::string &l = la < lb ? b : a;
    size_t i = 0, j = 0;
    bool skipped = false;
    while(i < s.size() && j < l.size()) {
      if(s[i] == l[j]) { ++i; ++j; continue; }
      if(skipped)return false;
      skipped = true;
      ++j;
    }
    return true;
  }

  bool fires(Project::Events::UseKind kind)
  {
    using K = Project::Events::UseKind;
    return kind == K::BoundOut || kind == K::Code || kind == K::SystemOut || kind == K::EngineOut;
  }

  bool listens(Project::Events::UseKind kind)
  {
    using K = Project::Events::UseKind;
    return kind == K::BoundIn || kind == K::Code || kind == K::SystemIn;
  }
}

P64::Events::ParsedName Project::Events::parse(const std::string &name)
{
  return P64::Events::parseName(name.c_str(), name.size());
}

std::string Project::Events::nameProblem(const std::string &name)
{
  using P64::Events::NameError;
  switch(parse(name).error)
  {
    case NameError::None: return "";
    case NameError::MissingSeparator: return "'" + name + "' has no namespace: write namespace::Name, for example scene::DoorsLocked";
    case NameError::BadNamespace: return "'" + name + "': the namespace must be lower case letters, digits or _, starting with a letter";
    case NameError::BadName: return "'" + name + "': the name may only use letters, digits, _ and dots, with no empty part between dots";
    case NameError::BadPrefabNumber: return "'" + name + "': prefab_ must be followed by a number from 0 to 255 without leading zeros";
    case NameError::GlobalNeedsSystem: return "'" + name + "': global names are global::System.Pin, naming the system that declares the pin";
  }
  return "'" + name + "' is not a valid event name";
}

std::string Project::Events::namespaceOf(const std::string &name)
{
  auto pos = name.find("::");
  return pos == std::string::npos ? std::string{} : name.substr(0, pos);
}

bool Project::Events::isBuiltinNamespace(const std::string &ns)
{
  if(ns == "global" || ns == "scene" || ns == "ui" || ns == "object" || ns == "prefab")return true;
  if(ns.size() > 7 && ns.rfind("prefab_", 0) == 0) {
    return std::all_of(ns.begin() + 7, ns.end(), [](char c) { return std::isdigit((unsigned char)c); });
  }
  return false;
}

std::string Project::Events::canonicalName(const std::string &name)
{
  auto ns = namespaceOf(name);
  if(ns.size() > 7 && ns.rfind("prefab_", 0) == 0 && isBuiltinNamespace(ns)) {
    return "prefab" + name.substr(ns.size());
  }
  return name;
}

uint8_t Project::Events::recordKind(bool input, Scope scope)
{
  return static_cast<uint8_t>((input ? 1 : 0) | ((static_cast<uint8_t>(scope) & 0x7) << 1));
}

// ---- catalog ----

void Project::Events::Catalog::loadFile(const fs::path &file, bool engineFile, std::vector<std::string> &errors)
{
  std::ifstream in(file);
  if(!in) {
    errors.push_back(file.string() + ": cannot be read");
    return;
  }
  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(in);
  } catch(const std::exception &e) {
    errors.push_back(file.string() + ": " + e.what());
    return;
  }

  if(doc.contains("namespaces") && doc["namespaces"].is_object()) {
    for(const auto &[ns, value] : doc["namespaces"].items()) {
      if(isBuiltinNamespace(ns)) continue; // built in, nothing to declare
      auto probe = ns + "::X";
      if(!nameProblem(probe).empty()) {
        errors.push_back(file.string() + ": '" + ns + "' is not a valid namespace (lower case letters, digits, _)");
        continue;
      }
      std::string docText{};
      if(value.is_object() && value.contains("doc") && value["doc"].is_string()) docText = value["doc"];
      else if(value.is_string()) docText = value;
      namespaces[ns] = docText;
    }
  }

  if(doc.contains("events") && doc["events"].is_object()) {
    for(const auto &[name, value] : doc["events"].items()) {
      auto problem = nameProblem(name);
      if(!problem.empty()) {
        errors.push_back(file.string() + ": " + problem);
        continue;
      }
      CatalogEvent ev{};
      ev.engine = engineFile;
      if(value.is_object()) {
        if(value.contains("doc") && value["doc"].is_string()) ev.doc = value["doc"];
        if(value.contains("value") && value["value"].is_string()) ev.value = value["value"];
        if(value.contains("engine") && value["engine"].is_boolean()) ev.engine = value["engine"];
      } else if(value.is_string()) {
        ev.doc = value;
      }
      events[name] = ev;
    }
  }
}

void Project::Events::Catalog::loadDir(const fs::path &dir, bool engineFiles, std::vector<std::string> &errors)
{
  std::error_code ec;
  if(!fs::is_directory(dir, ec))return;
  std::vector<fs::path> files{};
  for(const auto &entry : fs::directory_iterator(dir, ec)) {
    if(entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for(const auto &file : files) loadFile(file, engineFiles, errors);
}

bool Project::Events::Catalog::isDeclaredNamespace(const std::string &ns) const
{
  return isBuiltinNamespace(ns) || namespaces.contains(ns);
}

const Project::Events::CatalogEvent* Project::Events::Catalog::find(const std::string &name) const
{
  auto it = events.find(canonicalName(name));
  return it == events.end() ? nullptr : &it->second;
}

// ---- source scan ----

std::vector<Project::Events::SourceUse> Project::Events::scanSource(const std::string &code, const std::string &fileName)
{
  std::vector<SourceUse> res{};
  static const std::regex literal(R"re("([^"\\\n]*)"_(event|pin)\b)re");

  int line = 1;
  size_t lineStartPos = 0;
  auto begin = std::sregex_iterator(code.begin(), code.end(), literal);
  for(auto it = begin; it != std::sregex_iterator(); ++it)
  {
    const auto pos = static_cast<size_t>(it->position(0));
    for(size_t i = lineStartPos; i < pos; ++i) if(code[i] == '\n') ++line;
    lineStartPos = pos;
    res.push_back({
      .name = (*it)[1].str(),
      .file = fileName,
      .line = line,
      .isPin = (*it)[2].str() == "pin",
    });
  }
  return res;
}

std::vector<Project::Events::SourceUse> Project::Events::scanSourceDir(const fs::path &dir)
{
  std::vector<SourceUse> res{};
  std::error_code ec;
  if(!fs::is_directory(dir, ec))return res;

  std::vector<fs::path> files{};
  for(const auto &entry : fs::recursive_directory_iterator(dir, ec)) {
    if(!entry.is_regular_file())continue;
    auto ext = entry.path().extension().string();
    if(ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".cc") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  for(const auto &file : files) {
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    auto found = scanSource(ss.str(), fs::relative(file, dir, ec).generic_string());
    res.insert(res.end(), found.begin(), found.end());
  }
  return res;
}

// ---- project index ----

void Project::Events::Index::add(const std::string &name, UseKind kind, const std::string &where)
{
  uses.push_back({name, kind, where});
}

void Project::Events::Index::addSystem(const SystemDecl &decl, std::vector<Message> &messages)
{
  if(decl.system == "Engine") {
    messages.push_back({Message::Level::Error, decl.script + ": the system name 'Engine' is reserved for the engine's own events"});
    return;
  }
  for(const auto &other : systems) {
    if(other.system == decl.system) {
      messages.push_back({Message::Level::Error, "system '" + decl.system + "' is declared by both " + other.script + " and " + decl.script + "; a system belongs to one global script"});
      return;
    }
  }
  for(const auto &pin : decl.in) {
    if(std::find(decl.out.begin(), decl.out.end(), pin) != decl.out.end()) {
      messages.push_back({Message::Level::Error, decl.script + ": system '" + decl.system + "' declares '" + pin + "' as both input and output"});
    }
  }
  systems.push_back(decl);
  for(const auto &pin : decl.in) add("global::" + decl.system + "." + pin, UseKind::SystemIn, decl.script);
  for(const auto &pin : decl.out) add("global::" + decl.system + "." + pin, UseKind::SystemOut, decl.script);
}

void Project::Events::Index::addSourceUses(const std::vector<SourceUse> &found)
{
  for(const auto &use : found) {
    if(use.isPin) sourcePins.push_back(use);
    else add(use.name, UseKind::Code, use.file + ":" + std::to_string(use.line));
  }
}

void Project::Events::Index::addCatalog(const Catalog &catalog)
{
  for(const auto &[name, ev] : catalog.events) {
    if(ev.engine) add(name, UseKind::EngineOut, "engine");
  }
}

const Project::Events::SystemDecl* Project::Events::Index::findSystem(const std::string &system) const
{
  for(const auto &decl : systems) if(decl.system == system) return &decl;
  return nullptr;
}

std::string Project::Events::bindingProblem(const Index &index, const Catalog &catalog, const std::string &name, bool input)
{
  if(namespaceOf(name) != "global")return "";

  auto rest = name.substr(8); // after "global::"
  auto dot = rest.find('.');
  auto system = rest.substr(0, dot);
  auto pin = rest.substr(dot + 1);

  if(system == "Engine") {
    const auto *ev = catalog.find(name);
    if(!ev || !ev->engine) return "'" + name + "' is not an event the engine fires";
    if(!input) return "'" + name + "' is fired by the engine; an output cannot fire it";
    return "";
  }

  const auto *decl = index.findSystem(system);
  if(!decl) return "'" + name + "': no global script declares the system '" + system + "' (P64_GLOBAL_EVENTS_IN/OUT)";
  const bool isIn = std::find(decl->in.begin(), decl->in.end(), pin) != decl->in.end();
  const bool isOut = std::find(decl->out.begin(), decl->out.end(), pin) != decl->out.end();
  if(!isIn && !isOut) return "'" + name + "': system '" + system + "' (" + decl->script + ") has no pin '" + pin + "'";
  // an object input listens to what the system fires; an object output fires into a system input
  if(input && !isOut) return "'" + name + "' is an input of system '" + system + "': an object input cannot listen to it, only fire into it";
  if(!input && !isIn) return "'" + name + "' is an output of system '" + system + "': an object output cannot fire it, only listen to it";
  return "";
}

std::vector<std::string> Project::Events::Index::allNames() const
{
  std::set<std::string> names{};
  for(const auto &use : uses) {
    if(nameProblem(use.name).empty()) names.insert(canonicalName(use.name));
  }
  return {names.begin(), names.end()};
}

std::vector<Project::Events::Message> Project::Events::Index::check(const Catalog &catalog) const
{
  using Level = Message::Level;
  std::vector<Message> res{};

  // canonical name -> every use
  std::map<std::string, std::vector<const Use*>> byName{};
  for(const auto &use : uses) {
    if(!nameProblem(use.name).empty()) {
      if(use.kind == UseKind::Code) {
        res.push_back({Level::Warning, use.where + ": " + nameProblem(use.name)});
      }
      continue; // bindings with bad names already failed while writing the object
    }
    byName[canonicalName(use.name)].push_back(&use);
  }

  // two different names with the same id
  std::map<uint32_t, std::string> byHash{};
  for(const auto &[name, list] : byName) {
    auto hash = parse(name).id.hash;
    auto [it, inserted] = byHash.try_emplace(hash, name);
    if(!inserted && it->second != name) {
      res.push_back({Level::Error, "the event names '" + it->second + "' and '" + name + "' have the same id; rename one of them"});
    }
  }

  for(const auto &[name, list] : byName)
  {
    const auto ns = namespaceOf(name);
    bool anyFires = false, anyListens = false, anyBinding = false;
    for(const auto *use : list) {
      anyFires |= fires(use->kind);
      anyListens |= listens(use->kind);
      anyBinding |= use->kind == UseKind::BoundIn || use->kind == UseKind::BoundOut;
    }

    if(!catalog.isDeclaredNamespace(ns)) {
      // bindings fail, code only warns: code may hold names for something else
      auto level = anyBinding ? Level::Error : Level::Warning;
      res.push_back({level, "'" + name + "' (" + list.front()->where + "): the namespace '" + ns +
        "' is not declared; add it to a file in the project's events/ folder, or use a built-in namespace"});
    }

    if(ns == "global") {
      for(const auto *use : list) {
        if(use->kind != UseKind::Code)continue;
        auto system = name.substr(8, name.find('.') - 8);
        if(system != "Engine" && !findSystem(system)) {
          res.push_back({Level::Warning, use->where + ": '" + name + "': no global script declares the system '" + system + "'"});
          break;
        }
      }
    }

    for(const auto *use : list) {
      if(use->kind == UseKind::BoundIn && !anyFires) {
        res.push_back({Level::Warning, use->where + " listens to '" + name + "', but nothing fires it (no output, code or system uses this name)"});
      }
      if(use->kind == UseKind::BoundOut && !anyListens) {
        res.push_back({Level::Info, use->where + " fires '" + name + "', but nothing listens to it"});
      }
    }
  }

  // names that are probably typos of each other. A one-character difference only counts when one
  // name is only listened to and the other only fired: Wave1 / Wave2 used both ways are fine.
  struct Sides { bool fired{false}; bool listened{false}; };
  std::vector<std::string> names{};
  std::vector<Sides> sides{};
  for(const auto &[name, list] : byName) {
    Sides s{};
    for(const auto *use : list) { s.fired |= fires(use->kind); s.listened |= listens(use->kind); }
    names.push_back(name);
    sides.push_back(s);
  }
  for(size_t i = 0; i < names.size(); ++i) {
    for(size_t j = i + 1; j < names.size(); ++j) {
      if(lower(names[i]) == lower(names[j])) {
        res.push_back({Level::Warning, "'" + names[i] + "' and '" + names[j] + "' differ only by case; names are case-sensitive"});
        continue;
      }
      const bool oneSided = (sides[i].listened && !sides[i].fired && sides[j].fired && !sides[j].listened)
                         || (sides[j].listened && !sides[j].fired && sides[i].fired && !sides[i].listened);
      if(oneSided && differsByOneChar(names[i], names[j])) {
        res.push_back({Level::Warning, "'" + names[i] + "' and '" + names[j] + "' differ by one character and one is only fired, the other only listened to: is one a typo?"});
      }
    }
  }

  // send() to pins nobody declares
  std::set<std::string> reported{};
  for(const auto &use : sourcePins) {
    if(!declaredInputPins.contains(use.name) && reported.insert(use.name).second) {
      res.push_back({Level::Warning, use.file + ":" + std::to_string(use.line) + ": send() to \"" + use.name +
        "\"_pin, but no object script declares an input named " + use.name});
    }
  }

  return res;
}
