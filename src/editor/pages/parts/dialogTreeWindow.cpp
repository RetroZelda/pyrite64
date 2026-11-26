/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.1).
#include "dialogTreeWindow.h"

#include <algorithm>
#include <cfloat>
#include <format>
#include <map>
#include <set>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IconsMaterialDesignIcons.h"
#include "ImNodeFlow.h"

#include "../../../context.h"
#include "../../imgui/theme.h"
#include "../../imgui/notification.h"
#include "../../../project/project.h"
#include "../../../project/dialog/dialogTree.h"
#include "../../../project/dialog/speakers.h"
#include "../../../project/locale/locProject.h"
#include "../../../utils/fs.h"
#include "../../../utils/json.h"
#include "../../../utils/logger.h"

namespace
{
  namespace DT = Project::Dialog;
  using DT::NodeType;

  struct Flow {}; // the data type of flow pins; yes/no pins are bool

  bool sameType(ImFlow::Pin* out, ImFlow::Pin* in) { return out->getDataType() == in->getDataType(); }

  std::shared_ptr<ImFlow::PinStyle> flowStyle()
  {
    static auto s = [] {
      auto p = std::make_shared<ImFlow::PinStyle>(IM_COL32(0xDD, 0xDD, 0xDD, 0xFF), 3, 6.0f, 7.0f, 6.5f, 1.3f);
      p->extra.padding.y = 16;
      p->extra.animated = true;
      return p;
    }();
    return s;
  }

  std::shared_ptr<ImFlow::PinStyle> boolStyle()
  {
    static auto s = [] {
      auto p = std::make_shared<ImFlow::PinStyle>(IM_COL32(0x60, 0xC0, 0x80, 0xFF), 0, 6.0f, 7.0f, 6.5f, 1.3f);
      p->extra.padding.y = 16;
      return p;
    }();
    return s;
  }

  struct TypeLook { const char* title; ImU32 color; };
  TypeLook lookOf(NodeType t)
  {
    switch(t) {
      case NodeType::Start:     return {ICON_MDI_PLAY " Start", IM_COL32(60, 140, 70, 255)};
      case NodeType::Line:      return {ICON_MDI_MESSAGE_TEXT_OUTLINE " Line", IM_COL32(60, 100, 160, 255)};
      case NodeType::Choice:    return {ICON_MDI_FORMAT_LIST_BULLETED " Choice", IM_COL32(130, 80, 160, 255)};
      case NodeType::Branch:    return {ICON_MDI_CALL_SPLIT " Branch", IM_COL32(160, 120, 40, 255)};
      case NodeType::Condition: return {ICON_MDI_HELP_CIRCLE_OUTLINE " Condition", IM_COL32(50, 130, 110, 255)};
      case NodeType::Logic:     return {ICON_MDI_GATE_AND " Logic", IM_COL32(50, 110, 100, 255)};
      case NodeType::Input:     return {ICON_MDI_LOGIN " Input", IM_COL32(70, 120, 120, 255)};
      case NodeType::Event:     return {ICON_MDI_LIGHTNING_BOLT " Event", IM_COL32(170, 90, 50, 255)};
      case NodeType::GoTo:      return {ICON_MDI_ARROW_RIGHT_BOLD " Go To", IM_COL32(90, 90, 110, 255)};
      case NodeType::End:       return {ICON_MDI_STOP " End", IM_COL32(150, 60, 60, 255)};
      default: break;
    }
    return {"?", IM_COL32(80, 80, 80, 255)};
  }

  /** Text of a key in a language, "" when there is none. */
  std::string textOf(const Project::Loc::Localization &loc, const std::string &key, int lang)
  {
    if(lang < 0 || lang >= static_cast<int>(loc.tables.size())) return {};
    auto it = loc.tables[lang].entries.find(key);
    return it == loc.tables[lang].entries.end() ? std::string{} : it->second.text;
  }
}

namespace Editor
{
  class DlgNode;

  struct DialogTreeWindow::State
  {
    Project::AssetManagerEntry* asset{};
    std::string name{};
    std::string keyPrefix{};
    ImFlow::ImNodeFlow graph{};
    std::map<uint32_t, std::shared_ptr<DlgNode>> nodes{};
    uint32_t nextId{1};
    std::string savedState{};
    bool dirty{false};
    bool isInit{false};
    int previewLang{-1};
    std::string menuSearch{};
    int fitFrames{2}; // fit the view to the nodes once they have a size

    void fitView()
    {
      auto &all = graph.getNodes();
      if(all.empty()) return;
      ImVec2 mn{FLT_MAX, FLT_MAX}, mx{-FLT_MAX, -FLT_MAX};
      for(auto &[uid, node] : all) {
        const ImVec2 p = node->getPos(), sz = node->getSize();
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y);
        mx.x = std::max(mx.x, p.x + sz.x); mx.y = std::max(mx.y, p.y + sz.y);
      }
      const ImVec2 canvas = graph.getGrid().size();
      if(canvas.x < 1.0f || canvas.y < 1.0f) return;
      const float bw = std::max(mx.x - mn.x, 1.0f), bh = std::max(mx.y - mn.y, 1.0f);
      const float scale = std::clamp(std::min(canvas.x / bw, canvas.y / bh) * 0.9f, 0.3f, 1.0f);
      const ImVec2 center{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f};
      graph.setScale(scale);
      graph.setScroll({canvas.x / (2.0f * scale) - center.x, canvas.y / (2.0f * scale) - center.y});
    }

    Project::Dialog::Speakers speakers{};
    std::vector<const Project::AssetManagerEntry*> conditionScripts{};

    // Play: walking the tree in the editor
    bool playing{false};
    uint32_t playNode{0};
    std::vector<std::string> playLog{};

    DlgNode* addNode(NodeType type, ImVec2 pos);
    Project::Dialog::Tree toTree();
    void load(const Project::Dialog::Tree &tree);
  };

  class DlgNode : public ImFlow::BaseNode
  {
    public:
      DT::Node data{};
      DialogTreeWindow::State* st{};
      // the line's text in the preview language, while it is edited
      std::vector<std::string> buffers{};
      int activeSlot{-1}; // the text box being typed in, which keeps its buffer

      DlgNode(DT::Node node, DialogTreeWindow::State* state) : data{std::move(node)}, st{state}
      {
        const auto look = lookOf(data.type);
        setTitle(look.title);
        setStyle(std::make_shared<ImFlow::NodeStyle>(look.color, ImColor(255, 255, 255, 255), 6.0f));
        // pins are made by syncPins() after ImNodeFlow::addNode: the node has no graph before that
      }

      // ImNodeFlow's inPin()/outPin() assume the pin exists; these return null when it does not
      ImFlow::Pin* findIn(const std::string &name)
      {
        for(const auto &p : getIns()) if(p->getName() == name) return p.get();
        return nullptr;
      }
      ImFlow::Pin* findOut(const std::string &name)
      {
        for(const auto &p : getOuts()) if(p->getName() == name) return p.get();
        return nullptr;
      }

      void syncPins()
      {
        const auto ins = DT::inputsOf(data);
        const auto outs = DT::outputsOf(data);
        std::set<std::string> wantIn{}, wantOut{};
        for(const auto &p : ins) wantIn.insert(p.name);
        for(const auto &p : outs) wantOut.insert(p.name);
        std::vector<std::string> dropIns{}, dropOuts{};
        for(const auto &p : getIns()) if(!wantIn.contains(p->getName())) dropIns.push_back(p->getName());
        for(const auto &p : getOuts()) if(!wantOut.contains(p->getName())) dropOuts.push_back(p->getName());
        for(const auto &n : dropIns) dropIN(n.c_str());
        for(const auto &n : dropOuts) dropOUT(n.c_str());
        for(const auto &p : ins) {
          if(findIn(p.name)) continue;
          if(p.kind == DT::PinKind::Flow) addIN_uid<Flow>(p.name, p.name, sameType, flowStyle());
          else addIN_uid<bool>(p.name, p.name, sameType, boolStyle());
        }
        for(const auto &p : outs) {
          if(findOut(p.name)) continue;
          if(p.kind == DT::PinKind::Flow) (void)addOUT_uid<Flow>(p.name, p.name, flowStyle());
          else (void)addOUT_uid<bool>(p.name, p.name, boolStyle());
        }
      }

      void draw() override;

    private:
      void drawText(const std::string &key, size_t slot, float width, float lines);
  };

  DlgNode* DialogTreeWindow::State::addNode(NodeType type, ImVec2 pos)
  {
    DT::Node n{};
    n.id = nextId++;
    n.type = type;
    if(type == NodeType::Line) n.key = keyPrefix + "." + std::to_string(n.id);
    if(type == NodeType::Choice) {
      n.options.push_back({.key = DT::optionKey(keyPrefix, n.id, 0), .isDefault = true});
      n.options.push_back({.key = DT::optionKey(keyPrefix, n.id, 1), .cancel = true});
    }
    auto node = graph.addNode<DlgNode>(pos, n, this);
    node->syncPins();
    nodes[n.id] = node;
    return node.get();
  }

  Project::Dialog::Tree DialogTreeWindow::State::toTree()
  {
    DT::Tree tree{};
    tree.nextId = nextId;
    for(auto it = nodes.begin(); it != nodes.end();) {
      if(it->second->toDestroy()) { it = nodes.erase(it); continue; }
      auto n = it->second->data;
      n.x = it->second->getPos().x;
      n.y = it->second->getPos().y;
      tree.nodes.push_back(n);
      ++it;
    }
    for(const auto &weak : graph.getLinks()) {
      auto link = weak.lock();
      if(!link || !link->left() || !link->right()) continue;
      auto* from = dynamic_cast<DlgNode*>(link->left()->getParent());
      auto* to = dynamic_cast<DlgNode*>(link->right()->getParent());
      if(!from || !to || from->toDestroy() || to->toDestroy()) continue;
      tree.links.push_back({from->data.id, link->left()->getName(), to->data.id, link->right()->getName()});
    }
    return tree;
  }

  void DialogTreeWindow::State::load(const Project::Dialog::Tree &tree)
  {
    for(auto &[id, node] : nodes) node->destroy();
    nodes.clear();
    nextId = tree.nextId;
    for(const auto &n : tree.nodes) {
      auto node = graph.addNode<DlgNode>({n.x, n.y}, n, this);
      node->syncPins();
      nodes[n.id] = node;
    }
    for(const auto &l : tree.links) {
      auto a = nodes.find(l.from), b = nodes.find(l.to);
      if(a == nodes.end() || b == nodes.end()) continue;
      auto* out = a->second->findOut(l.out);
      auto* in = b->second->findIn(l.in);
      if(out && in) out->createLink(in, true);
    }
  }

  void DlgNode::drawText(const std::string &key, size_t slot, float width, float lines)
  {
    if(!ctx.project || !Project::Loc::hasLocalization(ctx.project->getPath())) {
      ImGui::TextDisabled("no locale/ yet (View > Strings)");
      return;
    }
    auto &loc = Project::Loc::edit(*ctx.project);
    const int lang = st->previewLang < 0 ? loc.sourceIndex() : st->previewLang;
    if(buffers.size() <= slot) buffers.resize(slot + 1);
    ImGui::PushID(static_cast<int>(slot));
    if(activeSlot != static_cast<int>(slot)) buffers[slot] = textOf(loc, key, lang);
    ImGui::InputTextMultiline("##text", &buffers[slot], ImVec2(width, ImGui::GetTextLineHeight() * lines + 8.0f));
    if(ImGui::IsItemActivated()) activeSlot = static_cast<int>(slot);
    if(ImGui::IsItemDeactivated() && activeSlot == static_cast<int>(slot)) activeSlot = -1;
    if(ImGui::IsItemDeactivatedAfterEdit()) {
      Project::Loc::Uses one{};
      one[key].push_back({"dialog tree", {}, 0});
      loc.addMissing(one);
      loc.setText(key, static_cast<size_t>(lang), buffers[slot]);
      Project::Loc::saveCached(*ctx.project);
    }
    ImGui::PopID();
  }

  void DlgNode::draw()
  {
    const float w = 240.0f;
    ImGui::PushID(static_cast<int>(data.id));
    ImGui::PushItemWidth(w);
    switch(data.type)
    {
      case NodeType::Start:
        ImGui::InputTextWithHint("##name", "main start (no name)", &data.name);
        ImGui::SetItemTooltip("A named start lets code begin the tree somewhere else (Dialog::start with a start name)");
        break;

      case NodeType::Line: {
        // speaker
        const char* speakerLabel = data.speaker.empty() ? "(no speaker)" : data.speaker.c_str();
        if(ImGui::BeginCombo("##speaker", speakerLabel)) {
          if(ImGui::Selectable("(no speaker)", data.speaker.empty())) data.speaker.clear();
          for(const auto &s : st->speakers.list) if(ImGui::Selectable(s.id.c_str(), data.speaker == s.id)) data.speaker = s.id;
          ImGui::EndCombo();
        }
        drawText(data.key, 0, w, 3.0f);
        ImGui::TextDisabled("%s", data.key.c_str());
        static const char* LANGS[] = {"speaker", "game", "paired"};
        if(ImGui::BeginCombo("##lang", ("language: " + data.lang).c_str())) {
          for(auto *l : LANGS) if(ImGui::Selectable(l, data.lang == l)) data.lang = l;
          ImGui::EndCombo();
        }
        if(const auto *sp = st->speakers.find(data.speaker); sp && !sp->portraits.empty()) {
          if(ImGui::BeginCombo("##portrait", data.portrait.empty() ? "portrait: default" : ("portrait: " + data.portrait).c_str())) {
            if(ImGui::Selectable("default", data.portrait.empty())) data.portrait.clear();
            for(const auto &[pname, img] : sp->portraits) if(ImGui::Selectable(pname.c_str(), data.portrait == pname)) data.portrait = pname;
            ImGui::EndCombo();
          }
        }
        ImGui::Checkbox("Timed", &data.timed);
        ImGui::SetItemTooltip("Turns by itself after a read time (lines during gameplay)");
        ImGui::SameLine();
        ImGui::Checkbox("No voice", &data.noVoice);
        break;
      }

      case NodeType::Choice: {
        bool changed = false;
        int removeAt = -1;
        for(size_t k = 0; k < data.options.size(); ++k) {
          auto &opt = data.options[k];
          ImGui::PushID(static_cast<int>(k));
          ImGui::Text("%zu", k + 1);
          ImGui::SameLine();
          drawText(opt.key, k, w - 20.0f, 1.0f);
          if(ImGui::Checkbox("B", &opt.cancel) && opt.cancel) for(auto &o : data.options) if(&o != &opt) o.cancel = false;
          ImGui::SetItemTooltip("Cancel: B picks this option");
          ImGui::SameLine();
          if(ImGui::RadioButton("default", opt.isDefault)) for(auto &o : data.options) o.isDefault = &o == &opt;
          ImGui::SetItemTooltip("The cursor starts here");
          ImGui::SameLine();
          if(ImGui::SmallButton(ICON_MDI_CLOSE)) removeAt = static_cast<int>(k);
          ImGui::SetNextItemWidth(w * 0.7f);
          ImGui::InputTextWithHint("##event", "event when picked", &opt.event);
          if(!opt.event.empty()) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w * 0.3f - 8.0f);
            ImGui::InputInt("##value", &opt.eventValue, 0);
          }
          ImGui::PopID();
        }
        if(removeAt >= 0 && data.options.size() > 1) { data.options.erase(data.options.begin() + removeAt); changed = true; }
        if(data.options.size() < 8 && ImGui::SmallButton(ICON_MDI_PLUS " option")) {
          size_t n = 0;
          for(const auto &o : data.options) {
            auto dot = o.key.rfind('.');
            try { n = std::max(n, static_cast<size_t>(std::stoul(o.key.substr(dot + 1)))); } catch(...) {}
          }
          data.options.push_back({.key = DT::optionKey(st->keyPrefix, data.id, n)});
          changed = true;
        }
        if(changed) syncPins();
        break;
      }

      case NodeType::Branch:
        ImGui::TextDisabled("True when the Condition is,\nor when nothing is wired in");
        break;

      case NodeType::Condition: {
        auto *script = ctx.project ? ctx.project->getAssets().getEntryByUUID(data.script) : nullptr;
        if(ImGui::BeginCombo("##script", script ? script->name.c_str() : "pick a condition script")) {
          for(const auto *s : st->conditionScripts) {
            if(ImGui::Selectable(s->name.c_str(), s->getUUID() == data.script)) { data.script = s->getUUID(); data.args.clear(); }
          }
          ImGui::EndCombo();
        }
        if(script) {
          for(const auto &field : script->params.fields) {
            if(field.name.starts_with("condition")) continue; // the header: no use in a tree
            auto nameIt = field.attr.find("P64::Name");
            std::string label = nameIt != field.attr.end() ? nameIt->second : field.name;
            if(label.size() > 1 && label.front() == '"') label = label.substr(1, label.size() - 2);
            auto &val = data.args[field.name];
            ImGui::SetNextItemWidth(w * 0.5f);
            ImGui::InputTextWithHint(("##" + field.name).c_str(), field.defaultValue.c_str(), &val);
            ImGui::SameLine();
            ImGui::TextUnformatted(label.c_str());
          }
          ImGui::TextDisabled("runs on the object that started the dialog");
        }
        break;
      }

      case NodeType::Logic: {
        static const char* OPS[] = {"and", "or", "xor", "not"};
        if(ImGui::BeginCombo("##logic", data.logic.c_str())) {
          for(auto *o : OPS) if(ImGui::Selectable(o, data.logic == o)) data.logic = o;
          ImGui::EndCombo();
        }
        break;
      }

      case NodeType::Input:
        ImGui::InputTextWithHint("##name", "HasTicket", &data.name);
        ImGui::SetItemTooltip("A yes/no the scene wires in from its event graph");
        break;

      case NodeType::Event:
        ImGui::InputTextWithHint("##event", "prefab::Gate.Open", &data.event);
        ImGui::InputInt("value", &data.value, 0);
        break;

      case NodeType::GoTo: {
        const auto *target = ctx.project && data.tree ? ctx.project->getAssets().getEntryByUUID(data.tree) : nullptr;
        if(ImGui::BeginCombo("##tree", target ? target->name.c_str() : "this tree")) {
          if(ImGui::Selectable("this tree", data.tree == 0)) data.tree = 0;
          if(ctx.project) {
            for(const auto &e : ctx.project->getAssets().getTypeEntries(Project::FileType::DIALOG_TREE)) {
              if(ImGui::Selectable(e.name.c_str(), e.getUUID() == data.tree)) data.tree = e.getUUID();
            }
          }
          ImGui::EndCombo();
        }
        ImGui::InputTextWithHint("##start", "start name (main)", &data.start);
        break;
      }

      case NodeType::End:
        ImGui::InputInt("value", &data.value, 0);
        ImGui::SetItemTooltip("Closed fires with this value");
        break;

      default: break;
    }
    ImGui::PopItemWidth();
    ImGui::PopID();
  }
}

Editor::DialogTreeWindow::DialogTreeWindow(uint64_t assetUUID) : state{std::make_unique<State>()}
{
  auto &st = *state;
  st.asset = ctx.project ? ctx.project->getAssets().getEntryByUUID(assetUUID) : nullptr;
  if(!st.asset) return;
  st.name = ICON_MDI_CHAT_PROCESSING_OUTLINE " " + st.asset->name + "###dlg" + std::to_string(assetUUID);
  st.keyPrefix = DT::keyPrefixOf(st.asset->name);

  std::vector<std::string> errors{};
  Project::Dialog::loadSpeakers(ctx.project->getPath(), st.speakers, errors);
  for(const auto &e : ctx.project->getAssets().getTypeEntries(Project::FileType::CODE_OBJ)) {
    if(e.condition.isCondition) st.conditionScripts.push_back(&e);
  }

  DT::Tree tree{};
  try {
    DT::parse(Utils::JSON::loadFile(st.asset->path), tree, errors);
  } catch(const std::exception &e) {
    errors.push_back(e.what());
  }
  for(const auto &e : errors) Utils::Logger::log("Dialog " + st.asset->name + ": " + e, Utils::Logger::LEVEL_WARN);
  st.load(tree);
  st.savedState = DT::serialize(st.toTree());

  st.graph.rightClickPopUpContent([this](ImFlow::BaseNode* node) {
    auto &s = *state;
    if(node) {
      if(ImGui::Selectable(ICON_MDI_TRASH_CAN_OUTLINE " Remove")) {
        node->destroy();
        ImGui::CloseCurrentPopup();
      }
      return;
    }
    const ImVec2 pos = s.graph.screen2grid(ImGui::GetMousePosOnOpeningCurrentPopup());
    for(int t = 0; t < static_cast<int>(NodeType::Count); ++t) {
      const auto type = static_cast<NodeType>(t);
      if(ImGui::MenuItem(lookOf(type).title)) {
        s.addNode(type, pos);
        ImGui::CloseCurrentPopup();
      }
    }
  });

  st.graph.droppedLinkPopUpContent([this](ImFlow::Pin* pin) {
    auto &s = *state;
    const ImVec2 pos = s.graph.screen2grid(ImGui::GetMousePosOnOpeningCurrentPopup());
    for(int t = 0; t < static_cast<int>(NodeType::Count); ++t) {
      const auto type = static_cast<NodeType>(t);
      if(!ImGui::MenuItem(lookOf(type).title)) continue;
      auto* node = s.addNode(type, pos);
      // connect the first pin of the other side with the same data type
      const auto &pins = pin->getType() == ImFlow::PinType_Output ? node->getIns() : node->getOuts();
      for(const auto &p : pins) {
        if(p->getDataType() != pin->getDataType()) continue;
        if(pin->getType() == ImFlow::PinType_Output) pin->createLink(p.get());
        else p->createLink(pin);
        break;
      }
      ImGui::CloseCurrentPopup();
    }
  });
}

Editor::DialogTreeWindow::~DialogTreeWindow() = default;

bool Editor::DialogTreeWindow::isDirty() const { return state->dirty; }
uint64_t Editor::DialogTreeWindow::getAssetUUID() const { return state->asset ? state->asset->getUUID() : 0; }
const std::string& Editor::DialogTreeWindow::getName() const { return state->name; }

void Editor::DialogTreeWindow::save()
{
  auto &st = *state;
  if(!st.asset) return;
  const auto text = DT::serialize(st.toTree());
  Utils::FS::saveTextFile(st.asset->path, text);
  st.savedState = text;
  st.dirty = false;
}

bool Editor::DialogTreeWindow::draw()
{
  auto &st = *state;
  if(!st.asset) return false;

  if(!st.isInit) {
    st.isInit = true;
    ImGui::SetNextWindowSize({1000, 640}, ImGuiCond_Once);
  }

  bool open = true;
  const std::string title = st.name + (st.dirty ? " *" : "");
  if(ImGui::Begin(st.name.c_str(), &open, ImGuiWindowFlags_NoCollapse | (st.dirty ? ImGuiWindowFlags_UnsavedDocument : 0))) {
    // ---- toolbar
    if(ImGui::Button(ICON_MDI_CONTENT_SAVE " Save")) save();
    ImGui::SameLine();
    if(ImGui::Button(ICON_MDI_FIT_TO_PAGE_OUTLINE " Fit")) st.fitView();
    ImGui::SameLine();
    if(ImGui::Button(st.playing ? ICON_MDI_STOP " Stop" : ICON_MDI_PLAY " Play")) {
      st.playing = !st.playing;
      st.playLog.clear();
      st.playNode = 0;
      if(st.playing) {
        for(const auto &[id, node] : st.nodes) if(node->data.type == NodeType::Start && node->data.name.empty()) { st.playNode = id; break; }
      }
    }
    ImGui::SetItemTooltip("Walk the tree here: you answer each condition and pick each option");
    if(ctx.project && Project::Loc::hasLocalization(ctx.project->getPath())) {
      const auto &loc = Project::Loc::cached(*ctx.project);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(160);
      const int cur = st.previewLang < 0 ? loc.sourceIndex() : st.previewLang;
      const auto curName = cur >= 0 && cur < static_cast<int>(loc.config.languages.size()) ? loc.config.languages[cur].name : std::string{"-"};
      if(ImGui::BeginCombo("##lang", (ICON_MDI_TRANSLATE " " + curName).c_str())) {
        for(size_t i = 0; i < loc.config.languages.size(); ++i) {
          if(loc.config.languages[i].pseudo) continue;
          if(ImGui::Selectable(loc.config.languages[i].name.c_str(), static_cast<int>(i) == cur)) {
            st.previewLang = static_cast<int>(i);
            for(auto &[id, n] : st.nodes) n->buffers.clear();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::SetItemTooltip("The language the lines are shown and edited in");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("right-click: add a node   drag from a pin: add and wire   keys: %s.*", st.keyPrefix.c_str());

    // ---- play panel
    const float playW = st.playing ? 320.0f : 0.0f;
    if(st.playing) {
      ImGui::BeginChild("##play", ImVec2(playW, 0), ImGuiChildFlags_Borders);
      auto it = st.nodes.find(st.playNode);
      auto nextOf = [&](uint32_t node, const std::string &pin) -> uint32_t {
        auto tree = st.toTree();
        const auto *l = tree.linkFrom(node, pin);
        return l ? l->to : 0;
      };
      static const Project::Loc::Localization noLoc{};
      const auto &loc = ctx.project ? Project::Loc::cached(*ctx.project) : noLoc;
      const int lang = st.previewLang < 0 ? loc.sourceIndex() : st.previewLang;
      for(const auto &line : st.playLog) ImGui::TextWrapped("%s", line.c_str());
      ImGui::Separator();
      if(it == st.nodes.end()) {
        ImGui::TextDisabled("The dialog closed.");
      } else {
        const auto &d = it->second->data;
        switch(d.type) {
          case NodeType::Start:
            st.playNode = nextOf(d.id, "Next");
            break;
          case NodeType::Line: {
            const std::string text = textOf(loc, d.key, lang);
            const std::string who = d.speaker.empty() ? "" : d.speaker + ": ";
            ImGui::TextWrapped("%s%s", who.c_str(), text.empty() ? "(no text yet)" : text.c_str());
            if(ImGui::Button("Next " ICON_MDI_CHEVRON_RIGHT)) { st.playLog.push_back(who + text); st.playNode = nextOf(d.id, "Next"); }
            break;
          }
          case NodeType::Choice:
            for(size_t k = 0; k < d.options.size(); ++k) {
              const std::string text = textOf(loc, d.options[k].key, lang);
              if(ImGui::Button(std::format("{}{}##opt{}", text.empty() ? "(option)" : text, d.options[k].cancel ? "  [B]" : "", k).c_str())) {
                st.playLog.push_back("> " + text + (d.options[k].event.empty() ? "" : "  (fires " + d.options[k].event + ")"));
                st.playNode = nextOf(d.id, "Option " + std::to_string(k + 1));
              }
            }
            break;
          case NodeType::Branch:
            ImGui::TextWrapped("Is the condition true?");
            if(ImGui::Button("Yes")) st.playNode = nextOf(d.id, "True");
            ImGui::SameLine();
            if(ImGui::Button("No")) st.playNode = nextOf(d.id, "False");
            break;
          case NodeType::Event:
            st.playLog.push_back(std::format("(fires {} with {})", d.event, d.value));
            st.playNode = nextOf(d.id, "Next");
            break;
          case NodeType::GoTo:
            ImGui::TextWrapped("Goes to %s (start '%s'): Play does not follow it.", d.tree ? "another tree" : "this tree", d.start.c_str());
            break;
          case NodeType::End:
            ImGui::TextWrapped("Ends with value %d.", d.value);
            break;
          default:
            st.playNode = 0;
            break;
        }
      }
      ImGui::EndChild();
      ImGui::SameLine();
    }

    // ---- canvas
    ImGui::BeginChild("##canvas", ImVec2(0, 0), ImGuiChildFlags_None);
    st.graph.setSize(ImGui::GetContentRegionAvail());
    auto &gridStyle = st.graph.getStyle();
    gridStyle.colors.background = ImGui::Theme::getColorU32("nodeBackground", IM_COL32(33, 41, 45, 255));
    gridStyle.colors.grid = ImGui::Theme::getColorU32("nodeGrid", IM_COL32(200, 200, 200, 40));
    st.graph.getGrid().config().color = gridStyle.colors.background;
    const ImU32 nodeBody = ImGui::Theme::getColorU32("nodeBody", IM_COL32(55, 64, 75, 255));
    for(auto &[uid, node] : st.graph.getNodes()) if(node && node->getStyle()) node->getStyle()->bg = nodeBody;
    st.graph.update();
    if(st.fitFrames > 0 && --st.fitFrames == 0) st.fitView(); // after a frame, when the nodes have a size
    ImGui::EndChild();

    // a flow output leads to one place: keep its newest wire
    for(auto &[id, node] : st.nodes) {
      for(const auto &out : node->getOuts()) {
        if(out->getDataType() != typeid(Flow)) continue;
        auto links = out->getLinks();
        if(links.size() <= 1) continue;
        for(size_t i = 0; i + 1 < links.size(); ++i) {
          if(auto l = links[i].lock()) l->right()->deleteLink(l.get());
        }
      }
    }
  }
  ImGui::End();

  st.dirty = DT::serialize(st.toTree()) != st.savedState;
  (void)title;
  return open;
}
