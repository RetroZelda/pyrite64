/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7).
#include "dialogTree.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <set>
#include <tuple>

#include "../../../n64/engine/include/dialog/dialogData.h"
#include "../../../n64/engine/include/events/types.h"
#include "../../../n64/engine/include/text/locId.h"

namespace
{
  using namespace Project::Dialog;
  namespace D = P64::Dialog;

  constexpr const char* TYPE_NAMES[] = {"start", "line", "choice", "branch", "condition", "logic", "input", "event", "goto", "end"};

  void put8(std::vector<uint8_t> &o, uint8_t v) { o.push_back(v); }
  void put16(std::vector<uint8_t> &o, uint16_t v) { o.push_back(uint8_t(v >> 8)); o.push_back(uint8_t(v)); }
  void put32(std::vector<uint8_t> &o, uint32_t v) { put16(o, uint16_t(v >> 16)); put16(o, uint16_t(v)); }
  void set32(std::vector<uint8_t> &o, size_t at, uint32_t v) { o[at] = uint8_t(v >> 24); o[at + 1] = uint8_t(v >> 16); o[at + 2] = uint8_t(v >> 8); o[at + 3] = uint8_t(v); }
  void align4(std::vector<uint8_t> &o) { while(o.size() % 4) o.push_back(0); }

  bool isFlow(NodeType t)
  {
    return t == NodeType::Start || t == NodeType::Line || t == NodeType::Choice || t == NodeType::Branch
        || t == NodeType::Event || t == NodeType::GoTo || t == NodeType::End;
  }

  uint32_t nameHash(const std::string &s) { return s.empty() ? 0 : P64::Loc::varHash(s.data(), s.size()); }

  std::string describe(const Node &n)
  {
    return std::format("{} #{}", typeName(n.type), n.id);
  }

  struct Builder
  {
    const Tree &tree;
    const BuildEnv &env;
    std::vector<std::string> &errors;
    std::vector<std::string> &warnings;

    std::map<uint32_t, uint16_t> runtimeIndex{};   // node id -> runtime node
    std::vector<const Node*> flowNodes{};
    std::vector<std::vector<uint8_t>> exprs{};
    std::map<uint32_t, uint16_t> condIndex{};      // Condition node id -> condition
    std::vector<const Node*> conds{};
    std::map<std::string, uint8_t> inputIndex{};
    std::vector<std::string> inputs{};

    uint16_t nextOf(const Node &n, const std::string &pin)
    {
      const Link* l = tree.linkFrom(n.id, pin);
      if(!l) return D::NONE;
      const Node* to = tree.find(l->to);
      if(!to || !isFlow(to->type) || to->type == NodeType::Start || l->in != "In") {
        errors.push_back(describe(n) + ": '" + pin + "' must lead to the In of a line, choice, branch, event, go to or end");
        return D::NONE;
      }
      return runtimeIndex[to->id];
    }

    bool compileValue(const Node &n, std::vector<uint8_t> &code, int depth)
    {
      if(depth > 32) { errors.push_back(describe(n) + ": conditions are wired in a loop"); return false; }
      switch(n.type) {
        case NodeType::Condition: {
          auto it = condIndex.find(n.id);
          if(it == condIndex.end()) {
            it = condIndex.emplace(n.id, static_cast<uint16_t>(conds.size())).first;
            conds.push_back(&n);
          }
          put8(code, uint8_t(D::Expr::Cond));
          put16(code, it->second);
          return true;
        }
        case NodeType::Input: {
          if(n.name.empty()) { errors.push_back(describe(n) + ": an input needs a name"); return false; }
          auto it = inputIndex.find(n.name);
          if(it == inputIndex.end()) {
            if(inputs.size() >= 255) { errors.push_back("a tree has at most 255 inputs"); return false; }
            it = inputIndex.emplace(n.name, static_cast<uint8_t>(inputs.size())).first;
            inputs.push_back(n.name);
          }
          put8(code, uint8_t(D::Expr::Input));
          put8(code, it->second);
          return true;
        }
        case NodeType::Logic: {
          const auto in = tree.linksInto(n.id, "Inputs");
          if(in.empty()) { errors.push_back(describe(n) + ": nothing is wired into Inputs"); return false; }
          if(in.size() > 255) { errors.push_back(describe(n) + ": too many inputs"); return false; }
          for(const Link* l : in) {
            const Node* src = tree.find(l->from);
            if(!src || !compileValue(*src, code, depth + 1)) return false;
          }
          D::Expr op = D::Expr::And;
          if(n.logic == "or") op = D::Expr::Or;
          else if(n.logic == "xor") op = D::Expr::Xor;
          else if(n.logic == "not") op = D::Expr::Not;
          else if(n.logic != "and") { errors.push_back(describe(n) + ": '" + n.logic + "' is not and, or, xor or not"); return false; }
          put8(code, uint8_t(op));
          put8(code, uint8_t(in.size()));
          return true;
        }
        default:
          errors.push_back(describe(n) + ": only conditions, inputs and logic nodes give a yes or no");
          return false;
      }
    }

    /** The expression wired into a yes/no input pin; NO_EXPR when nothing is wired. */
    int16_t exprOf(const Node &n, const std::string &pin)
    {
      const auto in = tree.linksInto(n.id, pin);
      if(in.empty()) return D::NO_EXPR;
      std::vector<uint8_t> code{};
      for(const Link* l : in) {
        const Node* src = tree.find(l->from);
        if(!src || !compileValue(*src, code, 0)) return D::NO_EXPR;
      }
      if(in.size() > 1) { put8(code, uint8_t(D::Expr::And)); put8(code, uint8_t(in.size())); }
      put8(code, uint8_t(D::Expr::End));
      if(exprs.size() >= 0x7FFF) { errors.push_back("too many conditions in one tree"); return D::NO_EXPR; }
      exprs.push_back(std::move(code));
      return static_cast<int16_t>(exprs.size() - 1);
    }

    void eventOf(const Node &n, const std::string &name, uint32_t &hash, uint8_t &scope, uint8_t &up)
    {
      const auto res = P64::Events::parseName(name.data(), name.size());
      if(res.error != P64::Events::NameError::None) {
        errors.push_back(describe(n) + ": '" + name + "' is not an event name (namespace::Name)");
        return;
      }
      hash = res.id.hash;
      scope = static_cast<uint8_t>(res.id.scope);
      up = res.id.up;
    }

    std::vector<uint8_t> nodeBytes(const Node &n)
    {
      std::vector<uint8_t> o{};
      switch(n.type) {
        case NodeType::Start:
          put8(o, uint8_t(D::NodeType::Start)); put8(o, 0); put16(o, nextOf(n, "Next"));
          break;
        case NodeType::Line: {
          std::string why{};
          if(P64::Loc::checkKey(n.key.data(), n.key.size()) != P64::Loc::KeyError::None) errors.push_back(describe(n) + ": the key '" + n.key + "' is not valid");
          int speaker = -1;
          if(!n.speaker.empty()) {
            speaker = env.speakerIndex ? env.speakerIndex(n.speaker) : -1;
            if(speaker < 0) errors.push_back(describe(n) + ": no speaker has the id '" + n.speaker + "' (locale/speakers.json)");
          }
          int portrait = -1;
          if(!n.portrait.empty() && speaker >= 0) {
            portrait = env.portraitIndex ? env.portraitIndex(n.speaker, n.portrait) : -1;
            if(portrait < 0) errors.push_back(describe(n) + ": the speaker '" + n.speaker + "' has no portrait '" + n.portrait + "'");
          }
          uint8_t lang = uint8_t(D::Lang::Speaker);
          if(n.lang == "game") lang = uint8_t(D::Lang::Game);
          else if(n.lang == "paired") lang = uint8_t(D::Lang::Paired);
          else if(n.lang != "speaker") errors.push_back(describe(n) + ": language '" + n.lang + "' is not speaker, game or paired");
          uint8_t flags = 0;
          if(n.noVoice) flags |= D::LINE_NO_VOICE;
          if(n.timed) flags |= D::LINE_TIMED;
          put8(o, uint8_t(D::NodeType::Line)); put8(o, flags); put16(o, nextOf(n, "Next"));
          put32(o, P64::Loc::keyId(n.key.data(), n.key.size()).hash);
          put8(o, speaker < 0 ? 0xFF : uint8_t(speaker)); put8(o, lang); put8(o, portrait < 0 ? 0xFF : uint8_t(portrait)); put8(o, 0);
          break;
        }
        case NodeType::Choice: {
          if(n.options.empty()) errors.push_back(describe(n) + ": a choice needs at least one option");
          if(n.options.size() > 8) errors.push_back(describe(n) + ": a choice has at most 8 options");
          int cancels = 0, defaults = 0;
          for(const auto &opt : n.options) { cancels += opt.cancel; defaults += opt.isDefault; }
          if(cancels > 1) errors.push_back(describe(n) + ": only one option can be Cancel");
          if(defaults > 1) errors.push_back(describe(n) + ": only one option can be the default");
          put8(o, uint8_t(D::NodeType::Choice)); put8(o, 0); put16(o, D::NONE);
          put8(o, uint8_t(std::min<size_t>(n.options.size(), 8))); put8(o, 0); put16(o, 0);
          for(size_t k = 0; k < n.options.size() && k < 8; ++k) {
            const auto &opt = n.options[k];
            const std::string num = std::to_string(k + 1);
            if(P64::Loc::checkKey(opt.key.data(), opt.key.size()) != P64::Loc::KeyError::None) errors.push_back(describe(n) + ": option " + num + ": the key '" + opt.key + "' is not valid");
            uint8_t flags = 0;
            if(opt.cancel) flags |= D::OPTION_CANCEL;
            if(opt.isDefault) flags |= D::OPTION_DEFAULT;
            uint32_t ev = 0; uint8_t scope = 0, up = 0;
            if(!opt.event.empty()) { flags |= D::OPTION_EVENT; eventOf(n, opt.event, ev, scope, up); }
            put32(o, P64::Loc::keyId(opt.key.data(), opt.key.size()).hash);
            put16(o, nextOf(n, "Option " + num));
            put8(o, flags); put8(o, 0);
            put16(o, uint16_t(exprOf(n, "Show " + num)));
            put16(o, uint16_t(exprOf(n, "Enable " + num)));
            put32(o, ev); put8(o, scope); put8(o, up); put16(o, 0);
            put32(o, static_cast<uint32_t>(opt.eventValue));
          }
          break;
        }
        case NodeType::Branch:
          put8(o, uint8_t(D::NodeType::Branch)); put8(o, 0); put16(o, nextOf(n, "True"));
          put16(o, uint16_t(exprOf(n, "Condition"))); put16(o, nextOf(n, "False"));
          break;
        case NodeType::Event: {
          uint32_t ev = 0; uint8_t scope = 0, up = 0;
          if(n.event.empty()) errors.push_back(describe(n) + ": an event node needs an event name");
          else eventOf(n, n.event, ev, scope, up);
          put8(o, uint8_t(D::NodeType::Event)); put8(o, 0); put16(o, nextOf(n, "Next"));
          put32(o, ev); put8(o, scope); put8(o, up); put16(o, 0); put32(o, static_cast<uint32_t>(n.value));
          break;
        }
        case NodeType::GoTo: {
          int asset = -1;
          if(n.tree != 0) {
            asset = env.treeAssetIndex ? env.treeAssetIndex(n.tree) : -1;
            if(asset < 0) errors.push_back(describe(n) + ": the tree it goes to is not in the project");
          }
          put8(o, uint8_t(D::NodeType::GoTo)); put8(o, 0); put16(o, D::NONE);
          put16(o, asset < 0 ? D::NONE : uint16_t(asset)); put16(o, 0); put32(o, nameHash(n.start));
          break;
        }
        case NodeType::End:
          put8(o, uint8_t(D::NodeType::End)); put8(o, 0); put16(o, D::NONE);
          put32(o, static_cast<uint32_t>(n.value));
          break;
        default: break;
      }
      return o;
    }

    std::vector<uint8_t> run()
    {
      // flow nodes in id order are the runtime nodes
      std::vector<const Node*> sorted{};
      for(const auto &n : tree.nodes) sorted.push_back(&n);
      std::sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) { return a->id < b->id; });
      for(const Node* n : sorted) {
        if(!isFlow(n->type)) continue;
        runtimeIndex[n->id] = static_cast<uint16_t>(flowNodes.size());
        flowNodes.push_back(n);
      }
      if(flowNodes.size() >= D::NONE) errors.push_back("a tree has too many nodes");

      std::vector<std::pair<uint32_t, uint16_t>> starts{};
      std::set<std::string> startNames{};
      for(const Node* n : flowNodes) {
        if(n->type != NodeType::Start) continue;
        if(!startNames.insert(n->name).second) errors.push_back("two Start nodes are called '" + n->name + "'");
        starts.emplace_back(nameHash(n->name), runtimeIndex[n->id]);
      }
      if(starts.empty()) errors.push_back("the tree has no Start node");

      std::vector<std::vector<uint8_t>> nodeData{};
      for(const Node* n : flowNodes) nodeData.push_back(nodeBytes(*n));

      // warn about nodes no flow reaches
      std::set<uint32_t> reached{};
      std::vector<uint32_t> todo{};
      for(const Node* n : flowNodes) if(n->type == NodeType::Start) todo.push_back(n->id);
      while(!todo.empty()) {
        const uint32_t id = todo.back(); todo.pop_back();
        if(!reached.insert(id).second) continue;
        for(const auto &l : tree.links) {
          const Node* to = tree.find(l.to);
          if(l.from == id && to && isFlow(to->type)) todo.push_back(l.to);
        }
      }
      for(const Node* n : flowNodes) {
        if(!reached.contains(n->id)) warnings.push_back(describe(*n) + " is never reached from a Start");
      }

      // condition values
      std::vector<std::vector<uint8_t>> condArgs{};
      std::vector<uint16_t> condScript{};
      for(const Node* c : conds) {
        const int idx = env.scriptIndex ? env.scriptIndex(c->script) : -1;
        if(idx < 0) errors.push_back(describe(*c) + ": pick a condition script");
        std::vector<uint8_t> blob{};
        std::string err{};
        if(idx >= 0 && env.writeArgs && !env.writeArgs(c->script, c->args, blob, err)) errors.push_back(describe(*c) + ": " + err);
        align4(blob);
        condArgs.push_back(std::move(blob));
        condScript.push_back(idx < 0 ? 0 : uint16_t(idx));
      }

      // assemble
      std::vector<uint8_t> o{};
      o.push_back('P'); o.push_back('6'); o.push_back('4'); o.push_back('D');
      put16(o, D::VERSION); put16(o, uint16_t(flowNodes.size()));
      put16(o, uint16_t(starts.size())); put16(o, uint16_t(exprs.size()));
      put16(o, uint16_t(conds.size())); put16(o, uint16_t(inputs.size()));
      const size_t offsetsAt = o.size();
      for(int i = 0; i < 4; ++i) put32(o, 0);
      for(const auto &[hash, node] : starts) { put32(o, hash); put16(o, node); put16(o, 0); }

      set32(o, offsetsAt, uint32_t(o.size()));
      const size_t nodeTable = o.size();
      for(size_t i = 0; i < nodeData.size(); ++i) put32(o, 0);
      for(size_t i = 0; i < nodeData.size(); ++i) {
        set32(o, nodeTable + i * 4, uint32_t(o.size()));
        o.insert(o.end(), nodeData[i].begin(), nodeData[i].end());
        align4(o);
      }

      set32(o, offsetsAt + 4, uint32_t(o.size()));
      const size_t exprTable = o.size();
      for(size_t i = 0; i < exprs.size(); ++i) put32(o, 0);
      for(size_t i = 0; i < exprs.size(); ++i) {
        set32(o, exprTable + i * 4, uint32_t(o.size()));
        o.insert(o.end(), exprs[i].begin(), exprs[i].end());
      }
      align4(o);

      set32(o, offsetsAt + 8, uint32_t(o.size()));
      const size_t condTable = o.size();
      for(size_t i = 0; i < conds.size(); ++i) { put16(o, condScript[i]); put16(o, uint16_t(condArgs[i].size())); put32(o, 0); }

      set32(o, offsetsAt + 12, uint32_t(o.size()));
      for(const auto &name : inputs) put32(o, nameHash(name));

      for(size_t i = 0; i < conds.size(); ++i) {
        align4(o);
        set32(o, condTable + i * 8 + 4, uint32_t(o.size()));
        o.insert(o.end(), condArgs[i].begin(), condArgs[i].end());
      }
      align4(o);
      return o;
    }
  };
}

const char* Project::Dialog::typeName(NodeType t)
{
  const auto i = static_cast<size_t>(t);
  return i < std::size(TYPE_NAMES) ? TYPE_NAMES[i] : "?";
}

Project::Dialog::NodeType Project::Dialog::typeFromName(const std::string &name)
{
  for(size_t i = 0; i < std::size(TYPE_NAMES); ++i) if(name == TYPE_NAMES[i]) return static_cast<NodeType>(i);
  return NodeType::Count;
}

std::vector<Project::Dialog::PinDef> Project::Dialog::inputsOf(const Node &node)
{
  std::vector<PinDef> pins{};
  switch(node.type) {
    case NodeType::Line: case NodeType::Event: case NodeType::GoTo: case NodeType::End:
      pins.push_back({"In", PinKind::Flow, true});
      break;
    case NodeType::Choice:
      pins.push_back({"In", PinKind::Flow, true});
      for(size_t k = 0; k < node.options.size(); ++k) {
        pins.push_back({"Show " + std::to_string(k + 1), PinKind::Bool, false});
        pins.push_back({"Enable " + std::to_string(k + 1), PinKind::Bool, false});
      }
      break;
    case NodeType::Branch:
      pins.push_back({"In", PinKind::Flow, true});
      pins.push_back({"Condition", PinKind::Bool, false});
      break;
    case NodeType::Logic:
      pins.push_back({"Inputs", PinKind::Bool, true});
      break;
    default: break;
  }
  return pins;
}

std::vector<Project::Dialog::PinDef> Project::Dialog::outputsOf(const Node &node)
{
  std::vector<PinDef> pins{};
  switch(node.type) {
    case NodeType::Start: case NodeType::Line: case NodeType::Event:
      pins.push_back({"Next", PinKind::Flow});
      break;
    case NodeType::Choice:
      for(size_t k = 0; k < node.options.size(); ++k) pins.push_back({"Option " + std::to_string(k + 1), PinKind::Flow});
      break;
    case NodeType::Branch:
      pins.push_back({"True", PinKind::Flow});
      pins.push_back({"False", PinKind::Flow});
      break;
    case NodeType::Condition: case NodeType::Logic: case NodeType::Input:
      pins.push_back({"Result", PinKind::Bool});
      break;
    default: break;
  }
  return pins;
}

Project::Dialog::Node* Project::Dialog::Tree::find(uint32_t id)
{
  for(auto &n : nodes) if(n.id == id) return &n;
  return nullptr;
}

const Project::Dialog::Node* Project::Dialog::Tree::find(uint32_t id) const
{
  for(const auto &n : nodes) if(n.id == id) return &n;
  return nullptr;
}

Project::Dialog::Node& Project::Dialog::Tree::add(NodeType type, float x, float y, const std::string &keyPrefix)
{
  Node n{};
  n.id = nextId++;
  n.type = type;
  n.x = x;
  n.y = y;
  if(type == NodeType::Line) n.key = keyPrefix + "." + std::to_string(n.id);
  if(type == NodeType::Choice) {
    n.options.push_back({.key = optionKey(keyPrefix, n.id, 0), .isDefault = true});
    n.options.push_back({.key = optionKey(keyPrefix, n.id, 1), .cancel = true});
  }
  nodes.push_back(n);
  return nodes.back();
}

void Project::Dialog::Tree::remove(uint32_t id)
{
  std::erase_if(nodes, [id](const Node &n) { return n.id == id; });
  std::erase_if(links, [id](const Link &l) { return l.from == id || l.to == id; });
}

std::vector<const Project::Dialog::Link*> Project::Dialog::Tree::linksInto(uint32_t node, const std::string &pin) const
{
  std::vector<const Link*> res{};
  for(const auto &l : links) if(l.to == node && l.in == pin) res.push_back(&l);
  return res;
}

const Project::Dialog::Link* Project::Dialog::Tree::linkFrom(uint32_t node, const std::string &pin) const
{
  for(const auto &l : links) if(l.from == node && l.out == pin) return &l;
  return nullptr;
}

bool Project::Dialog::parse(const nlohmann::json &doc, Tree &out, std::vector<std::string> &errors)
{
  out = {};
  if(!doc.is_object()) { errors.push_back("the file is not a JSON object"); return false; }
  uint32_t maxId = 0;
  if(doc.contains("nodes") && doc["nodes"].is_array()) {
    for(const auto &j : doc["nodes"]) {
      Node n{};
      n.id = j.value("id", 0u);
      n.type = typeFromName(j.value("type", std::string{}));
      if(n.type == NodeType::Count || n.id == 0) { errors.push_back("a node has no id or an unknown type"); continue; }
      if(j.contains("pos") && j["pos"].is_array() && j["pos"].size() == 2) { n.x = j["pos"][0]; n.y = j["pos"][1]; }
      n.name = j.value("name", std::string{});
      n.speaker = j.value("speaker", std::string{});
      n.key = j.value("key", std::string{});
      n.lang = j.value("lang", std::string{"speaker"});
      n.portrait = j.value("portrait", std::string{});
      n.noVoice = j.value("noVoice", false);
      n.timed = j.value("timed", false);
      if(j.contains("options") && j["options"].is_array()) {
        for(const auto &o : j["options"]) {
          n.options.push_back({o.value("key", std::string{}), o.value("cancel", false), o.value("default", false),
                               o.value("event", std::string{}), o.value("eventValue", 0)});
        }
      }
      if(j.contains("script")) n.script = j["script"].is_string() ? std::stoull(j["script"].get<std::string>()) : j.value("script", uint64_t{0});
      if(j.contains("args") && j["args"].is_object()) {
        for(const auto &[k, v] : j["args"].items()) if(v.is_string()) n.args[k] = v;
      }
      n.logic = j.value("logic", std::string{"and"});
      n.event = j.value("event", std::string{});
      n.value = j.value("value", 0);
      if(j.contains("tree")) n.tree = j["tree"].is_string() ? std::stoull(j["tree"].get<std::string>()) : j.value("tree", uint64_t{0});
      n.start = j.value("start", std::string{});
      maxId = std::max(maxId, n.id);
      if(out.find(n.id)) { errors.push_back(std::format("two nodes have the id {}", n.id)); continue; }
      out.nodes.push_back(std::move(n));
    }
  }
  if(doc.contains("links") && doc["links"].is_array()) {
    for(const auto &j : doc["links"]) {
      Link l{j.value("from", 0u), j.value("out", std::string{}), j.value("to", 0u), j.value("in", std::string{})};
      if(!out.find(l.from) || !out.find(l.to)) { errors.push_back("a link names a node that does not exist"); continue; }
      out.links.push_back(l);
    }
  }
  out.nextId = std::max(doc.value("nextId", 1u), maxId + 1);
  return true;
}

std::string Project::Dialog::serialize(const Tree &tree)
{
  nlohmann::json doc = nlohmann::json::object();
  doc["version"] = 1;
  doc["nextId"] = tree.nextId;
  std::vector<const Node*> sorted{};
  for(const auto &n : tree.nodes) sorted.push_back(&n);
  std::sort(sorted.begin(), sorted.end(), [](const Node* a, const Node* b) { return a->id < b->id; });
  auto nodes = nlohmann::json::array();
  for(const Node* n : sorted) {
    nlohmann::json j = nlohmann::json::object();
    j["id"] = n->id;
    j["type"] = typeName(n->type);
    j["pos"] = {std::round(n->x), std::round(n->y)};
    switch(n->type) {
      case NodeType::Start: case NodeType::Input: j["name"] = n->name; break;
      case NodeType::Line:
        j["key"] = n->key;
        j["speaker"] = n->speaker;
        j["lang"] = n->lang;
        if(!n->portrait.empty()) j["portrait"] = n->portrait;
        if(n->noVoice) j["noVoice"] = true;
        if(n->timed) j["timed"] = true;
        break;
      case NodeType::Choice: {
        auto opts = nlohmann::json::array();
        for(const auto &o : n->options) {
          nlohmann::json jo = {{"key", o.key}};
          if(o.cancel) jo["cancel"] = true;
          if(o.isDefault) jo["default"] = true;
          if(!o.event.empty()) { jo["event"] = o.event; jo["eventValue"] = o.eventValue; }
          opts.push_back(jo);
        }
        j["options"] = opts;
        break;
      }
      case NodeType::Condition: {
        j["script"] = std::to_string(n->script);
        auto args = nlohmann::json::object();
        for(const auto &[k, v] : n->args) args[k] = v;
        j["args"] = args;
        break;
      }
      case NodeType::Logic: j["logic"] = n->logic; break;
      case NodeType::Event: j["event"] = n->event; j["value"] = n->value; break;
      case NodeType::GoTo: j["tree"] = std::to_string(n->tree); j["start"] = n->start; break;
      case NodeType::End: j["value"] = n->value; break;
      default: break;
    }
    nodes.push_back(j);
  }
  doc["nodes"] = nodes;
  auto links = tree.links;
  std::sort(links.begin(), links.end(), [](const Link &a, const Link &b) {
    return std::tie(a.from, a.out, a.to, a.in) < std::tie(b.from, b.out, b.to, b.in);
  });
  auto jl = nlohmann::json::array();
  for(const auto &l : links) jl.push_back({{"from", l.from}, {"out", l.out}, {"to", l.to}, {"in", l.in}});
  doc["links"] = jl;
  return doc.dump(2) + "\n";
}

std::string Project::Dialog::keyPrefixOf(const std::string &fileName)
{
  std::string stem = fileName;
  if(auto dot = stem.find('.'); dot != std::string::npos) stem = stem.substr(0, dot);
  std::string out = "dlg.";
  for(char c : stem) {
    const char l = char(std::tolower(static_cast<unsigned char>(c)));
    out += ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_') ? l : '_';
  }
  if(out == "dlg.") out += "tree";
  return out;
}

std::string Project::Dialog::optionKey(const std::string &keyPrefix, uint32_t node, size_t option)
{
  return keyPrefix + "." + std::to_string(node) + "." + std::to_string(option + 1);
}

std::vector<std::string> Project::Dialog::keysOf(const Tree &tree)
{
  std::vector<std::string> keys{};
  for(const auto &n : tree.nodes) {
    if(n.type == NodeType::Line && !n.key.empty()) keys.push_back(n.key);
    if(n.type == NodeType::Choice) for(const auto &o : n.options) if(!o.key.empty()) keys.push_back(o.key);
  }
  return keys;
}

std::vector<std::string> Project::Dialog::eventsOf(const Tree &tree)
{
  std::vector<std::string> events{};
  for(const auto &n : tree.nodes) {
    if(n.type == NodeType::Event && !n.event.empty()) events.push_back(n.event);
    if(n.type == NodeType::Choice) for(const auto &o : n.options) if(!o.event.empty()) events.push_back(o.event);
  }
  return events;
}

std::vector<uint8_t> Project::Dialog::build(const Tree &tree, const BuildEnv &env, std::vector<std::string> &errors, std::vector<std::string> &warnings)
{
  Builder b{tree, env, errors, warnings};
  return b.run();
}
