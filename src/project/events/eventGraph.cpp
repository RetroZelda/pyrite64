/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventGraph.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <tuple>

#include "eventBindings.h"
#include "eventNesting.h"
#include "eventPins.h"
#include "eventRules.h"
#include "../project.h"
#include "../../utils/fs.h"
#include "../scene/object.h"
#include "../scene/prefab.h"
#include "../component/components.h"
#include "../canvas/canvas.h"

namespace fs = std::filesystem;
using namespace Project::Events;
using namespace Project::Events::Graph;

namespace
{
  constexpr int COMP_CODE = 0;
  constexpr uint64_t ENGINE_SCRIPT_FIRST = 0xC0E0000000000001ull;
  constexpr uint64_t ENGINE_SCRIPT_LAST = 0xC0E0000000000013ull; // Fork: conditions, And to Not are 10 to 13
  constexpr uint64_t SCRIPT_DELAY = 0xC0E0000000000002ull;
  constexpr uint64_t SCRIPT_FRAME_DELAY = 0xC0E0000000000003ull;

  // ---- the objects behind a graph

  // the nodes of a graph: the objects written in its file (an instance's children are the
  // objects added to it, never its prefab's content)
  void walk(Project::Object &obj, std::vector<Project::Object*> &out)
  {
    out.push_back(&obj);
    for(auto &child : obj.children) walk(*child, out);
  }

  Project::Object* findObj(Project::Object &root, uint32_t uuid)
  {
    if(root.uuid == uuid) return &root;
    for(auto &child : root.children) {
      if(auto *found = findObj(*child, uuid)) return found;
    }
    return nullptr;
  }

  Project::Component::Entry* findComp(Project::Object &obj, uint64_t uuid)
  {
    for(auto &comp : obj.components) if(comp.uuid == uuid) return &comp;
    return nullptr;
  }

  Bindings* bindingsOf(Project::Object &obj, uint64_t comp)
  {
    if(comp == 0) return &obj.events;
    auto *entry = findComp(obj, comp);
    return entry ? &entry->events : nullptr;
  }

  // an object inside an instance, following the prefab definitions like the inspector does
  struct Inner
  {
    Project::Object *node{}; // the definition node
    Project::Object *src{};  // where its components are: its prefab's root when it is an instance
  };

  Project::Object* defRoot(Project::Project &project, const Project::Object &instance)
  {
    auto prefab = project.getAssets().getPrefabByUUID(instance.uuidPrefab.value);
    return prefab ? &prefab->obj : nullptr;
  }

  Inner resolveInner(Project::Project &project, Project::Object &instance, const std::vector<uint32_t> &path)
  {
    Project::Object *node = defRoot(project, instance);
    if(!node) return {};
    for(auto uuid : path) {
      Project::Object *next = nullptr;
      for(auto &c : node->children) if(c->uuid == uuid) { next = c.get(); break; }
      if(!next && node->isPrefabInstance()) {
        if(auto *inner = defRoot(project, *node)) {
          for(auto &c : inner->children) if(c->uuid == uuid) { next = c.get(); break; }
        }
      }
      if(!next) return {};
      node = next;
    }
    Project::Object *src = node;
    if(node->isPrefabInstance()) {
      if(auto *inner = defRoot(project, *node)) src = inner;
    }
    return {node, src};
  }

  std::string sanitize(const std::string &text)
  {
    std::string res{};
    for(char c : text) {
      if(std::isalnum(static_cast<unsigned char>(c)) || c == '_') res += c;
    }
    return res.empty() ? std::string{"Object"} : res;
  }

  // ---- problems, as the build reports them

  struct Rules
  {
    Catalog catalog{};
    Index index{}; // the global systems only
  };

  Rules loadRules(Project::Project &project)
  {
    Rules rules{};
    std::vector<std::string> errors{};
    rules.catalog.loadDir("data/events", true, errors);
    rules.catalog.loadDir(fs::path{project.getPath()} / "events", false, errors);
    std::vector<Message> messages{};
    for(const auto &script : project.getAssets().getTypeEntries(Project::FileType::CODE_GLOBAL)) {
      for(const auto &sys : script.systemPins) {
        rules.index.addSystem({sys.system, script.name, sys.in, sys.out}, messages);
      }
    }
    return rules;
  }

  struct Ctx
  {
    Project::Project &project;
    Rules rules;
    Kind kind;
  };

  std::string problemOf(const Ctx &c, const std::string &name, bool input, bool ownBinding)
  {
    if(auto problem = nameProblem(name); !problem.empty()) return problem;
    const auto ns = namespaceOf(name);
    if(!c.rules.catalog.isDeclaredNamespace(ns)) {
      return "the namespace '" + ns + "' is not declared; add it to a file in the project's events/ folder, or use a built-in namespace";
    }
    if(c.kind == Kind::Scene && ownBinding && isPrefabName(name)) {
      return "prefab:: names are not allowed in a scene's own graph";
    }
    return bindingProblem(c.rules.index, c.rules.catalog, name, input);
  }

  std::string hintOf(const Ctx &c, const std::string &name)
  {
    if(!isPrefabName(name)) return {};
    if(c.kind == Kind::Scene) return "'" + name + "' points above the outermost prefab here, so it reaches nothing";
    if(parse(name).id.up >= 1) return "'" + name + "' is an event of a prefab around this one";
    return {};
  }

  void finishPin(const Ctx &c, Pin &pin, bool ownBinding)
  {
    for(const auto &name : pin.names) {
      if(pin.problem.empty()) pin.problem = problemOf(c, name, pin.ref.input, ownBinding);
      if(pin.hint.empty()) pin.hint = hintOf(c, name);
    }
    if(!pin.declared && pin.problem.empty()) {
      pin.problem = "This pin is no longer declared; its bindings are kept but do nothing";
    }
  }

  // pins of one component or of the object's own pins: the declared ones, then any the file still binds
  void addPins(const Ctx &c, Category &cat, const PinRef &base, const PinList &declared, const Bindings &bindings)
  {
    for(const bool input : {true, false}) {
      const auto &map = input ? bindings.in : bindings.out;
      std::vector<std::pair<std::string, bool>> rows{};
      for(const auto &pin : input ? declared.in : declared.out) rows.push_back({pin, true});
      for(const auto &[pin, names] : map) {
        if(declared.indexOf(input, pin) < 0 && !names.empty()) rows.push_back({pin, false});
      }
      for(const auto &[name, isDeclared] : rows) {
        Pin pin{};
        pin.ref = base;
        pin.ref.pin = name;
        pin.ref.input = input;
        pin.label = name;
        pin.declared = isDeclared;
        if(auto it = map.find(name); it != map.end()) pin.names = it->second;
        finishPin(c, pin, true);
        cat.pins.push_back(pin);
      }
    }
  }

  // Fork: conditions. What the pins of a condition script's category are for, and the events the
  // script itself binds to Check.
  void markConditionPins(Category &cat, const PinList &pins)
  {
    if(!pins.isCondition())return;
    for(auto &pin : cat.pins) {
      if(!pin.declared)continue;
      if(pin.ref.input) {
        pin.reads = pins.readsConditions(pin.ref.pin);
        if(pin.ref.pin == "Check" && pins.isConditionPin(true, pin.ref.pin)) {
          for(const auto &trigger : pins.triggers) {
            const bool written = std::any_of(pin.names.begin(), pin.names.end(), [&](const std::string &n) { return key(n) == key(trigger); });
            if(!written) pin.locked.push_back(trigger);
          }
        }
      } else {
        pin.result = pin.ref.pin == "Result" && pins.isConditionPin(false, pin.ref.pin);
      }
    }
  }

  // Fork: conditions. The static values of a condition, as the node shows them: "Compare: Less", "Value: 25"
  std::vector<std::string> conditionSummary(const Ctx &c, const Project::Component::Entry &comp)
  {
    auto *script = c.project.getAssets().getEntryByUUID(Project::Component::Code::getScriptUUID(comp));
    if(!script) return {};
    std::vector<std::string> res{};
    for(const auto &field : script->params.fields) {
      auto label = field.attr.find("P64::Name");
      if(label == field.attr.end())continue;
      auto value = Project::Component::Code::getStoredArg(comp, field.name);
      if(value.empty()) value = field.defaultValue;
      if(field.name == "conditionStartsEnabled") {
        // the usual case says nothing
        if(value == "0" || value == "false") res.emplace_back("Starts disabled");
        continue;
      }
      if(field.name == "conditionAutoCheck") {
        if(value == "0" || value == "false") res.emplace_back("Auto Check off");
        continue;
      }
      if(!field.enumValues.empty()) {
        try {
          const auto number = std::stoll(value.empty() ? "0" : value);
          for(const auto &[enumValue, enumName] : field.enumValues) {
            if(enumValue == number) { value = enumName; break; }
          }
        } catch(...) {}
      } else if(field.type == Utils::DataType::BOOL) {
        value = (value == "1" || value == "true") ? "on" : "off";
      }
      res.push_back(label->second + ": " + value);
    }
    return res;
  }

  Node plainNode(const Ctx &c, Project::Object &obj)
  {
    Node node{};
    node.uuid = obj.uuid;
    node.name = obj.name;

    Category own{"o", "Object", false, {}};
    addPins(c, own, {obj.uuid, {}, 0, {}, false, false}, objectPins(), obj.events);
    node.categories.push_back(own);

    for(auto &comp : obj.components) {
      auto pins = pinsOf(c.project, comp);
      if(pins.empty() && comp.events.empty())continue;
      Category cat{"c:" + std::to_string(comp.uuid), displayName(c.project, comp), true, {}};
      addPins(c, cat, {obj.uuid, {}, comp.uuid, {}, false, false}, pins, comp.events);
      markConditionPins(cat, pins);
      node.categories.push_back(cat);
      if(comp.id == COMP_CODE) {
        const auto script = Project::Component::Code::getScriptUUID(comp);
        if(script >= ENGINE_SCRIPT_FIRST && script <= ENGINE_SCRIPT_LAST) node.builtin = true;
        if(script == SCRIPT_DELAY || script == SCRIPT_FRAME_DELAY) node.delay = true;
        if(pins.isCondition() && !node.condition) {
          node.condition = true;
          node.summary = conditionSummary(c, comp);
        }
      }
    }
    return node;
  }

  const Override* findOverride(const Project::Object &inst, const PinRef &ref)
  {
    for(const auto &ovr : inst.eventOverrides) {
      if(!ovr.isLink() && ovr.path == ref.path && ovr.comp == ref.comp && ovr.pin == ref.pin && ovr.input == ref.input) return &ovr;
    }
    return nullptr;
  }

  const Override* findLink(const Project::Object &inst, const std::string &link)
  {
    for(const auto &ovr : inst.eventOverrides) {
      if(ovr.isLink() && ovr.link == link) return &ovr;
    }
    return nullptr;
  }

  // a pin of an instance node: what the prefab binds it to that reaches this graph, minus what
  // this instance mutes, plus what it adds
  Pin instancePin(const Ctx &c, Project::Object &inst, const PinRef &ref, const Bindings *defBindings, const std::string &label)
  {
    Pin pin{};
    pin.ref = ref;
    pin.label = label;
    const auto *ovr = findOverride(inst, ref);
    if(ovr) pin.muted = ovr->mute;
    if(defBindings) {
      const auto &map = ref.input ? defBindings->in : defBindings->out;
      if(auto it = map.find(ref.pin); it != map.end()) {
        for(const auto &name : it->second) {
          // prefab:: names stay inside the prefab and object:: names inside the object
          if(isPrefabName(name) || namespaceOf(name) == "object")continue;
          if(std::find(pin.muted.begin(), pin.muted.end(), name) != pin.muted.end())continue;
          pin.names.push_back(name);
        }
      }
    }
    if(ref.path.empty() && ref.comp == 0) {
      // bindings written on the instance node itself (the build reads them like overrides)
      const auto &map = ref.input ? inst.events.in : inst.events.out;
      if(auto it = map.find(ref.pin); it != map.end()) pin.names.insert(pin.names.end(), it->second.begin(), it->second.end());
    }
    if(ovr) {
      for(const auto &name : ovr->add) {
        if(std::find(pin.names.begin(), pin.names.end(), name) == pin.names.end()) pin.names.push_back(name);
      }
    }
    finishPin(c, pin, true);
    return pin;
  }

  std::string linkLabel(const std::string &link)
  {
    auto here = shiftPrefabNumber(link, -1);
    if(here.rfind("prefab::", 0) == 0) return here.substr(8);
    return here;
  }

  Node instanceNode(const Ctx &c, Project::Object &inst)
  {
    Node node{};
    node.uuid = inst.uuid;
    node.name = inst.name;
    node.instance = true;
    auto prefab = c.project.getAssets().getPrefabByUUID(inst.uuidPrefab.value);
    if(!prefab) {
      node.prefab = "missing prefab";
      return node;
    }
    auto &def = prefab->obj;
    node.prefab = def.name;

    // the root's pins are always shown
    {
      Category own{"p::0", "Object", false, {}};
      for(const bool input : {true, false}) {
        for(const auto &pinName : input ? objectPins().in : objectPins().out) {
          own.pins.push_back(instancePin(c, inst, {inst.uuid, {}, 0, pinName, input, false}, &def.events, pinName));
        }
      }
      node.categories.push_back(own);
    }
    for(auto &comp : def.components) {
      auto pins = pinsOf(c.project, comp);
      if(pins.empty())continue;
      Category cat{"p::" + std::to_string(comp.uuid), displayName(c.project, comp), false, {}};
      for(const bool input : {true, false}) {
        for(const auto &pinName : input ? pins.in : pins.out) {
          cat.pins.push_back(instancePin(c, inst, {inst.uuid, {}, comp.uuid, pinName, input, false}, &comp.events, pinName));
        }
      }
      markConditionPins(cat, pins);
      if(pins.isCondition()) node.condition = true;
      node.categories.push_back(cat);
    }
    // components added to this instance are written in this graph, like a plain object's
    for(auto &comp : inst.components) {
      auto pins = pinsOf(c.project, comp);
      if(pins.empty() && comp.events.empty())continue;
      Category cat{"c:" + std::to_string(comp.uuid), displayName(c.project, comp) + " (this instance)", true, {}};
      addPins(c, cat, {inst.uuid, {}, comp.uuid, {}, false, false}, pins, comp.events);
      markConditionPins(cat, pins);
      node.categories.push_back(cat);
    }

    // pins marked as exposed in the prefab, and pins this instance overrides, grouped by inner
    // object and component
    std::vector<Exposed> shownInner{};
    for(const auto &e : def.eventExposed) {
      if(!e.path.empty()) shownInner.push_back(e);
    }
    for(const auto &ovr : inst.eventOverrides) {
      if(ovr.isLink() || ovr.path.empty())continue;
      const bool known = std::any_of(shownInner.begin(), shownInner.end(), [&](const Exposed &e) {
        return e.path == ovr.path && e.comp == ovr.comp && e.pin == ovr.pin && e.input == ovr.input;
      });
      if(!known) shownInner.push_back({ovr.path, ovr.comp, ovr.pin, ovr.input, {}});
    }
    std::map<std::pair<std::vector<uint32_t>, uint64_t>, Category> exposed{};
    for(const auto &e : shownInner) {
      auto inner = resolveInner(c.project, inst, e.path);
      if(!inner.node)continue;
      auto &cat = exposed[{e.path, e.comp}];
      const Bindings *bindings = nullptr;
      std::string compName = "Object";
      if(e.comp == 0) {
        bindings = &inner.src->events;
      } else {
        auto *comp = findComp(*inner.src, e.comp);
        if(!comp) comp = findComp(*inner.node, e.comp);
        if(!comp)continue;
        bindings = &comp->events;
        compName = displayName(c.project, *comp);
      }
      if(cat.key.empty()) {
        std::string pathKey{};
        for(auto uuid : e.path) pathKey += (pathKey.empty() ? "" : "/") + std::to_string(uuid);
        cat.key = "p:" + pathKey + ":" + std::to_string(e.comp);
        cat.title = inner.node->name + " / " + compName;
        cat.openByDefault = true;
      }
      cat.pins.push_back(instancePin(c, inst, {inst.uuid, e.path, e.comp, e.pin, e.input, false}, bindings,
                                     e.alias.empty() ? e.pin : e.alias));
    }
    for(auto &[k, cat] : exposed) node.categories.push_back(cat);

    // parent links: the events of the graphs around the prefab that its content uses
    const auto uses = Nesting::parentLinkUses(c.project, inst.uuidPrefab.value);
    if(!uses.empty()) {
      Category links{"links", "Parent links", true, {}};
      for(const auto &[link, use] : uses) {
        const auto *ovr = findLink(inst, link);
        const auto target = ovr ? ovr->to : shiftPrefabNumber(link, -1);
        for(const bool input : {true, false}) {
          if(input ? !use.listened : !use.fired)continue;
          Pin pin{};
          pin.ref = {inst.uuid, {}, 0, link, input, true};
          pin.label = linkLabel(link);
          pin.names = {target};
          finishPin(c, pin, false);
          links.pins.push_back(pin);
        }
      }
      node.categories.push_back(links);
    }
    return node;
  }

  // a UI canvas: its pins grouped as the canvas editor shows them
  Node canvasNode(const Ctx &c, const Project::Canvas &canvas, uint64_t uuid)
  {
    Node node{};
    node.canvas = uuid;
    node.name = canvas.name;
    node.prefab = "UI canvas";
    const auto pins = canvasPins(canvas);
    Category show{"v", "Canvas", true, {}}, clips{"clips", "Clips", true, {}}, events{"events", "Events", true, {}}, focus{"focus", "Focus", true, {}};
    for(const bool input : {true, false}) {
      for(const auto &name : input ? pins.in : pins.out) {
        Pin pin{};
        pin.ref = {0, {}, 0, name, input, false, uuid};
        pin.label = name;
        const auto &map = input ? canvas.bindings.in : canvas.bindings.out;
        if(auto it = map.find(name); it != map.end()) pin.names = it->second;
        finishPin(c, pin, false);
        for(const auto &n : pin.names) {
          const auto scope = parse(n).id.scope;
          if(pin.problem.empty() && (scope == Scope::Prefab || scope == Scope::Object)) {
            pin.problem = "a canvas is not in the object tree, so it can only use ui::, scene::, global:: or a game namespace";
          }
        }
        Category &cat = (name == "Show" || name == "Hide") ? show
          : (name.starts_with("Event.") ? events
          : ((name.starts_with("Focused.") || name.starts_with("Blurred.") || name.starts_with("Activated.")) ? focus : clips));
        cat.pins.push_back(pin);
      }
    }
    for(auto *cat : {&show, &clips, &events, &focus}) if(!cat->pins.empty()) node.categories.push_back(*cat);
    return node;
  }

  Project::Canvas* findCanvasAsset(Project::Project &project, uint64_t uuid)
  {
    for(const auto &entry : project.getAssets().getTypeEntries(Project::FileType::CANVAS)) {
      if(entry.canvas && entry.conf.uuid == uuid) return entry.canvas.get();
    }
    return nullptr;
  }

  // a name a new wire between two pins can share: global:: names only connect through the
  // Global node (an input cannot listen to what an output fires into a system)
  bool usableHere(const Model &model, const std::string &name)
  {
    if(!nameProblem(name).empty()) return false;
    if(namespaceOf(name) == "global") return false;
    if(isPrefabName(name)) {
      if(model.kind == Kind::Scene) return false;
      if(parse(name).id.up >= 1) return false;
    }
    if(namespaceOf(name) == "object" && model.focus == 0) return false;
    return true;
  }
}

bool Project::Events::Graph::PinRef::operator<(const PinRef &other) const
{
  return std::tie(node, path, comp, pin, input, link, canvas) < std::tie(other.node, other.path, other.comp, other.pin, other.input, other.link, other.canvas);
}

bool Project::Events::Graph::Node::bound() const
{
  for(const auto &cat : categories) {
    for(const auto &pin : cat.pins) {
      if(pin.ref.link)continue; // every instance has its links; they do not make it interesting
      if(!pin.names.empty() || !pin.muted.empty() || !pin.locked.empty()) return true;
    }
  }
  return false;
}

const Node* Project::Events::Graph::Model::find(uint32_t uuid) const
{
  for(const auto &node : nodes) if(node.uuid == uuid) return &node;
  return nullptr;
}

const Node* Project::Events::Graph::Model::findCanvas(uint64_t uuid) const
{
  for(const auto &node : canvases) if(node.canvas == uuid) return &node;
  return nullptr;
}

const Pin* Project::Events::Graph::Model::findPin(const PinRef &ref) const
{
  const auto *node = ref.canvas ? findCanvas(ref.canvas) : find(ref.node);
  if(!node) return nullptr;
  for(const auto &cat : node->categories) {
    for(const auto &pin : cat.pins) if(pin.ref == ref) return &pin;
  }
  return nullptr;
}

std::string Project::Events::Graph::key(const std::string &name)
{
  return isPrefabName(name) ? shiftPrefabNumber(name, 0) : name;
}

Model Project::Events::Graph::collect(Project &project, Object &root, Kind kind, uint32_t focus)
{
  Ctx c{project, loadRules(project), kind};
  Model model{};
  model.kind = kind;
  model.focus = focus;
  model.ns = focus ? "object" : (kind == Kind::Scene ? "scene" : "prefab");

  std::vector<Object*> objects{};
  if(kind == Kind::Scene) {
    for(auto &child : root.children) walk(*child, objects);
  } else {
    walk(root, objects);
  }
  for(auto *obj : objects) {
    if(focus && obj->uuid != focus)continue;
    const bool asInstance = obj->isPrefabInstance() && !(kind == Kind::Prefab && obj == &root);
    model.nodes.push_back(asInstance ? instanceNode(c, *obj) : plainNode(c, *obj));
    if(focus) model.title = obj->name;
  }

  // the Global node: one region per system, and the engine's own events
  for(const auto &decl : c.rules.index.systems) model.systems.push_back({decl.system, decl.in, decl.out});
  std::sort(model.systems.begin(), model.systems.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
  System engine{"Engine", {}, {}};
  for(const auto &[name, ev] : c.rules.catalog.events) {
    if(ev.engine && name.rfind("global::Engine.", 0) == 0) engine.out.push_back(name.substr(15));
  }
  model.systems.push_back(engine);

  // the scope node: what the engine fires into this graph's scope
  const std::string scopeNs = kind == Kind::Scene ? "scene" : "prefab";
  for(const auto &[name, ev] : c.rules.catalog.events) {
    if(ev.engine && namespaceOf(name) == scopeNs) model.scopeEvents.push_back(name);
  }

  for(const auto &entry : project.getAssets().getTypeEntries(FileType::CANVAS)) {
    if(entry.canvas) model.canvases.push_back(canvasNode(c, *entry.canvas, entry.conf.uuid));
  }

  for(const auto &use : scanSourceDir(fs::path{project.getPath()} / "src" / "user")) {
    if(!use.isPin) model.codeNames.insert(key(use.name));
  }
  // Fork: conditions, the events node graphs fire ("Fire Event" nodes)
  for(const auto &graph : project.getAssets().getTypeEntries(FileType::NODE_GRAPH)) {
    for(const auto &use : scanNodeGraph(Utils::FS::loadTextFile(graph.path), graph.name)) {
      if(nameProblem(use.name).empty()) model.codeNames.insert(key(use.name));
    }
  }
  for(const auto &node : model.canvases) {
    for(const auto &cat : node.categories) {
      for(const auto &pin : cat.pins) for(const auto &name : pin.names) model.allNames.insert(key(name));
    }
  }
  for(const auto &node : model.nodes) {
    for(const auto &cat : node.categories) {
      for(const auto &pin : cat.pins) {
        for(const auto &name : pin.names) model.allNames.insert(key(name));
        for(const auto &name : pin.locked) model.allNames.insert(key(name));
      }
    }
  }
  return model;
}

std::vector<Wire> Project::Events::Graph::wires(const Model &model, const std::set<std::string> &eventNodes, bool globalShown, bool scopeShown)
{
  struct Sides { std::vector<PinRef> out{}, in{}; };
  std::map<std::string, Sides> byName{};
  std::vector<const Node*> all{};
  for(const auto &node : model.nodes) all.push_back(&node);
  for(const auto &node : model.canvases) all.push_back(&node);
  for(const auto *nodePtr : all) {
    const auto &node = *nodePtr;
    for(const auto &cat : node.categories) {
      for(const auto &pin : cat.pins) {
        for(const auto &name : pin.names) {
          if(!nameProblem(name).empty())continue;
          auto &sides = byName[key(name)];
          (pin.ref.input ? sides.in : sides.out).push_back(pin.ref);
        }
        // Fork: conditions, the triggers a script binds to its Check input are wires too
        for(const auto &name : pin.locked) {
          if(!nameProblem(name).empty())continue;
          byName[key(name)].in.push_back(pin.ref);
        }
      }
    }
  }

  std::set<std::string> systemIns{};
  for(const auto &sys : model.systems) {
    for(const auto &pin : sys.in) systemIns.insert("global::" + sys.name + "." + pin);
  }

  std::vector<Wire> res{};
  auto pinEnd = [](const PinRef &ref) { return End{End::Type::Pin, ref}; };
  for(const auto &[name, sides] : byName)
  {
    if(eventNodes.contains(name)) {
      for(const auto &out : sides.out) res.push_back({pinEnd(out), {End::Type::Event, {}}, name});
      for(const auto &in : sides.in) res.push_back({{End::Type::Event, {}}, pinEnd(in), name});
      continue;
    }
    const auto ns = namespaceOf(name);
    if(ns == "global") {
      if(!globalShown)continue;
      if(systemIns.contains(name)) {
        for(const auto &out : sides.out) res.push_back({pinEnd(out), {End::Type::Global, {}}, name});
      } else {
        for(const auto &in : sides.in) res.push_back({{End::Type::Global, {}}, pinEnd(in), name});
      }
      continue;
    }
    if(scopeShown && std::find(model.scopeEvents.begin(), model.scopeEvents.end(), name) != model.scopeEvents.end()) {
      for(const auto &in : sides.in) res.push_back({{End::Type::Scope, {}}, pinEnd(in), name});
      continue;
    }
    if(isPrefabName(name) && (model.kind == Kind::Scene || parse(name).id.up >= 1))continue;
    for(const auto &out : sides.out) {
      for(const auto &in : sides.in) {
        if(ns == "object" && out.node != in.node)continue;
        res.push_back({pinEnd(out), pinEnd(in), name});
      }
    }
  }
  return res;
}

std::string Project::Events::Graph::wireName(const Model &model, const PinRef &out)
{
  const auto *pin = model.findPin(out);
  if(!pin) return {};
  for(const auto &name : pin->names) {
    if(usableHere(model, name)) return name;
  }
  return {};
}

std::string Project::Events::Graph::autoName(const Model &model, const PinRef &out, const std::string &ns)
{
  const auto *node = out.canvas ? model.findCanvas(out.canvas) : model.find(out.node);
  const auto *pin = model.findPin(out);
  const std::string base = (ns.empty() ? model.ns : ns) + "::" + sanitize(node ? node->name : "Object") + "." +
                           sanitize(pin ? pin->label : out.pin);
  std::string candidate = base;
  for(int n = 2; model.allNames.contains(candidate) || model.codeNames.contains(candidate); ++n) {
    candidate = base + "_" + std::to_string(n);
  }
  return candidate;
}

bool Project::Events::Graph::bind(Project &project, Object &root, const Model &model, const PinRef &ref, const std::string &name)
{
  if(name.empty()) return false;
  if(ref.canvas) {
    auto *canvas = findCanvasAsset(project, ref.canvas);
    if(!canvas) return false;
    auto &names = (ref.input ? canvas->bindings.in : canvas->bindings.out)[ref.pin];
    if(std::find(names.begin(), names.end(), name) != names.end()) return false;
    names.push_back(name);
    canvas->save();
    return true;
  }
  const auto *node = model.find(ref.node);
  auto *obj = findObj(root, ref.node);
  if(!node || !obj) return false;

  if(ref.link) {
    std::erase_if(obj->eventOverrides, [&](const Override &o) { return o.isLink() && o.link == ref.pin; });
    if(name != shiftPrefabNumber(ref.pin, -1)) obj->eventOverrides.push_back({.link = ref.pin, .to = name});
    return true;
  }

  const bool direct = !node->instance || (ref.path.empty() && ref.comp != 0 && findComp(*obj, ref.comp));
  if(direct) {
    auto *bindings = bindingsOf(*obj, ref.comp);
    if(!bindings) return false;
    auto &names = (ref.input ? bindings->in : bindings->out)[ref.pin];
    if(std::find(names.begin(), names.end(), name) != names.end()) return false;
    names.push_back(name);
    return true;
  }

  // an instance's pin: hear a muted binding again, or add one for this instance
  Override *ovr = nullptr;
  for(auto &o : obj->eventOverrides) {
    if(!o.isLink() && o.path == ref.path && o.comp == ref.comp && o.pin == ref.pin && o.input == ref.input) { ovr = &o; break; }
  }
  if(!ovr) {
    Override fresh{};
    fresh.path = ref.path;
    fresh.comp = ref.comp;
    fresh.pin = ref.pin;
    fresh.input = ref.input;
    obj->eventOverrides.push_back(fresh);
    ovr = &obj->eventOverrides.back();
  }
  if(std::erase(ovr->mute, name) > 0) {
    std::erase_if(obj->eventOverrides, [](const Override &o) { return o.empty(); });
    return true;
  }
  if(std::find(ovr->add.begin(), ovr->add.end(), name) != ovr->add.end()) return false;
  ovr->add.push_back(name);
  return true;
}

bool Project::Events::Graph::unbind(Project &project, Object &root, const Model &model, const PinRef &ref, const std::string &name)
{
  if(ref.canvas) {
    auto *canvas = findCanvasAsset(project, ref.canvas);
    if(!canvas) return false;
    auto &map = ref.input ? canvas->bindings.in : canvas->bindings.out;
    auto it = map.find(ref.pin);
    if(it == map.end() || std::erase(it->second, name) == 0) return false;
    if(it->second.empty()) map.erase(it);
    canvas->save();
    return true;
  }
  const auto *node = model.find(ref.node);
  auto *obj = findObj(root, ref.node);
  if(!node || !obj) return false;

  if(ref.link) {
    return std::erase_if(obj->eventOverrides, [&](const Override &o) { return o.isLink() && o.link == ref.pin; }) > 0;
  }

  auto eraseFrom = [&](Bindings &bindings) {
    auto &map = ref.input ? bindings.in : bindings.out;
    auto it = map.find(ref.pin);
    if(it == map.end()) return false;
    const bool removed = std::erase(it->second, name) > 0;
    if(it->second.empty()) map.erase(it);
    return removed;
  };

  const bool direct = !node->instance || (ref.path.empty() && ref.comp != 0 && findComp(*obj, ref.comp));
  if(direct) {
    auto *bindings = bindingsOf(*obj, ref.comp);
    return bindings && eraseFrom(*bindings);
  }

  // an instance's pin: drop what this instance added, else mute what the prefab binds
  for(auto &o : obj->eventOverrides) {
    if(o.isLink() || o.path != ref.path || o.comp != ref.comp || o.pin != ref.pin || o.input != ref.input)continue;
    if(std::erase(o.add, name) > 0) {
      std::erase_if(obj->eventOverrides, [](const Override &x) { return x.empty(); });
      return true;
    }
  }
  if(ref.path.empty() && ref.comp == 0 && eraseFrom(obj->events)) return true;

  const auto *pin = model.findPin(ref);
  if(!pin || std::find(pin->names.begin(), pin->names.end(), name) == pin->names.end()) return false;
  Override *ovr = nullptr;
  for(auto &o : obj->eventOverrides) {
    if(!o.isLink() && o.path == ref.path && o.comp == ref.comp && o.pin == ref.pin && o.input == ref.input) { ovr = &o; break; }
  }
  if(!ovr) {
    Override fresh{};
    fresh.path = ref.path;
    fresh.comp = ref.comp;
    fresh.pin = ref.pin;
    fresh.input = ref.input;
    obj->eventOverrides.push_back(fresh);
    ovr = &obj->eventOverrides.back();
  }
  ovr->mute.push_back(name);
  return true;
}

std::string Project::Events::Graph::connect(Project &project, Object &root, const Model &model, const PinRef &out, const PinRef &in)
{
  if(out.input || !in.input) return {};
  auto name = wireName(model, out);
  if(name.empty()) {
    name = autoName(model, out, (out.canvas || in.canvas) ? "ui" : "");
    if(!Graph::bind(project, root, model, out, name)) return {};
  }
  Graph::bind(project, root, model, in, name);
  return name;
}

bool Project::Events::Graph::disconnect(Project &project, Object &root, const Model &model, const Wire &wire)
{
  bool changed = false;
  if(wire.to.type == End::Type::Pin) changed |= unbind(project, root, model, wire.to.pin, wire.name);
  if(wire.from.type != End::Type::Pin) return changed;
  if(wire.to.type != End::Type::Pin) return unbind(project, root, model, wire.from.pin, wire.name) || changed;

  // a direct wire: the output keeps firing while something else still listens, or when the
  // event is not one this graph makes
  const bool canvasWire = wire.from.pin.canvas || wire.to.pin.canvas;
  if(namespaceOf(wire.name) != (canvasWire ? std::string{"ui"} : model.ns) || model.codeNames.contains(key(wire.name))) return changed;
  std::vector<const Node*> everyNode{};
  for(const auto &node : model.nodes) everyNode.push_back(&node);
  for(const auto &node : model.canvases) everyNode.push_back(&node);
  for(const auto *nodePtr : everyNode) {
    const auto &node = *nodePtr;
    for(const auto &cat : node.categories) {
      for(const auto &pin : cat.pins) {
        if(!pin.ref.input || pin.ref == wire.to.pin)continue;
        for(const auto &name : pin.names) if(key(name) == key(wire.name)) return changed;
      }
    }
  }
  return unbind(project, root, model, wire.from.pin, wire.name) || changed;
}

namespace
{
  std::string objectPath(Project::Object &root, uint32_t uuid)
  {
    std::function<bool(Project::Object&, std::string&)> find = [&](Project::Object &node, std::string &path) {
      for(auto &child : node.children) {
        std::string sub = path.empty() ? child->name : path + "/" + child->name;
        if(child->uuid == uuid) { path = sub; return true; }
        if(find(*child, sub)) { path = sub; return true; }
      }
      return false;
    };
    if(root.uuid == uuid) return root.name;
    std::string path{};
    return find(root, path) ? path : std::string{"?"};
  }

  std::string categoryName(Project::Project &project, Project::Object &root, const PinRef &ref)
  {
    if(ref.comp == 0) return "Object";
    auto *obj = findObj(root, ref.node);
    if(!obj) return "?";
    if(auto *comp = findComp(*obj, ref.comp)) return displayName(project, *comp);
    if(obj->isPrefabInstance()) {
      auto inner = resolveInner(project, *obj, ref.path);
      if(inner.src) if(auto *comp = findComp(*inner.src, ref.comp)) return displayName(project, *comp);
      if(inner.node) if(auto *comp = findComp(*inner.node, ref.comp)) return displayName(project, *comp);
    }
    return "?";
  }
}

std::string Project::Events::Graph::describe(Project &project, Object &root, const PinRef &ref)
{
  if(ref.canvas) {
    auto *canvas = findCanvasAsset(project, ref.canvas);
    return "canvas:" + (canvas ? canvas->name : std::string{"?"}) + "|" + ref.pin;
  }
  std::string res = objectPath(root, ref.node);
  if(auto *obj = findObj(root, ref.node); obj && !ref.path.empty() && obj->isPrefabInstance()) {
    // the inner object's names, as parsePin() reads them
    std::vector<uint32_t> prefix{};
    for(auto uuid : ref.path) {
      prefix.push_back(uuid);
      auto inner = resolveInner(project, *obj, prefix);
      res += "/" + (inner.node ? inner.node->name : std::string{"?"});
    }
  }
  if(ref.link) return res + "|link." + ref.pin;
  return res + "|" + categoryName(project, root, ref) + "." + ref.pin;
}

PinRef Project::Events::Graph::parsePin(Project &project, Object &root, const Model &model, const std::string &text, bool input)
{
  const auto bar = text.find('|');
  if(bar == std::string::npos) return {};
  const auto path = text.substr(0, bar);
  const auto spec = text.substr(bar + 1);

  if(path.starts_with("canvas:")) {
    for(const auto &node : model.canvases) {
      if(node.name != path.substr(7))continue;
      PinRef ref{0, {}, 0, spec, input, false, node.canvas};
      return model.findPin(ref) ? ref : PinRef{};
    }
    return {};
  }

  // the object: names from the top; inside an instance they continue into its prefab
  Object *node = &root;
  Object *instance = nullptr;
  std::vector<uint32_t> inner{};
  size_t start = 0;
  while(start <= path.size()) {
    size_t end = path.find('/', start);
    if(end == std::string::npos) end = path.size();
    const auto name = path.substr(start, end - start);
    start = end + 1;
    Object *next = nullptr;
    if(instance) {
      auto parent = resolveInner(project, *instance, inner);
      for(auto *from : {parent.node, parent.src}) {
        if(!from || next)continue;
        for(auto &c : from->children) if(c->name == name) { next = c.get(); break; }
      }
      if(!next) return {};
      inner.push_back(next->uuid);
      continue;
    }
    for(auto &c : node->children) if(c->name == name) { next = c.get(); break; }
    if(!next && node == &root && root.name == name) next = &root;
    if(!next) return {};
    node = next;
    if(node->isPrefabInstance() && !(model.kind == Kind::Prefab && node == &root)) instance = node;
  }

  PinRef ref{};
  ref.node = node->uuid;
  ref.path = inner;
  ref.input = input;
  if(spec.rfind("link.", 0) == 0) {
    ref.pin = spec.substr(5);
    ref.link = true;
    return model.findPin(ref) ? ref : PinRef{};
  }
  const auto dot = spec.rfind('.');
  if(dot == std::string::npos) return {};
  const auto compName = spec.substr(0, dot);
  ref.pin = spec.substr(dot + 1);

  // the category: "Object", a component's display name or its uuid
  if(compName != "Object") {
    const auto *graphNode = model.find(node->uuid);
    if(!graphNode) return {};
    for(const auto &cat : graphNode->categories) {
      for(const auto &pin : cat.pins) {
        if(pin.ref.path != inner || pin.ref.pin != ref.pin || pin.ref.input != input || pin.ref.link || pin.ref.comp == 0)continue;
        if(categoryName(project, root, pin.ref) == compName || std::to_string(pin.ref.comp) == compName) {
          return pin.ref;
        }
      }
    }
    return {};
  }
  return model.findPin(ref) ? ref : PinRef{};
}
