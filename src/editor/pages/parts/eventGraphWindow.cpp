/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
#include "eventGraphWindow.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IconsMaterialDesignIcons.h"
#include "ImNodeFlow.h"
#include "json.hpp"

#include "../../../context.h"
#include "../../undoRedo.h"
#include "../../selectionUtils.h"
#include "../../imgui/theme.h"
#include "../../../project/project.h"
#include "../../../project/scene/scene.h"
#include "../../../project/scene/prefab.h"
#include "../../../project/scene/object.h"
#include "../../../project/component/components.h"
#include "../../../project/events/eventGraph.h"
#include "../../../project/events/eventRules.h"
#include "../../../project/events/eventUsages.h"
#include "../../../utils/fs.h"
#include "../../../utils/hash.h"
#include "../../../utils/logger.h"
#include "../../imgui/notification.h"

namespace fs = std::filesystem;
namespace G = Project::Events::Graph;

namespace
{
  struct EventPin {}; // every pin here has this data type: pins only carry wires

  constexpr ImU32 COL_PIN = IM_COL32(120, 180, 230, 255);
  constexpr ImU32 COL_PIN_LINK = IM_COL32(190, 150, 235, 255);
  constexpr ImU32 COL_PROBLEM = IM_COL32(235, 95, 85, 255);
  constexpr ImU32 COL_GLOBAL = IM_COL32(230, 170, 80, 255);
  constexpr ImU32 COL_SCOPE = IM_COL32(150, 210, 140, 255);
  constexpr ImU32 COL_EVENT = IM_COL32(235, 205, 110, 255);
  const ImVec4 TEXT_PROBLEM{0.92f, 0.37f, 0.33f, 1.0f};
  const ImVec4 TEXT_HINT{0.62f, 0.62f, 0.68f, 1.0f};

  constexpr uint64_t BUILTIN_FIRST = 0xC0E0000000000001ull;
  constexpr const char* BUILTIN_NAMES[] = {
    "Relay", "Delay", "Frame Delay", "Once", "Counter", "All", "Compare", "Gate", "Toggle",
    "Sequence", "Random", "Log", "Scene Loader", "Spawner", "Message Bridge",
  };

  std::shared_ptr<ImFlow::PinStyle> pinStyle(ImU32 color)
  {
    auto style = std::make_shared<ImFlow::PinStyle>(color, 0, 4.5f, 5.5f, 4.5f, 1.2f);
    style->extra.link_thickness = 2.2f;
    return style;
  }

  std::shared_ptr<ImFlow::NodeStyle> nodeStyle(ImU32 header, bool warn)
  {
    auto style = std::make_shared<ImFlow::NodeStyle>(header, ImColor(235, 240, 245, 255), 5.0f);
    style->bg = ImGui::Theme::getColorU32("nodeBody", IM_COL32(55, 64, 75, 255));
    if(warn) {
      style->border_color = IM_COL32(255, 165, 0, 255);
      style->border_thickness = -2.5f;
    }
    return style;
  }

  struct Row
  {
    std::shared_ptr<ImFlow::Pin> pin{};
    std::string tooltip{};
    bool problem{false};
    bool hinted{false};
  };

  struct Section
  {
    std::string key{};
    std::string title{};
    bool open{true};
    std::vector<Row> ins{}, outs{};
  };

  // One node: sections of pin rows, inputs on the left and outputs on the right. A collapsed
  // section only shows the pins that have wires.
  class GraphNode : public ImFlow::BaseNode
  {
    public:
      std::vector<Section> sections{};
      std::string subtitle{};
      std::string subtitleTip{};
      std::function<void(const std::string &key, bool open)> onToggle{};

      [[nodiscard]] bool customPinLayout() const override { return true; }

      void draw() override
      {
        const float gap = 28.0f;
        const float rowH = ImGui::GetTextLineHeightWithSpacing();
        auto visible = [](const Section &sec, const std::vector<Row> &rows) {
          std::vector<const Row*> res{};
          for(const auto &row : rows) if(sec.open || row.pin->isConnected()) res.push_back(&row);
          return res;
        };

        float width = std::max(110.0f, ImGui::CalcTextSize(subtitle.c_str()).x);
        for(const auto &sec : sections) {
          if(!sec.title.empty()) width = std::max(width, ImGui::CalcTextSize(sec.title.c_str()).x + ImGui::GetFrameHeight() + 8.0f);
          auto ins = visible(sec, sec.ins);
          auto outs = visible(sec, sec.outs);
          for(size_t i = 0; i < std::max(ins.size(), outs.size()); ++i) {
            float w = gap;
            if(i < ins.size()) w += ins[i]->pin->calcWidth();
            if(i < outs.size()) w += outs[i]->pin->calcWidth();
            width = std::max(width, w);
          }
        }

        const ImVec2 start = ImGui::GetCursorPos();
        float y = start.y;
        if(!subtitle.empty()) {
          ImGui::SetCursorPos({start.x, y});
          ImGui::TextDisabled("%s", subtitle.c_str());
          if(!subtitleTip.empty()) ImGui::SetItemTooltip("%s", subtitleTip.c_str());
          y += rowH;
        }
        for(auto &sec : sections)
        {
          ImGui::PushID(sec.key.c_str());
          if(!sec.title.empty()) {
            ImGui::SetCursorPos({start.x, y});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {1.0f, 0.0f});
            if(ImGui::SmallButton(sec.open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT)) {
              sec.open = !sec.open;
              if(onToggle) onToggle(sec.key, sec.open);
            }
            ImGui::PopStyleVar();
            ImGui::SetItemTooltip(sec.open ? "Collapse: only pins with wires stay" : "Show every pin");
            ImGui::SameLine();
            ImGui::TextDisabled("%s", sec.title.c_str());
            y += rowH;
          }
          auto ins = visible(sec, sec.ins);
          auto outs = visible(sec, sec.outs);
          for(size_t i = 0; i < std::max(ins.size(), outs.size()); ++i) {
            auto drawRow = [&](const Row &row, float x) {
              if(row.problem) ImGui::PushStyleColor(ImGuiCol_Text, TEXT_PROBLEM);
              else if(row.hinted) ImGui::PushStyleColor(ImGuiCol_Text, TEXT_HINT);
              row.pin->setPos({x, y});
              row.pin->update();
              if(row.problem || row.hinted) ImGui::PopStyleColor();
              if(!row.tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.tooltip.c_str());
            };
            if(i < ins.size()) drawRow(*ins[i], start.x);
            if(i < outs.size()) drawRow(*outs[i], start.x + width - outs[i]->pin->calcWidth());
            y += rowH;
          }
          ImGui::PopID();
        }
        ImGui::SetCursorPos({start.x, y});
        ImGui::Dummy({width, 1.0f});
      }
  };

  // what one pin of the canvas stands for
  struct PinInfo
  {
    G::End::Type type{G::End::Type::Pin};
    G::PinRef ref{};    // a pin of an object node
    std::string name{}; // a pin of an Event, Global or Scope node: its event
  };

  size_t mix(size_t seed, size_t value)
  {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
  }

  size_t hashString(const std::string &s) { return std::hash<std::string>{}(s); }

  size_t hashBindings(size_t h, const Project::Events::Bindings &b)
  {
    for(const auto *map : {&b.in, &b.out}) {
      for(const auto &[pin, names] : *map) {
        h = mix(h, hashString(pin));
        for(const auto &n : names) h = mix(h, hashString(n));
      }
      h = mix(h, 7);
    }
    return h;
  }

  // everything the graph shows depends on, cheap enough to check every frame
  size_t signatureOf(const Project::Object &obj, size_t h)
  {
    h = mix(h, obj.uuid);
    h = mix(h, hashString(obj.name));
    h = mix(h, obj.uuidPrefab.value);
    h = hashBindings(h, obj.events);
    for(const auto &comp : obj.components) {
      h = mix(h, comp.uuid);
      h = hashString(comp.name) ^ h;
      if(comp.id == 0) h = mix(h, Project::Component::Code::getScriptUUID(comp));
      h = hashBindings(h, comp.events);
    }
    for(const auto &o : obj.eventOverrides) {
      h = mix(h, hashString(o.link + ">" + o.to + "|" + o.pin));
      for(auto u : o.path) h = mix(h, u);
      h = mix(h, o.comp + (o.input ? 1 : 0));
      for(const auto &n : o.add) h = mix(h, hashString(n));
      for(const auto &n : o.mute) h = mix(h, hashString("-" + n));
    }
    for(const auto &e : obj.eventExposed) h = mix(h, hashString(e.pin) ^ e.comp);
    for(const auto &child : obj.children) h = signatureOf(*child, h);
    return h;
  }

  nlohmann::json vecJson(ImVec2 v) { return {std::round(v.x), std::round(v.y)}; }
  ImVec2 jsonVec(const nlohmann::json &j)
  {
    if(!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number()) return {0.0f, 0.0f};
    return {j[0].get<float>(), j[1].get<float>()};
  }

  // the path of definition uuids from a prefab root down to an object of its own file
  bool pathTo(const Project::Object &node, uint32_t uuid, std::vector<uint32_t> &path)
  {
    for(const auto &child : node.children) {
      path.push_back(child->uuid);
      if(child->uuid == uuid || pathTo(*child, uuid, path)) return true;
      path.pop_back();
    }
    return false;
  }

  Project::Object* findObj(Project::Object &root, uint32_t uuid)
  {
    if(root.uuid == uuid) return &root;
    for(auto &child : root.children) if(auto *f = findObj(*child, uuid)) return f;
    return nullptr;
  }
}

struct Editor::EventGraphWindow::State
{
  ImFlow::ImNodeFlow canvas{"EventGraph"};

  // the graph shown
  std::string key{};                       // changes when another graph is shown
  G::Kind kind{G::Kind::Scene};
  Project::Object *root{nullptr};
  std::shared_ptr<Project::Prefab> prefab{};
  uint64_t prefabUUID{0};
  uint32_t editInstance{0};                // prefab mode: the scene instance being edited
  int sceneId{0};
  uint32_t focus{0};
  bool focusMode{false};

  // derived from the bindings
  G::Model model{};
  size_t signature{0};
  double collectedAt{0.0};
  bool needLinks{true};
  bool rebuilding{false};

  // canvas content
  std::map<uint32_t, std::shared_ptr<GraphNode>> objectNodes{};
  std::map<uint64_t, std::shared_ptr<GraphNode>> canvasNodes{};
  std::map<std::string, std::shared_ptr<GraphNode>> eventNodes{};
  std::shared_ptr<GraphNode> globalNode{}, scopeNode{};
  std::map<const ImFlow::Pin*, PinInfo> pins{};
  std::map<const ImFlow::Link*, G::Wire> linkWires{};
  std::vector<std::pair<ImFlow::Pin*, ImFlow::Pin*>> pending{};

  // layout file
  std::string layoutPath{};
  nlohmann::json layout = nlohmann::json::object();
  std::string layoutSaved{};

  // selection sync
  std::vector<uint32_t> lastNodeSel{};
  std::vector<uint32_t> lastCtxSel{};

  // the event browser (phase 7): every event of the project, its uses, rename
  bool browserOpen{false};
  bool browserStale{true};
  std::vector<Project::Events::Usages::Use> uses{};
  std::string browseFilter{};
  std::string browseSelected{};
  std::string renameText{};

  // pop-ups
  ImVec2 popupPos{};
  const ImFlow::Link *popupLink{nullptr};
  std::string popupFilter{};
  std::string newEventName{};

  // ---- layout

  nlohmann::json& layoutScope()
  {
    // focus mode keeps its own positions, per object
    if(!focus) return layout;
    auto &f = layout["focus"][std::to_string(focus)];
    if(!f.is_object()) f = nlohmann::json::object();
    return f;
  }

  void loadLayout()
  {
    layout = nlohmann::json::object();
    if(!layoutPath.empty() && fs::exists(layoutPath)) {
      try {
        layout = nlohmann::json::parse(Utils::FS::loadTextFile(layoutPath));
      } catch(...) {
        Utils::Logger::log("Events: could not read " + layoutPath + ", starting a new layout", Utils::Logger::LEVEL_WARN);
      }
      if(!layout.is_object()) layout = nlohmann::json::object();
    }
    layoutSaved = layout.dump(2);
    auto &view = layoutScope()["view"];
    if(view.is_object()) {
      canvas.setScroll(jsonVec(view["scroll"]));
      if(view.contains("zoom") && view["zoom"].is_number()) canvas.setScale(std::clamp(view["zoom"].get<float>(), 0.3f, 2.0f));
    } else {
      canvas.setScroll({40.0f, 40.0f});
      canvas.setScale(1.0f);
    }
  }

  void captureLayout()
  {
    auto &scope = layoutScope();
    auto &nodes = scope["nodes"];
    if(!nodes.is_object()) nodes = nlohmann::json::object();
    for(auto &[uuid, node] : objectNodes) nodes[std::to_string(uuid)]["pos"] = vecJson(node->getPos());
    auto &canvasPos = scope["canvasNodes"];
    if(!canvasPos.is_object()) canvasPos = nlohmann::json::object();
    for(auto &[uuid, node] : canvasNodes) canvasPos[std::to_string(uuid)]["pos"] = vecJson(node->getPos());
    auto &events = scope["events"];
    if(!events.is_object()) events = nlohmann::json::object();
    for(auto &[name, node] : eventNodes) events[name]["pos"] = vecJson(node->getPos());
    if(globalNode) scope["global"]["pos"] = vecJson(globalNode->getPos());
    if(scopeNode) scope["scope"]["pos"] = vecJson(scopeNode->getPos());
    scope["view"]["scroll"] = vecJson(canvas.getScroll());
    scope["view"]["zoom"] = std::round(canvas.getScale() * 100.0f) / 100.0f;
  }

  void saveLayout()
  {
    if(layoutPath.empty())return;
    auto text = layout.dump(2);
    if(text == layoutSaved)return;
    std::error_code ec;
    fs::create_directories(fs::path{layoutPath}.parent_path(), ec);
    Utils::FS::saveTextFile(layoutPath, text);
    layoutSaved = text;
  }

  bool sectionOpen(const std::string &nodeKey, const std::string &sectionKey, bool fallback)
  {
    auto &scope = layoutScope();
    if(!scope.contains("open") || !scope["open"].contains(nodeKey)) return fallback;
    auto &open = scope["open"][nodeKey];
    return open.contains(sectionKey) && open[sectionKey].is_boolean() ? open[sectionKey].get<bool>() : fallback;
  }

  std::set<uint32_t> shownByHand()
  {
    std::set<uint32_t> res{};
    auto &scope = layoutScope();
    if(scope.contains("shown") && scope["shown"].is_array()) {
      for(auto &v : scope["shown"]) if(v.is_number_unsigned()) res.insert(v.get<uint32_t>());
    }
    return res;
  }

  void setShownByHand(uint32_t uuid, bool shown)
  {
    auto set = shownByHand();
    if(shown) set.insert(uuid); else set.erase(uuid);
    layoutScope()["shown"] = nlohmann::json(std::vector<uint32_t>{set.begin(), set.end()});
  }

  std::set<uint64_t> canvasesByHand()
  {
    std::set<uint64_t> res{};
    auto &scope = layoutScope();
    if(scope.contains("canvases") && scope["canvases"].is_array()) {
      for(auto &v : scope["canvases"]) if(v.is_number_unsigned()) res.insert(v.get<uint64_t>());
    }
    return res;
  }

  void setCanvasByHand(uint64_t uuid, bool shown)
  {
    auto set = canvasesByHand();
    if(shown) set.insert(uuid); else set.erase(uuid);
    layoutScope()["canvases"] = nlohmann::json(std::vector<uint64_t>{set.begin(), set.end()});
  }

  bool flag(const char* name)
  {
    auto &scope = layoutScope();
    return scope.contains(name) && scope[name].is_object() && scope[name].value("shown", false);
  }

  // ---- which graph

  void identify(Project::Scene &scene)
  {
    std::string newKey{};
    auto *editObj = Editor::SelectionUtils::getPrefabEditObject(scene);
    std::shared_ptr<Project::Prefab> newPrefab{};
    if(editObj) newPrefab = ctx.project->getAssets().getPrefabByUUID(editObj->uuidPrefab.value);

    uint32_t newFocus = 0;
    if(focusMode) {
      newFocus = ctx.selSubPath.empty() ? ctx.selObjectUUID : ctx.selSubPath.back();
      if(newPrefab && ctx.selSubPath.empty()) newFocus = newPrefab->obj.uuid;
    }

    if(newPrefab) {
      newKey = "prefab:" + std::to_string(newPrefab->uuid.value);
    } else {
      newKey = "scene:" + std::to_string(scene.getId());
    }
    if(newFocus) newKey += ":focus:" + std::to_string(newFocus);
    if(newKey == key) {
      root = newPrefab ? &newPrefab->obj : &scene.getRootObject(); // undo can swap objects
      return;
    }

    if(!key.empty()) { captureLayout(); saveLayout(); }
    clear();
    key = newKey;
    prefab = newPrefab;
    focus = newFocus;
    if(prefab) {
      kind = G::Kind::Prefab;
      root = &prefab->obj;
      prefabUUID = prefab->uuid.value;
      editInstance = editObj->uuid;
      layoutPath.clear();
      for(const auto &entry : ctx.project->getAssets().getTypeEntries(Project::FileType::PREFAB)) {
        if(entry.prefab == prefab) layoutPath = entry.path + ".events.json";
      }
    } else {
      kind = G::Kind::Scene;
      root = &scene.getRootObject();
      prefabUUID = 0;
      editInstance = 0;
      sceneId = scene.getId();
      layoutPath = (fs::path{ctx.project->getPath()} / "data" / "scenes" / std::to_string(sceneId) / "events.layout.json").string();
    }
    loadLayout();
    signature = 0;
  }

  void clear()
  {
    rebuilding = true;
    for(auto &[uid, node] : canvas.getNodes()) {
      for(auto &pin : node->getIns()) pin->deleteLinks();
    }
    canvas.getNodes().clear();
    rebuilding = false;
    objectNodes.clear();
    canvasNodes.clear();
    eventNodes.clear();
    globalNode = nullptr;
    scopeNode = nullptr;
    pins.clear();
    highlighted.clear();
    linkWires.clear();
    pending.clear();
    lastNodeSel.clear();
    lastCtxSel.clear();
  }

  // drops every object node (and every wire), e.g. to place them all again
  void dropObjectNodes()
  {
    rebuilding = true;
    for(auto &[uid, node] : canvas.getNodes()) {
      for(auto &pin : node->getIns()) pin->deleteLinks();
    }
    for(auto &[uuid, node] : objectNodes) {
      resetPins(*node);
      canvas.getNodes().erase(node->getUID());
    }
    objectNodes.clear();
    for(auto &[uuid, node] : canvasNodes) {
      resetPins(*node);
      canvas.getNodes().erase(node->getUID());
    }
    canvasNodes.clear();
    for(auto &[name, node] : eventNodes) {
      resetPins(*node);
      canvas.getNodes().erase(node->getUID());
    }
    eventNodes.clear();
    for(auto *special : {&globalNode, &scopeNode}) {
      if(!*special)continue;
      resetPins(**special);
      canvas.getNodes().erase((*special)->getUID());
      *special = nullptr;
    }
    linkWires.clear();
    rebuilding = false;
    signature = 0;
  }

  // ---- nodes

  std::shared_ptr<ImFlow::Pin> makePin(GraphNode &node, const PinInfo &info, bool input, const std::string &label, ImU32 color)
  {
    static uint64_t nextUid = 1;
    std::shared_ptr<ImFlow::Pin> pin{};
    if(input) {
      auto in = node.addIN_uid<EventPin>(nextUid++, label, ImFlow::ConnectionFilter::None(), pinStyle(color));
      in->allowSameNodeConnections(true); // a node may send events to itself
      pin = in;
    } else {
      pin = node.addOUT_uid<EventPin>(nextUid++, label, pinStyle(color));
    }
    pins[pin.get()] = info;
    return pin;
  }

  void resetPins(GraphNode &node)
  {
    for(auto &pin : node.getIns()) { pins.erase(pin.get()); highlighted.erase(pin.get()); }
    for(auto &pin : node.getOuts()) { pins.erase(pin.get()); highlighted.erase(pin.get()); }
    node.m_ins.clear();
    node.m_outs.clear();
    node.sections.clear();
  }

  std::shared_ptr<GraphNode> addNode(ImVec2 pos)
  {
    auto node = canvas.addNode<GraphNode>(pos);
    node->setIndestructable(true); // removing objects is the hierarchy's job
    return node;
  }

  void fillObjectNode(GraphNode &node, const G::Node &src, bool warn)
  {
    resetPins(node);
    std::string title = src.name;
    ImU32 header = IM_COL32(58, 88, 120, 255);
    if(src.canvas) { title = ICON_MDI_MONITOR_DASHBOARD " " + src.name + "  (UI canvas)"; header = IM_COL32(40, 110, 120, 255); }
    else if(src.instance) { title += "  " ICON_MDI_PACKAGE_VARIANT_CLOSED " " + src.prefab; header = IM_COL32(104, 76, 140, 255); }
    else if(src.builtin) header = IM_COL32(62, 112, 84, 255);
    node.setTitle(title);
    node.setStyle(nodeStyle(header, warn));
    node.subtitle = warn ? ICON_MDI_ALERT " Loop without a delay" : "";
    node.subtitleTip = warn ? "Its outputs reach its own inputs again with no Delay or Frame Delay in between.\n"
                              "Allowed, but an event chain that keeps going is cut after 16 hops." : "";
    const std::string nodeKey = src.canvas ? "c" + std::to_string(src.canvas) : std::to_string(src.uuid);
    for(const auto &cat : src.categories) {
      Section sec{cat.key, cat.title, sectionOpen(nodeKey, cat.key, cat.openByDefault), {}, {}};
      for(const auto &pin : cat.pins) {
        PinInfo info{G::End::Type::Pin, pin.ref, {}};
        Row row{};
        row.problem = !pin.problem.empty();
        row.hinted = !pin.hint.empty();
        const ImU32 color = row.problem ? COL_PROBLEM : (pin.ref.link ? COL_PIN_LINK : COL_PIN);
        row.pin = makePin(node, info, pin.ref.input, pin.label, color);
        std::string tip{};
        for(const auto &n : pin.names) tip += (tip.empty() ? "" : "\n") + n;
        for(const auto &n : pin.muted) tip += (tip.empty() ? "" : "\n") + n + " (muted on this instance)";
        if(pin.ref.link) tip = "Parent link " + pin.ref.pin + (tip.empty() ? "" : ": " + tip) + "\nDrag a wire to repoint it for this instance";
        if(tip.empty()) tip = pin.ref.input ? "Not bound: drag a wire into it" : "Not bound: drag a wire out of it";
        if(row.problem) tip += "\n" + pin.problem;
        if(row.hinted) tip += "\n" + pin.hint;
        row.tooltip = tip;
        (pin.ref.input ? sec.ins : sec.outs).push_back(row);
      }
      node.sections.push_back(sec);
    }
    node.onToggle = [this, nodeKey](const std::string &sectionKey, bool open) {
      layoutScope()["open"][nodeKey][sectionKey] = open;
    };
  }

  void fillEventNode(GraphNode &node, const std::string &name)
  {
    resetPins(node);
    node.setTitle(ICON_MDI_FLASH " " + name);
    node.setStyle(nodeStyle(IM_COL32(128, 100, 40, 255), false));
    Section sec{"e", "", true, {}, {}};
    Row in{}, out{};
    in.pin = makePin(node, {G::End::Type::Event, {}, name}, true, "fire", COL_EVENT);
    in.tooltip = "Outputs wired here fire " + name;
    out.pin = makePin(node, {G::End::Type::Event, {}, name}, false, "listen", COL_EVENT);
    out.tooltip = "Inputs wired here listen to " + name;
    sec.ins.push_back(in);
    sec.outs.push_back(out);
    node.sections.push_back(sec);
  }

  void fillGlobalNode(GraphNode &node)
  {
    resetPins(node);
    node.setTitle(ICON_MDI_EARTH " Global");
    node.subtitle = "global:: events, one region per system";
    node.setStyle(nodeStyle(IM_COL32(126, 72, 52, 255), false));
    for(const auto &sys : model.systems) {
      Section sec{"s:" + sys.name, sys.name, sectionOpen("global", "s:" + sys.name, false), {}, {}};
      for(const auto &pin : sys.in) {
        const auto name = "global::" + sys.name + "." + pin;
        sec.ins.push_back({makePin(node, {G::End::Type::Global, {}, name}, true, pin, COL_GLOBAL),
                           name + "\nAn input of system " + sys.name + ": outputs wired here fire it", false, false});
      }
      for(const auto &pin : sys.out) {
        const auto name = "global::" + sys.name + "." + pin;
        sec.outs.push_back({makePin(node, {G::End::Type::Global, {}, name}, false, pin, COL_GLOBAL),
                            name + (sys.name == "Engine" ? "\nFired by the engine" : "\nAn output of system " + sys.name), false, false});
      }
      node.sections.push_back(sec);
    }
    node.onToggle = [this](const std::string &sectionKey, bool open) { layoutScope()["open"]["global"][sectionKey] = open; };
  }

  void fillScopeNode(GraphNode &node)
  {
    resetPins(node);
    node.setTitle(kind == G::Kind::Scene ? ICON_MDI_TARGET " Scene events" : ICON_MDI_TARGET " Prefab events");
    node.setStyle(nodeStyle(IM_COL32(72, 92, 72, 255), false));
    Section sec{"scope", "", true, {}, {}};
    for(const auto &name : model.scopeEvents) {
      const auto label = name.substr(name.find("::") + 2);
      sec.outs.push_back({makePin(node, {G::End::Type::Scope, {}, name}, false, label, COL_SCOPE), name + "\nFired by the engine", false, false});
    }
    node.sections.push_back(sec);
  }

  // nodes that loop back to themselves through wires without a Delay or Frame Delay in between
  std::set<uint32_t> loopNodes(const std::vector<G::Wire> &wires)
  {
    std::map<uint32_t, std::set<uint32_t>> next{};
    std::map<std::string, std::vector<uint32_t>> eventIn{}, eventOut{};
    for(const auto &w : wires) {
      if(w.from.pin.canvas || w.to.pin.canvas)continue; // a canvas is no object of the graph
      if(w.from.type == G::End::Type::Pin && w.to.type == G::End::Type::Pin) next[w.from.pin.node].insert(w.to.pin.node);
      if(w.from.type == G::End::Type::Pin && w.to.type == G::End::Type::Event) eventIn[w.name].push_back(w.from.pin.node);
      if(w.from.type == G::End::Type::Event && w.to.type == G::End::Type::Pin) eventOut[w.name].push_back(w.to.pin.node);
    }
    for(auto &[name, froms] : eventIn) for(auto f : froms) for(auto t : eventOut[name]) next[f].insert(t);

    std::set<uint32_t> res{};
    for(const auto &node : model.nodes) {
      if(node.delay)continue;
      // a path from the node back to itself that passes no delay object
      std::set<uint32_t> seen{};
      std::vector<uint32_t> stack{next[node.uuid].begin(), next[node.uuid].end()};
      while(!stack.empty()) {
        auto u = stack.back(); stack.pop_back();
        if(u == node.uuid) { res.insert(u); break; }
        const auto *n = model.find(u);
        if(!n || n->delay || !seen.insert(u).second)continue;
        for(auto v : next[u]) stack.push_back(v);
      }
    }
    return res;
  }

  // Places nodes without a saved position in columns: a node goes one column right of everything
  // that wires into it, so events flow left to right. Saved positions are kept.
  std::map<std::string, ImVec2> arrange(const std::vector<G::Wire> &wires, const std::set<uint32_t> &shown,
                                        const std::set<uint64_t> &shownCanvases,
                                        const std::set<std::string> &eventNames, bool showGlobal, bool showScope)
  {
    auto keyOf = [](const G::End &end, const std::string &name) -> std::string {
      switch(end.type) {
        case G::End::Type::Pin: return end.pin.canvas ? "c:" + std::to_string(end.pin.canvas) : "o:" + std::to_string(end.pin.node);
        case G::End::Type::Event: return "e:" + name;
        case G::End::Type::Global: return "global";
        case G::End::Type::Scope: return "scope";
      }
      return {};
    };
    std::vector<std::string> keys{};
    if(showScope) keys.push_back("scope");
    for(const auto &node : model.nodes) if(shown.contains(node.uuid)) keys.push_back("o:" + std::to_string(node.uuid));
    for(const auto &name : eventNames) keys.push_back("e:" + name);
    for(auto uuid : shownCanvases) keys.push_back("c:" + std::to_string(uuid));

    std::map<std::string, int> rank{};
    for(const auto &k : keys) rank[k] = 0;
    for(size_t pass = 0; pass < keys.size(); ++pass) {
      bool moved = false;
      for(const auto &w : wires) {
        auto a = keyOf(w.from, w.name), b = keyOf(w.to, w.name);
        if(a == b || a == "global" || b == "global" || !rank.contains(a) || !rank.contains(b))continue;
        if(rank[b] < rank[a] + 1 && rank[a] + 1 < (int)keys.size()) { rank[b] = rank[a] + 1; moved = true; }
      }
      if(!moved)break;
    }
    // the Global node: right of its senders when objects fire into it, else left of its listeners
    int maxRank = 0;
    for(auto &[k, r] : rank) maxRank = std::max(maxRank, r);
    bool globalIsSink = false;
    for(const auto &w : wires) if(w.to.type == G::End::Type::Global) globalIsSink = true;
    if(showGlobal) rank["global"] = globalIsSink ? maxRank + 1 : 0;

    auto height = [&](const std::string &k) -> float {
      if(k == "scope") return 40.0f + 22.0f * (float)model.scopeEvents.size();
      if(k == "global") return 60.0f + 26.0f * (float)model.systems.size();
      if(k.starts_with("e:")) return 70.0f;
      const auto *node = k.starts_with("c:") ? model.findCanvas(std::stoull(k.substr(2))) : model.find((uint32_t)std::stoul(k.substr(2)));
      float h = 34.0f;
      if(node) for(const auto &cat : node->categories) {
        size_t ins = 0, outs = 0;
        for(const auto &pin : cat.pins) (pin.ref.input ? ins : outs)++;
        h += 22.0f + (cat.openByDefault ? 22.0f * (float)std::max(ins, outs) : 0.0f);
      }
      return h;
    };
    // nodes no wire reaches go in a grid right of the flow, so they don't stretch a column
    std::set<std::string> wired{};
    for(const auto &w : wires) { wired.insert(keyOf(w.from, w.name)); wired.insert(keyOf(w.to, w.name)); }
    int lastRank = 0;
    for(auto &[k, r] : rank) if(wired.contains(k)) lastRank = std::max(lastRank, r);

    std::map<int, float> nextY{};
    std::map<std::string, ImVec2> res{};
    std::vector<std::string> order = keys;
    if(showGlobal) order.push_back("global");
    int loose = 0;
    float looseY = 0.0f, rowH = 0.0f;
    for(const auto &k : order) {
      if(!wired.contains(k) && k != "global" && k != "scope") {
        const int col = loose++ % 3;
        if(col == 0) { looseY += rowH; rowH = 0.0f; }
        res[k] = {40.0f + (float)(lastRank + 1 + col) * 320.0f + (wired.empty() ? -320.0f : 0.0f), 40.0f + looseY};
        rowH = std::max(rowH, height(k) + 30.0f);
        continue;
      }
      const int r = rank[k];
      float &y = nextY[r];
      res[k] = {40.0f + (float)r * 320.0f, 40.0f + y};
      y += height(k) + 30.0f;
    }
    return res;
  }

  void refresh(bool force)
  {
    if(!root)return;
    const size_t sig = signatureOf(*root, mix(std::hash<std::string>{}(key), focus));
    const double now = ImGui::GetTime();
    if(!force && sig == signature && now - collectedAt < 2.0)return;
    const bool changed = sig != signature || force;
    signature = sig;
    collectedAt = now;
    auto fresh = G::collect(*ctx.project, *root, kind, focus);
    if(!changed && fresh.nodes.size() == model.nodes.size())  {
      // periodic re-check (scripts may have new pins): rebuild only if pins changed
      bool same = true;
      for(size_t i = 0; i < fresh.nodes.size() && same; ++i) {
        same = fresh.nodes[i].categories.size() == model.nodes[i].categories.size();
        for(size_t c = 0; same && c < fresh.nodes[i].categories.size(); ++c) {
          same = fresh.nodes[i].categories[c].pins.size() == model.nodes[i].categories[c].pins.size();
        }
      }
      if(same)return;
    }
    model = std::move(fresh);
    rebuild();
  }

  void rebuild()
  {
    rebuilding = true;
    for(auto &[uid, node] : canvas.getNodes()) {
      for(auto &pin : node->getIns()) pin->deleteLinks();
    }
    linkWires.clear();

    // which nodes: bound objects, objects shown by hand, and anything a wire reaches
    auto &scope = layoutScope();
    std::set<std::string> eventNames{};
    if(scope.contains("events") && scope["events"].is_object()) {
      for(auto &[name, v] : scope["events"].items()) eventNames.insert(name);
    }
    bool anyGlobal = false, anyScope = false;
    for(const auto &node : model.nodes) for(const auto &cat : node.categories) for(const auto &pin : cat.pins) {
      for(const auto &n : pin.names) {
        if(Project::Events::namespaceOf(n) == "global") anyGlobal = true;
        if(std::find(model.scopeEvents.begin(), model.scopeEvents.end(), n) != model.scopeEvents.end()) anyScope = true;
      }
    }
    const bool showGlobal = anyGlobal || flag("global");
    const bool showScope = anyScope || flag("scope");
    auto wires = G::wires(model, eventNames, showGlobal, showScope);

    std::set<uint32_t> shown = shownByHand();
    std::set<uint64_t> shownCanvases = canvasesByHand();
    for(const auto &node : model.nodes) if(node.bound() || focus) shown.insert(node.uuid);
    for(const auto &w : wires) {
      for(const auto *end : {&w.from, &w.to}) {
        if(end->type != G::End::Type::Pin)continue;
        if(end->pin.canvas) shownCanvases.insert(end->pin.canvas);
        else shown.insert(end->pin.node);
      }
    }
    const auto loops = loopNodes(wires);

    // object nodes
    auto &savedNodes = scope["nodes"];
    const auto spots = arrange(wires, shown, shownCanvases, eventNames, showGlobal, showScope);
    auto spot = [&](const std::string &k) { auto it = spots.find(k); return it != spots.end() ? it->second : ImVec2{40.0f, 40.0f}; };
    for(auto it = objectNodes.begin(); it != objectNodes.end();) {
      if(!shown.contains(it->first) || !model.find(it->first)) {
        resetPins(*it->second);
        canvas.getNodes().erase(it->second->getUID());
        it = objectNodes.erase(it);
      } else ++it;
    }
    for(const auto &src : model.nodes) {
      if(!shown.contains(src.uuid))continue;
      auto &node = objectNodes[src.uuid];
      if(!node) {
        const auto keyStr = std::to_string(src.uuid);
        ImVec2 pos = savedNodes.is_object() && savedNodes.contains(keyStr) ? jsonVec(savedNodes[keyStr]["pos"]) : spot("o:" + keyStr);
        node = addNode(pos);
      }
      fillObjectNode(*node, src, loops.contains(src.uuid));
    }

    // canvas nodes
    auto &savedCanvases = scope["canvasNodes"];
    for(auto it = canvasNodes.begin(); it != canvasNodes.end();) {
      if(!shownCanvases.contains(it->first) || !model.findCanvas(it->first)) {
        resetPins(*it->second);
        canvas.getNodes().erase(it->second->getUID());
        it = canvasNodes.erase(it);
      } else ++it;
    }
    for(auto uuid : shownCanvases) {
      const auto *src = model.findCanvas(uuid);
      if(!src)continue;
      auto &node = canvasNodes[uuid];
      if(!node) {
        const auto keyStr = std::to_string(uuid);
        ImVec2 pos = savedCanvases.is_object() && savedCanvases.contains(keyStr) ? jsonVec(savedCanvases[keyStr]["pos"]) : spot("c:" + keyStr);
        node = addNode(pos);
      }
      fillObjectNode(*node, *src, false);
    }

    // event nodes
    for(auto it = eventNodes.begin(); it != eventNodes.end();) {
      if(!eventNames.contains(it->first)) {
        resetPins(*it->second);
        canvas.getNodes().erase(it->second->getUID());
        it = eventNodes.erase(it);
      } else ++it;
    }
    for(const auto &name : eventNames) {
      auto &node = eventNodes[name];
      if(!node) {
        auto &saved = scope["events"][name];
        node = addNode(saved.is_object() && saved.contains("pos") ? jsonVec(saved["pos"]) : spot("e:" + name));
      }
      fillEventNode(*node, name);
    }

    auto special = [&](std::shared_ptr<GraphNode> &node, bool show, const char* layoutKey, ImVec2 fallback, auto fill) {
      if(!show) {
        if(node) { resetPins(*node); canvas.getNodes().erase(node->getUID()); node = nullptr; }
        return;
      }
      if(!node) {
        auto &saved = scope[layoutKey];
        node = addNode(saved.is_object() && saved.contains("pos") ? jsonVec(saved["pos"]) : fallback);
      }
      fill(*node);
    };
    special(globalNode, showGlobal, "global", spot("global"), [&](GraphNode &n) { fillGlobalNode(n); });
    special(scopeNode, showScope, "scope", spot("scope"), [&](GraphNode &n) { fillScopeNode(n); });

    // wires
    std::map<std::pair<uint32_t, std::string>, ImFlow::Pin*> globalPins{};
    auto findPin = [&](const G::End &end, const std::string &name, bool input) -> ImFlow::Pin* {
      for(auto &[pin, info] : pins) {
        if(info.type != end.type)continue;
        const bool isIn = const_cast<ImFlow::Pin*>(pin)->getType() == ImFlow::PinType_Input;
        if(isIn != input)continue;
        if(end.type == G::End::Type::Pin ? info.ref == end.pin : info.name == name) return const_cast<ImFlow::Pin*>(pin);
      }
      return nullptr;
    };
    for(const auto &w : wires) {
      auto *from = findPin(w.from, w.name, false);
      auto *to = findPin(w.to, w.name, true);
      if(!from || !to)continue;
      to->createLink(from, true);
      auto links = to->getLinks();
      if(!links.empty()) if(auto link = links.back().lock()) linkWires[link.get()] = w;
    }
    rebuilding = false;
    needLinks = false;
  }

  // ---- hover: the wires of the pin under the pointer, followed downstream through event nodes

  std::set<ImFlow::Pin*> highlighted{};

  void highlight(ImFlow::Pin *hovered)
  {
    std::set<ImFlow::Pin*> next{};
    if(hovered && pins.contains(hovered)) {
      std::vector<ImFlow::Pin*> todo{hovered};
      while(!todo.empty()) {
        auto *pin = todo.back();
        todo.pop_back();
        if(!next.insert(pin).second)continue;
        for(auto &weak : pin->getLinks()) {
          auto link = weak.lock();
          if(!link)continue;
          auto *other = pin->getType() == ImFlow::PinType_Output ? link->right() : link->left();
          next.insert(other);
          // an event node passes it on: from its "fire" input to its "listen" output
          auto it = pins.find(other);
          if(pin->getType() == ImFlow::PinType_Output && it != pins.end() && it->second.type == G::End::Type::Event) {
            for(auto &out : other->getParent()->getOuts()) todo.push_back(out.get());
          }
        }
      }
    }
    if(next == highlighted)return;
    for(auto *pin : highlighted) if(pins.contains(pin)) { pin->getStyle()->extra.link_thickness = 2.2f; }
    for(auto *pin : next) pin->getStyle()->extra.link_thickness = 4.2f;
    highlighted = next;
  }

  // ---- edits

  void edited(const char* what)
  {
    Editor::UndoRedo::getHistory().markChanged(what);
    if(prefabUUID) ctx.project->getAssets().markPrefabDirty(prefabUUID);
    signature = 0; // collect again next frame
  }

  void applyPending()
  {
    auto list = std::move(pending);
    pending.clear();
    bool changed = false;
    for(auto [left, right] : list) {
      auto a = pins.find(left);
      auto b = pins.find(right);
      if(a == pins.end() || b == pins.end())continue;
      const auto &out = a->second;
      const auto &in = b->second;
      if(out.type == G::End::Type::Pin && in.type == G::End::Type::Pin) {
        changed |= !G::connect(*ctx.project, *root, model, out.ref, in.ref).empty();
      } else if(out.type == G::End::Type::Pin) {
        changed |= G::bind(*ctx.project, *root, model, out.ref, in.name);
      } else if(in.type == G::End::Type::Pin) {
        changed |= G::bind(*ctx.project, *root, model, in.ref, out.name);
      }
    }
    if(changed) edited("Wire Events");
    else if(!list.empty()) signature = 0; // drop the wire that did nothing
  }

  void removeWire(const ImFlow::Link *link)
  {
    auto it = linkWires.find(link);
    if(it == linkWires.end())return;
    if(G::disconnect(*ctx.project, *root, model, it->second)) edited("Remove Event Wire");
  }

  // ---- selection

  void syncSelection()
  {
    std::vector<uint32_t> nodeSel{};
    for(auto &[uuid, node] : objectNodes) if(node->isSelected()) nodeSel.push_back(uuid);

    std::vector<uint32_t> ctxSel{};
    if(kind == G::Kind::Prefab) {
      if(ctx.selObjectUUID == editInstance) ctxSel.push_back(ctx.selSubPath.empty() ? root->uuid : ctx.selSubPath.back());
    } else {
      ctxSel = ctx.getSelectedObjectUUIDs();
      std::sort(ctxSel.begin(), ctxSel.end());
    }

    if(nodeSel != lastNodeSel) {
      // picked in the graph: select the objects
      if(!nodeSel.empty()) {
        if(kind == G::Kind::Prefab) {
          std::vector<uint32_t> path{};
          if(nodeSel.back() == root->uuid || pathTo(*root, nodeSel.back(), path)) {
            if(path.empty()) ctx.setObjectSelection(editInstance);
            else ctx.setNestedSelection(editInstance, path);
          }
        } else if(nodeSel.size() == 1) {
          ctx.setObjectSelection(nodeSel.front());
        } else {
          ctx.setObjectSelectionList(nodeSel, nodeSel.back());
        }
      }
      lastNodeSel = nodeSel;
      lastCtxSel = ctxSel;
      return;
    }
    if(ctxSel != lastCtxSel) {
      // picked elsewhere: select the nodes
      for(auto &[uuid, node] : objectNodes) {
        node->selected(std::find(ctxSel.begin(), ctxSel.end(), uuid) != ctxSel.end());
      }
      lastCtxSel = ctxSel;
      lastNodeSel.clear();
      for(auto uuid : ctxSel) if(objectNodes.contains(uuid)) lastNodeSel.push_back(uuid);
    }
  }

  // ---- pop-ups

  void addEventNode(const std::string &name, ImVec2 pos)
  {
    if(name.empty())return;
    layoutScope()["events"][name]["pos"] = vecJson(pos);
    signature = 0;
  }

  void addBuiltin(size_t index, ImVec2 pos)
  {
    auto *scene = ctx.project->getScenes().getLoadedScene();
    if(!scene)return;
    Project::Object *obj = nullptr;
    if(kind == G::Kind::Scene) {
      Editor::UndoRedo::getHistory().markChanged("Add Built-in Event Object");
      auto added = scene->addObject(focus ? *findObj(*root, focus) : *root);
      obj = added.get();
    } else {
      auto path = Editor::SelectionUtils::addPrefabObject(*scene, editInstance, {}, false);
      uint64_t prefabId = 0;
      obj = path.empty() ? nullptr : Editor::SelectionUtils::findEditedPrefabNode(*scene, editInstance, path, prefabId);
      if(prefabId) ctx.project->getAssets().markPrefabDirty(prefabId);
    }
    if(!obj)return;
    obj->name = BUILTIN_NAMES[index];
    obj->addComponent(0); // Code
    if(!obj->components.empty()) Project::Component::Code::setScript(obj->components.back(), BUILTIN_FIRST + index, false);
    layoutScope()["nodes"][std::to_string(obj->uuid)]["pos"] = vecJson(pos);
    setShownByHand(obj->uuid, true);
    signature = 0;
  }

  void drawCanvasMenu(ImFlow::BaseNode *hovered)
  {
    if(ImGui::IsWindowAppearing()) {
      popupPos = canvas.screen2grid(ImGui::GetMousePos());
      popupLink = hovered ? nullptr : canvas.getHoveredLink();
      popupFilter.clear();
      newEventName.clear();
    }

    if(popupLink) {
      auto it = linkWires.find(popupLink);
      if(it != linkWires.end()) {
        ImGui::TextDisabled("%s", it->second.name.c_str());
        if(ImGui::Selectable(ICON_MDI_CLOSE " Remove wire")) { removeWire(popupLink); popupLink = nullptr; }
        return;
      }
    }

    if(hovered) {
      for(auto &[uuid, node] : objectNodes) {
        if(node.get() != hovered)continue;
        const auto *src = model.find(uuid);
        if(ImGui::Selectable(ICON_MDI_CROSSHAIRS_GPS " Focus on this object")) {
          focusMode = true;
          ctx.setObjectSelection(kind == G::Kind::Prefab ? editInstance : uuid);
          if(kind == G::Kind::Prefab) {
            std::vector<uint32_t> path{};
            if(pathTo(*root, uuid, path)) ctx.setNestedSelection(editInstance, path);
          }
        }
        if(shownByHand().contains(uuid) && src && !src->bound()) {
          if(ImGui::Selectable(ICON_MDI_EYE_OFF " Hide from the graph")) { setShownByHand(uuid, false); signature = 0; }
        }
        if(src && src->instance && kind == G::Kind::Scene && ImGui::Selectable(ICON_MDI_PENCIL " Edit its prefab")) {
          ctx.setObjectSelection(uuid);
          ctx.prefabEditUUID = uuid;
        }
        return;
      }
      for(auto &[uuid, node] : canvasNodes) {
        if(node.get() != hovered)continue;
        if(canvasesByHand().contains(uuid) && ImGui::Selectable(ICON_MDI_EYE_OFF " Hide from the graph")) {
          setCanvasByHand(uuid, false);
          signature = 0;
        }
        ImGui::TextDisabled("Its bindings are saved in the canvas file");
        return;
      }
      for(auto &[name, node] : eventNodes) {
        if(node.get() != hovered)continue;
        if(ImGui::Selectable(ICON_MDI_CLOSE " Remove the event node")) {
          layoutScope()["events"].erase(name);
          signature = 0;
        }
        ImGui::TextDisabled("Bindings stay; only the node goes");
        return;
      }
      if(hovered == globalNode.get() || hovered == scopeNode.get()) {
        const char* layoutKey = hovered == globalNode.get() ? "global" : "scope";
        if(ImGui::Selectable(ICON_MDI_EYE_OFF " Hide")) { layoutScope()[layoutKey]["shown"] = false; signature = 0; }
        ImGui::TextDisabled("Shown again while a pin uses its events");
        return;
      }
      return;
    }

    // empty canvas: add things
    ImGui::TextDisabled("Add to the graph");
    ImGui::SetNextItemWidth(240);
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##filter", "Search objects and events", &popupFilter);

    if(ImGui::BeginMenu(ICON_MDI_CUBE_OUTLINE " Object")) {
      int count = 0;
      for(const auto &node : model.nodes) {
        if(objectNodes.contains(node.uuid))continue;
        if(!popupFilter.empty() && node.name.find(popupFilter) == std::string::npos)continue;
        if(++count > 60)break;
        if(ImGui::Selectable((node.name + (node.instance ? " (" + node.prefab + ")" : "")).c_str())) {
          setShownByHand(node.uuid, true);
          layoutScope()["nodes"][std::to_string(node.uuid)]["pos"] = vecJson(popupPos);
          signature = 0;
        }
      }
      if(count == 0) ImGui::TextDisabled("Every object is shown");
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(ICON_MDI_FLASH " Event node")) {
      ImGui::SetNextItemWidth(240);
      if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
      bool enter = ImGui::InputTextWithHint("##event", (model.ns + "::Name").c_str(), &newEventName, ImGuiInputTextFlags_EnterReturnsTrue);
      const auto problem = Project::Events::nameProblem(newEventName);
      if(!newEventName.empty() && !problem.empty()) ImGui::TextColored(TEXT_PROBLEM, "%s", problem.c_str());
      if(enter && problem.empty()) { addEventNode(newEventName, popupPos); ImGui::CloseCurrentPopup(); }
      ImGui::Separator();
      int count = 0;
      std::set<std::string> known{model.allNames.begin(), model.allNames.end()};
      known.insert(model.scopeEvents.begin(), model.scopeEvents.end());
      for(const auto &name : known) {
        if(!popupFilter.empty() && name.find(popupFilter) == std::string::npos)continue;
        if(++count > 40)break;
        if(ImGui::Selectable(name.c_str())) addEventNode(name, popupPos);
      }
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(ICON_MDI_MONITOR_DASHBOARD " UI canvas")) {
      int count = 0;
      for(const auto &node : model.canvases) {
        if(canvasNodes.contains(node.canvas))continue;
        if(!popupFilter.empty() && node.name.find(popupFilter) == std::string::npos)continue;
        ++count;
        if(ImGui::Selectable(node.name.c_str())) {
          setCanvasByHand(node.canvas, true);
          layoutScope()["canvasNodes"][std::to_string(node.canvas)]["pos"] = vecJson(popupPos);
          signature = 0;
        }
      }
      if(count == 0) ImGui::TextDisabled(model.canvases.empty() ? "The project has no canvas" : "Every canvas is shown");
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu(ICON_MDI_PUZZLE_OUTLINE " Built-in object")) {
      for(size_t i = 0; i < std::size(BUILTIN_NAMES); ++i) {
        if(!popupFilter.empty() && std::string{BUILTIN_NAMES[i]}.find(popupFilter) == std::string::npos)continue;
        if(ImGui::Selectable(BUILTIN_NAMES[i])) addBuiltin(i, popupPos);
      }
      ImGui::EndMenu();
    }
    if(!globalNode && ImGui::Selectable(ICON_MDI_EARTH " Global node")) {
      layoutScope()["global"]["shown"] = true;
      layoutScope()["global"]["pos"] = vecJson(popupPos);
      signature = 0;
    }
    if(!scopeNode && ImGui::Selectable(kind == G::Kind::Scene ? ICON_MDI_TARGET " Scene events node" : ICON_MDI_TARGET " Prefab events node")) {
      layoutScope()["scope"]["shown"] = true;
      layoutScope()["scope"]["pos"] = vecJson(popupPos);
      signature = 0;
    }
  }

  void drawBrowser(Project::Scene &scene)
  {
    namespace U = Project::Events::Usages;
    if(browserStale) {
      uses = U::collect(*ctx.project, &scene);
      browserStale = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##browseFilter", "Search events", &browseFilter);
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_REFRESH "##refresh")) browserStale = true;
    ImGui::SetItemTooltip("Look through every scene, prefab, canvas and script again");

    // the names, with how often they are fired and listened to
    const float listH = ImGui::GetContentRegionAvail().y * 0.5f;
    ImGui::BeginChild("names", ImVec2(0, listH), ImGuiChildFlags_Borders);
    std::string last{};
    for(size_t i = 0; i < uses.size(); ++i) {
      const auto k = G::key(uses[i].name);
      if(k == last)continue;
      last = k;
      if(!browseFilter.empty() && k.find(browseFilter) == std::string::npos)continue;
      int fires = 0, listens = 0, code = 0;
      for(size_t j = i; j < uses.size() && G::key(uses[j].name) == k; ++j) {
        if(uses[j].kind == U::Use::Kind::Code) ++code;
        else if(uses[j].input) ++listens;
        else ++fires;
      }
      const bool problem = !Project::Events::nameProblem(k).empty();
      if(problem) ImGui::PushStyleColor(ImGuiCol_Text, TEXT_PROBLEM);
      const std::string label = k + "##" + k;
      if(ImGui::Selectable(label.c_str(), browseSelected == k)) { browseSelected = k; renameText = k; }
      if(problem) ImGui::PopStyleColor();
      ImGui::SetItemTooltip("%d firing, %d listening, %d in code%s", fires, listens, code,
                            (fires == 0 && code == 0) ? "\nNothing fires it" : ((listens == 0 && code == 0) ? "\nNothing listens to it" : ""));
    }
    ImGui::EndChild();

    if(browseSelected.empty()) {
      ImGui::TextDisabled("Pick an event to see where it is used");
      return;
    }
    ImGui::TextUnformatted(browseSelected.c_str());
    ImGui::BeginChild("uses", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.2f), ImGuiChildFlags_Borders);
    for(const auto &use : uses) {
      if(G::key(use.name) != browseSelected)continue;
      const char* icon = use.kind == U::Use::Kind::Code ? ICON_MDI_CODE_BRACES
                       : (use.input ? ICON_MDI_ARROW_RIGHT_BOLD : ICON_MDI_ARROW_LEFT_BOLD);
      ImGui::PushID(&use);
      if(ImGui::Selectable((std::string{icon} + " " + use.where).c_str())) {
        // an object of the open scene: select it
        if(use.kind == U::Use::Kind::Scene && use.sceneId == scene.getId() && use.object) ctx.setObjectSelection(use.object);
      }
      ImGui::SetItemTooltip("%s", use.kind == U::Use::Kind::Code ? "A \"...\"_event literal in game code"
                                 : (use.input ? "Listens to it" : "Fires it"));
      ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const auto problem = Project::Events::nameProblem(renameText);
    if(!problem.empty()) ImGui::PushStyleColor(ImGuiCol_Text, TEXT_PROBLEM);
    ImGui::InputText("##rename", &renameText);
    if(!problem.empty()) { ImGui::PopStyleColor(); ImGui::SetItemTooltip("%s", problem.c_str()); }
    const bool canRename = problem.empty() && G::key(renameText) != browseSelected;
    if(!canRename) ImGui::BeginDisabled();
    if(ImGui::Button(ICON_MDI_RENAME " Rename everywhere")) {
      Editor::UndoRedo::getHistory().markChanged("Rename Event");
      auto result = U::rename(*ctx.project, &scene, browseSelected, renameText);
      std::string msg = "Renamed " + browseSelected + " to " + renameText + ": " + std::to_string(result.bindings) +
                        " bindings, " + std::to_string(result.files.size()) + " other files";
      Utils::Logger::log(msg);
      for(const auto &c : result.code) Utils::Logger::log("Rename: game code still uses " + browseSelected + " at " + c, Utils::Logger::LEVEL_WARN);
      Editor::Noti::add(result.code.empty() ? Editor::Noti::Type::SUCCESS : Editor::Noti::Type::INFO,
                        result.code.empty() ? msg : msg + "; change the game code by hand (see the log)");
      if(prefabUUID) ctx.project->getAssets().markPrefabDirty(prefabUUID);
      browseSelected = renameText;
      browserStale = true;
      signature = 0;
    }
    if(!canRename) ImGui::EndDisabled();
    ImGui::SetItemTooltip("Every scene, prefab and canvas; game code is listed in the log, not changed");
  }

  void drawDroppedMenu(ImFlow::Pin *dragged)
  {
    if(ImGui::IsWindowAppearing()) {
      popupPos = canvas.screen2grid(ImGui::GetMousePos());
      popupFilter.clear();
    }
    auto it = pins.find(dragged);
    if(it == pins.end() || it->second.type != G::End::Type::Pin) {
      ImGui::TextDisabled("Drop it on a pin");
      return;
    }
    const auto ref = it->second.ref;
    const auto *pin = model.findPin(ref);
    std::string name = ref.input ? std::string{} : G::wireName(model, ref);
    if(name.empty() && pin && !pin->names.empty()) name = pin->names.front();
    if(name.empty()) name = G::autoName(model, ref);

    ImGui::TextDisabled("%s", G::describe(*ctx.project, *root, ref).c_str());
    if(ImGui::Selectable((ICON_MDI_FLASH " New event node: " + name).c_str())) {
      if(G::bind(*ctx.project, *root, model, ref, name)) edited("Wire Events");
      addEventNode(name, popupPos);
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(240);
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##bind", "Bind to an event", &popupFilter);
    std::set<std::string> known{model.allNames.begin(), model.allNames.end()};
    known.insert(model.scopeEvents.begin(), model.scopeEvents.end());
    for(const auto &sys : model.systems) {
      for(const auto &p : ref.input ? sys.out : sys.in) known.insert("global::" + sys.name + "." + p);
    }
    int count = 0;
    for(const auto &n : known) {
      if(!popupFilter.empty() && n.find(popupFilter) == std::string::npos)continue;
      if(++count > 40)break;
      if(ImGui::Selectable(n.c_str()) && G::bind(*ctx.project, *root, model, ref, n)) edited("Bind Event");
    }
    if(!popupFilter.empty() && !known.contains(popupFilter) && Project::Events::nameProblem(popupFilter).empty()) {
      if(ImGui::Selectable(("Bind to " + popupFilter).c_str()) && G::bind(*ctx.project, *root, model, ref, popupFilter)) edited("Bind Event");
    }
  }
};

Editor::EventGraphWindow::EventGraphWindow()
  : state{std::make_unique<State>()}
{
  auto *s = state.get();
  s->canvas.onLinkCreateHook = [s](std::shared_ptr<ImFlow::Link> link) {
    if(!s->rebuilding && link) s->pending.push_back({link->left(), link->right()});
  };
  s->canvas.rightClickPopUpContent([s](ImFlow::BaseNode *node) { s->drawCanvasMenu(node); });
  s->canvas.droppedLinkPopUpContent([s](ImFlow::Pin *pin) { s->drawDroppedMenu(pin); });
}

Editor::EventGraphWindow::~EventGraphWindow()
{
  if(state && !state->key.empty()) {
    state->captureLayout();
    state->saveLayout();
  }
}

void Editor::EventGraphWindow::draw()
{
  auto &s = *state;
  if(!ctx.project)return;
  auto *scene = ctx.project->getScenes().getLoadedScene();
  if(!scene) {
    ImGui::TextDisabled("Open a scene to see its events");
    return;
  }

  s.identify(*scene);
  s.refresh(false);

  // toolbar
  {
    std::string title = s.kind == G::Kind::Scene
      ? ICON_MDI_SITEMAP " Scene " + std::to_string(s.sceneId) + ": " + scene->conf.name.value
      : ICON_MDI_PACKAGE_VARIANT_CLOSED " Prefab " + (s.prefab ? s.prefab->obj.name : std::string{});
    if(s.focus) {
      const auto *node = s.model.find(s.focus);
      title += "  " ICON_MDI_CROSSHAIRS_GPS " " + (node ? node->name : std::string{"?"}) + " (object:: events)";
    }
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    if(ImGui::Checkbox("Focus", &s.focusMode)) s.signature = 0;
    ImGui::SetItemTooltip("Show only the selected object; new wires make object:: events");
    ImGui::SameLine();
    if(ImGui::Checkbox("Browse", &s.browserOpen) && s.browserOpen) s.browserStale = true;
    ImGui::SetItemTooltip("Every event of the project: where it is used, and renaming it");
    ImGui::SameLine();
    if(ImGui::SmallButton(ICON_MDI_AUTO_FIX " Arrange")) {
      auto &scope = s.layoutScope();
      scope["nodes"] = nlohmann::json::object();
      for(const char* k : {"global", "scope"}) if(scope.contains(k) && scope[k].is_object()) scope[k].erase("pos");
      if(scope.contains("events") && scope["events"].is_object()) for(auto &[name, v] : scope["events"].items()) v.erase("pos");
      s.dropObjectNodes();
    }
    ImGui::SetItemTooltip("Place every object node again");
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_MDI_HELP_CIRCLE_OUTLINE);
    ImGui::SetItemTooltip("Drag from an output to an input to wire them: both get bound to one event.\n"
                          "Right-click the canvas to add objects, event nodes, built-in objects, the Global node.\n"
                          "Click a wire and press Delete, or right-click it, to remove it.\n"
                          "Red pins have a binding the build rejects; hover them for the reason.");
    if(s.objectNodes.empty() && s.eventNodes.empty()) {
      ImGui::TextDisabled("Nothing is wired yet. Right-click to add objects, then drag from an output to an input.");
    }
  }

  if(s.browserOpen) {
    ImGui::BeginChild("eventBrowser", ImVec2(300, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    s.drawBrowser(*scene);
    ImGui::EndChild();
    ImGui::SameLine();
  }
  ImGui::BeginChild("eventCanvas", ImVec2(0, 0), false);
  s.canvas.setSize(ImGui::GetContentRegionAvail());
  auto &gridStyle = s.canvas.getStyle();
  gridStyle.colors.background = ImGui::Theme::getColorU32("nodeBackground", IM_COL32(33, 41, 45, 255));
  gridStyle.colors.grid = ImGui::Theme::getColorU32("nodeGrid", IM_COL32(200, 200, 200, 40));
  s.canvas.getGrid().config().color = gridStyle.colors.background;
  s.canvas.update();
  s.highlight(s.canvas.m_hovering); // the pin under the pointer this frame
  const bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  ImGui::EndChild();

  s.applyPending();
  if(canvasFocused && !ImGui::IsAnyItemActive() && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
    std::vector<const ImFlow::Link*> selected{};
    for(auto &[link, wire] : s.linkWires) if(link->isSelected()) selected.push_back(link);
    for(auto *link : selected) s.removeWire(link);
  }
  s.syncSelection();

  if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    s.captureLayout();
    s.saveLayout();
  }
}
