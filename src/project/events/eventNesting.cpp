/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventNesting.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

#include "eventBindings.h"
#include "eventPins.h"
#include "eventRules.h"
#include "../project.h"
#include "../scene/object.h"
#include "../scene/prefab.h"

namespace fs = std::filesystem;
using namespace Project::Events;

namespace
{
  void namesOf(const Bindings &bindings, std::vector<std::string> &out)
  {
    for(const auto &[pin, names] : bindings.in) out.insert(out.end(), names.begin(), names.end());
    for(const auto &[pin, names] : bindings.out) out.insert(out.end(), names.begin(), names.end());
  }

  // every name one node writes in its own graph: its bindings and, on an instance node, the names
  // its overrides add and the targets of its links (mutes name bindings of the inner prefab)
  void namesOfNode(const Project::Object &obj, std::vector<std::string> &out)
  {
    namesOf(obj.events, out);
    for(const auto &comp : obj.components) namesOf(comp.events, out);
    for(const auto &ovr : obj.eventOverrides) {
      if(ovr.isLink()) out.push_back(ovr.to);
      else out.insert(out.end(), ovr.add.begin(), ovr.add.end());
    }
  }

  void namesOfTree(const Project::Object &obj, std::vector<std::string> &out, const Project::Object *skip = nullptr)
  {
    if(&obj == skip)return;
    namesOfNode(obj, out);
    for(const auto &child : obj.children) namesOfTree(*child, out, skip);
  }

  void forEachName(Project::Object &obj, const std::function<void(std::string&)> &fn)
  {
    auto inBindings = [&](Bindings &bindings) {
      for(auto *map : {&bindings.in, &bindings.out}) {
        for(auto &[pin, names] : *map) for(auto &name : names) fn(name);
      }
    };
    inBindings(obj.events);
    for(auto &comp : obj.components) inBindings(comp.events);
    for(auto &ovr : obj.eventOverrides) {
      if(ovr.isLink()) fn(ovr.to);
      else for(auto &name : ovr.add) fn(name);
    }
  }

  // names used outside any one scene: by every prefab definition, the game code and the engine
  void projectNames(Project::Project &project, std::set<std::string> &out)
  {
    std::vector<std::string> names{};
    for(const auto &entry : project.getAssets().getTypeEntries(Project::FileType::PREFAB)) {
      if(entry.prefab) namesOfTree(entry.prefab->obj, names);
    }
    for(const auto &use : scanSourceDir(fs::path{project.getPath()} / "src" / "user")) {
      if(!use.isPin) names.push_back(use.name);
    }
    Catalog catalog{};
    std::vector<std::string> errors{};
    catalog.loadDir("data/events", true, errors);
    catalog.loadDir(fs::path{project.getPath()} / "events", false, errors);
    for(const auto &[name, ev] : catalog.events) names.push_back(name);
    out.insert(names.begin(), names.end());
  }

  const Override* findLink(const Project::Object &instance, const std::string &link)
  {
    for(const auto &ovr : instance.eventOverrides) {
      if(ovr.isLink() && ovr.link == link) return &ovr;
    }
    return nullptr;
  }

  uint8_t prefabNumber(const std::string &name)
  {
    return parse(name).id.up;
  }

  void linksOfPrefab(Project::Project &project, uint64_t uuid, std::set<std::string> &out, std::set<uint64_t> &visiting);

  // one node of a prefab's own graph and everything below it that is written in the same graph
  void linksOfGraph(Project::Project &project, const Project::Object &node, std::set<std::string> &out, std::set<uint64_t> &visiting)
  {
    std::vector<std::string> names{};
    namesOfNode(node, names);
    for(const auto &name : names) {
      if(isPrefabName(name) && prefabNumber(name) >= 1) out.insert(name);
    }
    if(node.isPrefabInstance()) {
      // the nested prefab's links: repointed (counted above as link targets), or one number lower
      std::set<std::string> inner{};
      linksOfPrefab(project, node.uuidPrefab.value, inner, visiting);
      for(const auto &link : inner) {
        if(findLink(node, link))continue;
        auto here = shiftPrefabNumber(link, -1);
        if(prefabNumber(here) >= 1) out.insert(here);
      }
    }
    for(const auto &child : node.children) linksOfGraph(project, *child, out, visiting);
  }

  void linksOfPrefab(Project::Project &project, uint64_t uuid, std::set<std::string> &out, std::set<uint64_t> &visiting)
  {
    if(!visiting.insert(uuid).second)return; // a prefab containing itself: refused by the editor anyway
    if(auto prefab = project.getAssets().getPrefabByUUID(uuid)) linksOfGraph(project, prefab->obj, out, visiting);
    visiting.erase(uuid);
  }

  void addUnique(std::vector<std::string> &list, const std::string &name)
  {
    if(std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
  }
}

std::vector<std::string> Project::Events::Nesting::parentLinks(Project &project, uint64_t prefabUUID)
{
  std::set<std::string> links{};
  std::set<uint64_t> visiting{};
  linksOfPrefab(project, prefabUUID, links, visiting);
  return {links.begin(), links.end()};
}

void Project::Events::Nesting::beforeWrap(Project &project, Object &sceneRoot, Object &obj, Report &report)
{
  std::vector<std::string> inside{}, outside{};
  namesOfTree(obj, inside);
  namesOfTree(sceneRoot, outside, &obj);

  std::set<std::string> used{outside.begin(), outside.end()};
  projectNames(project, used);

  std::set<std::string> local{};
  for(const auto &name : inside) {
    if(namespaceOf(name) == "scene" && nameProblem(name).empty() && !used.contains(name)) local.insert(name);
  }
  if(local.empty())return;

  std::function<void(Object&)> rewrite = [&](Object &node) {
    forEachName(node, [&](std::string &name) { name = nameAfterWrap(name, local); });
    for(auto &child : node.children) rewrite(*child);
  };
  rewrite(obj);

  for(const auto &name : local) {
    report.push_back("'" + name + "' is only used inside '" + obj.name + "', so it became '" +
      nameAfterWrap(name, local) + "': each copy of the new prefab has its own");
  }
}

void Project::Events::Nesting::afterUnpack(Project &project, Object &sceneRoot, Object &inst,
                                           const std::vector<UnpackedNode> &nodes, Report &report)
{
  // the overrides are applied below; what is left of them moves to nested instances
  std::vector<Override> pending = inst.eventOverrides;
  inst.eventOverrides.clear();

  UnpackRules rules{};
  for(const auto &ovr : pending) {
    if(ovr.isLink()) rules.links.push_back({ovr.link, ovr.to});
  }
  std::erase_if(pending, [](const Override &o) { return o.isLink(); });

  // 1. the prefab's own prefab:: events: written by its content, or reached by a nested
  //    instance's link that is not repointed (prefab_1:: there is prefab:: here)
  std::map<std::string, int> written{}, viaLinks{};
  for(const auto &node : nodes) {
    std::vector<std::string> names{};
    namesOfNode(*node.def, names);
    for(const auto &name : names) {
      if(isPrefabName(name) && prefabNumber(name) == 0) ++written[canonicalName(name)];
    }
    if(node.def->isPrefabInstance()) {
      for(const auto &link : parentLinks(project, node.def->uuidPrefab.value)) {
        if(findLink(*node.def, link))continue;
        auto here = shiftPrefabNumber(link, -1);
        if(prefabNumber(here) == 0) ++viaLinks[canonicalName(here)];
      }
    }
  }

  // 2. scene names already used for something else
  std::set<std::string> taken{};
  {
    std::vector<std::string> names{};
    namesOfTree(sceneRoot, names);
    for(const auto &ovr : pending) names.insert(names.end(), ovr.add.begin(), ovr.add.end());
    for(const auto &[from, to] : rules.links) names.push_back(to);
    taken.insert(names.begin(), names.end());
    projectNames(project, taken);
  }
  auto isTaken = [&](const std::string &name) { return taken.contains(name); };

  // 3. a scene name for each of them. One that only a single link reaches connects to nothing,
  //    so its link stays as it is.
  std::set<std::string> unused{};
  std::vector<std::pair<std::string, std::string>> lifecycle{}; // root output pin -> scene name
  std::set<std::string> locals{};
  for(const auto &[name, n] : written) locals.insert(name);
  for(const auto &[name, n] : viaLinks) locals.insert(name);
  for(const auto &name : locals)
  {
    const int uses = (written.contains(name) ? written[name] : 0) + (viaLinks.contains(name) ? viaLinks[name] : 0);
    const auto pin = lifecyclePin(name);
    if(!pin.empty()) {
      auto mapped = freeSceneName(name, inst.name, isTaken, true);
      taken.insert(mapped);
      rules.local[name] = mapped;
      lifecycle.push_back({pin, mapped});
      report.push_back("'" + name + "' of '" + inst.name + "' became '" + mapped + "', fired by its " + pin + " pin");
      continue;
    }
    if(!written.contains(name) && uses == 1) { unused.insert(name); continue; }
    auto mapped = freeSceneName(name, inst.name, isTaken);
    taken.insert(mapped);
    rules.local[name] = mapped;
    report.push_back("'" + name + "' of '" + inst.name + "' became '" + mapped + "'");
  }

  // a binding of the definition, in the scene's terms; overrides on the same pin are applied
  auto convert = [&](const Bindings &src, const std::vector<uint32_t> &path, uint64_t comp, const std::string &where) {
    Bindings res = src;
    for(auto &ovr : pending) {
      if(ovr.path != path || ovr.comp != comp)continue;
      auto &names = (ovr.input ? res.in : res.out)[ovr.pin];
      for(auto it = ovr.mute.begin(); it != ovr.mute.end();) {
        auto found = std::find(names.begin(), names.end(), *it);
        if(found != names.end()) { names.erase(found); it = ovr.mute.erase(it); }
        else ++it;
      }
    }
    for(auto *map : {&res.in, &res.out}) {
      for(auto &[pin, names] : *map) {
        for(auto it = names.begin(); it != names.end();) {
          auto mapped = nameAfterUnpack(*it, rules);
          if(mapped.empty()) {
            report.push_back(where + "." + pin + ": '" + *it + "' reached nothing in the scene and was removed");
            it = names.erase(it);
          } else {
            *it = mapped;
            ++it;
          }
        }
      }
    }
    for(auto &ovr : pending) {
      if(ovr.path != path || ovr.comp != comp)continue;
      for(const auto &name : ovr.add) {
        auto &names = (ovr.input ? res.in : res.out)[ovr.pin];
        addUnique(names, name);
      }
      ovr.add.clear();
    }
    res.prune();
    return res;
  };

  auto merge = [](Bindings &dst, const Bindings &src) {
    for(const auto &[pin, names] : src.in) for(const auto &name : names) dst.add(true, pin, name);
    for(const auto &[pin, names] : src.out) for(const auto &name : names) dst.add(false, pin, name);
  };

  // 4. the bindings of every node
  for(const auto &node : nodes)
  {
    const auto &def = *node.def;
    auto &obj = *node.obj;
    const std::string where = node.path.empty() ? inst.name : inst.name + " / " + obj.name;

    merge(obj.events, convert(def.events, node.path, 0, where));
    if(!def.isPrefabInstance()) {
      for(const auto &comp : def.components) {
        auto it = std::find_if(obj.components.begin(), obj.components.end(), [&](const auto &c) { return c.uuid == comp.uuid; });
        if(it != obj.components.end()) it->events = convert(comp.events, node.path, comp.uuid, where + " / " + displayName(project, *it));
      }
      continue;
    }

    // a nested instance: its overrides and links, now written in the scene's graph
    obj.eventOverrides.clear();
    for(const auto &ovr : def.eventOverrides)
    {
      Override moved = ovr;
      if(ovr.isLink()) {
        moved.to = nameAfterUnpack(ovr.to, rules);
        if(moved.to.empty())continue; // reaches nothing, like the default link in a scene
      } else {
        moved.add.clear();
        for(const auto &name : ovr.add) {
          auto mapped = nameAfterUnpack(name, rules);
          if(mapped.empty()) report.push_back(where + "." + ovr.pin + ": '" + name + "' reached nothing in the scene and was removed");
          else addUnique(moved.add, mapped);
        }
        if(moved.empty())continue;
      }
      obj.eventOverrides.push_back(moved);
    }
    for(const auto &link : parentLinks(project, def.uuidPrefab.value)) {
      if(findLink(def, link))continue;
      auto here = shiftPrefabNumber(link, -1);
      if(prefabNumber(here) == 0 && unused.contains(canonicalName(here)))continue;
      auto to = nameAfterUnpack(here, rules);
      if(to.empty())continue; // reaches nothing, like the default link in a scene
      obj.eventOverrides.push_back({.link = link, .to = to});
      report.push_back("'" + obj.name + "': its link '" + link + "' now points at '" + to + "'");
    }
  }

  // 5. overrides on content of nested instances move to those instances
  for(auto &ovr : pending)
  {
    if(ovr.empty())continue; // fully applied above
    const bool plainTarget = std::any_of(nodes.begin(), nodes.end(), [&](const UnpackedNode &node) {
      return !node.def->isPrefabInstance() && node.path == ovr.path;
    });
    if(plainTarget)continue; // what is left mutes a binding that no longer exists
    const UnpackedNode *owner = nullptr;
    for(const auto &node : nodes) {
      if(!node.def->isPrefabInstance() || node.path.size() > ovr.path.size())continue;
      if(!std::equal(node.path.begin(), node.path.end(), ovr.path.begin()))continue;
      if(!owner || node.path.size() > owner->path.size()) owner = &node;
    }
    if(!owner) {
      report.push_back("an override of '" + inst.name + "' on pin " + ovr.pin + " targets an object that no longer exists; it was removed");
      continue;
    }
    Override moved = ovr;
    moved.path.assign(ovr.path.begin() + (long)owner->path.size(), ovr.path.end());
    moved.hint.clear();
    owner->obj->eventOverrides.push_back(moved);
  }

  // 6. the root's own pins fire what were the instance's life-cycle events
  for(const auto &[pin, name] : lifecycle) inst.events.add(false, pin, name);
  inst.events.prune();
}
