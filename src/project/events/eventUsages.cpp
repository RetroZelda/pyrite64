/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventUsages.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>

#include "eventBindings.h"
#include "eventGraph.h"
#include "eventPins.h"
#include "eventRules.h"
#include "../project.h"
#include "../../utils/fs.h"
#include "../scene/scene.h"
#include "../scene/sceneManager.h"
#include "../scene/prefab.h"
#include "../scene/object.h"
#include "../canvas/canvas.h"
#include "../component/components.h"

namespace fs = std::filesystem;
using namespace Project::Events;
using Usage = Project::Events::Usages::Use;

namespace
{
  void usesOfObject(Project::Project &project, Project::Object &obj, const std::string &parent, Usage::Kind kind,
                    int sceneId, std::vector<Usage> &out)
  {
    const std::string where = parent + " / " + obj.name;
    auto add = [&](const std::string &name, bool input, const std::string &w) {
      out.push_back({name, kind, w, input, sceneId, obj.uuid});
    };
    auto bindings = [&](const Bindings &b, const std::string &w) {
      for(const auto &[pin, names] : b.in) for(const auto &n : names) add(n, true, w + "." + pin);
      for(const auto &[pin, names] : b.out) for(const auto &n : names) add(n, false, w + "." + pin);
    };
    bindings(obj.events, where);
    for(const auto &comp : obj.components) bindings(comp.events, where + " / " + displayName(project, comp));
    for(const auto &ovr : obj.eventOverrides) {
      if(ovr.isLink()) add(ovr.to, true, where + " (link " + ovr.link + ")");
      else for(const auto &n : ovr.add) add(n, ovr.input, where + " (override on " + ovr.pin + ")");
    }
    for(const auto &child : obj.children) usesOfObject(project, *child, where, kind, sceneId, out);
  }

  // the scenes of the project: the open one as it is in memory, the others from their files
  void forEachScene(Project::Project &project, Project::Scene *openScene,
                    const std::function<void(Project::Scene&, bool open)> &fn)
  {
    for(const auto &entry : project.getScenes().getEntries()) {
      if(openScene && openScene->getId() == entry.getId()) {
        fn(*openScene, true);
        continue;
      }
      if(!fs::exists(fs::path{project.getPath()} / "data" / "scenes" / std::to_string(entry.getId()) / "scene.json"))continue;
      Project::Scene scene{entry.getId(), project.getPath()};
      fn(scene, false);
    }
  }

  bool renameIn(std::vector<std::string> &names, const std::string &from, const std::string &to, int &count)
  {
    bool changed = false;
    for(auto &n : names) {
      if(Graph::key(n) != Graph::key(from))continue;
      n = to;
      ++count;
      changed = true;
    }
    // two bindings may now be the same one
    std::vector<std::string> unique{};
    for(const auto &n : names) if(std::find(unique.begin(), unique.end(), n) == unique.end()) unique.push_back(n);
    names = unique;
    return changed;
  }

  bool renameBindings(Bindings &b, const std::string &from, const std::string &to, int &count)
  {
    bool changed = false;
    for(auto *map : {&b.in, &b.out}) {
      for(auto &[pin, names] : *map) changed |= renameIn(names, from, to, count);
    }
    return changed;
  }

  bool renameObject(Project::Object &obj, const std::string &from, const std::string &to, int &count)
  {
    bool changed = renameBindings(obj.events, from, to, count);
    for(auto &comp : obj.components) changed |= renameBindings(comp.events, from, to, count);
    for(auto &ovr : obj.eventOverrides) {
      if(ovr.isLink()) {
        // the link as the nested prefab writes it, and where it points
        if(Graph::key(ovr.link) == Graph::key(from)) { ovr.link = to; ++count; changed = true; }
        if(Graph::key(ovr.to) == Graph::key(from)) { ovr.to = to; ++count; changed = true; }
      } else {
        changed |= renameIn(ovr.add, from, to, count);
        changed |= renameIn(ovr.mute, from, to, count);
      }
    }
    for(auto &child : obj.children) changed |= renameObject(*child, from, to, count);
    return changed;
  }
}

std::vector<Usage> Project::Events::Usages::collect(Project &project, Scene *openScene)
{
  std::vector<Usage> res{};
  forEachScene(project, openScene, [&](Scene &scene, bool) {
    const std::string where = "scene " + std::to_string(scene.getId()) + " (" + scene.getName() + ")";
    for(auto &child : scene.getRootObject().children) usesOfObject(project, *child, where, Usage::Kind::Scene, scene.getId(), res);
  });
  for(const auto &entry : project.getAssets().getTypeEntries(FileType::PREFAB)) {
    if(entry.prefab) usesOfObject(project, entry.prefab->obj, "prefab " + entry.name, Usage::Kind::Prefab, 0, res);
  }
  for(const auto &entry : project.getAssets().getTypeEntries(FileType::CANVAS)) {
    if(!entry.canvas)continue;
    const auto &canvas = *entry.canvas;
    for(const bool input : {true, false}) {
      for(const auto &[pin, names] : input ? canvas.bindings.in : canvas.bindings.out) {
        for(const auto &n : names) res.push_back({n, Usage::Kind::Canvas, "canvas " + canvas.name + "." + pin, input, 0, 0});
      }
    }
  }
  for(const auto &use : scanSourceDir(fs::path{project.getPath()} / "src" / "user")) {
    if(use.isPin)continue;
    // Fork: conditions, a trigger listens
    res.push_back({use.name, Usage::Kind::Code, use.file + ":" + std::to_string(use.line) + (use.trigger ? " (condition trigger)" : ""),
                   use.trigger, 0, 0});
  }
  // Fork: conditions, the events node graphs fire
  for(const auto &graph : project.getAssets().getTypeEntries(FileType::NODE_GRAPH)) {
    for(const auto &use : scanNodeGraph(Utils::FS::loadTextFile(graph.path), graph.name)) {
      res.push_back({use.name, Usage::Kind::Code, "node graph " + graph.name + " (Fire Event)", false, 0, 0});
    }
  }
  std::stable_sort(res.begin(), res.end(), [](const Usage &a, const Usage &b) {
    return Graph::key(a.name) < Graph::key(b.name);
  });
  return res;
}

Project::Events::Usages::RenameResult Project::Events::Usages::rename(Project &project, Scene *openScene,
                                                                      const std::string &from, const std::string &to)
{
  RenameResult res{};
  if(from.empty() || to.empty() || Graph::key(from) == Graph::key(to)) return res;

  forEachScene(project, openScene, [&](Scene &scene, bool open) {
    bool changed = false;
    for(auto &child : scene.getRootObject().children) changed |= renameObject(*child, from, to, res.bindings);
    if(changed && !open) {
      scene.save();
      res.files.push_back("data/scenes/" + std::to_string(scene.getId()) + "/scene.json");
    }
  });
  for(const auto &entry : project.getAssets().getTypeEntries(FileType::PREFAB)) {
    if(entry.prefab && renameObject(entry.prefab->obj, from, to, res.bindings)) {
      entry.prefab->save(entry.path);
      res.files.push_back(entry.path);
    }
  }
  for(const auto &entry : project.getAssets().getTypeEntries(FileType::CANVAS)) {
    if(entry.canvas && renameBindings(entry.canvas->bindings, from, to, res.bindings)) {
      entry.canvas->save();
      res.files.push_back(entry.canvas->path);
    }
  }
  // Fork: conditions, node graphs are reported like code: the name is typed into a Fire Event node
  for(const auto &graph : project.getAssets().getTypeEntries(FileType::NODE_GRAPH)) {
    for(const auto &use : scanNodeGraph(Utils::FS::loadTextFile(graph.path), graph.name)) {
      if(Graph::key(use.name) == Graph::key(from)) res.code.push_back("node graph " + graph.name + " (Fire Event)");
    }
  }
  for(const auto &use : scanSourceDir(fs::path{project.getPath()} / "src" / "user")) {
    if(!use.isPin && Graph::key(use.name) == Graph::key(from)) res.code.push_back(use.file + ":" + std::to_string(use.line));
  }
  return res;
}
