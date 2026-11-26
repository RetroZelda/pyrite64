/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 4.3 to 4.5).
#include "locProject.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <map>

#include "../project.h"
#include "../../utils/fs.h"
#include "../../utils/json.h"

namespace fs = std::filesystem;

namespace
{
  fs::path configPath(const std::string &projectPath) { return fs::path{Project::Loc::localeDir(projectPath)} / "locale.json"; }
  fs::path tablePath(const std::string &projectPath, const std::string &code) { return fs::path{Project::Loc::localeDir(projectPath)} / (code + ".json"); }

  /** A string override on a prefab instance that reads like a key: valid, with a dot and a letter. */
  bool looksLikeKey(const std::string &s)
  {
    if(s.find('.') == std::string::npos) return false;
    bool letter = false;
    for(char c : s) if(c >= 'a' && c <= 'z') { letter = true; break; }
    return letter && Project::Loc::isValidKey(s);
  }

  void scanJson(Project::Project &project, const nlohmann::json &doc, const std::string &where,
                Project::Loc::Uses &uses, std::vector<std::string> &errors)
  {
    if(doc.is_array()) {
      for(const auto &v : doc) scanJson(project, v, where, uses, errors);
      return;
    }
    if(!doc.is_object()) return;

    const std::string objName = doc.contains("name") && doc["name"].is_string() ? doc["name"].get<std::string>() : std::string{};

    if(doc.contains("components") && doc["components"].is_array()) {
      for(const auto &comp : doc["components"]) {
        if(!comp.is_object() || !comp.contains("data") || !comp["data"].is_object()) continue;
        const auto &data = comp["data"];
        if(!data.contains("script") || !data["script"].is_number_unsigned()) continue;
        auto *script = project.getAssets().getEntryByUUID(data["script"].get<uint64_t>());
        if(!script) continue;
        const nlohmann::json *args = data.contains("args") && data["args"].is_object() ? &data["args"] : nullptr;
        for(const auto &field : script->params.fields) {
          if(field.type != Utils::DataType::LOC_TEXT) continue;
          std::string key{};
          if(args && args->contains(field.name) && (*args)[field.name].is_string()) key = (*args)[field.name];
          if(key.empty()) key = field.defaultValue;
          if(key.empty()) continue;
          std::string why{};
          const std::string at = where + ": " + objName + ": " + script->name + "." + field.name;
          if(!Project::Loc::isValidKey(key, &why)) {
            errors.push_back(at + ": '" + key + "': " + why);
            continue;
          }
          uses[key].push_back({at, {}, 0});
        }
      }
    }

    if(doc.contains("propOverrides") && doc["propOverrides"].is_object()) {
      for(const auto &[id, val] : doc["propOverrides"].items()) {
        if(!val.is_string()) continue;
        const std::string s = val;
        if(!s.starts_with("9:")) continue; // GenericValue type 9: a string
        const std::string payload = s.substr(2);
        if(looksLikeKey(payload)) uses[payload].push_back({where + ": " + objName + " (prefab instance override)", {}, 0});
      }
    }

    for(const auto &[k, v] : doc.items()) {
      if(k == "components" || k == "propOverrides") continue;
      if(v.is_object() || v.is_array()) scanJson(project, v, where, uses, errors);
    }
  }

  struct Cache
  {
    std::string projectPath{};
    bool loaded{false};
    Project::Loc::Localization loc{};
    std::vector<std::string> errors{};
    Project::Loc::Uses uses{};
    std::map<std::string, fs::file_time_type> mtimes{};
    std::chrono::steady_clock::time_point lastCheck{};
  };

  Cache cache{};

  std::map<std::string, fs::file_time_type> localeMtimes(const std::string &projectPath)
  {
    std::map<std::string, fs::file_time_type> res{};
    std::error_code ec{};
    const fs::path dir = Project::Loc::localeDir(projectPath);
    if(!fs::exists(dir, ec)) return res;
    for(const auto &e : fs::directory_iterator(dir, ec)) {
      if(e.path().extension() == ".json") res[e.path().filename().string()] = fs::last_write_time(e.path(), ec);
    }
    return res;
  }

  void reloadCache(Project::Project &project)
  {
    cache.projectPath = project.getPath();
    cache.errors.clear();
    cache.loc = {};
    Project::Loc::load(cache.projectPath, cache.loc, cache.errors);
    cache.mtimes = localeMtimes(cache.projectPath);
    cache.loaded = true;
    cache.lastCheck = std::chrono::steady_clock::now();
  }
}

std::string Project::Loc::localeDir(const std::string &projectPath)
{
  return (fs::path{projectPath} / "locale").string();
}

bool Project::Loc::hasLocalization(const std::string &projectPath)
{
  std::error_code ec{};
  return fs::exists(configPath(projectPath), ec);
}

bool Project::Loc::load(const std::string &projectPath, Localization &out, std::vector<std::string> &errors)
{
  out = {};
  if(!hasLocalization(projectPath)) return false;
  try {
    parseConfig(Utils::JSON::loadFile(configPath(projectPath)), out.config, errors);
  } catch(const std::exception &e) {
    errors.push_back(std::string("locale.json: ") + e.what());
    return true;
  }
  for(const auto &lang : out.config.languages) {
    Table t{};
    t.code = lang.code;
    if(!lang.pseudo) {
      const auto path = tablePath(projectPath, lang.code);
      try {
        if(fs::exists(path)) parseTable(Utils::JSON::loadFile(path), t, errors);
      } catch(const std::exception &e) {
        errors.push_back(lang.code + ".json: " + e.what());
      }
    }
    out.tables.push_back(std::move(t));
  }
  return true;
}

bool Project::Loc::save(const std::string &projectPath, const Localization &loc)
{
  std::error_code ec{};
  fs::create_directories(localeDir(projectPath), ec);
  bool ok = Utils::FS::saveTextFileIfChanged(configPath(projectPath), serializeConfig(loc.config));
  for(size_t i = 0; i < loc.tables.size() && i < loc.config.languages.size(); ++i) {
    if(loc.config.languages[i].pseudo) continue;
    ok &= Utils::FS::saveTextFileIfChanged(tablePath(projectPath, loc.tables[i].code), serializeTable(loc.tables[i]));
  }
  return ok;
}

bool Project::Loc::create(const std::string &projectPath, const std::string &sourceCode, const std::string &sourceName)
{
  if(hasLocalization(projectPath)) return true;
  Localization loc{};
  loc.config.source = sourceCode;
  loc.config.languages.push_back({.code = sourceCode, .name = sourceName});
  loc.tables.push_back({.code = sourceCode});
  return save(projectPath, loc);
}

Project::Loc::Uses Project::Loc::scanProject(Project &project, std::vector<std::string> &errors)
{
  Uses uses{};
  std::error_code ec{};
  const fs::path root{project.getPath()};

  // C++: src/user, every source and header
  const fs::path userDir = root / "src" / "user";
  if(fs::exists(userDir, ec)) {
    for(auto it = fs::recursive_directory_iterator(userDir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if(ec) break;
      if(!it->is_regular_file()) continue;
      const auto ext = it->path().extension().string();
      if(ext != ".cpp" && ext != ".h" && ext != ".hpp" && ext != ".cc" && ext != ".inl") continue;
      scanCpp(Utils::FS::loadTextFile(it->path()), fs::relative(it->path(), root, ec).generic_string(), uses, errors);
    }
  }

  // scenes
  const fs::path scenesDir = root / "data" / "scenes";
  if(fs::exists(scenesDir, ec)) {
    for(const auto &dir : fs::directory_iterator(scenesDir, ec)) {
      const auto file = dir.path() / "scene.json";
      if(!fs::exists(file, ec)) continue;
      try {
        auto doc = Utils::JSON::loadFile(file);
        std::string name{};
        if(doc.contains("conf") && doc["conf"].is_object()) name = doc["conf"].value("name", std::string{});
        scanJson(project, doc, "scene " + dir.path().filename().string() + (name.empty() ? "" : " (" + name + ")"), uses, errors);
      } catch(const std::exception &e) {
        errors.push_back(file.string() + ": " + e.what());
      }
    }
  }

  // prefabs
  for(const auto &entry : project.getAssets().getTypeEntries(FileType::PREFAB)) {
    try {
      scanJson(project, Utils::JSON::loadFile(entry.path), "prefab " + entry.projectPath, uses, errors);
    } catch(const std::exception &e) {
      errors.push_back(entry.projectPath + ": " + e.what());
    }
  }
  return uses;
}

Project::Loc::RefreshResult Project::Loc::refresh(Project &project, Localization &loc)
{
  RefreshResult res{};
  res.uses = scanProject(project, res.errors);
  res.added = loc.addMissing(res.uses);
  res.unused = loc.unused(res.uses);
  save(project.getPath(), loc);
  return res;
}

const Project::Loc::Localization& Project::Loc::cached(Project &project)
{
  if(!cache.loaded || cache.projectPath != project.getPath()) {
    reloadCache(project);
    cache.uses.clear();
  } else {
    auto now = std::chrono::steady_clock::now();
    if(now - cache.lastCheck > std::chrono::seconds(2)) {
      cache.lastCheck = now;
      if(localeMtimes(cache.projectPath) != cache.mtimes) reloadCache(project);
    }
  }
  return cache.loc;
}

Project::Loc::Localization& Project::Loc::edit(Project &project)
{
  cached(project);
  return cache.loc;
}

bool Project::Loc::saveCached(Project &project)
{
  cached(project);
  bool ok = save(cache.projectPath, cache.loc);
  cache.mtimes = localeMtimes(cache.projectPath);
  return ok;
}

const Project::Loc::Uses& Project::Loc::cachedUses(Project &project)
{
  cached(project);
  return cache.uses;
}

Project::Loc::RefreshResult Project::Loc::refreshCached(Project &project)
{
  cached(project);
  auto res = refresh(project, cache.loc);
  cache.uses = res.uses;
  cache.mtimes = localeMtimes(cache.projectPath);
  return res;
}

const std::vector<std::string>& Project::Loc::cachedErrors(Project &project)
{
  cached(project);
  return cache.errors;
}

void Project::Loc::invalidateCache()
{
  cache = {};
}
